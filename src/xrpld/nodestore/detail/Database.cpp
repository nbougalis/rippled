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

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/core/detail/Workers.h>
#include <xrpld/nodestore/Database.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/json/json.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/jss.h>
#include <chrono>

namespace ripple {
namespace NodeStore {

/** The thread pool and task handler for a database.

    The ordering of the base classes matters: Workers::Callback must be
    constructed before and must outlive Workers.
 */
class Database::DatabaseWorkers : public Workers::Callback, public Workers
{
    Database& db_;

public:
    explicit DatabaseWorkers(
        Database& db,
        unsigned int count,
        std::string_view name)
        : Workers(*this, name, count, WakePolicy::eager), db_(db)
    {
    }

    void
    processTask(unsigned int instance) override
    {
        auto node = [this]() -> decltype(db_.read_)::node_type {
            std::lock_guard lock(db_.readLock_);
            XRPL_ASSERT(
                !db_.read_.empty(),
                "ripple::Database::DatabaseWorkers::processTask : non-empty "
                "read queue");

            if (!db_.read_.empty()) [[likely]]
                return db_.read_.extract(db_.read_.begin());

            return {};
        }();

        if (!node)
            return;

        auto const& hash = node.key();
        auto const& data = node.mapped();

        auto const seqn = data[0].first;

        auto obj = db_.fetchNodeObject(hash, seqn, FetchType::async);

        for (auto const& req : data)
            req.second(
                (seqn == req.first) || db_.isSameDB(req.first, seqn)
                    ? obj
                    : db_.fetchNodeObject(hash, req.first, FetchType::async));
    }

