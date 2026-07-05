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

#ifndef RIPPLE_SHAMAP_SHAMAPNODEID_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPNODEID_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <tuple>

namespace ripple {

/** Identifies a node inside a SHAMap */
class SHAMapNodeID : public CountedObject<SHAMapNodeID>
{
    uint256 id_;
    unsigned int depth_ = 0;

public:
    SHAMapNodeID() = default;
    SHAMapNodeID(SHAMapNodeID const& other) = default;
    SHAMapNodeID(unsigned int depth, uint256 const& hash);

    SHAMapNodeID&
    operator=(SHAMapNodeID const& other) = default;

    bool
    isRoot() const
    {
        return depth_ == 0;
    }

    // Get the wire format (256-bit nodeID, 1-byte depth)
    std::string
    getRawString() const;

    unsigned int
    getDepth() const
    {
        return depth_;
    }

    uint256 const&
    getNodeID() const
    {
        return id_;
    }

    SHAMapNodeID
    getChildNodeID(unsigned int m) const;

    /** Comparison operators */
    friend auto
    operator<=>(SHAMapNodeID const& lhs, SHAMapNodeID const& rhs)
    {
        return std::tie(lhs.depth_, lhs.id_) <=> std::tie(rhs.depth_, rhs.id_);
    }

    friend bool
    operator==(SHAMapNodeID const& lhs, SHAMapNodeID const& rhs)
    {
        return lhs.depth_ == rhs.depth_ && lhs.id_ == rhs.id_;
    }
};

inline std::string
to_string(SHAMapNodeID const& node)
{
    if (node.isRoot())
        return "NodeID(root)";

    return "NodeID(" + std::to_string(node.getDepth()) + "," +
        to_string(node.getNodeID()) + ")";
}

inline std::ostream&
operator<<(std::ostream& out, SHAMapNodeID const& node)
{
    return out << to_string(node);
}

/** Return an object representing a serialized SHAMap Node ID
 *
 * @param s A string of bytes
 * @param data a non-null pointer to a buffer of @param size bytes.
 * @param size the size, in bytes, of the buffer pointed to by @param data.
 * @return A seated optional if the buffer contained a serialized SHAMap
 *         node ID and an unseated optional otherwise.
 */
/** @{ */
[[nodiscard]] std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(std::span<std::byte const> data);

[[nodiscard]] inline std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(std::string const& s)
{
    return deserializeSHAMapNodeID(std::as_bytes(std::span(s)));
}
/** @} */

/** Returns the branch that would contain the given hash */
/** @{ */
[[nodiscard]] inline unsigned int
selectBranch(unsigned int depth, uint256 const& hash) noexcept
{
    XRPL_ASSERT(
        depth < 2 * uint256::bytes, "ripple::selectBranch : maximum depth");

    auto branch = static_cast<unsigned int>(*(hash.begin() + (depth / 2)));

    if (depth & 1)
        branch &= 0xf;
    else
        branch >>= 4;

    return branch;
}

[[nodiscard]] inline unsigned int
selectBranch(SHAMapNodeID const& id, uint256 const& hash) noexcept
{
    return selectBranch(id.getDepth(), hash);
}
/** @} */

}  // namespace ripple

#endif
