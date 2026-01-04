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

#ifndef RIPPLE_CORE_WORKERS_H_INCLUDED
#define RIPPLE_CORE_WORKERS_H_INCLUDED

#include <xrpl/basics/safe_cast.h>

#include <atomic>
#include <exception>
#include <memory>
#include <string_view>
#include <vector>

namespace ripple {

/** A simple thread pool.

    This class is a simple, fixed-sized thread pool. The pool only tracks
    the number of outstanding tasks, and dispatches work. When it detects
    that there is no work to be done, it puts threads to sleep.

    The pool does not decide which task to run; a callback, that the pool
    invokes, handles that. This makes it possible to implement a dispatch
    strategy (e.g. FIFO or priority queues) without requiring any changes
    to the thread pool itself.

    The threads will run (or sleep) until stop() is called explicitly, or
    automatically by the pool's destructor.

    Stopping takes priority over servicing new tasks. Once a stop request
    has been made, no new tasks will be serviced, which means that queued
    work may be potentially left unfinished. Tasks already dispatched and
    running under a worker thread will complete before the pool stops.
 */
class Workers
{
public:
    enum class WakePolicy {
        // Wake a sleeper only when the backlog exceeds the number of
        // awake workers. Assumes tasks are short and running workers
        // will return to the claim loop soon. Right for CPU-bound work.
        lazy,

        // Wake a sleeper whenever one exists and a task is unclaimed.
        // Right for tasks that block (e.g. in I/O), where an "awake"
        // worker may not revisit the claim loop for milliseconds.
        eager
    };

    /** Called to perform tasks as needed. */
    struct Callback
    {
        virtual ~Callback() = default;
        Callback() = default;
        Callback(Callback const&) = delete;
        Callback&
        operator=(Callback const&) = delete;

        /** Select and perform a task.

            The function is invoked precisely once for every call to the
            thread pool's addTask method. It executes on one of pool's threads.

            This function should process precisely one task.

            @param instance The worker thread instance.

            @throws This function should NOT throw an exception; if it does
                    the exception will be captured and passed to the
                    uncaughtException callback.

            @see Workers::addTask
        */
        virtual void
        processTask(unsigned int instance) = 0;

        /** Indicates that processTask threw an unexpected exception.

            @param instance The worker thread instance.
            @param eptr The exception that was thrown.
         * */
        virtual void
        uncaughtException(unsigned int instance, std::exception_ptr eptr)
        {
            // Default implementation does nothing
        }
    };

    /** Create a new thread pool with the given number of worker threads.

        The pool starts its threads immediately, and they go to sleep,
        waiting for work to arrive via @ref addTask.

        @param callback The task selection and execution algorithm.
        @param name The name for this pool (used to name its threads).
        @param count The number of threads for this pool. Must be non-zero.

        @throws std::logic_error if count is zero; other std::exception
                                 derived exceptions if thread creation
                                 or memory allocation fails.
     */
    explicit Workers(
        Callback& callback,
        std::string_view name,
        unsigned int count,
        WakePolicy wakePolicy = WakePolicy::lazy);

    ~Workers();

    /** Retrieve the number of threads in the thread pool. */
    [[nodiscard]] unsigned int
    count() const noexcept
    {
        return unsafe_cast<unsigned int>(workers_.size());
    }

    /** Stop all threads and wait until they exit.

        Pool threads will complete any currently executing tasks before
        exiting. Tasks that were queued but have not been dispatched by
        the pool yet will be discarded. Once stopped, the pool will not
        restart.

        @note This function is thread-safe and can be invoked multiple
              times, including multiple times concurrently. Calls will
              block until the pool has stopped and worker threads have
              exited.

        @warning This function cannot be called from a worker thread
                 owned by this pool. Doing so can result in either a
                 deadlock or a call to std::terminate.
    */
    void
    stop();

    /** Add a task to be performed.

        Every call to addTask will eventually result in a call to
        Callback::processTask unless the Workers object is destroyed or
        the number of threads is never set above zero.

        @note This function is thread-safe.
    */
    void
    addTask() noexcept;

private:
    /** The type we use for fast atomics.

        Note that this is deliberately using a 32-bit type so that
        we can leverage glibc's futex support on Linux.
     */
    using futex_t = std::uint32_t;

    /** The current state of a worker thread. */
    static constexpr futex_t worker_asleep = 0;
    static constexpr futex_t worker_active = 1;

    /** The current state of the pool, as a whole. */
    static constexpr futex_t pool_running = 1;
    static constexpr futex_t pool_stopped = 0;

    /** Required forward reference. */
    struct Worker;

    // Note that we are very deliberate with the memory layout of the
    // members of this class, to limit cache invalidations because of
    // writes.
    //
    // First, we have data which is read very frequently, but is only
    // rarely written to. These occupy their own cache line.
    //
    // Next, we have 3 groups of "hot" data, each on a dedicated cache
    // line:
    //
    // - Line 1: The dormitory pointer and its lock. We colocate them
    //           because they are dirtied together. The flag used for
    //           pool shutdown is also included in this block.
    // - Line 2: The head counter, which only producers write to.
    // - Line 3: The tail counter, which only consumers write to.

    /** The list of worker objects */
    std::vector<std::unique_ptr<Worker>> workers_;

    /** The job-handling callback, provided by our parent. */
    Callback& callback_;

    /** The wake policy for this pool. */
    WakePolicy const wakePolicy_;

    /** The dormitory for worker threads. */
    alignas(64) Worker* dormitory_ = nullptr;

    /** Number of workers currently sleeping in the dormitory.

        This is guarded by lock_. Incremented by a worker as it enrolls
        itself and decremented ONLY by a waker as it pops a worker; The
        wake-gating argument in addTask() depends on this:

        (workers_.size() - sleeping_) only grows via an explicit wake decision,
        which is what makes the backlog ramp self-limiting.
     */
    std::size_t sleeping_ = 0;

    /** The lock for manipulating the dormitory. */
    std::atomic<futex_t> lock_ = 0;

    /** Running indicator.

        This is used to coordinate multiple concurrent calls to @ref stop
        and ensure that calls block until all workers have exited.
     */
    std::atomic<futex_t> running_ = pool_running;

    /** Queued task tracking:

        We track the total number of tasks added, and total number of tasks
        dispatched. The key insight is that, during normal operation, these
        counters are only ever incremented. This constraint allows us avoid
        using locking when we are pushing or popping tasks onto the stack.

        The choice of std::uint64_t is deliberate: at a rate of 1,000 tasks
        per second, a std::uint32_t would overflow in a little less than 50
        days. An std::uint64_t, on the other hand, is good for well over 50
        years even at a rate of 1,000,000,000 tasks per second. By then, we
        are really due for a reboot.
     */

    /** The total number of tasks that have been queued since startup. */
    alignas(64) std::atomic<std::uint64_t> head_ = 1;

    /** The total number of tasks that have been dispatched for processing. */
    alignas(64) std::atomic<std::uint64_t> tail_ = 1;

    /** The function that worker threads run. */
    void
    run(Worker& me) noexcept;
};

}  // namespace ripple

#endif
