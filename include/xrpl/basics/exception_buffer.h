//------------------------------------------------------------------------------
/*
    This file is part of xahaud: https://github.com/xahau/xahaud
    Copyright (c) 2026, the Xahaud developers.

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

#ifndef XAHAU_BASICS_EXCEPTION_BUFFER_H_INCLUDED
#define XAHAU_BASICS_EXCEPTION_BUFFER_H_INCLUDED

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace ripple {
/** A fixed-size buffer for storing exception messages.

    This class provides a simple, noexcept-safe buffer for exception classes
    to use for storing custom messages. It is a fixed-capacity string, which
    truncates silently and always ensures proper NUL termination.

    It is primarily intended to be used as a base class for custom exception
    types that need to construct their message in a noexcept context.
*/
class exception_buffer
{
    /** The buffer into which we write. Zero-initialized. */
    char buf_[128] = {};

    /** The pointer to the next available position in the buffer. */
    char* ptr_ = buf_;

    /** The end of the usable buffer, with space for the NUL terminator. */
    char* const end_ = ptr_ + std::size(buf_) - 1;

public:
    /** Constructs a buffer with an initial message.

        @param s The initial string to append.
     */
    exception_buffer(std::string_view s = {}) noexcept
    {
        append(s);
    }

    /** Copy and move constructors.

        These are needed because we need @ref ptr_ and @ref end_ to
        point to this object's buffer, not the other's!

        @param other The "other" buffer.
     */
    /** @{ */
    exception_buffer(exception_buffer const& other) noexcept
        : exception_buffer(other.str())
    {
    }

    exception_buffer(exception_buffer&& other) noexcept
        : exception_buffer(other.str())
    {
    }
    /** @} */

    /** Unified copy & move assignment */
    exception_buffer&
    operator=(exception_buffer other) noexcept
    {
        ptr_ = std::copy_n(other.buf_, std::size(other.buf_), buf_);
        return *this;
    }

    /** Appends any type that is directly convertible to a string_view.

        As many bytes as possible will be appended; any excess is will
        be truncated.

        @param s The string to append.
        @return Always returns true.
     */
    bool
    append(std::string_view s) noexcept
    {
        if (auto const n = std::min<std::size_t>(s.size(), end_ - ptr_))
            ptr_ = std::copy_n(s.begin(), n, ptr_);
        return true;
    }

    /** Appends any type for which an ADL-found @to_string exists.

        As many bytes as possible will be appended; any excess is will
        be truncated. Any exceptions thrown @c to_string are swallowed
        and nothing is appended.

        @param arg The value to convert and append.
        @return false if the to_string threw an exception; true otherwise.
     */
    template <typename T>
        requires(
            !std::is_convertible_v<T, std::string_view> &&
            requires(T const& v) {
                { to_string(v) } -> std::convertible_to<std::string_view>;
            })
    bool
    append(T const& arg) noexcept
    {
        try
        {
            return append(to_string(arg));
        }
        catch (...)
        {
            return false;
        }
    }

    /** Shorthand for @ref append. */
    /** @{ */
    exception_buffer&
    operator+=(std::string_view s) noexcept
    {
        append(s);
        return *this;
    }

    template <typename T>
        requires(
            !std::is_convertible_v<T, std::string_view> &&
            requires(T const& v) {
                { to_string(v) } -> std::convertible_to<std::string_view>;
            })
    exception_buffer&
    operator+=(T const& arg) noexcept
    {
        append(arg);
        return *this;
    }
    /** @} */

    /** Returns a string_view containing the current message.

        @returns A string_view into the internal buffer, as it was at
                 the time the function was called.

        @note The view remains valid until this object is destroyed,
              assigned-to, or otherwise modified.
     */
    std::string_view
    str() const noexcept
    {
        return {buf_, static_cast<std::size_t>(ptr_ - buf_)};
    }

    /** Return a pointer to the NUL-terminated message buffer.

        @return A pointer to the internal buffer. Always NUL-terminated.
    */
    char const*
    c_str() const noexcept
    {
        return buf_;
    }

    /** Return whether the buffer is empty.

        @return true if no content has been appended, false otherwise.
    */
    bool
    empty() const noexcept
    {
        return ptr_ == buf_;
    }
};

/** Concatenates arguments into a std::string, stopping on allocation failure.

    Each argument is appended in order; if an allocation failure occurs
    while appending an argument, concatenation stops. If an argument is
    convertible to std::string_view, it is appended directly; otherwise
    we call to_string.

    @note The failing argument, if there is one, does not get partially
          appended.

    @tparam Args    The argument types to concatenate.
    @param args     The arguments to concatenate.
    @return         A possibly empty std::string, containing as many of
                    the arguments as could be appended before failure.
*/
template <typename... Args>
std::string
safe_string_concat(Args const&... args) noexcept
{
    std::string result;

    auto append_one = [&](auto const& x) -> bool {
        try
        {
            if constexpr (std::is_convertible_v<decltype(x), std::string_view>)
                result.append(static_cast<std::string_view>(x));
            else
                result.append(to_string(x));

            return true;
        }
        catch (...)
        {
            return false;
        }
    };

    (append_one(args) && ...);
    return result;
}

}  // namespace ripple

#endif
