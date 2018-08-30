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

#include <ripple/basics/contract.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/ToString.h>
#include <ripple/beast/core/LexicalCast.h>
#include <boost/algorithm/string.hpp>
#include <ripple/beast/net/IPEndpoint.h>
#include <boost/regex.hpp>
#include <algorithm>
#include <cstdarg>

namespace ripple {

template <typename InputIterator, typename OutputIterator>
bool from_hex(InputIterator first, InputIterator last, OutputIterator out)
{
    try
    {
        boost::algorithm::unhex(first, last, out);
        return true;
    }
    catch (boost::algorithm::hex_decode_error const&)
    {
        return false;
    }
}

template <typename OutputIterator>
bool from_hex(std::string x, OutputIterator out)
{
    try
    {
        boost::algorithm::unhex(x.begin(), x.end(), out);
        return true;
    }
    catch (boost::algorithm::hex_decode_error const&)
    {
        return false;
    }
}

boost::optional<std::string> from_hex(std::string s)
{
    try
    {
        return boost::algorithm::unhex(s);
    }
    catch (boost::algorithm::hex_decode_error const&)
    {
        return boost::none;
    }
}

uint64_t uintFromHex (std::string const& strSrc)
{
    uint64_t uValue (0);

    if (strSrc.size () > 16)
        Throw<std::invalid_argument> ("overlong 64-bit value");

    for (auto c : strSrc)
    {
        int ret = charUnHex (c);

        if (ret == -1)
            Throw<std::invalid_argument> ("invalid hex digit");

        uValue = (uValue << 4) | ret;
    }

    return uValue;
}

// TODO Callers should be using beast::URL and beast::parse_URL instead.
bool parseUrl (parsedURL& pUrl, std::string const& strUrl)
{
    // scheme://username:password@hostname:port/rest
    static boost::regex reUrl ("(?i)\\`\\s*([[:alpha:]][-+.[:alpha:][:digit:]]*)://([^/]+)(/.*)?\\s*?\\'");
    boost::smatch smMatch;

    bool bMatch = boost::regex_match (strUrl, smMatch, reUrl); // Match status code.

    if (bMatch)
    {
        pUrl.scheme = smMatch[1];
        boost::algorithm::to_lower (pUrl.scheme);
        pUrl.path = smMatch[3];
        pUrl.domain = smMatch[2];

        // now consider the domain/port fragment
        auto colonPos = pUrl.domain.find_last_of(':');
        if (colonPos != std::string::npos)
        {
            // use Endpoint class to see if this thing looks
            // like an IP addr...
            auto result {beast::IP::Endpoint::from_string_checked (pUrl.domain)};
            if (result.second)
            {
                pUrl.domain = result.first.address().to_string();
                pUrl.port = result.first.port();
            }
            else // otherwise we are DNS name + port
            {
                pUrl.port = beast::lexicalCast <std::uint16_t> (
                    pUrl.domain.substr(colonPos+1));
                pUrl.domain = pUrl.domain.substr(0, colonPos);
            }
        }
        //else, the whole thing is domain, not port
    }

    return bMatch;
}

std::string trim_whitespace (std::string str)
{
    boost::trim (str);
    return str;
}

boost::optional<std::uint64_t>
to_uint64(std::string const& s)
{
    std::uint64_t result;
    if (beast::lexicalCastChecked (result, s))
        return result;
    return boost::none;
}

} // ripple
