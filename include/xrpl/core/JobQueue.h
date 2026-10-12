#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/EnumArray.h>
#include <xrpl/basics/LocalValue.h>
#include <xrpl/basics/Log.h>
#include <xrpl/core/ClosureCounter.h>
#include <xrpl/core/JobTypes.h>
#include <xrpl/core/detail/Workers.h>
#include <xrpl/json/json_value.h>

// Include only the specific Boost.Coroutine2 headers actually used here.
// Avoid `boost/coroutine2/all.hpp` because it transitively pulls in
// `boost/context/pooled_fixedsize_stack.hpp`, whose `.malloc()` / `.free()`
// member calls on `boost::pool` collide with MSVC's `_CRTDBG_MAP_ALLOC` macros
// in Debug builds (see cmake/XrplCompiler.cmake).
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/LoadEvent.h>
#include <xrpl/core/LoadMonitor.h>

#include <boost/context/protected_fixedsize_stack.hpp>
#include <boost/coroutine2/coroutine.hpp>

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

namespace xrpl {

namespace perf {
class PerfLog;
}  // namespace perf

/**
 * A pool of threads to perform work.
 *
 * A job posted will always run to completion.
 *
 * Coroutines that are suspended must be resumed,
 * and run to completion.
 *
 * When the JobQueue stops, it waits for all jobs
 * and coroutines to finish.
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
    /**
     * Coroutines must run to completion.
     */
    class Coro : public std::enable_shared_from_this<Coro>, public CountedObject<Coro>
    {
    private:
        detail::LocalValues lvs_;
        JobQueue& jq_;
        JobType const type_;
        std::string const name_;
        bool running_{false};
        std::mutex mutex_;
        std::mutex mutexRun_;
        std::condition_variable cv_;
        boost::coroutines2::coroutine<void>::push_type* yield_{};
        boost::coroutines2::coroutine<void>::pull_type coro_;
#ifndef NDEBUG
        bool finished_ = false;
#endif

    public:
        template <class F>
        Coro(CoroCreator, JobQueue&, JobType, std::string, F&&);

        // Not copyable or movable
        Coro(Coro const&) = delete;
        Coro&
        operator=(Coro const&) = delete;

        Coro(Coro&&) = delete;
        Coro&
        operator=(Coro&&) = delete;

        ~Coro();

        /**
         * Suspend coroutine execution.
         *  Effects:
         *    The coroutine's stack is saved.
         *    The associated Job thread is released.
         *  Note:
         *    The associated Job function returns.
         *    Undefined behavior if called consecutively without a corresponding
         * post.
         */
        void
        yield() const;

        /**
         * Schedule coroutine execution.
         * Effects:
         *   Returns immediately.
         *   A new job is scheduled to resume the execution of the coroutine.
         *   When the job runs, the coroutine's stack is restored and execution
         *     continues at the beginning of coroutine function or the
         * statement after the previous call to yield. Undefined behavior if
         * called after the coroutine has completed with a return (as opposed to
         * a yield()). Undefined behavior if post() or resume() called
         * consecutively without a corresponding yield.
         *
         * @return true if the Coro's job is added to the JobQueue.
         */
        bool
        post();

        /**
         * Resume coroutine execution.
         * Effects:
         *    The coroutine continues execution from where it last left off
         *      using this same thread.
         *    If the coroutine has already completed, returns immediately
         *      (handles the documented post-before-yield race condition).
         * Undefined behavior if resume() or post() called consecutively
         *   without a corresponding yield.
         */
        void
        resume();

        /**
         * Returns true if the Coro is still runnable (has not returned).
         */
        [[nodiscard]] bool
        runnable() const;

        /**
         * Once called, the Coro allows early exit without an assert.
         */
        void
        expectEarlyExit();

        /**
         * Waits until coroutine returns from the user function.
         */
        void
        join();
    };

    JobQueue(
        std::size_t threadCount,
        beast::insight::Collector::Ptr const& collector,
        beast::Journal journal,
        Logs& logs,
        perf::PerfLog& perfLog);
    ~JobQueue() override;