    void
    uncaughtException(unsigned int instance, std::exception_ptr eptr) override
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
};

Database::Database(
    Scheduler& scheduler,
    unsigned int threads,
    Section const& config,
    beast::Journal journal,
    std::string_view name)
    : j_(journal)
    , scheduler_(scheduler)
    , earliestLedgerSeq_(
          get<std::uint32_t>(config, "earliest_seq", XRP_LEDGER_EARLIEST_SEQ))
{
    if (earliestLedgerSeq_ == 0)
        Throw<std::runtime_error>("Invalid earliest_seq");

    if (threads)
        workers_ = std::make_unique<DatabaseWorkers>(*this, threads, name);

    JLOG(debugLog().fatal()) << this << ": database '" << name
                             << "' created with " << threads << " read threads";
}

Database::~Database()
{
    // NOTE!
    // Any derived class should call the stop() method in its
    // destructor.  Otherwise, occasionally, the derived class may
    // crash during shutdown when its members are accessed by one of
    // these threads after the derived class is destroyed but before
    // this base class is destroyed.
    stop();
}

bool
Database::isStopping() const
{
    return readStopping_.load(std::memory_order_relaxed);
}

void
Database::stop()
{
    {
        std::lock_guard lock(readLock_);
        readStopping_.store(true, std::memory_order_relaxed);
    }

    if (workers_)
        workers_->stop();  // joins; dispatched tasks complete their callbacks

    {
        std::lock_guard lock(readLock_);
        read_.clear();  // discard undispatched fetches, as today
    }
}

void
Database::asyncFetch(
    uint256 const& hash,
    std::uint32_t seq,
    std::function<void(boost::intrusive_ptr<NodeObject> const&)>&& callback)
{
    // With no read pool, service the request on the caller's thread. The
    // callback contract is unchanged; only the asynchrony (and the dedup,
    // which is an optimization) is lost.
    if (!workers_)
    {
        if (!isStopping())
            callback(fetchNodeObject(hash, seq, FetchType::async));

        return;
    }

    bool newKey = [&]() {
        std::lock_guard lock(readLock_);
        if (isStopping())
            return false;

        auto& v = read_[hash];
        v.emplace_back(seq, std::move(callback));

        return (v.size() == 1);
    }();

    if (newKey)
        workers_->addTask();
}

void
Database::importInternal(Backend& dstBackend, Database& srcDB)
{
    Batch batch;
    batch.reserve(batchWritePreallocationSize);
    auto storeBatch = [&]() {
        try
        {
            dstBackend.storeBatch(batch);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "Database::importInternal: Exception caught "
                                "from storeBatch: "
                             << e.what();
            return;
        }

        std::uint64_t sz{0};
        for (auto const& nodeObject : batch)
            sz += nodeObject->data().size();
        storeStats(batch.size(), sz);
        batch.clear();
    };

    srcDB.for_each([&](boost::intrusive_ptr<NodeObject> nodeObject) {
        XRPL_ASSERT(
            nodeObject,
            "ripple::NodeStore::Database::importInternal : non-null node");
        if (!nodeObject)  // This should never happen
            return;

        batch.emplace_back(std::move(nodeObject));
        if (batch.size() >= batchWritePreallocationSize)
            storeBatch();
    });

    if (!batch.empty())
        storeBatch();
}

// Perform a fetch and report the time it took
boost::intrusive_ptr<NodeObject>
Database::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    FetchType fetchType,
    bool duplicate)
{
    FetchReport fetchReport(fetchType);

    using namespace std::chrono;
    auto const begin{steady_clock::now()};

    auto nodeObject{fetchNodeObject(hash, ledgerSeq, fetchReport, duplicate)};
    auto dur = steady_clock::now() - begin;
    fetchDurationUs_ += duration_cast<microseconds>(dur).count();
    if (nodeObject)
    {
        fetchHitCount_.fetch_add(1, std::memory_order_relaxed);
        fetchSz_ += nodeObject->data().size();
    }
    fetchTotalCount_.fetch_add(1, std::memory_order_relaxed);

    fetchReport.elapsed = duration_cast<milliseconds>(dur);
    scheduler_.onFetch(fetchReport);
    return nodeObject;
}

bool
Database::storeLedger(
    Ledger const& srcLedger,
    std::shared_ptr<Backend> dstBackend)
{
    auto fail = [&](std::string const& msg) {
        JLOG(j_.error()) << "Source ledger sequence " << srcLedger.info().seq
                         << ". " << msg;
        return false;
    };

    if (srcLedger.info().hash.isZero())
        return fail("Invalid hash");
    if (srcLedger.info().accountHash.isZero())
        return fail("Invalid account hash");

    auto& srcDB = const_cast<Database&>(srcLedger.stateMap().family().db());
    if (&srcDB == this)
        return fail("Source and destination databases are the same");

    Batch batch;
    batch.reserve(batchWritePreallocationSize);
    auto storeBatch = [&, fname = __func__]() {
        std::uint64_t sz{0};
        for (auto const& nodeObject : batch)
            sz += nodeObject->data().size();

        try
        {
            dstBackend->storeBatch(batch);
        }
        catch (std::exception const& e)
        {
            fail(
                std::string("Exception caught in function ") + fname +
                ". Error: " + e.what());
            return false;
        }

        storeStats(batch.size(), sz);
        batch.clear();
        return true;
    };

    // Store ledger header
    {
        Serializer s(sizeof(std::uint32_t) + sizeof(LedgerInfo));
        s.add32(HashPrefix::ledgerMaster);
        addRaw(srcLedger.info(), s);
        auto nObj = NodeObject::createObject(
            hotLEDGER, std::move(s.modData()), srcLedger.info().hash);
        batch.emplace_back(std::move(nObj));
    }

    bool error = false;
    auto visit = [&](SHAMapTreeNode& node) {
        if (!isStopping())
        {
            if (auto nodeObject = srcDB.fetchNodeObject(
                    node.getHash().as_uint256(), srcLedger.info().seq))
            {
                batch.emplace_back(std::move(nodeObject));
                if (batch.size() < batchWritePreallocationSize || storeBatch())
                    return true;
            }
        }

        error = true;
        return false;
    };

    // Store the state map
    if (srcLedger.stateMap().getHash().isNonZero())
    {
        if (!srcLedger.stateMap().isValid())
            return fail("Invalid state map");

        srcLedger.stateMap().snapShot(false)->visitNodes(visit);
        if (error)
            return fail("Failed to store state map");
    }

    // Store the transaction map
    if (srcLedger.info().txHash.isNonZero())
    {
        if (!srcLedger.txMap().isValid())
            return fail("Invalid transaction map");

        srcLedger.txMap().snapShot(false)->visitNodes(visit);
        if (error)
            return fail("Failed to store transaction map");
    }

    if (!batch.empty() && !storeBatch())
        return fail("Failed to store");

    return true;
}

void
Database::getCountsJson(Json::Value& obj)
{
    XRPL_ASSERT(
        obj.isObject(),
        "ripple::NodeStore::Database::getCountsJson : valid input type");

    {
        std::unique_lock lock(readLock_);
        obj["read_queue"] = static_cast<Json::UInt>(read_.size());
    }

    if (workers_)
        obj["read_threads_total"] = safe_cast<Json::UInt>(workers_->count());
    else
        obj["read_threads_total"] = 0;

    if (auto name = getName(); !name.empty())
        obj[jss::name] = name;

    obj[jss::node_writes] = std::to_string(storeCount_);
    obj[jss::node_reads_total] = std::to_string(fetchTotalCount_);
    obj[jss::node_reads_hit] = std::to_string(fetchHitCount_);
    obj[jss::node_written_bytes] = std::to_string(storeSz_);
    obj[jss::node_read_bytes] = std::to_string(fetchSz_);
    obj[jss::node_reads_duration_us] = std::to_string(fetchDurationUs_);
}

}  // namespace NodeStore
}  // namespace ripple
