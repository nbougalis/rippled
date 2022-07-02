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

#ifndef RIPPLE_CORE_JOBQUEUE_H_INCLUDED
#define RIPPLE_CORE_JOBQUEUE_H_INCLUDED

#include <xrpld/core/ClosureCounter.h>
#include <xrpld/core/JobTypes.h>
#include <xrpld/core/detail/Workers.h>
#include <xrpl/basics/LocalValue.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/json/json.h>

#include <boost/coroutine/all.hpp>
#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>

#include <condition_variable>
#include <deque>

namespace ripple {

namespace perf {
class PerfLog;
}

class Logs;

/** A pool of threads to perform work.

    A job posted will always run to completion.

    Coroutines that are suspended must be resumed,
    and run to completion.

    When the JobQueue stops, it waits for all jobs
    and coroutines to finish.
*/
class JobQueue : private Workers::Callback
{
    // A small class used to prevent construction of coroutines without
    // going through the JobQueue APIs.
    struct CoroCreator
    {
        explicit constexpr CoroCreator() = default;
    };

public:
    /** Coroutines must run to completion. */
    class Coro : public std::enable_shared_from_this<Coro>,
                 public CountedObject<Coro>
    {
    private:
        detail::LocalValues lvs_;
        JobQueue& jq_;
        JobType const type_;
        std::string const name_;
        bool running_;
        std::mutex mutex_;
        std::mutex mutex_run_;
        std::condition_variable cv_;
        boost::coroutines::asymmetric_coroutine<void>::pull_type coro_;
        boost::coroutines::asymmetric_coroutine<void>::push_type* yield_;
#ifndef NDEBUG
        bool finished_ = false;
#endif

    public:
        template <class F>
        Coro(
            JobQueue::CoroCreator,
            JobQueue& jq,
            JobType type,
            std::string const& name,
            F&& f);

        Coro(Coro const&) = delete;
        Coro&
        operator=(Coro const&) = delete;

        Coro(Coro&&) = delete;
        Coro&
        operator=(Coro&&) = delete;

        virtual ~Coro();

        /** Suspend the execution of a running coroutine.

            The coroutine's stack is saved and the job thread on which the
            coroutine is executed is released back to its thread pool.

            @note Calling this when the coroutine is already suspended will
                  result in undefined behavior. A call to post() or resume()
                  must come first.
        */
        void
        yield() const;

        /** Schedule a job to execute the coroutine.

            Once the job starts running, the coroutine execution context is
            set up and execution begins either at the start of the coroutine
            or, if yield() was previous called, at the statement after that
            yield().

            @note Calling this can result in undefined behavior if
                  - the coroutine has finished executing by using 'return'
                    instead of a call to 'yield()'; or
                  - the coroutine is either executing or scheduled for
                    execution.

            @return true if the coro was scheduled on the job queue; false
                    otherwise.
        */
        bool
        post();

        /** Resume execution of a suspended coroutine on the current thread.

            The coroutine will continues execution from where it last left,
            i.e. in the statement following the yield() that the corooutine
            executed last.

            @note Calling this when the coroutine has either terminated its
                  own execution (by calling `return` instead of doing a call
                  to yield()) or is either scheduled for execution or is
                  already executing will result in undefined behavior.
        */
        void
        resume();

        /** true if the coroutine is still runnable (i.e. has not returned). */
        bool
        runnable() const;

        /** Once called, the Coro allows early exit without an assert. */
        void
        expectEarlyExit();

        /** Waits until coroutine returns from the user function. */
        void
        join();
    };

    using JobFunction = std::function<void()>;

    JobQueue(
        std::size_t threadCount,
        beast::insight::Collector::ptr const& collector,
        beast::Journal journal,
        Logs& logs,
        perf::PerfLog& perfLog);
    ~JobQueue();

    /** Adds a job to the JobQueue.

        @param type The type of job.
        @param name Name of the job.
        @param jobHandler Lambda with signature void (Job&).  Called when the
       job is executed.

        @return true if jobHandler added to queue.
    */
    template <
        typename JobHandler,
        typename = std::enable_if_t<std::is_same<
            decltype(std::declval<JobHandler&&>()()),
            void>::value>>
    bool
    addJob(JobType type, std::string const& name, JobHandler&& jobHandler)
    {
        if (auto optionalCountedJob =
                jobCounter_.wrap(std::forward<JobHandler>(jobHandler)))
        {
            addRefCountedJob(type, name, std::move(*optionalCountedJob));
            return true;
        }

        return false;
    }

