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

#ifndef RIPPLE_NODESTORE_NODEOBJECT_H_INCLUDED
#define RIPPLE_NODESTORE_NODEOBJECT_H_INCLUDED

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/Protocol.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

// VFALCO NOTE Intentionally not in the NodeStore namespace

namespace ripple {

namespace detail {
constexpr std::uint16_t pinned_flag = 0x8000;
}

/** The types of node objects. */
// clang-format off
enum NodeObjectType : std::uint16_t {
    hotUNKNOWN              = 0,
    hotLEDGER               = 1,
    hotACCOUNT_NODE         = 3,
    hotTRANSACTION_NODE     = 4,

    // Used for negative cache entries
    hotDUMMY                = 512,

    // Uncached variants. There are routing-only values, which essentially
    // bypass the cache when these objects are stored; when stored on disk
    // the values are reduced to their single-byte equivalent value.
    pinnedLEDGER            = detail::pinned_flag + hotLEDGER,
    pinnedACCOUNT_NODE      = detail::pinned_flag + hotACCOUNT_NODE,
    pinnedTRANSACTION_NODE  = detail::pinned_flag + hotTRANSACTION_NODE,
};
// clang-format on

/** Returns true if the type is a pinned (routing-only) variant. */
inline bool
isPinnedType(NodeObjectType type) noexcept
{
    return (safe_cast(type) & detail::pinned_flag) != 0;
}

/** Map pinned types back to their serializable hot equivalents.

    @param type The node object type to convert
    @return the corresponding unpinned type, if the input is a pinned type;
            the input type unchanged otherwise.
*/
inline NodeObjectType
toHotType(NodeObjectType type) noexcept
{
    return checked_cast<NodeObjectType>(safe_cast(type) & ~detail::pinned_flag);
}

/** A simple object that the Ledger uses to store entries.

    NodeObjects are comprised of a type and a serialized data blob. They can
    be uniquely identified by the SHA512-Half hash of the data blob.

    @note No checking is performed to make sure the hash matches the data.
    @see SHAMap
*/
class NodeObject : public CountedObject<NodeObject>
{
public:
    static constexpr std::size_t keyBytes = 32;

private:
    NodeObject(
        NodeObjectType type,
        std::span<std::uint8_t const> data,
        uint256 const& hash) noexcept;

public:
    /** Create an object from fields.

        The caller's variable is modified during this call. The
        underlying storage for the Blob is taken over by the NodeObject.

        @param type The type of object.
        @param ledgerIndex The ledger in which this object appears.
        @param data A buffer containing the payload. The caller's variable
                    is overwritten.
        @param hash The 256-bit hash of the payload data.
    */
    static boost::intrusive_ptr<NodeObject>
    createObject(
        NodeObjectType type,
        std::span<std::uint8_t const> data,
        uint256 const& hash) noexcept;

    /** Returns the type of this object. */
    NodeObjectType
    type() const
    {
        return type_;
    }

    /** Returns the hash of the data. */
    uint256 const&
    key() const
    {
        return hash_;
    }

    /** Returns the size (in bytes) of the data associated with this object. */
    std::size_t
    size() const noexcept
    {
        return size_;
    }

    /** Returns the underlying data. */
    std::span<std::uint8_t const>
    data() const noexcept
    {
        return {
            reinterpret_cast<std::uint8_t const*>(this) + sizeof(*this), size_};
    }

    /** Support for boost::intrusive_ptr. */
    /** @{ */
    friend void
    intrusive_ptr_add_ref(NodeObject const* p) noexcept
    {
        std::uint16_t cur = p->refcount_.load(std::memory_order_relaxed);

        do
        {
            XRPL_ASSERT(
                cur != 0,
                "ripple::NodeObject::intrusive_ptr_add_ref : refcount was 0!");

            // In the (effectively impossible) even that the count is
            // saturated, we treat this object as pinned: it will not
            // be deallocated during the lifetime of the process.
            if (cur == std::numeric_limits<std::uint16_t>::max()) [[unlikely]]
                return;
        } while (!p->refcount_.compare_exchange_weak(
            cur,
            static_cast<std::uint16_t>(cur + 1),
            std::memory_order_relaxed,
            std::memory_order_relaxed));
    }

    friend void
    intrusive_ptr_release(NodeObject const* p) noexcept;
    /** @} */

    /** Returns true if there is only a single reference to this node object. */
    bool
    unique() const noexcept
    {
        return refcount_.load(std::memory_order_relaxed) == 1;
    }

private:
    /** The reference count (for boost::intrusive_ptr) which is never zero! */
    mutable std::atomic<std::uint16_t> refcount_;

    /** The type of the node object. */
    NodeObjectType const type_;

    /** The size (in bytes) of the data associated with this object. */
    std::uint32_t size_;

    /** The hash of this node object. */
    uint256 const hash_;
};

}  // namespace ripple

#endif
