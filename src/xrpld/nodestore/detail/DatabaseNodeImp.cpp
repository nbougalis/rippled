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

#include <xrpld/nodestore/detail/DatabaseNodeImp.h>
#include <xrpl/protocol/HashPrefix.h>

namespace ripple {
namespace NodeStore {

void
DatabaseNodeImp::store(
    NodeObjectType type,
    std::span<std::uint8_t const> data,
    uint256 const& hash,
    std::uint32_t)
{
    storeStats(1, data.size());

    // Pinned types bypass the cache and get reduced to hot equivalents.
    bool const skipCache = isPinnedType(type);

    if (skipCache)
        type = toHotType(type);

    auto obj = NodeObject::createObject(type, std::move(data), hash);
    backend_->store(obj);

    // Only cache non-pinned types; if the entry already existed in the cache
    // replace it only if it was an existing negative entry.
    if (!skipCache)
        (void)cache_.canonicalize(std::move(obj), [](NodeObject const& n) {
            return n.type() == hotDUMMY;
        });
}

void
DatabaseNodeImp::asyncFetch(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    std::function<void(boost::intrusive_ptr<NodeObject> const&)>&& callback)
{
    if (auto obj = cache_.fetch(hash); obj)
    {
        if (obj->type() == hotDUMMY)
            obj.reset();

        callback(obj);
        return;
    }

    Database::asyncFetch(hash, ledgerSeq, std::move(callback));
}

void
DatabaseNodeImp::sweep()
{
    cache_.trim();
}

boost::intrusive_ptr<NodeObject>
DatabaseNodeImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t,
    FetchReport& fetchReport,
    bool duplicate)
{
    auto nodeObject = cache_.fetch(hash);

    if (nodeObject != nullptr && nodeObject->type() == hotDUMMY)
    {
        JLOG(j_.trace()) << "fetchNodeObject " << hash
                         << ": negative cache entry found";
        return nullptr;
    }

    if (nodeObject == nullptr)
    {
        JLOG(j_.trace()) << "fetchNodeObject " << hash
                         << ": looking up in backend";

        Status status;

        try
        {
            status = backend_->fetch(hash.data(), &nodeObject);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal())
                << "fetchNodeObject " << hash
                << ": Exception fetching from backend: " << e.what();
            throw;
        }

        switch (status)
        {
            case ok:
                XRPL_ASSERT(
                    nodeObject != nullptr,
                    "backend returned success but an empty object!");
                nodeObject = cache_.canonicalize(std::move(nodeObject));
                break;

            case notFound:
                XRPL_ASSERT(
                    nodeObject == nullptr,
                    "backend returned 'not found' but produced an object!");

                {
                    auto entry = cache_.canonicalize(
                        NodeObject::createObject(hotDUMMY, {}, hash));

                    if (entry->type() != hotDUMMY)
                        nodeObject = std::move(entry);
                }

                break;

            case dataCorrupt:
                JLOG(j_.fatal()) << "fetchNodeObject " << hash
                                 << ": nodestore data is corrupted";
                break;

            default:
                JLOG(j_.warn())
                    << "fetchNodeObject " << hash
                    << ": backend returns unknown result " << status;
                break;
        }
    }

    if (nodeObject)
        fetchReport.wasFound = true;

    return nodeObject;
}

}  // namespace NodeStore
}  // namespace ripple
