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

#include <ripple/basics/Buffer.h>
#include <ripple/basics/Slice.h>

#include <boost/algorithm/hex.hpp>
#include <boost/optional.hpp>
#include <array>
#include <string>
#include <vector>

/** Convert input to a hex-encoded string */
/** @{ */
template <typename InputIterator>
std::string to_hex(InputIterator first, InputIterator last)
{
    std::string ret;
    boost::algorithm::hex (first, last, std::back_inserter(ret));
    return ret;
}

std::string to_hex(ripple::Slice s)
{
    return to_hex(s.data(), s.data() + s.size());
}

std::string to_hex(ripple::Buffer const& b)
{
    return to_hex(static_cast<ripple::Slice>(b));
}

std::string to_hex(std::string const& s)
{
    return boost::algorithm::hex(s);
}

template <typename T,
    typename = std::enable_if_t<
        std::is_same<T, std::uint8_t>::value ||
        std::is_same<T, char>::value ||
        std::is_same<T, unsigned char>::value>>
std::string to_hex(std::vector<T> const& v)
{
    return to_hex(v.begin(), v.end());
}

template <typename T, std::size_t N,
    typename = std::enable_if_t<
        std::is_same<T, std::uint8_t>::value ||
        std::is_same<T, char>::value ||
        std::is_same<T, unsigned char>::value>>
std::string to_hex(std::array<T, N> const& a)
{
    return to_hex(a.begin(), a.end());
}
/** @} */

/** Unpack a hex-encoded string */
/** @{ */
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

template<>
boost::optional<std::vector<unsigned char>> from_hex(std::string x)
{
    try
    {
        std::vector<unsigned char> v;
        v.reserve((x.size() + 1) / 2);
        boost::algorithm::unhex(x.begin(), x.end(), std::back_inserter(v));
        return v;
    }
    catch (boost::algorithm::hex_decode_error const& e)
    {
        return boost::none;
    }
}

template<>
boost::optional<std::vector<char>> from_hex(std::string x)
{
    try
    {
        std::vector<char> v;
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

bool
is_hex(std::string x)
{
    return is_hex(x, [](auto const) { return true; });
}

,
UnaryPredicate p
/** @} */

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <cassert>
#include <string>

namespace ripple {

/** @{ */
/** Converts a hex digit to the corresponding integer
    @param cDigit one of '0'-'9', 'A'-'F' or 'a'-'f'
    @return an integer from 0 to 15 on success; -1 on failure.
*/
int
charUnHex (unsigned char c);

inline
int
charUnHex (char c)
{
    return charUnHex (static_cast<unsigned char>(c));
}
/** @} */

}

#endif
