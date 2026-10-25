#pragma once

#include <xrpl/basics/base_uint.h>

#include <compare>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace xrpl {

/**
 * Identifies a node inside a SHAMap
 */
class SHAMapNodeID
{
    /**
     * The depth of the node, from 0 for the root to SHAMap::kLeafDepth.
     *
     * A node at depth d is reached from the root by d branch selections,
     * one per nibble of the key.
     *
     * @note Declared before id_ so that the defaulted spaceship operator,
     *       which compares members in declaration order, will order node
     *       IDs by depth first and then by prefix. Nothing relies on the
     *       order today, but this emulates the semantics of the previous
     *       hand-written comparison.
     */
    unsigned int depth_ = 0;

    /**
     * The path to the node: the first depth_ nibbles of any key beneath
     * it, with every nibble past that cleared.
     *
     * This is a prefix, not a key! A leaf's key is the full 256 bits; a
     * node's ID is that key truncated to its depth, so two leaves under
     * the same node share the node's ID as a prefix.
     */
    UInt256 id_;

    /**
     * Construct a node ID from its parts.
     *
     * This constructor is private because it does not validate any of
     * its arguments are consistent, although debug builds will assert.
     *
     * @param depth The depth of the node.
     * @param hash  The node's prefix, already masked to depth.
     */
    SHAMapNodeID(unsigned int depth, UInt256 const& hash) noexcept;

public:
    /**
     * The root node's ID: depth zero and an empty prefix.
     */
    SHAMapNodeID() = default;

    [[nodiscard]] bool
    isRoot() const noexcept
    {
        return depth_ == 0;
    }

    // Get the wire format (256-bit nodeID, 1-byte depth)
    [[nodiscard]] std::string
    getRawString() const;

    [[nodiscard]] unsigned int
    getDepth() const noexcept
    {
        return depth_;
    }

    [[nodiscard]] UInt256 const&
    getNodeID() const noexcept
    {
        return id_;
    }

    /**
     * The ID of this node's child along the given branch.
     *
     * The child sits one level deeper, with `branch` as the nibble that
     * selects it, so its ID is this node's prefix extended by that nibble.
     *
     * @param branch The branch to descend; must be below SHAMap::kBranchFactor.
     * @return The child's ID, at depth getDepth() + 1.
     * @throws std::logic_error if this node is at SHAMap::kLeafDepth, since
     *         a node there is a leaf and has no children and asking for one
     *         is a caller error.
     */
    [[nodiscard]] SHAMapNodeID
    getChildNodeID(unsigned int branch) const;

    /**
     * Test whether this node ID lies on the path to the given leaf key
     *
     * A node at depth d identifies the tree path spelled by the first d
     * nibbles of its key, so any leaf beneath it must agree on that prefix.
     * A node ID that fails this test names a different subtree than the one
     * it was built for.
     *
     * @param key  the key of a leaf below this node
     * @return whether this node ID is a prefix of the leaf key
     */
    [[nodiscard]] bool
    isPrefixOf(UInt256 const& key) const noexcept;

    /**
     * The ID of the node at a given depth on the path to a key.
     *
     * This is the only way to make a non-root node ID. The key is masked
     * to the first `depth` nibbles, so it need not be a leaf's key: any
     * 256-bit value with the right prefix produces the same ID.
     *
     * @param depth The depth of the node; at most SHAMap::kLeafDepth.
     * @param key   A key beneath the node, or any value sharing its prefix.
     * @return The node's ID.
     *
     * @note Debug builds assert that @p depth is at most SHAMap::kLeafDepth;
     *       release builds clamp it.
     */
    [[nodiscard]] static SHAMapNodeID
    createID(unsigned int depth, UInt256 const& key) noexcept;

    /**
     * Comparison operators
     *
     * Orders by depth, then by ID, following member declaration order. Being
     * defaulted, this also declares the defaulted operator==.
     */
    friend std::strong_ordering
    operator<=>(SHAMapNodeID const& lhs, SHAMapNodeID const& rhs) noexcept = default;
};

[[nodiscard]] inline std::string
to_string(SHAMapNodeID const& node)
{
    if (node.isRoot())
        return "NodeID(root)";

    return "NodeID(" + std::to_string(node.getDepth()) + "," + to_string(node.getNodeID()) + ")";
}

inline std::ostream&
operator<<(std::ostream& out, SHAMapNodeID const& node)
{
    return out << to_string(node);
}

/**
 * Parse a node ID from its wire format.
 *
 * The wire format is the 256-bit node ID followed by a single depth
 * byte, 33 bytes in all. The depth must not exceed SHAMap::kLeafDepth
 * and the ID must carry no bits below the first `depth` nibbles.
 *
 * @param data The serialized bytes.
 * @return The node ID, or an unseated optional if the input is not
 *         exactly 33 bytes or does not describe a valid node ID.
 *
 * @todo Since the input is bytes and not characters, this should really
 *       be accepting a `std::span<std::byte const>`; unfortunately, all
 *       callers currently hold a `std::string`, which directly converts
 *       into a `string_view` but not to a span of bytes.
 */
[[nodiscard]] std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(std::string_view data) noexcept;

/**
 * Returns the branch that would contain the given hash
 */
[[nodiscard]] unsigned int
selectBranch(SHAMapNodeID const& id, UInt256 const& hash) noexcept;

}  // namespace xrpl
