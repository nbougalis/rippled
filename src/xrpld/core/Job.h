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

#ifndef RIPPLE_CORE_JOB_H_INCLUDED
#define RIPPLE_CORE_JOB_H_INCLUDED

#include <xrpld/core/ClosureCounter.h>
#include <xrpld/core/LoadEvent.h>
#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/core/CurrentThreadName.h>

#include <functional>

namespace ripple {

/** Job type identifiers

    The position of a job type in this enum indicates the type's relative
    priority (a.k.a. importance) with respect to earlier jobs, with lower
    values indicating lower priority.

    Please leave specific priority levels numerically unspecified. If you
    wish to insert a job at a specific priority then simply add it at the
    right relative location.
 */
enum JobType : std::uint16_t {
    jtPACK,               // Make a fetch pack for a peer
    jtPUBOLDLEDGER,       // An old ledger has been accepted
    jtCLIENT,             // A placeholder for the priority of all jtCLIENT jobs
    jtCLIENT_SUBSCRIBE,   // A websocket subscription by a client
    jtCLIENT_FEE_CHANGE,  // Subscription for fee change by a client
    jtCLIENT_CONSENSUS,   // Subscription for consensus state change by a client
    jtCLIENT_ACCT_HIST,   // Subscription for account history by a client
    jtCLIENT_RPC,         // Client RPC request
    jtCLIENT_WEBSOCKET,   // Client websocket request
    jtRPC,                // A websocket command from the client
    jtSWEEP,              // Sweep for stale structures
    jtVALIDATION_ut,      // A validation from an untrusted source
    jtMANIFEST,           // A validator's manifest
    jtUPDATE_PF,          // Update pathfinding requests
    jtTRANSACTION_l,      // A local transaction
    jtREPLAY_REQ,         // Peer request a ledger delta or a skip list
    jtLEDGER_REQ,         // Peer request ledger/txnset data
    jtPROPOSAL_ut,        // A proposal from an untrusted source
    jtREPLAY_TASK,        // A Ledger replay task/subtask
    jtTRANSACTION,        // A transaction received from the network
    jtMISSING_TXN,        // Request missing transactions
    jtREQUESTED_TXN,      // Reply with requested transactions
    jtBATCH,              // Apply batched transactions
    jtLEDGER_DATA,        // Received data for a ledger we're acquiring
    jtADVANCE,            // Advance validated/acquired ledgers
    jtPUBLEDGER,          // Publish a fully-accepted ledger
    jtTXN_DATA,           // Fetch a proposed set
    jtWAL,                // Write-ahead logging
    jtVALIDATION_t,       // A validation from a trusted source
    jtWRITE,              // Write out hashed objects
    jtACCEPT,             // Accept a consensus ledger
    jtPROPOSAL_t,         // A proposal from a trusted source
    jtNETOP_CLUSTER,      // NetworkOPs cluster peer report
    jtNETOP_TIMER,        // NetworkOPs net timer processing
    jtADMIN,              // An administrative operation

    // Special job types which are not dispatched by the job pool
    jtPEER,
    jtDISK,
    jtTXN_PROC,
    jtOB_SETUP,
    jtPATH_FIND,
    jtHO_READ,
    jtHO_WRITE,
    jtGENERIC,  // Used just to measure time

    // Node store monitoring
    jtNS_SYNC_READ,
    jtNS_ASYNC_READ,
    jtNS_WRITE,
};

class Job : public CountedObject<Job>
{
public:
    using clock_type = std::chrono::steady_clock;

    Job() = delete;

    Job(JobType type,
        std::string name,
        std::reference_wrapper<LoadSampler const> sampler,
        std::function<void()> job)
        : type_(type)
        , queued_(clock_type::now())
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

    [[nodiscard]] JobType
    getType() const
    {
        return type_;
    }

    /** Returns the time when the job was queued. */
    [[nodiscard]] clock_type::time_point const&
    queue_time() const
    {
        return queued_;
    }

    /** A description of this specific job.

        Unlike the name associated with the type of this job, which is fixed,
        the description may include additional information or context that
        distinguishes this from other jobs of the same type.
     */
    [[maybe_unused]] std::string const&
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
        XRPL_ASSERT(next == nullptr, "ripple::Job::chain : already chained");
        XRPL_ASSERT(job != nullptr, "ripple::Job::chain : no job");

        next = job;
    }

    /** The next job in the type's queue, or nullptr.

        Links the intrusive FIFO rooted at JobQueue::Data::head. This
        field is owned by the JobQueue itself, which sets it when the
        job is appended and when its predecessor is dispatched; it is
        meaningless once this job itself has been dispatched.
     */
    Job* next = nullptr;

private:
    /** The job's underlying type. */
    JobType const type_;

    /** The time when the job was queued. */
    clock_type::time_point const queued_;

    /** The work that this job will perform, when executed. */
    std::function<void()> work_;

    /** Tracking job performance. */
    LoadEvent loadEvent_;
};

}  // namespace ripple

#endif
