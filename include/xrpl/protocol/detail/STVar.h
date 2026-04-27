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

#ifndef RIPPLE_PROTOCOL_STVAR_H_INCLUDED
#define RIPPLE_PROTOCOL_STVAR_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ripple {
namespace detail {

// clang-format off

inline constexpr struct defaultObject_t {} defaultObject;
inline constexpr struct nonPresentObject_t {} nonPresentObject;

// Concept to constrain STVar constructors, which
// instantiate ST* types from SerializedTypeID

template <typename... Args>
concept ValidConstructSTArgs =
    (std::is_same_v<
         std::tuple<std::remove_cvref_t<Args>...>,
         std::tuple<SField>> ||
     std::is_same_v<
         std::tuple<std::remove_cvref_t<Args>...>,
         std::tuple<SerialIter, SField>>);

// clang-format on

/** A "variant" that can hold any STBase-derived object.

    Objects whose size is less than or equal to @c max_size bytes are
    constructed in-place in a buffer internal to the instance; larger
    objects are allocated on the heap. Polymorphic copy and move will
    be delegated to STBase::copy and STBase::move.

    @note STVar does not preserve the concrete type of the contained
          object statically. Access is through a reference to STBase
          via get() and dynamic_cast at the call site.

    @see STBase::copy, STBase::move, STObject
 */
class STVar : public CountedObject<STVar>
{
    /** This is the largest "small object" that we can accommodate.

        This is a tradeoff: the larger this is, the more objects can leverage
        SBO and avoid an allocation, but the more wasteful we are.

        This value was 72 previously, apparently carefully selected to ensure
        that all types could be constructed in the internal buffer. As of the
        time of this writing, the three largest types are:

            1. STXChainBridge at 224 bytes;
            2. STAmount at 80 bytes (more than doubled as a result of MPT!);
            3. STIssue at 64 bytes.

        Since all other types fit in 48 bytes or fewer, shrinking the size of
        the internal buffer to that size and accepting that STIssue will also
        require a dynamic allocation seems like a reasonable trade-off.

        Increasing the max_size to 56 would not allow additional types to fit
        in the internal buffer but would make each STVar occupy one cacheline
        which can have some performance benefits. But since we keep all STVar
        instances in contiguous memory, it is unlikely to be meaningful.
     */
    static std::size_t constexpr max_size = 48;

    /** The pointer to the object contained, if any.

        If an object is contained, this pointer will either point to
        heap-allocated memory or to the internal buffer.
     */
    STBase* p_ = nullptr;

    /** The internal buffer used for in-place constructions of small objects.

        By placing it after the pointer, we artificially enforce the alignment
        requirement we expect from STBase:
     */
    std::byte d_[max_size];

    static_assert(
        alignof(STBase) == alignof(void*),
        "STBase alignment needs to be the same as the alignment of a pointer");

public:
    ~STVar()
    {
        destroy();
    }

    STVar(STVar const& other) : CountedObject(other)
    {
        if (other.p_ != nullptr)
            p_ = other.p_->copy(max_size, d_);
    }

    STVar&
    operator=(STVar const& rhs)
    {
        if (&rhs != this) [[likely]]
        {
            destroy();

            if (rhs.p_)
                p_ = rhs.p_->copy(max_size, d_);
        }

        return *this;
    }

    STVar(STVar&& other)
    {
        if (other.on_heap())
        {
            p_ = other.p_;
            other.p_ = nullptr;
        }
        else
        {
            p_ = other.p_->move(max_size, d_);
        }
    }

    STVar&
    operator=(STVar&& rhs)
    {
        if (&rhs != this) [[likely]]
        {
            destroy();

            if (rhs.on_heap())
            {
                p_ = rhs.p_;
                rhs.p_ = nullptr;
            }
            else
            {
                p_ = rhs.p_->move(max_size, d_);
            }
        }

        return *this;
    }

    STVar(STBase const& t)
    {
        p_ = t.copy(max_size, d_);
    }

    STVar(STBase&& t)
    {
        p_ = t.move(max_size, d_);
    }

    STVar(defaultObject_t, SField const& name);
    STVar(nonPresentObject_t, SField const& name);
    STVar(SerialIter& sit, SField const& name, int depth = 0);

    STBase&
    get()
    {
        return *p_;
    }

    STBase&
    operator*()
    {
        return get();
    }

    STBase*
    operator->()
    {
        return &get();
    }

    STBase const&
    get() const
    {
        return *p_;
    }

    STBase const&
    operator*() const
    {
        return get();
    }

    STBase const*
    operator->() const
    {
        return &get();
    }

    template <class T, class... Args>
        requires std::derived_from<T, STBase>
    friend STVar
    make_stvar(Args&&... args);

private:
    STVar() = default;

    STVar(SerializedTypeID id, SField const& name);

    bool
    on_heap() const noexcept
    {
        return static_cast<void const*>(p_) != static_cast<void const*>(d_);
    }

    void
    destroy()
    {
        if (on_heap())
            delete p_;
        else
            std::destroy_at(p_);

        p_ = nullptr;
    }

    template <class T, class... Args>
        requires std::derived_from<T, STBase>
    void
    construct(Args&&... args)
    {
        // The expectation is that all types constructed here share the
        // same alignment as STBase. It is possible for a derived class
        // to be "overaligned" but constructing such an instance in the
        // internal buffer would result in UB. If handling this case is
        // required without changing the alignment of the buffer, using
        // the heap allocation path is the best option.
        static_assert(
            alignof(T) == alignof(decltype(p_)),
            "Misaligned STBase-derived type");

        XRPL_ASSERT(p_ == nullptr, "STVar is already constructed");

        if constexpr (sizeof(T) > max_size)
            p_ = new T(std::forward<Args>(args)...);
        else
            p_ = std::construct_at(
                reinterpret_cast<T*>(d_), std::forward<Args>(args)...);
    }

    /** Construct the requested type according by the serialized type ID.

        As of now, the variadic args must either be:
          - a single SField; or
          - a SerialIter, followed by an SField.

        The depth is only relevant in the latter case.
     */
    template <typename... Args>
        requires ValidConstructSTArgs<Args...>
    void
    constructST(SerializedTypeID id, int depth, Args&&... arg);

    friend bool
    operator==(STVar const& lhs, STVar const& rhs)
    {
        return lhs.get().isEquivalent(rhs.get());
    }
};

template <class T, class... Args>
    requires std::derived_from<T, STBase>
STVar
make_stvar(Args&&... args)
{
    STVar st;
    st.construct<T>(std::forward<Args>(args)...);
    return st;
}

}  // namespace detail
}  // namespace ripple

#endif
