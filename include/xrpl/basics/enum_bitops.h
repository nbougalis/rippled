//------------------------------------------------------------------------------
/*
    This file is part of xahaud: https://github.com/xahau/xahaud
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

#ifndef XAHAU_BASICS_ENUM_BITOPS_H_INCLUDED
#define XAHAU_BASICS_ENUM_BITOPS_H_INCLUDED

#include <concepts>
#include <type_traits>

namespace ripple {

/** Opt-in bitwise operators for scoped enumerations.

    To enable bitwise operators (&, |, ^, ~, and their assignment forms) for
    an enum class, specialize enum_bitops::optin inside your namespace:

    @code
    enum class MyFlags : std::uint32_t { a = 1, b = 2, c = 4 };

    template <>
    struct enum_bitops::optin<MyFlags> : std::true_type {};
    @endcode
*/
namespace enum_bitops {

/** Specialise this to std::true_type to opt an enum class in. */
template <typename T>
struct optin : std::false_type
{
};

template <typename T>
concept candidate =
    optin<T>::value && std::unsigned_integral<std::underlying_type_t<T>>;

/** Convert an enum value to its underlying type.

    @note This can be replaced with `std::to_underlying` if and
          when the codebase transitions to C++23.
 */
template <candidate T>
constexpr auto
to_underlying(T val) noexcept
{
    return static_cast<std::underlying_type_t<T>>(val);
}

}  // namespace enum_bitops

// These need to be outside of the enum_bitops namespace so that they can
// discovered via ADL without the need for using declarations.
template <enum_bitops::candidate T>
constexpr T
operator&(T lhs, T rhs) noexcept
{
    return static_cast<T>(
        enum_bitops::to_underlying(lhs) & enum_bitops::to_underlying(rhs));
}

template <enum_bitops::candidate T>
constexpr T
operator|(T lhs, T rhs) noexcept
{
    return static_cast<T>(
        enum_bitops::to_underlying(lhs) | enum_bitops::to_underlying(rhs));
}

template <enum_bitops::candidate T>
constexpr T
operator^(T lhs, T rhs) noexcept
{
    return static_cast<T>(
        enum_bitops::to_underlying(lhs) ^ enum_bitops::to_underlying(rhs));
}

template <enum_bitops::candidate T>
constexpr T
operator~(T val) noexcept
{
    return static_cast<T>(~enum_bitops::to_underlying(val));
}

template <enum_bitops::candidate T>
constexpr T&
operator&=(T& lhs, T rhs) noexcept
{
    lhs = lhs & rhs;
    return lhs;
}

template <enum_bitops::candidate T>
constexpr T&
operator|=(T& lhs, T rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

template <enum_bitops::candidate T>
constexpr T&
operator^=(T& lhs, T rhs) noexcept
{
    lhs = lhs ^ rhs;
    return lhs;
}

}  // namespace ripple

#endif  // XAHAU_BASICS_ENUM_BITOPS_H_INCLUDED
