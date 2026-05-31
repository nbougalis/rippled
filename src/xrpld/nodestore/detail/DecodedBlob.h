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

#ifndef RIPPLE_NODESTORE_DECODEDBLOB_H_INCLUDED
#define RIPPLE_NODESTORE_DECODEDBLOB_H_INCLUDED

#include <xrpld/nodestore/NodeObject.h>

namespace ripple {
namespace NodeStore {

/** Convert a key and an associated blob into a NodeObject.

    This will extract the information required to construct a NodeObject. It
    also does consistency checking and returns the result, so it is possible
    to determine if the data is corrupted without throwing an exception. Not
    all forms of corruption are detected so further analysis will be needed
    to eliminate false negatives.

    @note This defines the database format of a NodeObject!
*/
inline boost::intrusive_ptr<NodeObject>
decodeNodeObject(void const* key, void const* data, std::size_t size)
{
    if (size <= 9)
        return {};

    auto const* p = static_cast<std::uint8_t const*>(data);
    auto const type = safe_cast<NodeObjectType>(p[8]);

    switch (type)
    {
        case hotUNKNOWN:
        case hotLEDGER:
        case hotACCOUNT_NODE:
        case hotTRANSACTION_NODE:
            return NodeObject::createObject(
                type, std::span{p + 9, size - 9}, uint256::fromVoid(key));
        default:
            return {};
    }
}

}  // namespace NodeStore
}  // namespace ripple

#endif
