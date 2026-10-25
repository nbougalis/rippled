#include <xrpl/shamap/SHAMapNodeID.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/shamap/SHAMap.h>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace xrpl::tests {

// An arbitrary 32-byte key reused across tests below that don't care about its specific value,
// only that it is a well-formed key.
constexpr UInt256 kTestKey("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");

TEST(SHAMapNodeIDTest, root_is_prefix_of_every_key)
{
    SHAMapNodeID const root;
    EXPECT_TRUE(root.isRoot());
    EXPECT_EQ(root.getDepth(), 0u);
    EXPECT_TRUE(root.isPrefixOf(UInt256{}));
    EXPECT_TRUE(root.isPrefixOf(kTestKey));
}

TEST(SHAMapNodeIDTest, child_id_matches_create_id_at_every_depth)
{
    // Walking the branches spelled by the key's own nibbles must keep every intermediate ID a
    // prefix of that key, and must land on the same ID that createID computes directly. The two
    // are independent code paths: one writes a nibble into a copy, the other masks the key.
    SHAMapNodeID id;
    for (auto depth = 0u; depth < SHAMap::kLeafDepth; ++depth)
    {
        id = id.getChildNodeID(selectBranch(id, kTestKey));
        EXPECT_EQ(id.getDepth(), depth + 1);
        EXPECT_TRUE(id.isPrefixOf(kTestKey)) << "depth " << id.getDepth();
        EXPECT_EQ(id, SHAMapNodeID::createID(depth + 1, kTestKey)) << "depth " << id.getDepth();
    }
    EXPECT_EQ(id.getDepth(), SHAMap::kLeafDepth);
}

TEST(SHAMapNodeIDTest, wrong_branch_is_not_prefix_of_key)
{
    SHAMapNodeID const root;
    auto const correct = selectBranch(root, kTestKey);
    ASSERT_EQ(correct, 0xbu);

    // An ID built from the wrong branch still has a valid depth and a self-consistent mask, so
    // isPrefixOf(kTestKey) below is what actually distinguishes the correct branch from the rest.
    for (auto branch = 0u; branch < SHAMap::kBranchFactor; ++branch)
    {
        auto const child = root.getChildNodeID(branch);
        EXPECT_EQ(child.getDepth(), 1u);
        EXPECT_EQ(child.isPrefixOf(kTestKey), branch == correct) << "branch " << branch;
    }
}

TEST(SHAMapNodeIDTest, prefix_check_is_depth_sensitive)
{
    // kTestKey and kOther agree on the first two nibbles ("b9") and then diverge.
    constexpr UInt256 kOther =
        kTestKey ^ UInt256{"00F0000000000000000000000000000000000000000000000000000000000000"};

    auto id = SHAMapNodeID{}.getChildNodeID(selectBranch(SHAMapNodeID{}, kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kOther)) << "shared first nibble";

    id = id.getChildNodeID(selectBranch(id, kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kOther)) << "shared second nibble";

    // Third nibble differs, so the deeper ID no longer covers kOther.
    id = id.getChildNodeID(selectBranch(id, kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kTestKey));
    EXPECT_FALSE(id.isPrefixOf(kOther));
}

TEST(SHAMapNodeIDTest, leaf_id_from_key_is_prefix_of_that_key)
{
    auto const leaf = SHAMapNodeID::createID(SHAMap::kLeafDepth, kTestKey);
    EXPECT_TRUE(leaf.isPrefixOf(kTestKey));

    // At full depth the prefix is the whole key, so nothing else matches.
    EXPECT_FALSE(leaf.isPrefixOf(kTestKey.next()));
}

TEST(SHAMapNodeIDTest, create_id_masks_key_to_depth)
{
    for (auto depth = 0u; depth <= SHAMap::kLeafDepth; ++depth)
    {
        auto const id = SHAMapNodeID::createID(depth, kTestKey);
        EXPECT_EQ(id.getDepth(), depth);
        EXPECT_TRUE(id.isPrefixOf(kTestKey)) << "depth " << depth;
    }
}

TEST(SHAMapNodeIDTest, comparison_orders_by_depth_then_prefix)
{
    // The spaceship is defaulted over (depth_, id_), so depth is the primary key.
    auto const shallow = SHAMapNodeID::createID(1, kTestKey);
    auto const deep = SHAMapNodeID::createID(2, kTestKey);
    EXPECT_LT(SHAMapNodeID{}, shallow);
    EXPECT_LT(shallow, deep);

    // At equal depth, the prefix decides.
    auto const branch0 = SHAMapNodeID{}.getChildNodeID(0);
    auto const branch15 = SHAMapNodeID{}.getChildNodeID(15);
    EXPECT_LT(branch0, branch15);
    EXPECT_NE(branch0, branch15);
    EXPECT_EQ(branch0, SHAMapNodeID{}.getChildNodeID(0));
}

// The guards below must hold with XRPL_ASSERT compiled out (NDEBUG), so each one
// has to be a real runtime check rather than an assert.

TEST(SHAMapNodeIDTest, child_of_leaf_depth_id_throws)
{
    auto const leafDepthID = SHAMapNodeID::createID(SHAMap::kLeafDepth, kTestKey);
    ASSERT_EQ(leafDepthID.getDepth(), SHAMap::kLeafDepth);
    EXPECT_THROW((void)leafDepthID.getChildNodeID(0), std::logic_error);
}

