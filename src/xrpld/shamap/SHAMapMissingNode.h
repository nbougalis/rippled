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

#ifndef RIPPLE_SHAMAP_SHAMAPMISSINGNODE_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPMISSINGNODE_H_INCLUDED

#include <xrpld/shamap/SHAMapTreeNode.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/exception_buffer.h>
#include <exception>
#include <string>

namespace ripple {

enum class SHAMapType : std::uint8_t {
    TRANSACTION = 1,  // A tree of transactions
    STATE = 2,        // A tree of state nodes
    FREE = 3,         // A tree not part of a ledger
};

class SHAMapMissingNode : public std::exception, exception_buffer
{
    SHAMapMissingNode(SHAMapType t, std::string_view tag) noexcept
        : exception_buffer("Missing Node: ")
    {
        append([t]() {
            if (t == SHAMapType::TRANSACTION)
                return "Transaction Tree: ";
            if (t == SHAMapType::STATE)
                return "State Tree: ";
            if (t == SHAMapType::FREE)
                return "Free Tree: ";

            return "";
        }());

        append(tag);
    }

public:
    SHAMapMissingNode(SHAMapType t, SHAMapHash const& hash) noexcept
        : SHAMapMissingNode(t, "hash")
    {
        append(" ");
        append(hash);
    }

    SHAMapMissingNode(SHAMapType t, uint256 const& id) noexcept
        : SHAMapMissingNode(t, "id")
    {
        append(" ");
        append(id);
    }

    char const*
    what() const noexcept override
    {
        return c_str();
    }
};

}  // namespace ripple

#endif
