//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_SAFE_CAST_H_INCLUDED
#define RIPPLE_BASICS_SAFE_CAST_H_INCLUDED

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace ripple {

namespace detail {
template <typename T>
using as_maxint =
    std::conditional_t<std::is_signed_v<T>, std::intmax_t, std::uintmax_t>;
}

/** Concept: every value of @p Src can be represented by @p Dest.

    Given two integral types, the cast is safe when the destination has a
    representable range that covers the range of the source; that is, the
    destination can hold every possible value that the source can.

    The concept uses @c std::cmp_less_equal and @c std::cmp_greater_equal
    to compare the bounds correctly; using plain operators would silently
    apply the usual arithmetic conversions, which could produce the wrong
    result for cross-signed comparisons.

    Note: @c std::cmp_* requires standard signed or unsigned integer
          arguments, which excludes character types. To address this
          we widen the type's min & max values via @ref as_maxint to
          the max-width same-sign integer type. This does not change
          the semantics of the comparison.
*/
template <typename Dest, typename Src>
concept can_safely_cast = std::is_integral_v<Src> && std::is_integral_v<Dest> &&
    std::cmp_less_equal(
        static_cast<detail::as_maxint<Dest>>(std::numeric_limits<Dest>::min()),
        static_cast<detail::as_maxint<Src>>(std::numeric_limits<Src>::min())) &&
    std::cmp_greater_equal(
        static_cast<detail::as_maxint<Dest>>(std::numeric_limits<Dest>::max()),
        static_cast<detail::as_maxint<Src>>(std::numeric_limits<Src>::max()));

/** Compile-time-checked static_cast, which rejects narrowing or sign-erasing
 * casts. */
/** @{ */
template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_integral_v<Src>)
inline constexpr Dest
safe_cast(Src s) noexcept
{
    static_assert(
        can_safely_cast<Dest, Src>,
        "This cast is not value-preserving. Please use unsafe_cast instead.");

    return static_cast<Dest>(s);
}

template <typename Dest, typename Src>
    requires(std::is_enum_v<Dest> && std::is_integral_v<Src>)
inline constexpr Dest
safe_cast(Src s) noexcept
{
    return static_cast<Dest>(safe_cast<std::underlying_type_t<Dest>>(s));
}

template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_enum_v<Src>)
inline constexpr Dest
safe_cast(Src s) noexcept
{
    return safe_cast<Dest>(static_cast<std::underlying_type_t<Src>>(s));
}

template <typename Src>
    requires(std::is_enum_v<Src>)
inline constexpr std::underlying_type_t<Src>
safe_cast(Src s) noexcept
{
    return safe_cast<std::underlying_type_t<Src>>(s);
}
/** @} */

/** Integral-to-integral cast that is known to be narrowing or sign-erasing.

    This is used to explicitly flag narrowing or sign-erasing conversions
    that are guaranteed to be safe in context (e.g. because of invariants,
    prior validation, or other preconditions).

    This compile-time check ensures the cast remains "unsafe", so that if
    the underlying types were later changed to make the conversion become
    inherently safe, the static assertion would fire with instructions to
    migrate the call site to @ref safe_cast.
 */
/** @{ */
template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_integral_v<Src>)
inline constexpr Dest
unsafe_cast(Src s) noexcept
{
    static_assert(
        !can_safely_cast<Dest, Src>,
        "This cast is value-preserving. Please use safe_cast instead.");
    return static_cast<Dest>(s);
}

template <typename Dest, typename Src>
    requires(std::is_enum_v<Dest> && std::is_integral_v<Src>)
inline constexpr Dest
unsafe_cast(Src s) noexcept
{
    return static_cast<Dest>(unsafe_cast<std::underlying_type_t<Dest>>(s));
}

template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_enum_v<Src>)
inline constexpr Dest
unsafe_cast(Src s) noexcept
{
    return unsafe_cast<Dest>(static_cast<std::underlying_type_t<Src>>(s));
}
/** @} */

/** Integral-to-integral cast where the caller has performed a bounds-check.

    This serves to document that some runtime precondition or external
    invariant, which is not always visible to the compiler, guarantees
    the conversion being requested is value-preserving.

    This is meant to be used in generic code where the same expression
    may be value-preserving for one instantiation but not another, and
    the call site has an explicit check.

    Unlike @ref safe_cast and @ref unsafe_cast, this imposes no static
    check on the type relationship. It will allow both safe and unsafe
    casts, since the caller's claim is about runtime values, not types.
 */
/** @{ */
template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_integral_v<Src>)
inline constexpr Dest
checked_cast(Src s) noexcept
{
    return static_cast<Dest>(s);
}

template <typename Dest, typename Src>
    requires(std::is_enum_v<Dest> && std::is_integral_v<Src>)
inline constexpr Dest
checked_cast(Src s) noexcept
{
    return static_cast<Dest>(checked_cast<std::underlying_type_t<Dest>>(s));
}

template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_enum_v<Src>)
inline constexpr Dest
checked_cast(Src s) noexcept
{
    return checked_cast<Dest>(static_cast<std::underlying_type_t<Src>>(s));
}
/** @} */

}  // namespace ripple

#endif