TEST(SHAMapNodeIDDeathTest, out_of_range_depth_is_clamped)
{
    // A depth past kLeafDepth has no mask in the 65-entry table. createID is the only way to make
    // a non-root ID, so it is the one place that bounds the depth: it asserts, and clamps so that
    // a build with asserts compiled out (or routed to Antithesis's non-fatal handler) cannot read
    // past the table. Only such a build reaches the clamp, so only it can assert on the result.
#if defined(NDEBUG) || defined(ENABLE_VOIDSTAR)
    for (auto const depth : {SHAMap::kLeafDepth + 1u, 100u, 255u, 256u, 320u})
    {
        auto const id = SHAMapNodeID::createID(depth, kTestKey);

        // Clamped to a real depth, not the depth asked for, and not a byte-narrowed version of it:
        // 256 would otherwise become 0 and name the root, 320 would become 64.
        EXPECT_EQ(id.getDepth(), SHAMap::kLeafDepth) << "depth " << depth;

        // id_ and depth_ still agree, so the object is usable rather than merely non-crashing.
        EXPECT_TRUE(id.isPrefixOf(kTestKey)) << "depth " << depth;
        EXPECT_EQ(id, SHAMapNodeID::createID(SHAMap::kLeafDepth, kTestKey)) << "depth " << depth;
    }
#else
    EXPECT_DEATH(
        (void)SHAMapNodeID::createID(SHAMap::kLeafDepth + 1u, kTestKey), "depth within tree");
#endif
}

TEST(SHAMapNodeIDDeathTest, select_branch_clamps_leaf_depth)
{
    // selectBranch's own precondition is depth < kLeafDepth: a depth-64 ID has no nibble left
    // to select. That makes it unlike the guards above, which have a throw/return reachable
    // even with XRPL_ASSERT compiled out; selectBranch has no such path, so the two build
    // configurations have to be tested differently.
    //
    // Under ENABLE_VOIDSTAR, XRPL_ASSERT routes to Antithesis's assert_impl, which only records
    // the hit and returns rather than aborting, even though NDEBUG is undefined there (voidstar
    // requires a Debug build). So the assert is live in name but never fatal, the same as the
    // NDEBUG case below.
    auto const leafDepthID = SHAMapNodeID::createID(SHAMap::kLeafDepth, kTestKey);

#if defined(NDEBUG) || defined(ENABLE_VOIDSTAR)
    // With the assert compiled out or routed to a non-fatal handler, the clamp is what stands
    // between this call and reading past the end of the 32-byte key. Clamping means it reads the
    // same byte, and returns the same branch, as the deepest ID that still has one: depth 63.
    auto const deepestWithBranchID = SHAMapNodeID::createID(SHAMap::kLeafDepth - 1u, kTestKey);
    auto const branch = selectBranch(leafDepthID, kTestKey);
    EXPECT_LT(branch, SHAMap::kBranchFactor);
    EXPECT_EQ(branch, selectBranch(deepestWithBranchID, kTestKey));
#else
    // In a debug build the assert is live and must reject this call outright, in a forked
    // process so a failure here cannot take down the rest of the suite.
    EXPECT_DEATH((void)selectBranch(leafDepthID, kTestKey), "depth below leaf depth");
#endif
}

TEST(SHAMapNodeIDTest, deserialize_rejects_wrong_length)
{
    // The wire format is exactly 33 bytes. Any other length is rejected before the contents are
    // looked at, so the bytes themselves can be anything; zero is a valid prefix at every depth.
    for (std::size_t length = 0; length <= 40; ++length)
    {
        std::string const wire(length, '\0');
        EXPECT_EQ(deserializeSHAMapNodeID(wire).has_value(), length == UInt256::size() + 1)
            << "length " << length;
    }
}

TEST(SHAMapNodeIDTest, deserialize_accepts_only_consistent_depth_and_prefix)
{
    // Eleven non-zero nibbles, then zeros. Odd, so that the boundary falls mid-byte: at depth 11
    // the mask keeps the high nibble of byte 5 and clears the low one.
    constexpr UInt256 kPrefix{"a1b2c3d4e5f00000000000000000000000000000000000000000000000000000"};
    constexpr unsigned int kPrefixNibbles = 11;

    for (unsigned int depth = 0; depth <= 255; ++depth)
    {
        std::string wire(kPrefix.begin(), kPrefix.end());
        wire.push_back(static_cast<char>(depth));

        // Accepted only when the depth is within the tree and the prefix carries nothing below
        // it: the first eleven nibbles are set, so any shallower depth is rejected.
        bool const expected = (depth >= kPrefixNibbles) && (depth <= SHAMap::kLeafDepth);

        auto const id = deserializeSHAMapNodeID(wire);
        ASSERT_EQ(id.has_value(), expected) << "depth " << depth;

        if (id.has_value())
        {
            EXPECT_EQ(id->getDepth(), depth) << "depth " << depth;
            EXPECT_EQ(id->getNodeID(), kPrefix) << "depth " << depth;
            EXPECT_EQ(id->getRawString(), wire) << "depth " << depth;
        }
    }
}

}  // namespace xrpl::tests
