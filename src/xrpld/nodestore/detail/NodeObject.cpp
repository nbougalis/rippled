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

#include <xrpld/nodestore/NodeObject.h>
#include <xrpl/basics/SlabAllocator.h>
#include <memory>

namespace ripple {

// clang-format off

// The sizes below were derived from the observed distribution of the
// relevant data in production (as of May 30, 2026).
//
// The distribution is sharply peaked with 4 regions accounting for a
// little over 98% of all allocations:
//
// 1. ~38% of entries: precisely 40 bytes
//    (Negative cache/hotDUMMY entries with no payload.)
// 2. ~35% of entries: precisely 556 bytes
//    (Compressed SHAMap Inner Nodes with a 516 byte payload.)
// 3. ~14% of entries: between 130 and 200 bytes.
// 4.  ~9% of entries: between 250 and 310 bytes.
//
// The size classes have been selected carefully, to snugly cover the
// peaks, minimizing memory slack and resource usage while maximizing
// performance and efficiency.
constinit slab::allocator_t<NodeObject, slab::heap_fallback,
    slab::config< 50000>,
    slab::config<320000, 160>,
    slab::config<200000, 280>,
    slab::config<150000, 520>
> slabber;
// clang-format on

void
intrusive_ptr_release(NodeObject const* p) noexcept
{
    XRPL_ASSERT(
        p != nullptr, "ripple::NodeObject::intrusive_ptr_release : null");

    std::uint16_t cur = p->refcount_.load(std::memory_order_relaxed);

    do
    {
        XRPL_ASSERT(
            cur != 0,
            "ripple::NodeObject::intrusive_ptr_release : refcount was 0!");

        // If the reference count is saturated, we have lost track of
        // how many holders we have. This event is extremely unlikely
        // and if it happens, we simply never release.
        if (cur == std::numeric_limits<std::uint16_t>::max()) [[unlikely]]
            return;
    } while (!p->refcount_.compare_exchange_weak(
        cur,
        static_cast<std::uint16_t>(cur - 1),
        std::memory_order_acq_rel,
        std::memory_order_relaxed));

    if (cur == 1)
    {
        if constexpr (!std::is_trivially_destructible_v<NodeObject>)
            std::destroy_at(p);

        if (!slabber.deallocate(p)) [[unlikely]]
            LogicError("NodeObject could not be deallocated");
    }
}

NodeObject::NodeObject(
    NodeObjectType type,
    std::span<std::uint8_t const> data,
    uint256 const& hash) noexcept
    : refcount_(1), type_(type), hash_(hash)
{
    XRPL_ASSERT(
        (data.size() == 0) == (type == hotDUMMY),
        "ripple::NodeObject::NodeObject : empty data iff dummy type");

    if (!std::in_range<decltype(size_)>(data.size()))
        LogicError("node object data size out of range");

    size_ = checked_cast<decltype(size_)>(data.size());

    if (size_ != 0)
        std::memcpy(
            reinterpret_cast<std::uint8_t*>(this) + sizeof(NodeObject),
            data.data(),
            data.size());
}

boost::intrusive_ptr<NodeObject>
NodeObject::createObject(
    NodeObjectType type,
    std::span<std::uint8_t const> data,
    uint256 const& hash) noexcept
{
    // We do not increment the reference count here because the
    // constructor of NodeObject explicitly sets it to 1.
    if (auto raw = slabber.allocate(data.size())) [[likely]]
        return {new (raw) NodeObject{type, data, hash}, false};

    return nullptr;
}

}  // namespace ripple
