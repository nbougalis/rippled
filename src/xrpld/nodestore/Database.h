//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2017 Ripple Labs Inc.

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

#ifndef RIPPLE_NODESTORE_DATABASE_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASE_H_INCLUDED

#include <xrpld/nodestore/Backend.h>
#include <xrpld/nodestore/NodeObject.h>
#include <xrpld/nodestore/Scheduler.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/protocol/SystemParameters.h>

#include <memory>

namespace ripple {

class Ledger;
namespace NodeStore {

/** Persistency layer for NodeObject

    A Node is a ledger object which is uniquely identified by a key, which is
    the 256-bit hash of the body of the node. The payload is a variable length
    block of serialized data.

    All ledger data is stored as node objects and as such, needs to be persisted
    between launches. Furthermore, since the set of node objects will in
    general be larger than the amount of available memory, purged node objects
    which are later accessed must be retrieved from the node store.

    @see NodeObject
*/
class alignas(64) Database
{
public:
    Database() = delete;

    /** Construct the node store.

        @param scheduler The scheduler to use for performing asynchronous tasks.
        @param threads The number of asynchronous read threads to create.
        @param config The configuration settings
        @param journal Destination for logging output.
        @param name A short name, used for naming our threads.
    */
    Database(
        Scheduler& scheduler,
        unsigned int threads,
        Section const& config,
        beast::Journal j,
        std::string_view name = "db");

    /** Destroy the node store.
        All pending operations are completed, pending writes flushed,
        and files closed before this returns.
    */
    virtual ~Database();

    /** Retrieve the name associated with this backend.
        This is used for diagnostics and may not reflect the actual path
        or paths used by the underlying backend.
    */
    virtual std::string
    getName() const = 0;

    /** Import objects from another database. */
    virtual void
    importDatabase(Database& source) = 0;

    /** Retrieve the estimated number of pending write operations.
        This is used for diagnostics.
    */
    virtual std::int32_t
    getWriteLoad() const = 0;

    /** Store the object.

        @param type The type of object.
        @param data The payload of the object.
        @param hash The 256-bit hash of the payload data.
        @param ledgerSeq The sequence of the ledger the object belongs to.
    */
    virtual void
    store(
        NodeObjectType type,
        std::span<std::uint8_t const> data,
        uint256 const& hash,
        std::uint32_t ledgerSeq) = 0;

    /* Check if two ledgers are in the same database

        If these two sequence numbers map to the same database,
        the result of a fetch with either sequence number would
        be identical.

        @param s1 The first sequence number
        @param s2 The second sequence number

        @return 'true' if both ledgers would be in the same DB

    */
    virtual bool
    isSameDB(std::uint32_t s1, std::uint32_t s2) = 0;

    virtual void
    sync() = 0;

    /** Fetch a node object.
        If the object is known to be not in the database, isn't found in the
        database during the fetch, or failed to load correctly during the fetch,
        `nullptr` is returned.

        @note This can be called concurrently.
        @param hash The key of the object to retrieve.
        @param ledgerSeq The sequence of the ledger where the object is stored.
        @param fetchType the type of fetch, synchronous or asynchronous.
        @return The object, or nullptr if it couldn't be retrieved.
    */
    boost::intrusive_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t ledgerSeq = 0,
        FetchType fetchType = FetchType::synchronous,
        bool duplicate = false);

    /** Fetch an object without waiting.
        If I/O is required to determine whether or not the object is present,
        `false` is returned. Otherwise, `true` is returned and `object` is set
        to refer to the object, or `nullptr` if the object is not present.
        If I/O is required, the I/O is scheduled and `true` is returned

        @note This can be called concurrently.
        @param hash The key of the object to retrieve
        @param seq The sequence of the ledger where the
                object is stored.
        @param callback Callback function when read completes
    */
    virtual void
    asyncFetch(
        uint256 const& hash,
        std::uint32_t seq,
        std::function<void(boost::intrusive_ptr<NodeObject> const&)>&&
            callback);

    /** Store a ledger from a different database.

        @param srcLedger The ledger to store.
        @return true if the operation was successful
    */
    virtual bool
    storeLedger(std::shared_ptr<Ledger const> const& srcLedger) = 0;

    /** Remove expired entries from the positive and negative caches. */
    virtual void
    sweep() = 0;

    /** Gather statistics pertaining to read and write activities.
     *
     * @param obj Json object reference into which to place counters.
     */
    std::uint64_t
    getStoreCount() const
    {
        return storeCount_;
    }

    std::uint64_t
    getFetchTotalCount() const
    {
        return fetchTotalCount_;
    }

    std::uint64_t
    getFetchHitCount() const
    {
        return fetchHitCount_;
    }

    std::uint64_t
    getStoreSize() const
    {
        return storeSz_;
    }

    std::uint64_t
    getFetchSize() const
    {
        return fetchSz_;
    }

