#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/LoadEvent.h>
#include <xrpl/core/LoadMonitor.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace xrpl {

/**
 * Job type identifiers
 *
 * The position of a job type in this enum indicates the type's relative
 * priority (a.k.a. importance) with respect to earlier jobs, with lower
 * values indicating lower priority.
 *
 * Please leave specific priority levels numerically unspecified. If you
 * wish to insert a job at a specific priority then simply add it at the
 * right relative location.
 */
enum class JobType : std::uint16_t {
    Pack,                 ///< Make a fetch pack for a peer
    PublishOldLedger,     ///< An old ledger has been accepted
    Client,               ///< A placeholder for the priority of all client jobs
    ClientSubscribe,      ///< A websocket subscription by a client
    ClientFeeChange,      ///< Subscription for fee change by a client
    ClientConsensus,      ///< Subscription for consensus state change by a client
    ClientAcctHist,       ///< Subscription for account history by a client
    ClientRpc,            ///< Client RPC request
    ClientWebsocket,      ///< Client websocket request
    Rpc,                  ///< A websocket command from the client
    Sweep,                ///< Sweep for stale structures
    UntrustedValidation,  ///< A validation from an untrusted source
    Manifest,             ///< A validator's manifest
    UpdatePf,             ///< Update pathfinding requests
    TransactionL,         ///< A local transaction
    ReplayReq,            ///< Peer request a ledger delta or a skip list
    LedgerReq,            ///< Peer request ledger/txnset data
    UntrustedProposal,    ///< A proposal from an untrusted source
    ReplayTask,           ///< A Ledger replay task/subtask
    Transaction,          ///< A transaction received from the network
    MissingTxn,           ///< Request missing transactions
    RequestedTxn,         ///< Reply with requested transactions
    Batch,                ///< Apply batched transactions
    LedgerData,           ///< Received data for a ledger we're acquiring
    Advance,              ///< Advance validated/acquired ledgers
    PublishLedger,        ///< Publish a fully-accepted ledger
    TxnData,              ///< Fetch a proposed set
    WriteAheadLog,        ///< Write-ahead logging
    TrustedValidation,    ///< A validation from a trusted source
    Write,                ///< Write out hashed objects
    Accept,               ///< Accept a consensus ledger
    TrustedProposal,      ///< A proposal from a trusted source
    NetopCluster,         ///< NetworkOPs cluster peer report
    NetopTimer,           ///< NetworkOPs net timer processing
    Admin,                ///< An administrative operation

    // Special job types which are not dispatched by the job pool
    Peer,
    Disk,
    TxnProc,
    ObSetup,
    PathFind,
    HoRead,
    HoWrite,
    Generic,  ///< Used just to measure time

    // Node store monitoring
    NsSyncRead,
    NsAsyncRead,
    NsWrite,
};

/**
 * The type-erased callable that holds the work a job performs.
 *
 * This is std::move_only_function where the standard library provides it,
 * and std::function otherwise. Depending on the implementation, the former
 * can hold a small callable in place where the latter would allocate.
 *
 * The two differ in ways that code using this alias must not depend on:
 *
 * - The callable must be copy constructible. std::move_only_function does
 *   not need that, but std::function does, and a move-only callable would
 *   only fail to compile where the fallback is in use.
 * - An empty instance must never be invoked. std::function throws
 *   std::bad_function_call; with std::move_only_function the behavior is
 *   undefined.
 * - The size of the type is not the same, so neither is the size of Job.
 *
 * @note The fallback exists for libc++, which does not implement P0288R9.
 *       Once every supported standard library does, remove the conditional
 *       and the first restriction with it.
 */
#if defined(__cpp_lib_move_only_function)
using JobFunction = std::move_only_function<void()>;
#else
using JobFunction = std::function<void()>;
#endif

class Job final : public CountedObject<Job>
{
public:
    using ClockType = std::chrono::steady_clock;

    Job() = delete;

    Job(std::string name, std::reference_wrapper<LoadSampler const> sampler, JobFunction&& job)
        : queued_(ClockType::now())
        , work_(std::move(job))
        , loadEvent_(sampler, std::move(name), false)
    {
    }

    Job(Job const&) = delete;

    Job&
    operator=(Job const&) = delete;

    Job(Job&&) = delete;

    Job&
    operator=(Job&&) = delete;

    /**
     * Returns the time when the job was queued.
     */
    [[nodiscard]] ClockType::time_point const&
    queueTime() const
    {
        return queued_;
    }

    /**
     * A description of this specific job.
     *
     * Unlike the name associated with the type of this job, which is fixed,
     * the description may include additional information or context that
     * distinguishes this from other jobs of the same type.
     */
    [[nodiscard]] std::string const&
    description() const
    {
        return loadEvent_.name();
    }

    void
    execute()
    {
        loadEvent_.start();

        work_();

        // Destroy the lambda, otherwise we won't include
        // its duration in the time measurement
        work_ = nullptr;
    }

    void
    chain(Job* job) noexcept
    {
        XRPL_ASSERT(next == nullptr, "xrpl::Job::chain : already chained");
        XRPL_ASSERT(job != nullptr, "xrpl::Job::chain : no job");

        next = job;
    }

    /**
     * The next job in the type's queue, or nullptr.
     *
     * Links the intrusive FIFO rooted at JobQueue::Data::first. This
     * field is owned by the JobQueue itself, which sets it when the
     * job is appended and when its predecessor is dispatched; it is
     * meaningless once this job itself has been dispatched.
     */
    Job* next = nullptr;

private:
    /**
     * The time when the job was queued.
     */
    ClockType::time_point const queued_;

    /**
     * The work that this job will perform, when executed.
     */
    JobFunction work_;

    /**
     * Tracking job performance.
     */
    LoadEvent loadEvent_;
};

}  // namespace xrpl