    /**
     * Adds a job to the JobQueue.
     *
     * @param type The type of job.
     * @param name Name of the job.
     * @param jobHandler Callable with signature void(). Called when the job is executed.
     *
     * @return true if jobHandler added to queue.
     *
     * @note The handler must be copy constructible, even though it is only
     *       ever moved: JobFunction is std::function on standard libraries
     *       that do not implement std::move_only_function, which means the
     *       target must be copyable. By enforcing the requirement here, we
     *       cause a move-only handler to fail to compile on every platform
     *       instead of only on those that use the fallback. The constraint
     *       can be removed when the std::function fallback is removed.
     */
    template <typename JobHandler>
    bool
    addJob(JobType type, std::string const& name, JobHandler&& jobHandler)
        requires(
            std::is_void_v<std::invoke_result_t<JobHandler>> &&
            std::copy_constructible<std::decay_t<JobHandler>>)
    {
        if (auto optionalCountedJob = jobCounter_.wrap(std::forward<JobHandler>(jobHandler)))
        {
            addRefCountedJob(type, name, std::move(*optionalCountedJob));
            return true;
        }

        return false;
    }

    /**
     * Creates a coroutine and adds a job to the queue which will run it.
     *
     * @param t The type of job.
     * @param name Name of the job.
     * @param f Has a signature of void(std::shared_ptr<Coro>). Called when the
     * job executes.
     *
     * @return shared_ptr to posted Coro.  nullptr if post was not successful.
     */
    template <class F>
    std::shared_ptr<Coro>
    postCoro(JobType t, std::string const& name, F&& f);

    /**
     * Jobs waiting at this priority.
     */
    int
    getJobCount(JobType t) const;

    /**
     * Jobs waiting plus running at this priority.
     */
    int
    getJobCountTotal(JobType t) const;

    /**
     * All waiting jobs at or greater than this priority.
     */
    int
    getJobCountGE(JobType t) const;

    /**
     * Returns a new load event for the particular job.
     */
    LoadEvent
    createLoadEvent(JobType t, std::string name);

    /**
     * Add multiple load events.
     */
    void
    addLoadEvents(JobType t, int count, std::chrono::milliseconds elapsed);

    // Cannot be const because LoadMonitor has no const methods.
    bool
    isOverloaded();

    // Cannot be const because LoadMonitor has no const methods.
    json::Value
    getJson(int c = 0);

    /**
     * Block until no jobs running.
     */
    void
    rendezvous();

    void
    stop();

    [[nodiscard]] bool
    isStopping() const noexcept
    {
        return state_.load(std::memory_order_acquire) >= State::Stopping;
    }

    [[nodiscard]] bool
    isStopped() const noexcept
    {
        return state_.load(std::memory_order_acquire) == State::Stopped;
    }

    /**
     * Returns the number of threads that this job queue is configured with.
     */
    [[nodiscard]] unsigned int
    threadCount() const noexcept
    {
        return workers_.count();
    }

private:
    friend class Coro;

    /**
     * Helper class to track which job types have at least one waiting job.
     *
     * One bit per JobType: set while the type's queue is non-empty. A
     * type at its concurrency limit with jobs still queued keeps its
     * bit set; dispatch discovers the saturation and moves on. Bits
     * for special (limit 0) types are never set.
     */
    class TypeMask
    {
        std::atomic<std::uint64_t> mask_ = 0;

        static_assert(kJobTypes.size() <= 64, "TypeMask must be able to represent every job type");

        [[nodiscard]] static constexpr std::uint64_t
        bit(JobType t) noexcept
        {
            return std::uint64_t{1} << static_cast<int>(t);
        }

    public:
        /**
         * Mark a type as having waiting jobs.
         */
        void
        set(JobType t) noexcept
        {
            mask_.fetch_or(bit(t), std::memory_order::relaxed);
        }

        /**
         * Mark a type as having no waiting jobs.
         */
        void
        clear(JobType t) noexcept
        {
            mask_.fetch_and(~bit(t), std::memory_order::relaxed);
        }

        /**
         * Visit marked types in priority order, invoking a callback.
         *
         * This will iterate from the highest priority (greatest enum
         * value) downward, until the callback either returns true or
         * the candidates are exhausted.
         *
         * The callback is invoked with each candidate JobType, and
         * should return true to accept a type (ending the scan) or
         * false to reject it and continue.
         *
         * The candidate set is fixed when the scan begins; types
         * marked or cleared during the scan (including by the
         * callback) do not affect which types are visited.
         *
         * @return true if the callback accepted a type.
         */
        template <std::invocable<JobType> F>
        [[nodiscard]] bool
        scan(F&& f) const
        {
            auto bits = mask_.load(std::memory_order::relaxed);

            while (bits != 0)
            {
                auto const shift = std::bit_width(bits) - 1;

                if (f(static_cast<JobType>(shift)))
                    return true;

                bits &= ~(std::uint64_t{1} << shift);
            }

            return false;
        }
    };