    /** Creates a coroutine and adds a job to the queue which will run it.

        @param t The type of job.
        @param name Name of the job.
        @param f The actual coroutine body; has a signature of:
                 void(std::shared_ptr<Coro>)

        @return shared_ptr to posted Coro. nullptr if post was not successful.
    */
    template <class F>
    std::shared_ptr<Coro>
    postCoro(JobType t, std::string const& name, F&& f);

    /** Jobs waiting at this priority. */
    int
    getJobCount(JobType t) const;

    /** Jobs waiting plus running at this priority. */
    int
    getJobCountTotal(JobType t) const;

    /** All waiting jobs at or greater than this priority. */
    int
    getJobCountGE(JobType t) const;

    /** Returns a new load event for the particular job. */
    LoadEvent
    createLoadEvent(JobType t, std::string name);

    /** Add multiple load events. */
    void
    addLoadEvents(JobType t, int count, std::chrono::milliseconds elapsed);

    // Cannot be const because LoadMonitor has no const methods.
    bool
    isOverloaded();

    // Cannot be const because LoadMonitor has no const methods.
    Json::Value
    getJson(int c = 0);

    /** Block until no jobs running. */
    void
    rendezvous();

    void
    stop();

    [[nodiscard]] bool
    isStopping() const noexcept
    {
        return state_.load(std::memory_order_acquire) >= state::stopping;
    }

    [[nodiscard]] bool
    isStopped() const noexcept
    {
        return state_.load(std::memory_order_acquire) == state::stopped;
    }

    /** Returns the number of threads that this job queue is configured with. */
    unsigned int
    thread_count() const noexcept
    {
        return workers_.count();
    }

private:
    friend class Coro;

    /** Helper class to track which job types have at least one waiting job.

        One bit per JobType: set while the type's queue is non-empty. A
        type at its concurrency limit with jobs still queued keeps its
        bit set; dispatch discovers the saturation and moves on. Bits
        for special (limit 0) types are never set.
     */
    class TypeMask
    {
        std::atomic<std::uint64_t> mask_ = 0;

        static_assert(
            jobTypes.size() <= 64,
            "TypeMask must be able to represent every job type");

        [[nodiscard]] static constexpr std::uint64_t
        bit(JobType t) noexcept
        {
            return std::uint64_t{1} << static_cast<int>(t);
        }

    public:
        /** Mark a type as having waiting jobs. */
        void
        set(JobType t) noexcept
        {
            mask_.fetch_or(bit(t), std::memory_order::relaxed);
        }

        /** Mark a type as having no waiting jobs. */
        void
        clear(JobType t) noexcept
        {
            mask_.fetch_and(~bit(t), std::memory_order::relaxed);
        }

        /** Visit marked types in priority order, invoking a callback.

            This will iterate from the highest priority (greatest enum
            value) downward, until the callback either returns true or
            the candidates are exhausted.

            The callback is invoked with each candidate JobType, and
            should return true to accept a type (ending the scan) or
            false to reject it and continue.

            The candidate set is fixed when the scan begins; types
            marked or cleared during the scan (including by the
            callback) do not affect which types are visited.

            @return true if the callback accepted a type.
         */
        template <std::invocable<JobType> F>
        [[nodiscard]] bool
        scan(F&& f) const
        {
            auto bits = mask_.load(std::memory_order::relaxed);

            while (bits)
            {
                auto const shift = std::bit_width(bits) - 1;

                if (f(static_cast<JobType>(shift)))
                    return true;

                bits &= ~(std::uint64_t{1} << shift);
            }

            return false;
        }
    };

    /** Per-type state for jobs managed by the JobQueue.

        Each JobType corresponds to exactly one Data instance, which gathers
        everything scoped to that type: the queue of the pending jobs, event
        reporting, accounting counters and load tracking.

        @note All members are currently protected by JobQueue::mutex_.
              A later stage of this refactor will give each Data its
              own lock, at which point this class becomes an
              independent locking domain. When that happens, add the
              mutex here and align instances to
              std::hardware_destructive_interference_size to prevent
              false sharing between adjacent array elements.

        @note Instances are not movable by design: LoadEvent objects hold
              references into `load`, so a constructed instance cannot be
              relocated after construction.
     */
    struct alignas(64) Data
    {
         /** The first job of this type awaiting execution, or nullptr.

           Jobs form an intrusive singly-linked FIFO through Job::next:
           they are appended at `last` and dispatched from here, oldest
           first. Priority across types is established by the position
           of the type in the JobType enum (higher value runs first);
           within a type, jobs run in insertion order.

           Always nullptr for "special" (limit 0) types, which are
           never dispatched.
        */
        Job* first = nullptr;

