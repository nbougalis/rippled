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

#ifndef RIPPLE_SHAMAP_SHAMAPITEM_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPITEM_H_INCLUDED

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/SlabAllocator.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <cassert>

namespace ripple {

// an item stored in a SHAMap
class SHAMapItem : public CountedObject<SHAMapItem>
{
    // These are used to support boost::intrusive_ptr reference counting
    // These functions are used internally by boost::intrusive_ptr to handle
    // lifetime management.
    friend void
    intrusive_ptr_add_ref(SHAMapItem const* x) noexcept;

    friend void
    intrusive_ptr_release(SHAMapItem const* x) noexcept;

    // This is the interface for creating new instances of this class.
    friend boost::intrusive_ptr<SHAMapItem const>
    make_shamapitem(uint256 const& tag, Slice data) noexcept;

private:
    uint256 const tag_;

    // We use std::uint32_t to minimize the size; there's no SHAMapItem whose
    // size exceeds 4GB and there won't ever be (famous last words?), so this
    // is safe.
    std::uint32_t const size_;

    // This is the reference count used to support boost::intrusive_ptr
    mutable std::atomic<std::uint32_t> refcount_ = 1;

    // Because of the unusual way in which SHAMapItem objects are constructed
    // the only way to properly create one is to first allocate enough memory
    // so we limit this constructor to codepaths that do this right and limit
    // arbitrary construction.
    SHAMapItem(uint256 const& tag, Slice data) noexcept
        : tag_(tag), size_(static_cast<std::uint32_t>(data.size()))
    {
        assert(data.size() != 0);

        std::memcpy(
            reinterpret_cast<std::uint8_t*>(this) + sizeof(*this),
            data.data(),
            data.size());
    }

public:
    SHAMapItem() = delete;

    SHAMapItem(SHAMapItem const& other) = delete;

    SHAMapItem&
    operator=(SHAMapItem const& other) = delete;

    SHAMapItem(SHAMapItem&& other) = delete;

    SHAMapItem&
    operator=(SHAMapItem&&) = delete;

    uint256 const&
    key() const noexcept
    {
        return tag_;
    }

    std::size_t
    size() const noexcept
    {
        return size_;
    }

    void const*
    data() const noexcept
    {
        return reinterpret_cast<std::uint8_t const*>(this) + sizeof(*this);
    }

    Slice
    slice() const noexcept
    {
        return {data(), size()};
    }
};

namespace detail {

// clang-format off
// The number of items per allocation and the bucket sizes are customized
// based on profiling data, with an eye on minimizing the number of slack
// bytes in a block.
inline constinit slab::allocator_t<SHAMapItem, slab::heap_fallback,
    slab::config<1000000, 128>,
    slab::config<1000000, 296>,
    slab::config<125000, 392>,
    slab::config<125000, 520>,
    slab::config<62500, 760>,
    slab::config<62500, 856>,
    slab::config<31250, 1048>
> slabber;
// clang-format on

}  // namespace detail

inline void
intrusive_ptr_add_ref(SHAMapItem const* x) noexcept
{
    assert(x);

    // In order to call this, we must already have an intrusive pointer
    // to this item, so its reference count should be at least 1.
    if (x->refcount_++ == 0)
        LogicError("SHAMapItem: the reference count is 0!");
}

inline void
intrusive_ptr_release(SHAMapItem const* x) noexcept
{
    assert(x);

    if (--x->refcount_ == 0)
    {
        auto const size = x->size();

        // We need to invoke the destructor for this object before we
        // release the memory.
        if constexpr (!std::is_trivially_destructible_v<SHAMapItem>)
            std::destroy_at(x);

        if (!detail::slabber.deallocate(x, size)) [[unlikely]]
            LogicError("SHAMapItem: failed to deallocate memory!");
    }
}

inline boost::intrusive_ptr<SHAMapItem const>
make_shamapitem(uint256 const& tag, Slice data) noexcept
{
    assert(data.size() != 0 && data.size() <= megabytes<std::size_t>(8));

    // We do not increment the reference count here on purpose: the
    // constructor of SHAMapItem explicitly sets it to 1.
    if (auto raw = detail::slabber.allocate(data.size())) [[likely]]
        return {new (raw) SHAMapItem{tag, data}, false};

    return nullptr;
}

inline boost::intrusive_ptr<SHAMapItem const>
make_shamapitem(SHAMapItem const& other) noexcept
{
    return make_shamapitem(other.key(), other.slice());
}

}  // namespace ripple

#endif