    void
    getCountsJson(Json::Value& obj);

    /** Returns the number of file descriptors the database expects to need */
    int
    fdRequired() const
    {
        return fdRequired_;
    }

    virtual void
    stop();

    bool
    isStopping() const;

    /** @return The earliest ledger sequence allowed
     */
    [[nodiscard]] std::uint32_t
    earliestLedgerSeq() const noexcept
    {
        return earliestLedgerSeq_;
    }

protected:
    void
    storeStats(std::uint64_t count, std::uint64_t sz)
    {
        XRPL_ASSERT(
            count <= sz,
            "ripple::NodeStore::Database::storeStats : valid inputs");
        storeCount_.fetch_add(count, std::memory_order_relaxed);
        storeSz_.fetch_add(sz, std::memory_order_relaxed);
    }

    // Called by the public import function
    void
    importInternal(Backend& dstBackend, Database& srcDB);

    // Called by the public storeLedger function
    bool
    storeLedger(Ledger const& srcLedger, std::shared_ptr<Backend> dstBackend);

    void
    updateFetchMetrics(
        uint64_t fetches,
        uint64_t hits,
        std::chrono::microseconds duration)
    {
        fetchTotalCount_.fetch_add(fetches, std::memory_order_relaxed);
        fetchHitCount_.fetch_add(hits, std::memory_order_relaxed);
        fetchDurationUs_.fetch_add(duration.count(), std::memory_order_relaxed);
    }

private:
    virtual boost::intrusive_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        FetchReport& fetchReport,
        bool duplicate) = 0;

    /** Visit every object in the database
        This is usually called during import.

        @note This routine will not be called concurrently with itself
                or other methods.
        @see import
    */
    virtual void
    for_each(std::function<void(boost::intrusive_ptr<NodeObject>)> f) = 0;

protected:
    beast::Journal const j_;
    Scheduler& scheduler_;
    int fdRequired_ = 0;

    // The default is XRP_LEDGER_EARLIEST_SEQ (32570) to match the XRP ledger
    // network's earliest allowed ledger sequence. Can be set through the
    // configuration file using the 'earliest_seq' field under the 'node_db'
    // stanza. If specified, the value must be greater than zero.
    // Only unit tests or alternate  networks should change this value.
    std::uint32_t const earliestLedgerSeq_;

private:
    /** Indicates we are stopping.

        Placed among the cold members, so frequent (lock-free) reads on the
        hot path, are never invalidated by counter traffic. Written to only
        once, under the mutex.
    */
    std::atomic<bool> readStopping_ = false;

    /** Number of objects written to the backend since startup. */
    alignas(64) std::atomic<std::uint64_t> storeCount_ = 0;

    /** Total bytes written to the backend since startup. */
    std::atomic<std::uint64_t> storeSz_ = 0;

    /** Number of fetch attempts since startup, hit or miss. */
    alignas(64) std::atomic<std::uint64_t> fetchTotalCount_ = 0;

    /** Number of fetch attempts that found the object. */
    std::atomic<std::uint64_t> fetchHitCount_ = 0;

    /** Total bytes of all objects returned by successful fetches. */
    std::atomic<std::uint64_t> fetchSz_ = 0;

    /** Cumulative wall time spent in fetchNodeObject. */
    std::atomic<std::chrono::microseconds::rep> fetchDurationUs_ = 0;

    /** Guards read_.

        This is held only for queue insertion and extraction; it is never
        held across a fetch, a callback, or a call into workers_.
     */
    alignas(64) mutable std::mutex readLock_;

    /** Pending asynchronous reads, keyed by object hash.

        Each key maps to the callbacks registered for it; the presence of a
        key means a fetch is queued or in flight for that key, and a second
        request for a hash adds another callback, instead of enqueueing new
        work. One task is created per key insertion; each task extracts and
        services exactly one key.
    */
    std::map<
        uint256,
        std::vector<std::pair<
            std::uint32_t,
            std::function<void(boost::intrusive_ptr<NodeObject> const&)>>>>
        read_;

    /** The read thread pool and its task handler.

        Services asynchronous fetches queued in read_: each task extracts
        one key and dispatches its callbacks (see asyncFetch for the task
        accounting, and the class definition in Database.cpp for details).

        Null when the database was constructed with zero read threads; in
        that case asyncFetch degrades to servicing requests synchronously
        on the caller's thread.

        @note This MUST remain the last data member. Its destruction stops
              the pool and joins its threads, which read every member that
              is declared above (the queue, its lock, and the stop flag);
              declaration order is what makes that join safe. Nothing may
              be declared after it.
     */
    /** @{ */
    class DatabaseWorkers;

    std::unique_ptr<DatabaseWorkers> workers_;
    /** @} */
};

}  // namespace NodeStore
}  // namespace ripple

#endif
