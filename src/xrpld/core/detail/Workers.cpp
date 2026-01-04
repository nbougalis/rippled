//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/core/detail/Workers.h>
#include <xrpl/basics/spinlock.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <thread>
#include <utility>

namespace ripple {

struct alignas(64) Workers::Worker
{
    unsigned int const instance;

    /** Wakeup signal for this worker.

        This is a stateful flag, not a pulse: a worker arms it by setting
        it to @ref worker_asleep before entering the dorm and waiting for
        someone to set it to @ref worker_active and notify.

        A wake that lands in the window between the worker releasing the
        lock and reaching wait() is not lost, because the observed value
        will be @ref worker_active and fall through immediately.

        Note that a worker does not draw any conclusion from being woken
        and will re-read head_ and tail_ and re-run the claim loop. This
        makes spurious wakes harmless by construction, and means a waker
        does not have to guarantee that the work being queued will still
        exist by the time the worker actually runs.
     */
    std::atomic<futex_t> signal = worker_active;

    /** The last job identifier processed by this worker.

        This is a debugging aid, representing the last tail value that
        this worker claimed. It is written, but not read or exposed.
     */
    std::uint64_t last = 0;

    /** A pointer to the next sleeping worker, if any.

        This is read and written only while holding the lock_. The
        owning thread writes it during enrollment; a puller reads
        it during pop. It is meaningless while the worker is awake.
     */
    Worker* next = nullptr;

    /** The name we assign to this thread.

        This is just a debugging aid, so that threads shows up with
        the right name in the debugger.
     */
    std::string name;

    /** The thread that this worker is running on.

        @note This MUST be the last member, so that every other member of
              the class is initialized before it, and the function we are
              going to run in the thread can never observe an object that
              is partially-constructed.
     */
    std::thread thread;

    Worker(unsigned int i, Workers& parent, std::string_view n)
        : instance(i)
        , name(std::string(n) + ":" + std::to_string(i))
        , thread([&parent, this] {
            beast::setCurrentThreadName(name);
            parent.run(*this);
        })
    {
    }