        /** The last job of this type awaiting execution, or nullptr.

            New jobs are appended here. Meaningful only for appending;
            nullptr exactly when `first` is nullptr.
         */
        Job* last = nullptr;

        /** The number of jobs awaiting execution.

            Cached because the intrusive list cannot report its length
            in constant time; maintained at the same two sites that
            link and unlink jobs. Equals the length of the first/last
            list at all times.
         */
        int count = 0;

        /** The number of jobs of this type currently executing.

            Jobs of this type are dispatchable if and only if this
            value is below the type's configured concurrency limit;
            queued jobs at a type that is at its limit simply wait,
            and need no other bookkeeping.
         */
        int running = 0;

        /** Worker wakeups withheld because the type was at its limit.

            The thread pool requires that every wakeup finds a job to
            run, so a job enqueued while its type is at its concurrency
            limit must not generate one. This count records each such
            withheld wakeup; when a job of this type completes (freeing
            a slot), one deferred count is converted into a
            Workers::addTask() call, making a queued job runnable.
         */
        int deferred = 0;

        /** Insight event tracking time jobs spend queued. */
        beast::insight::Event dequeue;

        /** Insight event tracking time jobs spend executing. */
        beast::insight::Event execute;

        /** Latency tracking and overload detection for this type. */
        LoadMonitor load;

        /** Construct the state for a single job type.

            @param info The compile-time attributes of the job type.
            @param collector The insight collector used to create the
                             reporting events. No events are created
                             for special (limit 0) types.
            @param logs Used to acquire the journal for `load`.
         */
        Data(
            JobTypeInfo const& info,
            beast::insight::Collector::ptr const& collector,
            Logs& logs)
            : load(
                  info.averageLatency,
                  info.peakLatency,
                  logs.journal("LoadMonitor"))
        {
            if (!info.special())
            {
                dequeue = collector->make_event(std::string(info.name) + "_q");
                execute = collector->make_event(std::string(info.name));
            }
        }

        Data(Data const&) = delete;

        Data&
        operator=(Data const&) = delete;

        Data(Data&&) = delete;

        Data&
        operator=(Data&&) = delete;

        /** The number of jobs queued or executing for this type. */
        [[nodiscard]] int
        outstanding() const noexcept
        {
            return running + count;
        }

        /** The number of jobs of this type awaiting execution. */
        [[nodiscard]] int
        queued() const noexcept
        {
            return count;
        }
    };

    beast::Journal journal_;

    mutable std::mutex mutex_;

    ClosureCounter<void> jobCounter_;

    enum class state : std::uint8_t { running = 0, stopping = 1, stopped = 2 };

    std::atomic<state> state_ = state::running;

    /** Per-type job state, indexed by JobType.

        Holds one Data instance for every entry in the jobTypes table,
        including "special" (limit 0) types, whose queues are always
        empty but whose load monitors are still used via
        createLoadEvent() and addLoadEvents().

        Job scheduling is defined entirely by this array: dispatch walks
        it from the highest index downward, executing the oldest waiting
        job of the first type below its concurrency limit.

        All access is protected by mutex_. Constructed in place via an
        index-sequence expansion in the JobQueue constructor, relying
        on guaranteed copy elision since Data is immovable.
     */
    std::array<Data, jobTypes.size()> data_;

    // The number of coroutines (active or suspended)
    std::atomic<int> totalCoroutines_ = 0;

    // The number of suspended coroutines
    std::atomic<int> suspendedCoroutines_ = 0;

    std::atomic<std::size_t> totalJobs_{0};

    perf::PerfLog& perfLog_;

    Workers workers_;

    /** Bitmask of job types with at least one waiting job.

        Bit i is set iff data_[i].jobs is non-empty. The bit reflects
        waiting jobs only: a type at its concurrency limit with jobs
        still queued keeps its bit set; dispatch discovers the
        saturation and moves on. Bits for special (limit 0) types are
        never set.
     */
    TypeMask jobMask_;

    beast::insight::Collector::ptr collector_;
    beast::insight::Gauge jobCountGauge_;
    beast::insight::Gauge activeThreadsGauge_;
    beast::insight::Hook hook_;

