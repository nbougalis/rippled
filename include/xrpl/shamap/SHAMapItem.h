#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/SlabAllocator.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

namespace xrpl {

// an item stored in a SHAMap
class alignas(8) SHAMapItem : public CountedObject<SHAMapItem>
{
    // These are used to support boost::intrusive_ptr reference counting
    // These functions are used internally by boost::intrusive_ptr to handle
    // lifetime management.
    friend void
    intrusive_ptr_add_ref(SHAMapItem const* x);

    friend void
    intrusive_ptr_release(SHAMapItem const* x);

    // This is the interface for creating new instances of this class.
    friend boost::intrusive_ptr<SHAMapItem>
    makeShamapitem(UInt256 const& tag, Slice data);

private:
    UInt256 const tag_;

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
    SHAMapItem(UInt256 const& tag, Slice data)
        : tag_(tag), size_(static_cast<std::uint32_t>(data.size()))
    {
        std::memcpy(
            reinterpret_cast<std::uint8_t*>(this) + sizeof(*this), data.data(), data.size());
    }

public:
    SHAMapItem() = delete;

    SHAMapItem(SHAMapItem const& other) = delete;

    SHAMapItem&
    operator=(SHAMapItem const& other) = delete;

    SHAMapItem(SHAMapItem&& other) = delete;

    SHAMapItem&
    operator=(SHAMapItem&&) = delete;

    UInt256 const&
    key() const
    {
        return tag_;
    }

    std::size_t
    size() const
    {
        return size_;
    }

    void const*
    data() const
    {
        return reinterpret_cast<std::uint8_t const*>(this) + sizeof(*this);
    }

    Slice
    slice() const
    {
        return {data(), size()};
    }
};

namespace detail {

/**
 * SHAMap Slab Allocator Configuration
 *
 * The classes are carefully fitted, based on a histogram of request sizes
 * collected on an XRPL mainnet node in October 2026.
 *
 * The distribution is strongly bimodal, with a secondary cluster between
 * the modes and a long, thin tail beyond them:
 *
 * | Extra | Block | Slab    | Share | Covers                                |
 * |------:|------:|--------:|------:|---------------------------------------|
 * |    88 |   128 | ~50 MiB | 40.7% | mode: 87 bytes (minimal AccountRoot)  |
 * |   152 |   192 | ~24 MiB | 13.3% | secondary cluster, 88..152 bytes      |
 * |   216 |   256 | ~50 MiB | 39.9% | mode: 211 bytes (minimal RippleState) |
 * |   344 |   384 | ~16 MiB |  2.7% | head of the tail, 217..344 bytes      |
 * |     - |  heap |       - |  3.4% | tail                                  |
 *
 * The modal classes carry zero slack. Together the slab allocator can serve
 * over 96% of requests without having to perform any per-request allocation
 * at ~6% mean slack.
 */
// clang-format off
inline constinit slab::Allocator<SHAMapItem, slab::HeapFallback,
    slab::Config<400'000,  88>,   // 128-byte blocks, ~50 MiB/slab
    slab::Config<130'000, 152>,   // 192-byte blocks, ~24 MiB/slab
    slab::Config<200'000, 216>,   // 256-byte blocks, ~50 MiB/slab
    slab::Config< 44'000, 344>    // 384-byte blocks, ~16 MiB/slab
> gSlabber;
// clang-format on

}  // namespace detail

inline void
intrusive_ptr_add_ref(SHAMapItem const* x)
{
    // This can only happen if someone releases the last reference to the
    // item while we were trying to increment the refcount.
    if (x->refcount_++ == 0)
        logicError("SHAMapItem: the reference count is 0!");
}

inline void
intrusive_ptr_release(SHAMapItem const* x)
{
    if (--x->refcount_ == 0)
    {
        auto p = reinterpret_cast<std::uint8_t const*>(x);

        // The SHAMapItem constructor isn't trivial (because the destructor
        // for CountedObject isn't) so we can't avoid calling it here, but
        // plan for a future where we might not need to.
        if constexpr (!std::is_trivially_destructible_v<SHAMapItem>)
            std::destroy_at(x);

        // If the slabber doesn't claim this pointer, it was allocated
        // manually, so we free it manually.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        if (!detail::gSlabber.deallocate(const_cast<std::uint8_t*>(p)))
            delete[] p;
    }
}

inline boost::intrusive_ptr<SHAMapItem>
makeShamapitem(UInt256 const& tag, Slice data)
{
    XRPL_ASSERT(
        data.size() <= megabytes<std::size_t>(16), "xrpl::makeShamapitem : maximum input size");

    // We do not increment the reference count here on purpose: the
    // constructor of SHAMapItem explicitly sets it to 1.
    if (auto raw = detail::gSlabber.allocate(data.size())) [[likely]]
        return {new (raw) SHAMapItem{tag, data}, false};

    Throw<std::bad_alloc>();
}

inline boost::intrusive_ptr<SHAMapItem>
makeShamapitem(SHAMapItem const& other)
{
    return makeShamapitem(other.key(), other.slice());
}

}  // namespace xrpl
