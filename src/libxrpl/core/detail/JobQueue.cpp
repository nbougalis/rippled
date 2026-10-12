#include <xrpl/core/JobQueue.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/SlabAllocator.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobTypes.h>
#include <xrpl/core/LoadEvent.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/json_value.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <mutex>
#include <new>
#include <numeric>
#include <string>
#include <thread>
#include <utility>

namespace xrpl {

namespace {

/**
 * The allocator that every Job comes from.
 *
 * Jobs are short-lived objects of a single size: each one is allocated by
 * addRefCountedJob() and released by processTask(), after it has executed
 * on a different thread. A slab allocator serves that pattern from a few
 * large, contiguous buffers instead of the general purpose heap.
 *
 * If no slab can provide memory, the request is served by the heap.
 *
 * @note This is shared by every JobQueue in the process and is deliberately
 *       not a member: the allocator does not release buffers that may still
 *       hold live objects when it is destroyed, so its lifetime must not be
 *       tied to that of any one JobQueue.
 */
constinit slab::Allocator<Job, slab::HeapFallback, slab::Config<16384>> gJobSlab;

}  // namespace

JobQueue::JobQueue(
    std::size_t threadCount,
    beast::insight::Collector::Ptr const& collector,
    beast::Journal journal,
    Logs& logs,
    perf::PerfLog& perfLog)
    : journal_(journal)
    , data_([&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<Data, sizeof...(I)>{
            Data{kJobTypes[static_cast<JobType>(I)], collector, logs}...};
    }(std::make_index_sequence<kJobTypes.size()>()))
    , perfLog_(perfLog)
    , workers_(*this, "JobQueue", threadCount, Workers::WakePolicy::Eager)
    , collector_(collector)
{
    JLOG(journal_.info()) << "Using " << threadCount << " threads";

    // This is important to do before dispatching any jobs
    // so that the logger knows the number of threads.
    perfLog_.resizeJobs(threadCount);

    hook_ = collector_->makeHook([this] {
        int jobs = 0;

        {
            std::scoped_lock const lock(mutex_);

            for (auto const& d : data_)
                jobs += d.count;
        }

        jobCountGauge_ = jobs;
    });

    jobCountGauge_ = collector_->makeGauge("job_count");
}

JobQueue::~JobQueue()
{
    // The owner normally calls stop() during an orderly shutdown, but that
    // never happens if the application fails to start. Either way, no job
    // may be queued or running past this point.
    stop();

    // Must unhook before destroying
    hook_ = beast::insight::Hook();
}

void
JobQueue::addRefCountedJob(JobType type, std::string const& name, JobFunction&& func)
{
    JLOG(journal_.debug()) << "Adding job '" << name << "' (" << kJobTypes[type].name << ")";

    XRPL_ASSERT(
        !kJobTypes[type].special(), "xrpl::JobQueue::addRefCountedJob : job type is dispatchable");

    // FIXME: Workaround incorrect client shutdown ordering
    // do not add jobs to a queue with no threads
    XRPL_ASSERT(
        (type >= JobType::Client && type <= JobType::ClientWebsocket) || workers_.count() > 0,
        "xrpl::JobQueue::addRefCountedJob : threads available or job "
        "requires no threads");

    // Every job is allocated here and released by processTask(), once it
    // has been executed.
    auto* const job = [&] {
        // Copying the name is the only step that can throw, so it is done
        // before taking memory: there is then nothing to give back on failure.
        std::string jobName(name);

        if (auto* const raw = gJobSlab.allocate()) [[likely]]
            return std::construct_at(
                static_cast<Job*>(raw),
                std::move(jobName),
                data_[type].load.sample(),
                std::move(func));

        Throw<std::bad_alloc>();
    }();

    // Counted before the job becomes visible in any queue: a quiescence
    // waiter that reads zero is guaranteed no job is queued or
    // executing. Safe to count here: the node is already fully
    // constructed, and linking below cannot fail.
    totalJobs_.fetch_add(1, std::memory_order::relaxed);

    std::scoped_lock const lock(mutex_);

    auto& d = data_[type];

    if (d.last != nullptr)
    {
        d.last->chain(job);
    }
    else
    {
        d.first = job;
    }

    d.last = job;
    ++d.count;

    if (d.count == 1)
        jobMask_.set(type);

    // The new job is now counted by outstanding(), so we use
    // less-than-or-equal here, where a check made before the insertion
    // would use less-than.
    if (d.outstanding() <= kJobTypes[type].limit)
    {
        workers_.addTask();
    }
    else
    {
        ++d.deferred;
    }

    perfLog_.jobQueue(type);
}

int
JobQueue::getJobCount(JobType t) const
{
    std::scoped_lock const lock(mutex_);
    return data_[t].queued();
}

int
JobQueue::getJobCountTotal(JobType t) const
{
    std::scoped_lock const lock(mutex_);
    return data_[t].outstanding();
}

int
JobQueue::getJobCountGE(JobType t) const
{
    std::scoped_lock const lock(mutex_);

    return std::accumulate(
        data_.begin() + static_cast<std::size_t>(t), data_.end(), 0, [](int sum, Data const& d) {
            return sum + d.queued();
        });
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
    return std::ranges::any_of(data_, [](auto& d) { return d.load.isOver(); });
}

json::Value
JobQueue::getJson(int)
{
    using namespace std::chrono_literals;
    json::Value ret(json::ValueType::Object);

    ret["threads"] = workers_.count();
    ret["coro.suspended"] = suspendedCoroutines_.load();
    ret["coro.total"] = totalCoroutines_.load();

    json::Value priorities = json::ValueType::Array;

    std::scoped_lock const lock(mutex_);

    for (auto const& info : kJobTypes)
    {
        if (info.type == JobType::Generic)
            continue;

        auto& d = data_[info.type];

        if (auto const s = d.load.getStats();
            (s.count != 0) || (d.count != 0) || (d.running != 0) || (s.peakLatency != 0ms))
        {
            json::Value& pri = priorities.append(json::ValueType::Object);

            pri["job_type"] = std::string(info.name);

            if (s.isOverloaded)
                pri["over_target"] = true;

            if (d.count != 0)
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
    quiesceCv_.wait(lock, [this] { return totalJobs_.load(std::memory_order_acquire) == 0; });
}

void
JobQueue::stop()
{
    if (State expected = State::Running;
        !state_.compare_exchange_strong(expected, State::Stopping, std::memory_order_acq_rel))
    {
        while (state_.load(std::memory_order_acquire) != State::Stopped)
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

    XRPL_ASSERT(suspendedCoroutines_ == 0, "xrpl::JobQueue::stop : no coros suspended");

    state_.store(State::Stopped, std::memory_order_release);
}

void
JobQueue::uncaughtException(unsigned int instance, std::exception_ptr e)
{
    try
    {
        if (e)
            std::rethrow_exception(e);

        logicError(
            beast::getCurrentThreadName() +
            ": Uncaught exception handler invoked with no exception_ptr");
    }
    catch (std::exception const& ex)
    {
        logicError(
            beast::getCurrentThreadName() +
            ": Exception caught during task processing: " + ex.what());
    }
    catch (...)
    {
        logicError(
            beast::getCurrentThreadName() + ": Unknown exception caught during task processing");
    }
}

void
JobQueue::processTask(unsigned int instance)
{
    // Take the oldest waiting job from the highest-priority type
    // that is under its concurrency limit.
    auto const [job, type] = [this] {
        Job* cj = nullptr;
        JobType ct{};

        std::scoped_lock const lock(mutex_);

        auto const found = jobMask_.scan([this, &cj, &ct](JobType t) {
            auto& d = data_[t];

            if (d.first == nullptr)
                return false;

            XRPL_ASSERT(
                d.running <= kJobTypes[t].limit,
                "xrpl::JobQueue::processTask : running within limit");

            // A type at its concurrency limit is skipped; its bit remains
            // set and a completing job of this type will re-enable it via
            // the deferred task machinery.
            if (d.running >= kJobTypes[t].limit)
                return false;

            ++d.running;

            cj = d.first;
            ct = t;
            d.first = cj->next;

            if (d.first == nullptr)
            {
                d.last = nullptr;
                jobMask_.clear(t);
            }

            --d.count;

            return true;
        });

        if (found && cj != nullptr) [[likely]]
            return std::pair{cj, ct};

        logicError("Attempt to get a job when none are available");
    }();

    using namespace std::chrono;

    auto const startTime = Job::ClockType::now();
    auto const qTime = ceil<microseconds>(startTime - job->queueTime());
    perfLog_.jobStart(type, qTime, startTime, instance);

    JLOG(journal_.trace()) << "Starting: " << kJobTypes[type].name << " (q_time=" << qTime.count()
                           << " microseconds)";

    // We run the accounting epilogue on both the normal path and during an
    // unwind, to keep the invariants of the pool intact even if when a job
    // throws. The exception, if any, bubbles up to Workers and comes back
    // via uncaughtException.
    ScopeExit const epilogue([&] noexcept {
        auto const xTime = ceil<microseconds>(Job::ClockType::now() - startTime);

        JLOG(journal_.trace()) << "Finished: " << kJobTypes[type].name
                               << " (x_time=" << xTime.count() << " microseconds)";

        if (xTime >= 10ms || qTime >= 10ms)
        {
            data_[type].dequeue.notify(qTime);
            data_[type].execute.notify(xTime);
        }

        perfLog_.jobFinish(type, xTime, instance);

        // All jobs are released here: stop()'s rendezvous guarantees the
        // queues drain before Workers can discard any tickets, so no job
        // is ever stranded.
        std::destroy_at(job);

        if (!gJobSlab.deallocate(job)) [[unlikely]]
            UNREACHABLE("xrpl::JobQueue::processTask : Job deallocation failed");

        {
            std::scoped_lock const lock(mutex_);

            auto& d = data_[type];

            // Queue a deferred task if possible. This doesn't mean the task
            // will run next; only that one is now runnable.
            if (d.deferred > 0)
            {
                XRPL_ASSERT(
                    d.outstanding() >= kJobTypes[type].limit,
                    "xrpl::JobQueue::processTask : deferral within limit");
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
                std::scoped_lock const quiesceLock(quiesceMutex_);
            }
            quiesceCv_.notify_all();
        }
    });

    job->execute();
}

}  // namespace xrpl
