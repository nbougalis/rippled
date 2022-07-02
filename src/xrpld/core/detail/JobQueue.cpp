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

#include <xrpld/core/JobQueue.h>
#include <xrpld/perflog/PerfLog.h>
#include <xrpl/basics/SlabAllocator.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/core/CurrentThreadName.h>

#include <mutex>
#include <utility>

namespace ripple {

// clang-format off
inline constinit
slab::allocator_t<Job, slab::heap_fallback, slab::config<16384>> jobslab;
// clang-format on

JobQueue::JobQueue(
    std::size_t threadCount,
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal,
    Logs& logs,
    perf::PerfLog& perfLog)
    : journal_(journal)
    , data_([&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<Data, sizeof...(I)>{
            Data{jobTypes[I], collector, logs}...};
    }(std::make_index_sequence<jobTypes.size()>()))
    , perfLog_(perfLog)
    , workers_(*this, "JobQueue", threadCount)
    , collector_(collector)
{
    JLOG(journal_.info()) << "Using " << threadCount << " threads";

    // This is important to do before dispatching any jobs
    // so that the logger knows the number of threads.
    perfLog_.resizeJobs(threadCount);

    hook_ = collector_->make_hook([this]() {
        int jobs = 0;
        int running = 0;

        {
            std::lock_guard dlock(mutex_);

            for (auto const& d : data_)
            {
                jobs += d.count;
                running += d.running;
            }
        }

        jobCountGauge_ = jobs;
        activeThreadsGauge_ = running;
    });

    jobCountGauge_ = collector_->make_gauge("job_count");
}

JobQueue::~JobQueue()
{
    XRPL_ASSERT(
        isStopped(),
        "ripple::JobQueue::~JobQueue : stop() not called prior to destruction");

    stop();

    // Must unhook before destroying
    hook_ = beast::insight::Hook();
}

void
JobQueue::addRefCountedJob(
    JobType type,
    std::string const& name,
    JobFunction func)
{
    JLOG(journal_.debug()) << "Adding job '" << name << "' (" << type << ")";

    XRPL_ASSERT(
        !jobTypes[type].special(),
        "ripple::JobQueue::addRefCountedJob : job type is dispatchable");

    // FIXME: Workaround incorrect client shutdown ordering
    // do not add jobs to a queue with no threads
    XRPL_ASSERT(
        (type >= jtCLIENT && type <= jtCLIENT_WEBSOCKET) ||
            workers_.count() > 0,
        "ripple::JobQueue::addRefCountedJob : threads available or job "
        "requires no threads");

    auto* job = [&]() {
        if (auto raw = jobslab.allocate(0)) [[likely]]
        {
            return new (raw)
                Job(type, name, data_[type].load.sample(), std::move(func));
        }

        LogicError("JobQueue: failed to allocate Job memory!");
    }();

    // Counted before the job becomes visible in any queue: a quiescence
    // waiter that reads zero is guaranteed no job is queued or
    // executing. Safe to count here: the node is already fully
    // constructed, and linking below cannot fail.
    totalJobs_.fetch_add(1, std::memory_order::relaxed);

    std::lock_guard lock(mutex_);

    auto& d = data_[type];

    if (d.last)
        d.last->chain(job);
    else
        d.first = job;
    d.last = job;
    ++d.count;

    if (d.count == 1)
        jobMask_.set(type);

    // The new job is now counted by outstanding(), so we use
    // less-than-or-equal here, where a check made before the insertion
    // would use less-than.
    if (d.outstanding() <= jobTypes[type].limit)
        workers_.addTask();
    else
        ++d.deferred;

    perfLog_.jobQueue(type);
}

int
JobQueue::getJobCount(JobType t) const
{
    std::lock_guard lock(mutex_);
    return data_[t].queued();
}

int
JobQueue::getJobCountTotal(JobType t) const
{
    std::lock_guard lock(mutex_);
    return data_[t].outstanding();
}

int
JobQueue::getJobCountGE(JobType t) const
{
    std::lock_guard lock(mutex_);

    return std::accumulate(
        data_.begin() + static_cast<std::size_t>(t),
        data_.end(),
        0,
        [](int sum, Data const& d) { return sum + d.queued(); });
}

LoadEvent
JobQueue::createLoadEvent(JobType t, std::string name)
{
    return {data_[t].load.sample(), std::move(name), true};
}

void
JobQueue::addLoadEvents(JobType t, int count, std::chrono::milliseconds elapsed)
{
    if (!isStopped()) [[likely]]
        data_[t].load.addSamples(count, elapsed);
}

bool
JobQueue::isOverloaded()
{
    return std::any_of(
        data_.begin(), data_.end(), [](auto& d) { return d.load.isOver(); });
}

Json::Value
JobQueue::getJson(int)
{
    using namespace std::chrono_literals;
    Json::Value ret(Json::objectValue);

    ret["threads"] = workers_.count();
    ret["coro.suspended"] = suspendedCoroutines_.load();
    ret["coro.total"] = totalCoroutines_.load();

    Json::Value priorities = Json::arrayValue;

    std::lock_guard dlock(mutex_);

    for (std::size_t i = 0; i < data_.size(); ++i)
    {
        auto const type = static_cast<JobType>(i);

        if (type == jtGENERIC)
            continue;

        auto& d = data_[i];

        if (auto const s = d.load.getStats();
            s.count || d.count != 0 || d.running || (s.peakLatency != 0ms))
        {
            Json::Value& pri = priorities.append(Json::objectValue);

            pri["job_type"] = std::string(jobTypes[i].name);

            if (s.isOverloaded)
                pri["over_target"] = true;

            if (d.count)
                pri["waiting"] = d.count;

            if (s.count != 0)
                pri["per_second"] = static_cast<int>(s.count);

            if (s.peakLatency != 0ms)
                pri["peak_time"] = static_cast<int>(s.peakLatency.count());

            if (s.averageLatency != 0ms)
                pri["avg_time"] = static_cast<int>(s.averageLatency.count());

            if (d.running != 0)
                pri["in_progress"] = d.running;
        }
    }

    ret["job_types"] = priorities;

    return ret;
}

void
JobQueue::rendezvous()
{
    std::unique_lock lock(quiesceMutex_);
    quiesceCv_.wait(lock, [this] {
        return totalJobs_.load(std::memory_order_acquire) == 0;
    });
}

void
JobQueue::stop()
{
    if (state expected = state::running; !state_.compare_exchange_strong(
            expected, state::stopping, std::memory_order_acq_rel))
    {
        while (state_.load(std::memory_order_acquire) != state::stopped)
            std::this_thread::yield();
        return;
    }

    jobCounter_.join("JobQueue", std::chrono::seconds(1), journal_);

    // Once the counter joins, all jobs have finished executing (i.e. they
    // have returned from Job::execute) and no new jobs will be added. But
    // a worker thread may still be inside processTask, between the return
    // of Job::execute and the completion of the epilogue that updates the
    // per-type counters. Wait for true quiescence before declaring the
    // queue stopped.
    rendezvous();

    XRPL_ASSERT(
        suspendedCoroutines_ == 0,
        "ripple::JobQueue::stop : no coros suspended");

    state_.store(state::stopped, std::memory_order_release);
}

void
JobQueue::uncaughtException(unsigned int instance, std::exception_ptr eptr)
{
    try
    {
        if (eptr)
            std::rethrow_exception(eptr);

        LogicError(
            beast::getCurrentThreadName() +
            ": Uncaught exception handler invoked with no exception_ptr");
    }
    catch (std::exception const& e)
    {
        LogicError(
            beast::getCurrentThreadName() +
            ": Exception caught during task processing: " + e.what());
    }
    catch (...)
    {
        LogicError(
            beast::getCurrentThreadName() +
            ": Unknown exception caught during task processing");
    }
}

void
JobQueue::processTask(unsigned int instance)
{
    // Take the oldest waiting job from the highest-priority type
    // that is under its concurrency limit.
    auto* job = [this]() {
        Job* claimed = nullptr;

        std::lock_guard lock(mutex_);

        auto found = jobMask_.scan([this, &claimed](JobType t) {
            auto& d = data_[t];

            if (d.first == nullptr)
                return false;

            XRPL_ASSERT(
                d.running <= jobTypes[t].limit,
                "ripple::JobQueue::processTask : running within limit");

            // A type at its concurrency limit is skipped; its bit remains
            // set and a completing job of this type will re-enable it via
            // the deferred task machinery.
            if (d.running >= jobTypes[t].limit)
                return false;

            ++d.running;

            claimed = d.first;
            d.first = claimed->next;

            if (d.first == nullptr)
            {
                d.last = nullptr;
                jobMask_.clear(t);
            }

            --d.count;

            return true;
        });

        if (found && claimed != nullptr) [[likely]]
            return claimed;

        LogicError("Attempt to get a job when none are available");
    }();

    auto const type = job->getType();

    using namespace std::chrono;

    auto const start_time = Job::clock_type::now();
    auto const q_time = ceil<microseconds>(start_time - job->queue_time());
    perfLog_.jobStart(type, q_time, start_time, instance);

    JLOG(journal_.trace()) << "Starting: " << jobTypes[type].name
                           << " (q_time=" << q_time.count() << " microseconds)";

    // We run the accounting epilogue on both the normal path and during an
    // unwind, to keep the invariants of the pool intact even if when a job
    // throws. The exception, if any, bubbles up to Workers and comes back
    // via uncaughtException.
    auto const epilogue = scope_exit([&]() noexcept {
        auto const x_time =
            ceil<microseconds>(Job::clock_type::now() - start_time);

        JLOG(journal_.trace())
            << "Finished: " << jobTypes[type].name
            << " (x_time=" << x_time.count() << " microseconds)";

        if (x_time >= 10ms || q_time >= 10ms)
        {
            data_[type].dequeue.notify(q_time);
            data_[type].execute.notify(x_time);
        }

        perfLog_.jobFinish(type, x_time, instance);

        // All job nodes are freed here: stop()'s rendezvous guarantees the
        // queues drain before Workers can discard any tickets, so no node
        // is ever stranded.
        if constexpr (!std::is_trivially_destructible_v<Job>)
            std::destroy_at(job);

        if (!jobslab.deallocate(job)) [[unlikely]]
            LogicError("JobQueue: failed to deallocate Job memory!");

        {
            std::lock_guard lock(mutex_);

            auto& d = data_[type];

            // Queue a deferred task if possible. This doesn't mean the task
            // will run next; only that one is now runnable.
            if (d.deferred > 0)
            {
                XRPL_ASSERT(
                    d.outstanding() >= jobTypes[type].limit,
                    "ripple::JobQueue::processTask : deferral within "
                    "limit");
                --d.deferred;
                workers_.addTask();
            }

            --d.running;
        }

        // The decrement is last: totalJobs_ reaching zero must imply that
        // all per-type state is final and no further addTask() call will
        // follow from this job. The acq_rel pairs with the acquire load in
        // the quiescence waiters.
        if (totalJobs_.fetch_sub(1, std::memory_order::acq_rel) == 1)
        {
            // Lock-then-notify closes the lost-wakeup race: a waiter that
            // read a nonzero count must either be parked (and will be
            // woken) or will re-check after we release. The empty critical
            // section is deliberate.
            {
                std::lock_guard qlock(quiesceMutex_);
            }
            quiesceCv_.notify_all();
        }
    });

    job->execute();
}

}  // namespace ripple
