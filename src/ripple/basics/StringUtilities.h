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

#ifndef RIPPLE_BASICS_STRINGUTILITIES_H_INCLUDED
#define RIPPLE_BASICS_STRINGUTILITIES_H_INCLUDED

#include <ripple/basics/Blob.h>
#include <ripple/basics/HexUtils.h>
#include <boost/endian/conversion.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <sstream>
#include <string>

namespace ripple {

inline std::string strHex (std::uint64_t v)
{
    v = boost::endian::native_to_big(v);
    auto const ptr = reinterpret_cast<const unsigned char*>(&v);
    return to_hex(ptr, ptr + sizeof(std::uint64_t));
}

inline static std::string sqlEscape (std::string const& s)
{
    return "X'" + to_hex(s) + "'";
}

inline static std::string sqlEscape (Blob const& b)
{
    return "X'" + to_hex(b.begin(), b.end()) + "'";
}

uint64_t uintFromHex (std::string const& strSrc);

struct parsedURL
{
    explicit parsedURL() = default;

    std::string scheme;
    std::string domain;
    boost::optional<std::uint16_t> port;
    std::string path;

    bool
    operator == (parsedURL const& other) const
    {
        return scheme == other.scheme &&
            domain == other.domain &&
            port == other.port &&
            path == other.path;
    }
};

bool parseUrl (parsedURL& pUrl, std::string const& strUrl);

std::string trim_whitespace (std::string str);

boost::optional<std::uint64_t> to_uint64(std::string const& s);

} // ripple

#endif
