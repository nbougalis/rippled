#pragma once

#include <xrpl/basics/EnumArray.h>
#include <xrpl/core/Job.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace xrpl {

/**
 * Defines the fixed attributes of a particular type of job.
 *
 * These attributes are defined at compile time and never change. They
 * include things like the name of the job, the limit of concurrent jobs
 * of this type, etc.
 */
struct JobTypeInfo
{
    JobType const type;

    /**
     * The job's display name
     */
    std::string_view const name;

    /**
     * The limit on the number of running jobs for this job type.
     *
     * A limit of 0 marks this as a "special job" which is not
     * dispatched via the job queue.
     */
    int const limit;

    /**
     * Average and peak latencies for this job type, if relevant.
     */
    /** @{ */
    std::optional<std::chrono::milliseconds> const averageLatency;
    std::optional<std::chrono::milliseconds> const peakLatency;
    /** @} */

    // Not default constructible
    JobTypeInfo() = delete;

    constexpr JobTypeInfo(
        JobType type,
        std::string_view name,
        int limit,
        std::optional<std::chrono::milliseconds> averageLatency = {},
        std::optional<std::chrono::milliseconds> peakLatency = {})
        : type(type)
        , name(name)
        , limit(limit)
        , averageLatency(averageLatency)
        , peakLatency(peakLatency)
    {
    }

    [[nodiscard]] constexpr bool
    special() const
    {
        return limit == 0;
    }
};

/**
 * The attributes of every job type, indexed by JobType.
 */
inline constexpr auto kJobTypes = [] consteval {
    using namespace std::chrono_literals;

    constexpr int maxLimit = std::numeric_limits<int>::max();

    constexpr JobTypeInfo jobs[] = {
        // clang-format off
        //                                                                   avg     peak
        // JobType          name                                    limit  latency  latency
        {JobType::Pack,                "makeFetchPack",                 1                  },
        {JobType::PublishOldLedger,    "publishAcqLedger",              2, 10000ms, 15000ms},
        {JobType::Client,              "clientCommand",          maxLimit,  2000ms,  5000ms},
        {JobType::ClientSubscribe,     "clientSubscribe",        maxLimit,  2000ms,  5000ms},
        {JobType::ClientFeeChange,     "clientFeeChange",        maxLimit,  2000ms,  5000ms},
        {JobType::ClientConsensus,     "clientConsensus",        maxLimit,  2000ms,  5000ms},
        {JobType::ClientAcctHist,      "clientAccountHistory",   maxLimit,  2000ms,  5000ms},
        {JobType::ClientRpc,           "clientRPC",              maxLimit,  2000ms,  5000ms},
        {JobType::ClientWebsocket,     "clientWebsocket",        maxLimit,  2000ms,  5000ms},
        {JobType::Rpc,                 "RPC",                    maxLimit                  },
        {JobType::Sweep,               "sweep",                         1                  },
        {JobType::UntrustedValidation, "untrustedValidation",    maxLimit,  2000ms,  5000ms},
        {JobType::Manifest,            "manifest",               maxLimit,  2000ms,  5000ms},
        {JobType::UpdatePf,            "updatePaths",                   1                  },
        {JobType::TransactionL,        "localTransaction",       maxLimit,   100ms,   500ms},
        {JobType::ReplayReq,           "ledgerReplayRequest",          10,   250ms,  1000ms},
        {JobType::LedgerReq,           "ledgerRequest",                 3                  },
        {JobType::UntrustedProposal,   "untrustedProposal",      maxLimit,   500ms,  1250ms},
        {JobType::ReplayTask,          "ledgerReplayTask",       maxLimit                  },
        {JobType::Transaction,         "transaction",            maxLimit,   250ms,  1000ms},
        {JobType::MissingTxn,          "handleHaveTransactions",     1200                  },
        {JobType::RequestedTxn,        "doTransactions",             1200                  },
        {JobType::Batch,               "batch",                  maxLimit,   250ms,  1000ms},
        {JobType::LedgerData,          "ledgerData",                    3                  },
        {JobType::Advance,             "advanceLedger",          maxLimit                  },
        {JobType::PublishLedger,       "publishNewLedger",       maxLimit,  3000ms,  4500ms},
        {JobType::TxnData,             "fetchTxnData",                  5                  },
        {JobType::WriteAheadLog,       "writeAhead",             maxLimit,  1000ms,  2500ms},
        {JobType::TrustedValidation,   "trustedValidation",      maxLimit,   500ms,  1500ms},
        {JobType::Write,               "writeObjects",           maxLimit,  1750ms,  2500ms},
        {JobType::Accept,              "acceptLedger",           maxLimit                  },
        {JobType::TrustedProposal,     "trustedProposal",        maxLimit,   100ms,   500ms},
        {JobType::NetopCluster,        "clusterReport",                 1,  9999ms,  9999ms},
        {JobType::NetopTimer,          "heartbeat",                     1,   999ms,   999ms},
        {JobType::Admin,               "administration",         maxLimit                  },
        {JobType::Peer,                "peerCommand",                   0,   200ms,  2500ms},
        {JobType::Disk,                "diskAccess",                    0,   500ms,  1000ms},
        {JobType::TxnProc,             "processTransaction",            0                  },
        {JobType::ObSetup,             "orderBookSetup",                0                  },
        {JobType::PathFind,            "pathFind",                      0                  },
        {JobType::HoRead,              "nodeRead",                      0                  },
        {JobType::HoWrite,             "nodeWrite",                     0                  },
        {JobType::Generic,             "generic",                       0                  },
        {JobType::NsSyncRead,          "SyncReadNode",                  0                  },
        {JobType::NsAsyncRead,         "AsyncReadNode",                 0                  },
        {JobType::NsWrite,             "WriteNode",                     0                  },
        // clang-format on
    };

    // Ensure that the order of entries in the table corresponds to the
    // order of entries in the enum:
    std::underlying_type_t<JobType> idx = 0;

    for (auto const& jt : jobs)
    {
        if (static_cast<std::underlying_type_t<JobType>>(jt.type) != idx)
            throw "The JobTypes array is improperly ordered";

        ++idx;
    }

    return EnumArray<JobTypeInfo, std::size(jobs), JobType>{std::to_array(jobs)};
}();

}  // namespace xrpl