    std::mutex quiesceMutex_;
    std::condition_variable quiesceCv_;

    // Adds a reference counted job to the JobQueue.
    //
    //    param type The type of job.
    //    param name Name of the job.
    //    param func A std::function<void()> that is called to do the work.
    void
    addRefCountedJob(JobType type, std::string const& name, JobFunction func);
    //
    //    return true if func added to queue.
    bool
    addRefCountedJob(
        JobType type,
        std::string const& name,
        JobFunction func);

    // Runs the next appropriate waiting Job.
    //
    // Pre-conditions:
    //  A RunnableJob must exist in the JobSet
    //
    // Post-conditions:
    //  The chosen RunnableJob will have Job::doJob() called.
    //
    // Invariants:
    //  <none>
    void
    processTask(unsigned int instance) override;

    // Called when an uncaught exception occurs while processing a task
    //
    // Invariants:
    //  <none>
    void
    uncaughtException(unsigned int instance, std::exception_ptr eptr) override;
};

/** RPC command handling details:

    An RPC command is received and is handled via ServerHandler(HTTP) or
    Handler(websocket), depending on the connection type. The handler then calls
    the JobQueue::postCoro() method to create a coroutine and run it at a later
    point. This frees up the handler thread and allows it to continue handling
    other requests while the RPC command completes its work asynchronously.

    postCoro() creates a Coro object. When the Coro ctor is called, and its
    coro_ member is initialized (a boost::coroutines::pull_type), execution
    automatically passes to the coroutine, which we don't want at this point,
    since we are still in the handler thread context. It's important to note
    here that construction of a boost pull_type automatically passes execution
    to the coroutine. A pull_type object automatically generates a push_type
    that is passed as a parameter (do_yield) in the signature of the function
    the pull_type was created with. This function is immediately called during
    coro_ construction and within it, Coro::yield_ is assigned the push_type
    parameter (do_yield) address and called (yield()) so we can return execution
    back to the caller's stack.

    postCoro() then calls Coro::post(), which schedules a job on the job
    queue to continue execution of the coroutine in a JobQueue worker thread at
    some later time. When the job runs, we lock on the Coro::mutex_ and call
    coro_ which continues where we had left off. Since we the last thing we did
    in coro_ was call yield(), the next thing we continue with is calling the
    function param f, that was passed into Coro ctor. It is within this
    function body that the caller specifies what he would like to do while
    running in the coroutine and allow them to suspend and resume execution.
    A task that relies on other events to complete, such as path finding, calls
    Coro::yield() to suspend its execution while waiting on those events to
    complete and continue when signaled via the Coro::post() method.

    There is a potential race condition that exists here where post() can get
    called before yield() after f is called. Technically the problem only occurs
    if the job that post() scheduled is executed before yield() is called.
    If the post() job were to be executed before yield(), undefined behavior
    would occur. The lock ensures that coro_ is not called again until we exit
    the coroutine. At which point a scheduled resume() job waiting on the lock
    would gain entry, harmlessly call coro_ and immediately return as we have
    already completed the coroutine.

    The race condition occurs as follows:

    1. The coroutine is running.
    2. The coroutine is about to suspend, but before it can do so, it must
       arrange for some event to wake it up.
    3. The coroutine arranges for some event to wake it up.
    4. Before the coroutine can suspend, that event occurs and the
       resumption of the coroutine is scheduled on the job queue.
    5. Again, before the coroutine can suspend, the resumption of the coroutine
       is dispatched.
    6. Again, before the coroutine can suspend, the resumption code runs the
       coroutine.

    The coroutine is now running in two threads. The lock prevents this from
    happening as step 6 will block until the lock is released which only
    happens after the coroutine completes.
*/

}  // namespace ripple

#include <xrpld/core/Coro.ipp>

namespace ripple {

template <class F>
std::shared_ptr<JobQueue::Coro>
JobQueue::postCoro(JobType t, std::string const& name, F&& f)
{
    if (state_.load(std::memory_order_acquire) != state::running)
        return nullptr;

    auto coro = std::make_shared<Coro>(
        CoroCreator{}, *this, t, name, std::forward<F>(f));

    if (!coro->post())
    {
        // The coroutine was not successfully posted, so we disable it. That
        // way its destructor can run with no negative side effects; then we
        // can destroy it.
        coro->expectEarlyExit();
        coro.reset();
    }

    return coro;
}

}  // namespace ripple

#endif
