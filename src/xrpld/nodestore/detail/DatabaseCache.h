//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2026, The Xahaud Developers

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

#ifndef RIPPLE_NODESTORE_NODEOBJECTCACHE_H_INCLUDED
#define RIPPLE_NODESTORE_NODEOBJECTCACHE_H_INCLUDED

#include <xrpld/nodestore/NodeObject.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/basics/spinlock.h>

#include <boost/intrusive_ptr.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>

namespace ripple {
namespace NodeStore {

/** A 12-way set-associative cache of NodeObjects.

    Each set occupies exactly two cache lines. The leading line holds the
    twelve 16-bit tags, the spinlock byte, the FIFO cursor, and the first
    four pointers; the trailing line holds the remaining eight pointers.

    A lookup hashes the key to derive both an index (which identifies the
    set that the item belongs to), and a 16-bit value, that serves as the
    in-set tag.

    The cache stores objects only. It has no internal notion of absent or
    negative entries. Callers that wish to record an absence should store
    some kind of sentinel object. This is the purpose of hotDUMMY: a type
    that identifies a NodeObject as a "dummy" used to indicate that there
    is no object with the given key.

    Note: the way sets are implemented can result in a significant amount
          of cache coherency traffic (as the lock is needed for reads and
          writes, and causes the cache line to become dirty). An improved
          lock-free implementation of this cache that would eliminate the
          issue is possible.
*/
class NodeObjectCache
{
    static constexpr std::size_t ways = 12;

    struct alignas(64) Set
    {
        // Tags first so the scan stays in the leading line; the lock
        // shares that hot line and the pointers trail, touched only
        // on a tag hit. Exactly two cache lines.
        std::array<std::uint16_t, ways> tags_ = {};
        std::atomic<std::uint8_t> lock_{0};
        std::uint8_t cursor_{0};
        std::array<boost::intrusive_ptr<NodeObject>, ways> ways_ = {};
    };

    std::pair<Set&, std::uint16_t>
    bucket(uint256 const& key) const noexcept
    {
        assert(size_ != 0);

        // The key is a cryptographic hash, so we do not need to process
        // it to ensure uniformity. We can keep it simple.
        auto const* kd = key.data();

        // Note that the byte order here is deliberately host-native, so
        // that the compiler can fold the operations down to simple load
        // instructions. This is fine for our purposes: this is internal
        // only and the ordering of bits is irrelevant provided that the
        // result is deterministic.
        std::uint32_t index = (static_cast<std::uint32_t>(kd[3]) << 24) |
            (static_cast<std::uint32_t>(kd[2]) << 16) |
            (static_cast<std::uint32_t>(kd[1]) << 8) | kd[0];

        return {
            sets_[(index ^ mix_) & (size_ - 1)],
            static_cast<std::uint16_t>(kd[31] << 8 | kd[30])};
    }

    std::unique_ptr<Set[]> sets_;
    std::uint32_t size_ = 0;
    std::uint32_t mix_;

    // The minimum and maximum number of objects the cache can
    // support. The restriction isn't technical; it's meant to
    // prevent excessive memory usage due to misconfiguration.
    static constexpr std::uint32_t min_object_count = 175000;
    static constexpr std::uint32_t max_object_count = 25000000;

public:
    /** Construct a cache with the requested approximate capacity.

        @param size Approximate requested capacity. 0 to disable caching.
        @param mix  The seed to use; 0 means "choose seed randomly".
     */
    NodeObjectCache(beast::Journal j, std::uint32_t size, std::uint32_t mix = 0)
        : mix_(mix)
    {
        while (mix_ == 0)
            mix_ = rand_int<std::uint32_t>();

        if (size != 0)
        {
            // We restrict the possible sizes to be powers of two, because
            // of how we calculate and clamp hash values to locate the set
            // that a particular item belongs to.
            size_ = std::bit_floor(
                std::clamp<std::uint32_t>(
                    size, min_object_count, max_object_count) /
                ways);

            if (size_ != 0)
                sets_ = std::make_unique<Set[]>(size_);
        }
    }

    /** Return the cached object for @p key, or null if not present. */
    boost::intrusive_ptr<NodeObject>
    fetch(uint256 const& key)
    {
        if (size_ != 0)
        {
            auto const [s, tag] = bucket(key);

            spinlock sl(s.lock_);
            std::lock_guard lock(sl);

            for (std::size_t i = 0; i < ways; ++i)
            {
                if (s.tags_[i] == tag && s.ways_[i] != nullptr &&
                    s.ways_[i]->key() == key)
                {
                    return s.ways_[i];
                }
            }
        }

        return {};
    }

