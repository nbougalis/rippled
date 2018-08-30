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

#ifndef RIPPLE_BASICS_HEXUTILS_H_INCLUDED
#define RIPPLE_BASICS_HEXUTILS_H_INCLUDED

#include <boost/algorithm/hex.hpp>
#include <boost/optional.hpp>
#include <string>
#include <vector>
#include <type_traits>

namespace ripple {
namespace detail {

// Trait to detect "bytes":
template<typename T>
using is_byte = std::integral_constant<bool,
    std::is_same<char, std::remove_cv_t<T>>::value ||
    std::is_same<unsigned char, std::remove_cv_t<T>>::value>;

template<class...>
using void_t = void; // FIXME: remove and use std::void_t once we go to C++17

template<class, class = void_t<>>
struct has_data_and_size
    : std::false_type
{
};

template<class T>
struct has_data_and_size<T, void_t<
    decltype(std::declval<T const>().data()),
    decltype(std::declval<T const>().size())>>
    : std::true_type
{
};

template<class, class = void_t<>>
struct is_range
    : std::false_type
{
};

template<class T>
struct is_range<T, void_t<
    decltype(std::begin(std::declval<T>())),
    decltype(std::begin(std::declval<T>()))>>
    : std::true_type
{
};

}

/** Convert input to a hex-encoded string */
/** @{ */
template <typename InputIterator,
    typename C = typename detail::is_byte<InputIterator>::value_type>
std::string to_hex(InputIterator first, InputIterator last)
{
    std::string ret;
    ret.reserve (std::distance(first, last) * 2);
    boost::algorithm::hex(first, last, std::back_inserter(ret));
    return ret;
}

std::string to_hex(char const* str)
{
    std::string ret;
    if (str != nullptr)
    {
        if (std::size_t len = strlen(str))
        {
            ret.reserve(len * 2);
            boost::algorithm::hex(str, str + len, std::back_inserter(ret));
        }
    }
    return ret;
}

template <typename T,
    typename = std::enable_if_t<
        detail::is_byte<typename T::value_type>::value &&
        detail::has_data_and_size<T>::value>>
std::string to_hex(T const& t)
{
    std::string ret;
    ret.reserve(t.size() * 2);
    boost::algorithm::hex(t.data(), t.data() + t.size(), std::back_inserter(ret));
    return ret;
}
/** @} */

/** Unpack a hex-encoded string */
/** @{ */
template <typename OutputIterator>
bool from_hex(std::string x, OutputIterator out)
{
    try
    {
        boost::algorithm::unhex(x.begin(), x.end(), out);
        return true;
    }
    catch (boost::algorithm::hex_decode_error const& e)
    {
        return false;
    }
}

template <typename T>
boost::optional<T> from_hex(std::string x) = delete;

template<>
boost::optional<std::string> from_hex(std::string x)
{
    try
    {
        return boost::algorithm::unhex(x);
    }
    catch (boost::algorithm::hex_decode_error const& e)
    {
        return boost::none;
    }
}

template<>
boost::optional<std::vector<std::uint8_t>> from_hex(std::string x)
{
    try
    {
        std::vector<std::uint8_t> v;
        v.reserve((x.size() + 1) / 2);
        boost::algorithm::unhex(x.begin(), x.end(), std::back_inserter(v));
        return v;
    }
    catch (boost::algorithm::hex_decode_error const& e)
    {
        return boost::none;
    }
}
/** @} */

/** Verifies that a string is a valid hex encoded string */
/** @{ */
template<class UnaryPredicate>
bool is_hex(std::string s, UnaryPredicate p)
{
    if (s.size() & 1)
        return false;

    auto x = s.begin();

    while (x != s.end())
    {
        std::uint8_t v = 0;

        for(int i = 0; i != 2; ++i)
        {
            auto c = *x++;

            if (c >= '0' && c <= '9')
                v = (v << 4) + static_cast<std::uint8_t>(c - '0');
            else if (c >= 'A' && c <= 'F')
                v = (v << 4) + static_cast<std::uint8_t>(c - 'A' + 10);
            else if (c >= 'a' && c <= 'f')
                v = (v << 4) + static_cast<std::uint8_t>(c - 'a' + 10);
            else
                return false;
        }

        if (!p(v))
            return false;
    }

    return true;
}
/** @} */

}

#endif
