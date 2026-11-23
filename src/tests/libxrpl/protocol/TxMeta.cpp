#include <xrpl/protocol/TxMeta.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMetaBuilder.h>

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <utility>

using namespace xrpl;

TEST(TxMeta, build)
{
    UInt256 const txID{7};

    TxMetaBuilder builder{txID, 5, std::nullopt, std::nullopt};

    // Recorded out of order; build sorts by key.
    builder.setAffectedNode(UInt256{2}, sfModifiedNode, ltOFFER);
    builder.setAffectedNode(UInt256{1}, sfCreatedNode, ltACCOUNT_ROOT);

    // Recording a known key again changes its type instead of adding a node.
    builder.setAffectedNode(UInt256{2}, sfDeletedNode, ltOFFER);

    TxMeta const meta = std::move(builder).build(tecCLAIM, 3);

    EXPECT_EQ(meta.getTxID(), txID);
    EXPECT_EQ(meta.getLgrSeq(), 5u);
    EXPECT_EQ(meta.getIndex(), 3u);
    EXPECT_EQ(meta.getResultTER(), tecCLAIM);
    EXPECT_FALSE(meta.getDeliveredAmount().has_value());

    auto const& nodes = meta.getNodes();
    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_EQ(nodes[0].getFieldH256(sfLedgerIndex), UInt256{1});
    EXPECT_TRUE(nodes[0].getFName() == sfCreatedNode);
    EXPECT_EQ(nodes[1].getFieldH256(sfLedgerIndex), UInt256{2});
    EXPECT_TRUE(nodes[1].getFName() == sfDeletedNode);

    // The serialized metadata parses back to the same metadata.
    Serializer s;
    meta.getAsObject().add(s);

    TxMeta const parsed{txID, 5, s.slice()};

    EXPECT_EQ(parsed.getIndex(), 3u);
    EXPECT_EQ(parsed.getResultTER(), tecCLAIM);
    EXPECT_TRUE(parsed.getAsObject() == meta.getAsObject());
}

TEST(TxMeta, invalidResult)
{
    // 50 is neither tes nor tec.
    STObject const obj(sfMetadata, [](STObject& md) {
        md.setFieldU8(sfTransactionResult, 50);
        md.setFieldU32(sfTransactionIndex, 0);
        md.setFieldArray(sfAffectedNodes, STArray{sfAffectedNodes});
    });

    EXPECT_THROW((TxMeta{UInt256{7}, 1, obj}), std::runtime_error);
}
