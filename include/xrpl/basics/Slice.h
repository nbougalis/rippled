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

#ifndef RIPPLE_BASICS_SLICE_H_INCLUDED
#define RIPPLE_BASICS_SLICE_H_INCLUDED

#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace ripple {

namespace detail {

template <class T>
concept is_byte_like = std::same_as<std::remove_cv_t<T>, char> ||
    std::same_as<std::remove_cv_t<T>, unsigned char> ||
    std::same_as<std::remove_cv_t<T>, uint8_t>;

}

/** An immutable linear range of bytes.

    A fully constructed Slice is guaranteed to be in a valid state.
    A Slice is lightweight and copyable, it retains no ownership
    of the underlying memory.
*/
class Slice
{
    std::uint8_t const* data_ = nullptr;
    std::size_t size_ = 0;

public:
    using value_type = std::uint8_t;
    using const_iterator = value_type const*;

    /** Default constructed Slice has length 0. */
    Slice() noexcept = default;

    Slice(Slice const&) noexcept = default;
    Slice&
    operator=(Slice const&) noexcept = default;

    /** Create a slice pointing to existing memory. */
    Slice(void const* data, std::size_t size) noexcept
        : data_(reinterpret_cast<std::uint8_t const*>(data)), size_(size)
    {
    }

    /** Return `true` if the byte range is empty. */
    [[nodiscard]] bool
    empty() const noexcept
    {
        return size_ == 0;
    }

    /** Returns the number of bytes in the storage.

        This may be zero for an empty range.
    */
    /** @{ */
    [[nodiscard]] std::size_t
    size() const noexcept
    {
        return size_;
    }

    [[nodiscard]] std::size_t
    length() const noexcept
    {
        return size_;
    }
    /** @} */

    /** Return a pointer to beginning of the storage.
        @note The return type is guaranteed to be a pointer
              to a single byte, to facilitate pointer arithmetic.
    */
    std::uint8_t const*
    data() const noexcept
    {
        return data_;
    }

    /** Access raw bytes. */
    std::uint8_t
    operator[](std::size_t i) const noexcept
    {
        XRPL_ASSERT(
            i < size_,
            "ripple::Slice::operator[](std::size_t) const : valid input");
        return data_[i];
    }

    /** Advance the buffer. */
    /** @{ */
    Slice&
    operator+=(std::size_t n)
    {
        if (n > size_)
            Throw<std::domain_error>("too small");
        data_ += n;
        size_ -= n;
        return *this;
    }

    Slice
    operator+(std::size_t n) const
    {
        Slice temp = *this;
        return temp += n;
    }
    /** @} */

    /** Shrinks the slice by moving its start forward by n characters. */
    void
    remove_prefix(std::size_t n)
    {
        data_ += n;
        size_ -= n;
    }

    /** Shrinks the slice by moving its end backward by n characters. */
    void
    remove_suffix(std::size_t n)
    {
        size_ -= n;
    }

    const_iterator
    begin() const noexcept
    {
        return data_;
    }

    const_iterator
    cbegin() const noexcept
    {
        return data_;
    }

    const_iterator
    end() const noexcept
    {
        return data_ + size_;
    }

    const_iterator
    cend() const noexcept
    {
        return data_ + size_;
    }

    /** Return a "sub slice" of given length starting at the given position

        Note that the subslice encompasses the range [pos, pos + rcount),
        where rcount is the smaller of count and size() - pos.

        @param pos position of the first character
        @count requested length

        @returns The requested subslice, if the request is valid.
        @throws std::out_of_range if pos > size()
     */
    Slice
    substr(
        std::size_t pos,
        std::size_t count = std::numeric_limits<std::size_t>::max()) const
    {
        if (pos > size())
            throw std::out_of_range("Requested sub-slice is out of bounds");

        return {data_ + pos, std::min(count, size() - pos)};
    }
};

//------------------------------------------------------------------------------

template <class Hasher>
inline void
hash_append(Hasher& h, Slice const& v)
{
    h(v.data(), v.size());
}

inline std::strong_ordering
operator<=>(Slice const& lhs, Slice const& rhs) noexcept
{
    return std::lexicographical_compare_three_way(
        lhs.data(),
        lhs.data() + lhs.size(),
        rhs.data(),
        rhs.data() + rhs.size());
    ;
}

inline bool
operator==(Slice const& lhs, Slice const& rhs) noexcept
{
    return std::equal(
        lhs.data(),
        lhs.data() + lhs.size(),
        rhs.data(),
        rhs.data() + rhs.size());
}
template <class Stream>
Stream&
operator<<(Stream& s, Slice const& v)
{
    s << strHex(v);
    return s;
}

template <class R>
    requires std::ranges::contiguous_range<R> &&
    detail::is_byte_like<std::ranges::range_value_t<R>>
Slice
makeSlice(R const& r) noexcept
{
    return Slice(std::ranges::data(r), std::ranges::size(r));
}

}  // namespace ripple

#endif
