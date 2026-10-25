#include <xrpl/shamap/SHAMapNodeID.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl {

namespace {

/**
 * Masks for each possible tree depth.
 *
 * The mask at depth `d` has the first `d` nibbles set. This means that
 * `key & kDepthMask[d]` returns the prefix that identifies the subtree
 * that a node at depth `d` is the root of.
 */
constexpr auto kDepthMask = []() consteval {
    std::array<UInt256, SHAMap::kLeafDepth + 1> masks;

    // One hex digit per nibble: the mask at depth d keeps the first d.
    std::array<char, 2 * UInt256::kBytes> hex;
    hex.fill('0');

    for (std::size_t d = 0; d <= SHAMap::kLeafDepth; ++d)
    {
        masks[d] = UInt256{std::string_view{hex}};

        if (d < SHAMap::kLeafDepth)
            hex[d] = 'F';
    }

    return masks;
}();

/**
 * The prefix of a key at a given depth.
 *
 * @param key   A leaf key, or any 256-bit value on the path to one.
 * @param depth The depth to truncate to; at most SHAMap::kLeafDepth.
 * @return `key` with everything past its first `depth` nibbles cleared:
 *         the ID of the node at that depth on the path to `key`.
 */
UInt256
maskedToDepth(UInt256 const& key, unsigned int depth) noexcept
{
    XRPL_ASSERT(
        depth <= SHAMap::kLeafDepth, "xrpl::SHAMapNodeID::maskedToDepth : depth within tree");
    return key & kDepthMask[depth];
}

}  // namespace

SHAMapNodeID::SHAMapNodeID(unsigned int depth, UInt256 const& hash) noexcept
    : depth_(depth), id_(hash)
{
    XRPL_ASSERT(
        depth_ <= SHAMap::kLeafDepth, "xrpl::SHAMapNodeID::SHAMapNodeID : depth within tree");
}

SHAMapNodeID
SHAMapNodeID::createID(unsigned int depth, UInt256 const& key) noexcept
{
    // Callers are expected to pass a depth they have already bounded; the
    // assert catches one that does not, and the clamp ensures a violation
    // will not become an out-of-bounds read of kDepthMask.
    XRPL_ASSERT(depth <= SHAMap::kLeafDepth, "xrpl::SHAMapNodeID::createID : depth within tree");
    depth = std::min(depth, SHAMap::kLeafDepth);

    return {depth, maskedToDepth(key, depth)};
}

std::string
SHAMapNodeID::getRawString() const
{
    Serializer s(33);
    s.addBitString(id_);
    s.add8(checkedCast<std::uint8_t>(depth_));
    return std::string(static_cast<char const*>(s.data()), s.size());
}

SHAMapNodeID
SHAMapNodeID::getChildNodeID(unsigned int branch) const
{
    XRPL_ASSERT(
        branch < SHAMap::kBranchFactor, "xrpl::SHAMapNodeID::getChildNodeID : valid branch input");

    // A node at kLeafDepth is a leaf: it has no children, and a child ID would sit at a depth
    // that no node can have. This is a caller error rather than a broken invariant, so it is
    // reported with an exception, not an assert.
    if (depth_ >= SHAMap::kLeafDepth)
        Throw<std::logic_error>(std::format("Request for child node ID of {}", to_string(*this)));

    // The child's ID is this node's prefix extended by one nibble. id_ is already masked to
    // depth_, so replacing nibble depth_ (the high half of its byte when depth_ is even, the
    // low half when odd) yields an ID masked to depth_ + 1 with no further work; this is why
    // the private constructor is used here rather than createID, which would mask again.
    auto id = id_;

    auto& b = id.begin()[depth_ / 2];
    b = ((depth_ & 1) != 0u) ? ((b & 0xF0) | branch) : ((b & 0x0F) | (branch << 4));

    return {depth_ + 1, id};
}

bool
SHAMapNodeID::isPrefixOf(UInt256 const& key) const noexcept
{
    return maskedToDepth(key, depth_) == id_;
}

[[nodiscard]] std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(std::string_view data) noexcept
{
    if (data.size() != UInt256::size() + 1)
        return std::nullopt;

    auto const bytes = std::as_bytes(std::span(data));

    auto const depth = std::to_integer<unsigned int>(bytes[UInt256::size()]);

    if (depth > SHAMap::kLeafDepth)
        return std::nullopt;

    UInt256 const id{bytes.first<UInt256::size()>()};

    std::optional ret{SHAMapNodeID::createID(depth, id)};

    // A serialized ID must carry no bits below its own depth: masking it must be a no-op.
    if (ret->getNodeID() != id)
        ret.reset();

    return ret;
}

[[nodiscard]] unsigned int
selectBranch(SHAMapNodeID const& id, UInt256 const& hash) noexcept
{
    XRPL_ASSERT(id.getDepth() < SHAMap::kLeafDepth, "xrpl::selectBranch : depth below leaf depth");

    // A depth-64 ID has no nibble left to select. Callers must not ask, but clamp anyway to keep
    // the read below the end of the 32-byte key.
    auto const depth = std::min(id.getDepth(), SHAMap::kLeafDepth - 1u);
    auto branch = static_cast<unsigned int>(*(hash.begin() + (depth / 2)));

    if ((depth & 1) != 0u)
    {
        branch &= 0xf;
    }
    else
    {
        branch >>= 4;
    }

    XRPL_ASSERT(branch < SHAMap::kBranchFactor, "xrpl::selectBranch : maximum result");
    return branch;
}

}  // namespace xrpl