    /** Reconcile @p entry against an existing object with the same key.

        If no entry exists in the cache for the object, @p entry is
        inserted and returned.

        If an entry exists, @p replace is invoked with the cached object
        and:
            - if the result is true, the cached object is replaced with
              @p entry, and the function returns @p entry.
            - otherwise, @p entry is discarded and the cached object is
              returned.

        @param entry   The candidate object; must be non-null.
        @param replace Predicate consulted on collision; receives the
                       cached pointer and returns true to overwrite.

        @return The canonical pointer to the object.
     */
    template <class Replace>
        requires std::predicate<Replace&, NodeObject const&>
    [[nodiscard]] boost::intrusive_ptr<NodeObject>
    canonicalize(boost::intrusive_ptr<NodeObject> entry, Replace&& replace)
    {
        assert(entry.get() != nullptr);

        if (size_ != 0)
        {
            auto const [s, tag] = bucket(entry->key());

            boost::intrusive_ptr<NodeObject> evicted;

            {
                spinlock sl(s.lock_);
                std::lock_guard lock(sl);

                std::size_t empty = ways, idle = ways;

                for (std::size_t i = 0; i < ways; ++i)
                {
                    if (s.ways_[i] == nullptr)
                    {
                        empty = i;
                        continue;
                    }

                    if (s.tags_[i] == tag && s.ways_[i]->key() == entry->key())
                    {
                        if (replace(*s.ways_[i]))
                            evicted = std::exchange(s.ways_[i], entry);

                        return s.ways_[i];
                    }

                    // We only look for idle slots if we haven't already found
                    // one to avoid doing atomic operations.
                    if (idle == ways && s.ways_[i]->unique())
                        idle = i;
                }

                // Pick a victim to evict: we prefer empty slots over idle slots
                // (to avoid evicting a negative cache entry) and only fall back
                // to FIFO-ish behavior if we have no other option.
                auto const victim = [&]() -> std::size_t {
                    if (empty < ways)
                        return empty;

                    if (idle < ways)
                        return idle;

                    if (s.cursor_ == ways - 1)
                        return std::exchange(s.cursor_, 0);

                    return s.cursor_++;
                }();

                evicted = std::exchange(s.ways_[victim], entry);
                s.tags_[victim] = tag;
            }
        }

        return entry;
    }

    /** Insert @p entry if no entry exists for its key, otherwise return
        the existing canonical instance.

        Equivalent to the predicate-taking overload with a never-replace
        predicate: the cache wins on collision, and @p entry is discarded
        if a different instance is already stored.

        @param entry The candidate object; must be non-null.

        @return The canonical pointer for the entry's key.
     */
    [[nodiscard]] boost::intrusive_ptr<NodeObject>
    canonicalize(boost::intrusive_ptr<NodeObject> entry)
    {
        return canonicalize(
            std::move(entry), [](NodeObject const&) { return false; });
    }

    /** Best-effort removal of every entry the cache solely owns.

        Typically invoked periodically (or when under memory pressure) to
        trim unnecessary items, this walks the sets one at a time to find
        and drop all items that are only referenced by the cache.

        Destruction of evicted items happens without holding a lock. This
        minimizes the amount of time that a particular set is locked, and
        thus, not available for other operations.

        @note This is safe to call concurrently with any other operation.
     */
    void
    trim()
    {
        std::for_each_n(sets_.get(), size_, [](Set& s) {
            // We move intrusive_ptr instances for objects that must be
            // removed into this, so we can release them without having
            // to hold a lock. This does not mean trim runs faster, but
            // it means that individual sets are locked for less time.
            decltype(s.ways_) dead;

            {
                spinlock sl(s.lock_);
                std::lock_guard lock(sl);

                for (std::size_t j = 0; j < ways; ++j)
                {
                    if (s.ways_[j] != nullptr && s.ways_[j]->unique())
                        dead[j] = std::move(s.ways_[j]);
                }
            }
        });
    }

    /** Best-effort removal of all entries.

        This walks the sets one at a time, clearing all the items stored
        in each set and resetting the eviction cursor.

        @note Because each set has its own lock, we cannot guarantee the
              cache will be empty on return, because a concurrent insert
              into a set this call has already cleared will survive. The
              caller is responsible for preventing insertions while this
              operation is in progress.
     */
    void
    clear()
    {
        std::for_each_n(sets_.get(), size_, [](Set& s) {
            decltype(s.ways_) dead;

            {
                spinlock sl(s.lock_);
                std::lock_guard lock(sl);

                std::swap(dead, s.ways_);
                s.tags_.fill(0);
                s.cursor_ = 0;
            }
        });
    }
};

}  // namespace NodeStore
}  // namespace ripple

#endif