    /**
     * Per-type state for jobs managed by the JobQueue.
     *
     * Each JobType corresponds to exactly one Data instance, which gathers
     * everything scoped to that type: the queue of the pending jobs, event
     * reporting, accounting counters and load tracking.
     *
     * @note All members are currently protected by JobQueue::mutex_.
     *       A later stage of this refactor will give each Data its
     *       own lock, at which point this class becomes an
     *       independent locking domain. When that happens, add the
     *       mutex here and align instances to
     *       std::hardware_destructive_interference_size to prevent
     *       false sharing between adjacent array elements.
     *
     * @note Instances are not movable by design: LoadEvent objects hold
     *       references into `load`, so a constructed instance cannot be
     *       relocated after construction.
     */
    struct alignas(64) Data
    {
        /**
         * The first job of this type awaiting execution, or nullptr.
         *
         * Jobs form an intrusive singly-linked FIFO through Job::next:
         * they are appended at `last` and dispatched from here, oldest
         * first. Priority across types is established by the position
         * of the type in the JobType enum (higher value runs first);
         * within a type, jobs run in insertion order.
         *
         * Always nullptr for "special" (limit 0) types, which are
         * never dispatched.
         */
        Job* first = nullptr;

        /**
         * The last job of this type awaiting execution, or nullptr.
         *
         * New jobs are appended here. Meaningful only for appending;
         * nullptr exactly when `first` is nullptr.
         */
        Job* last = nullptr;

        /**
         * The number of jobs awaiting execution.
         *
         * Cached because the intrusive list cannot report its length
         * in constant time; maintained at the same two sites that
         * link and unlink jobs. Equals the length of the first/last
         * list at all times.
         */
        int count = 0;

        /**
         * The number of jobs of this type currently executing.
         *
         * Jobs of this type are dispatchable if and only if this
         * value is below the type's configured concurrency limit;
         * queued jobs at a type that is at its limit simply wait,
         * and need no other bookkeeping.
         */
        int running = 0;

        /**
         * Worker wakeups withheld because the type was at its limit.
         *
         * The thread pool requires that every wakeup finds a job to
         * run, so a job enqueued while its type is at its concurrency
         * limit must not generate one. This count records each such
         * withheld wakeup; when a job of this type completes (freeing
         * a slot), one deferred count is converted into a
         * Workers::addTask() call, making a queued job runnable.
         */
        int deferred = 0;

        /**
         * Insight event tracking time jobs spend queued.
         */
        beast::insight::Event dequeue;

        /**
         * Insight event tracking time jobs spend executing.
         */
        beast::insight::Event execute;

        /**
         * Latency tracking and overload detection for this type.
         */
        LoadMonitor load;

        /**
         * Construct the state for a single job type.
         *
         * @param info The compile-time attributes of the job type.
         * @param collector The insight collector used to create the
         *                  reporting events. No events are created
         *                  for special (limit 0) types.
         * @param logs Used to acquire the journal for `load`.
         */
        Data(JobTypeInfo const& info, beast::insight::Collector::Ptr const& collector, Logs& logs)
            : load(info.averageLatency, info.peakLatency, logs.journal("LoadMonitor"))
        {
            if (!info.special())
            {
                dequeue = collector->makeEvent(std::string(info.name) + "_q");
                execute = collector->makeEvent(std::string(info.name));
            }
        }

        Data(Data const&) = delete;

        Data&
        operator=(Data const&) = delete;

        Data(Data&&) = delete;

        Data&
        operator=(Data&&) = delete;

        /**
         * The number of jobs queued or executing for this type.
         */
        [[nodiscard]] int
        outstanding() const noexcept
        {
            return running + count;
        }

        /**
         * The number of jobs of this type awaiting execution.
         */
        [[nodiscard]] int
        queued() const noexcept
        {
            return count;
        }
    };

    beast::Journal journal_;

    mutable std::mutex mutex_;

    ClosureCounter<void> jobCounter_;

    enum class State : std::uint8_t { Running = 0, Stopping = 1, Stopped = 2 };

    std::atomic<State> state_ = State::Running;

    /**
     * Per-type job state, indexed by JobType.
     *
     * Holds one Data instance for every entry in the kJobTypes table,
     * including "special" (limit 0) types, whose queues are always
     * empty but whose load monitors are still used via
     * createLoadEvent() and addLoadEvents().
     *
     * Job scheduling is defined entirely by this array: dispatch walks
     * it from the highest index downward, executing the oldest waiting
     * job of the first type below its concurrency limit.
     *
     * All access is protected by mutex_. Constructed in place via an
     * index-sequence expansion in the JobQueue constructor, relying
     * on guaranteed copy elision since Data is immovable.
     */
    EnumArray<Data, kJobTypes.size(), JobType> data_;