    /** Wake this worker.

        This is safe to call regardless of the state the worker is in. The
        waker does not promise that any work will be available.
     */
    void
    wake() noexcept
    {
        signal.store(worker_active, std::memory_order::release);
        signal.notify_one();
    }
};

Workers::Workers(
    Callback& callback,
    std::string_view name,
    unsigned int count,
    WakePolicy wakePolicy)
    : callback_(callback), wakePolicy_(wakePolicy)
{
    if (count == 0)
        throw std::logic_error("A non-zero number of threads is required.");

    try
    {
        workers_.reserve(count);

        for (unsigned int i = 0; i != count; ++i)
            workers_.push_back(std::make_unique<Worker>(i, *this, name));
    }
    catch (...)
    {
        // We cannot allow exceptions to escape uncontrolled from here
        // because we are not yet fully constructed and our destructor
        // will not be invoked. But our members are, and the automatic
        // unwinding will invoke their destructors. If any workers are
        // created, the destruction of the worker's thread will invoke
        // std::terminate unconditionally.
        if (!workers_.empty())
            stop();

        throw;
    }
}

Workers::~Workers()
{
    if (!workers_.empty())
        stop();
}

void
Workers::stop()
{
    // Signal workers to stop. Any workers in the process of going to the
    // dorm will either make it or they will find that the dorm is closed
    // and exit.
    //
    // Stopping takes priority over new work: queued tasks which have not
    // been dispatched will be discarded.
    if (auto const h = head_.exchange(0, std::memory_order::release); h != 0)
    {
        // Unlink all sleeping threads, and wake them, one at a time.
        auto* chain = [this]() {
            spinlock sl(lock_);
            std::lock_guard lock(sl);

            // Bulk form of the waker-side decrement: the dormitory is now
            // closed and everyone sleeping there is evicted.
            sleeping_ = 0;

            return std::exchange(dormitory_, nullptr);
        }();

        while (chain != nullptr)
        {
            // We need to read the next link before waking the worker.
            auto* next = std::exchange(chain->next, nullptr);
            chain->wake();
            chain = next;
        }

        for (auto& w : workers_)
        {
            if (w->thread.joinable())
                w->thread.join();
        }

        // We are done with the cleanup:
        running_.store(pool_stopped, std::memory_order::release);
        running_.notify_all();
    }

    // If this was not the stop that performed the cleanup, this will loop
    // until the cleanup has been completed:
    running_.wait(pool_running);
}

void
Workers::addTask() noexcept
{
    // Bump the task count, unless the pool is stopping. We intentionally
    // use release ordering in the compare_exchange_strong, extending the
    // release sequence of head_; this ensures that workers whose acquire
    // load observes this (or any later) value for head_ will synchronize
    // with us and, therefore, see everything we wrote prior to this call.
    // The ordering enforces the payload-publication guarantee; it cannot
    // be weakened.
    auto h = head_.load(std::memory_order::relaxed);

    do
    {
        // A zero head_ is the stop sentinel and must never be revived.
        if (h == 0)
            return;
    } while (!head_.compare_exchange_strong(
        h, h + 1, std::memory_order::release, std::memory_order::relaxed));

    // Our task is number h+1. If tail_ has already reached past it, it means
    // that a worker claimed it since our CAS above; there is no backlog that
    // is attributable to us, which means that no wake is needed.
    if (auto const t = tail_.load(std::memory_order::relaxed); t <= h)
    {
        // Wake a sleeper. If the pool has the "lazy" policy, we will only
        // wake a thread if the backlog exceeds the number of workers that
        // are awake; otherwise, we bet that an already active worker will
        // return to the claim loop soon and absorb this task. This bet is
        // self-correcting: the backlog is strictly increasing against the
        // fixed number of workers, so it eventually triggers and wakes up
        // another thread. The ramp continues until we either catch up, or
        // the dormitory is empty (i.e. all our threads are running).
        //
        // The eager policy skips the backlog calculation and always wakes
        // a sleeper if one exists. It is intended for workloads that have
        // long blocking tasks (e.g. in I/O), and an "active" worker might
        // not return to the claim loop for a long time.
        //
        // Regardless of the policy, wakes remain bounded: at most one per
        // unclaimed task, since the tail_ check above gates entry here.
        //
        // Suppressing a wake cannot strand a task: workers only complete
        // enrollment in the dormitory via the recheck under lock_; since
        // our CAS on head_ happens before we take that same lock, either
        // the worker enrolled first (so sleeping_ counted it) or its own
        // recheck ran after our critical section, saw that there was new
        // work and refused to sleep.
        //
        // Note that outstanding is computed from t, which could be stale
        // by the time we grab the lock. This is safe: the error can only
        // be high, so this errs toward waking; the woken worker may find
        // no work queued which is fine.
        if (auto* w = [this, outstanding = h + 1 - t]() -> Worker* {
                spinlock sl(lock_);
                std::lock_guard lock(sl);

                if (dormitory_ == nullptr)
                    return nullptr;

                if (wakePolicy_ == WakePolicy::lazy &&
                    (outstanding <= workers_.size() - sleeping_))
                    return nullptr;

                --sleeping_;
                return std::exchange(dormitory_, dormitory_->next);
            }())
        {
            w->next = nullptr;
            w->wake();
        }
    }
}

void
Workers::run(Worker& me) noexcept
{
    while (true)
    {
        // observing tail_ == t will synchronize with the claimer of
        // task t, whose own head_ read saw at least t; by read-read
        // coherence our head_ load below then also sees >= t.
        auto t = tail_.load(std::memory_order::acquire);

        // Join head_'s release sequence, so everything any producer
        // wrote before an addTask() we observe is visible before we
        // go looking for its task.
        auto h = head_.load(std::memory_order::acquire);

        // A zero head is the stop sentinel: no tasks will be dispatched
        // and we need to exit.
        if (h == 0)
            break;

        XRPL_ASSERT(h >= t, "ripple::Workers::run : head >= tail");

        if (h > t)
        {
            if (tail_.compare_exchange_strong(
                    t,
                    t + 1,
                    std::memory_order::release,
                    std::memory_order::relaxed))
            {
                me.last = t;

                try
                {
                    callback_.processTask(me.instance);
                }
                catch (...)
                {
                    callback_.uncaughtException(
                        me.instance, std::current_exception());
                }
            }

            continue;
        }

        static constexpr std::size_t spin_iterations = 30;

        if constexpr (spin_iterations != 0)
        {
            detail::spin_pause();

            for (std::size_t i = 0; i != spin_iterations; ++i)
            {
                if (head_.load(std::memory_order::relaxed) !=
                    tail_.load(std::memory_order::relaxed))
                    break;

                detail::spin_pause();
            }

            if (head_.load(std::memory_order::acquire) !=
                tail_.load(std::memory_order::relaxed))
                continue;
        }

        // There was no task, so we are about to try and go sleep. This
        // store happens before we go into the dormitory, which happens
        // under the lock.
        me.signal.store(worker_asleep, std::memory_order::relaxed);

        {
            spinlock sl(lock_);
            std::lock_guard lock(sl);

            if (head_.load(std::memory_order::relaxed) !=
                tail_.load(std::memory_order::relaxed))
                continue;

            me.next = std::exchange(dormitory_, &me);

            // One more sleeping thread.
            ++sleeping_;
        }

        // Acquire matches the wakers' release stores, but it is not needed
        // per se. We use it because this is the convention expected from a
        // wait/notify pair.
        me.signal.wait(worker_asleep, std::memory_order::acquire);
    }
}

}  // namespace ripple
