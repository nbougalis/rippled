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

#ifndef RIPPLE_NODESTORE_DATABASENODEIMP_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASENODEIMP_H_INCLUDED

#include <xrpld/nodestore/Database.h>
#include <xrpld/nodestore/detail/DatabaseCache.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/safe_cast.h>

#include <span>
#include <utility>

namespace ripple {
namespace NodeStore {

class DatabaseNodeImp : public Database
{
public:
    DatabaseNodeImp() = delete;
    DatabaseNodeImp(DatabaseNodeImp const&) = delete;
    DatabaseNodeImp&
    operator=(DatabaseNodeImp const&) = delete;

    DatabaseNodeImp(
        Scheduler& scheduler,
        int readThreads,
        std::shared_ptr<Backend> backend,
        Section const& config,
        beast::Journal j)
        : Database(scheduler, readThreads, config, j)
        , cache_(
              j,
              [&config]() -> std::size_t {
                  std::size_t ret = 0;

                  // Measurements on steady-state nodes show hit rates below
                  // 1%, suggesting that upper cache tiers together with the
                  // OS page cache make this layer largely redundant.
                  //
                  // Pending a thorough evaluation of the performance impact
                  // of this cache (especially during ledger acquisition) it
                  // makes sense to disable it.
                  //
                  // Because the 'cache_size' parameter is injected into the
                  // configuration automatically, we need to introduce a new
                  // option, that defaults to "false", so that operators can
                  // opt in, if desired.
                  if (get<bool>(config, "use_noc", false))
                  {
                      auto cs = get<int>(config, "cache_size");

                      if (!std::in_range<std::size_t>(cs))
                      {
                          Throw<std::runtime_error>(
                              "Specified negative value for cache_size");
                      }

                      ret = checked_cast<std::size_t>(cs);
                  }

                  return ret;
              }())
        , backend_(std::move(backend))
    {
        XRPL_ASSERT(
            backend_,
            "ripple::NodeStore::DatabaseNodeImp::DatabaseNodeImp : non-null "
            "backend");
    }

    ~DatabaseNodeImp()
    {
        stop();
    }

    std::string
    getName() const override
    {
        return backend_->getName();
    }

    std::int32_t
    getWriteLoad() const override
    {
        return backend_->getWriteLoad();
    }

    void
    importDatabase(Database& source) override
    {
        importInternal(*backend_.get(), source);
    }

    void
    store(
        NodeObjectType type,
        std::span<std::uint8_t const> data,
        uint256 const& hash,
        std::uint32_t) override;

    bool
    isSameDB(std::uint32_t, std::uint32_t) override
    {
        // only one database
        return true;
    }

    void
    sync() override
    {
        backend_->sync();
    }

    void
    asyncFetch(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        std::function<void(boost::intrusive_ptr<NodeObject> const&)>&& callback)
        override;

    bool
    storeLedger(std::shared_ptr<Ledger const> const& srcLedger) override
    {
        return Database::storeLedger(*srcLedger, backend_);
    }

    void
    sweep() override;

private:
    // Cache for database objects. This cache is not always initialized. Check
    // for null before using.
    NodeObjectCache cache_;

    // Persistent key/value storage
    std::shared_ptr<Backend> backend_;

    boost::intrusive_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t,
        FetchReport& fetchReport,
        bool duplicate) override;

    void
    for_each(std::function<void(boost::intrusive_ptr<NodeObject>)> f) override
    {
        backend_->for_each(f);
    }
};

}  // namespace NodeStore
}  // namespace ripple

#endif