    // The number of coroutines (active or suspended)
    std::atomic<int> totalCoroutines_ = 0;

    // The number of suspended coroutines
    std::atomic<int> suspendedCoroutines_ = 0;

    // The number of jobs that are queued or executing
    std::atomic<std::size_t> totalJobs_{0};

    perf::PerfLog& perfLog_;

    /**
     * Bitmask of job types with at least one waiting job.
     *
     * Bit i is set iff data_[i] has at least one queued job. The bit
     * reflects waiting jobs only: a type at its concurrency limit with
     * jobs still queued keeps its bit set; dispatch discovers the
     * saturation and moves on. Bits for special (limit 0) types are
     * never set.
     */
    TypeMask jobMask_;

    std::mutex quiesceMutex_;
    std::condition_variable quiesceCv_;

    // The worker threads use every member declared above, including on
    // their way out of processTask(). This must be declared after all of
    // them, so that the threads are joined before any of them is destroyed.
    Workers workers_;

    beast::insight::Collector::Ptr collector_;
    beast::insight::Gauge jobCountGauge_;
    beast::insight::Hook hook_;

    // Adds a reference counted job to the JobQueue.
    //
    //    param type The type of job.
    //    param name The name of the job.
    //    param func The work to perform when the job is executed.
    void
    addRefCountedJob(JobType type, std::string const& name, JobFunction&& func);

    // Runs the next appropriate waiting Job.
    //
    // Pre-conditions:
    //  A RunnableJob must exist in the JobSet
    //
    // Post-conditions:
    //  The chosen RunnableJob will have Job::execute() called.
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
    uncaughtException(unsigned int instance, std::exception_ptr e) override;
};

/*
    An RPC command is received and is handled via ServerHandler(HTTP) or
    Handler(websocket), depending on the connection type. The handler then calls
    the JobQueue::postCoro() method to create a coroutine and run it at a later
    point. This frees up the handler thread and allows it to continue handling
    other requests while the RPC command completes its work asynchronously.

    postCoro() creates a Coro object. When the Coro ctor is called, and its
    coro_ member is initialized (a boost::coroutines::pull_type), execution
    automatically passes to the coroutine, which we don't want at this point,
    since we are still in the handler thread context. It's important to note
   here that construction of a boost pull_type automatically passes execution to
   the coroutine. A pull_type object automatically generates a push_type that is
    passed as a parameter (do_yield) in the signature of the function the
    pull_type was created with. This function is immediately called during coro_
    construction and within it, Coro::yield_ is assigned the push_type
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
    would gain entry. resume() checks if the coroutine has already completed
    (coro_ converts to false) and, if so, skips invoking operator() since
    calling operator() on a completed boost::coroutine2 pull_type is undefined
    behavior.

    The race condition occurs as follows:

        1- The coroutine is running.
        2- The coroutine is about to suspend, but before it can do so, it must
            arrange for some event to wake it up.
        3- The coroutine arranges for some event to wake it up.
        4- Before the coroutine can suspend, that event occurs and the
   resumption of the coroutine is scheduled on the job queue. 5- Again, before
   the coroutine can suspend, the resumption of the coroutine is dispatched. 6-
   Again, before the coroutine can suspend, the resumption code runs the
            coroutine.
        The coroutine is now running in two threads.

        The lock prevents this from happening as step 6 will block until the
            lock is released which only happens after the coroutine completes.
*/

}  // namespace xrpl

#include <xrpl/core/Coro.ipp>  // IWYU pragma: keep

namespace xrpl {

template <class F>
std::shared_ptr<JobQueue::Coro>
JobQueue::postCoro(JobType t, std::string const& name, F&& f)
{
    if (state_.load(std::memory_order_acquire) != State::Running)
        return nullptr;

    // The first parameter is a detail type to make construction private and
    // the last is the function the coroutine runs, which has a signature of
    //    void(std::shared_ptr<Coro>)
    auto coro = std::make_shared<Coro>(CoroCreator{}, *this, t, name, std::forward<F>(f));

    if (!coro->post())
    {
        // The Coro was not successfully posted.  Disable it so it's destructor
        // can run with no negative side effects.  Then destroy it.
        coro->expectEarlyExit();
        coro.reset();
    }

    return coro;
}

}  // namespace xrpl
