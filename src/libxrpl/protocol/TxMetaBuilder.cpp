#include <xrpl/protocol/TxMetaBuilder.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace xrpl {

namespace {

STObject*
findAffectedNode(STArray& nodes, UInt256 const& index) noexcept
{
    for (auto& n : nodes)
    {
        if (n.getFieldH256(sfLedgerIndex) == index)
            return &n;
    }

    return nullptr;
}

}  // namespace

/**
 * The number of affected nodes the TxMetaBuilder reserves room for up front.
 *
 * This number must be large enough that growing the array is rare, while the
 * allocation stays small enough for glibc to service it using its per-thread
 * cache, which takes no locks. With default settings, that cache handles any
 * request of up to 1032 bytes; to stay conservative, we use 960 bytes as our
 * own cutoff. At 48 bytes per STObject, that is room for 20 nodes.
 *
 * In a sample of over 2.2M transactions taken in November 2026, the share of
 * transactions whose metadata had at most the given number of affected nodes
 * was:
 *
 *     <=  8 nodes: 92.49%
 *     <= 12 nodes: 97.86%
 *     <= 16 nodes: 98.96%
 *     <= 20 nodes: 99.36%
 */
constexpr std::size_t kReservedNodes = 960 / sizeof(STObject);

// Sanity check. With room for fewer than 8 nodes, more than 1 transaction in
// 10 would have to grow the array.
static_assert(kReservedNodes >= 8, "The TxMetaBuilder affected-node reservation size is too low.");

TxMetaBuilder::TxMetaBuilder(
    UInt256 const& transactionID,
    std::uint32_t ledger,
    std::optional<STAmount> deliveredAmount,
    std::optional<UInt256> parentBatchID)
    : transactionID_(transactionID)
    , ledgerSeq_(ledger)
    , deliveredAmount_(std::move(deliveredAmount))
    , parentBatchID_(std::move(parentBatchID))
    , nodes_(sfAffectedNodes, kReservedNodes)
{
}

void
TxMetaBuilder::setAffectedNode(UInt256 const& key, SField const& type, std::uint16_t nodeType)
{
    // make sure the node exists and force its type
    if (auto obj = findAffectedNode(nodes_, key))
    {
        obj->setFName(type);
        obj->setFieldU16(sfLedgerEntryType, nodeType);
        return;
    }

    nodes_.emplaceBack(type, [&](STObject& obj) {
        obj[sfLedgerIndex] = key;
        obj[sfLedgerEntryType] = nodeType;
    });
}

STObject&
TxMetaBuilder::getAffectedNode(std::shared_ptr<SLE> const& node, SField const& type)
{
    UInt256 const& index = node->key();

    if (auto ret = findAffectedNode(nodes_, index))
        return *ret;

    return nodes_.emplaceBack(type, [&](STObject& obj) {
        obj[sfLedgerIndex] = index;
        obj[sfLedgerEntryType] = node->getFieldU16(sfLedgerEntryType);
    });
}

STObject&
TxMetaBuilder::getAffectedNode(UInt256 const& key)
{
    if (auto ret = findAffectedNode(nodes_, key))
        return *ret;

    // LCOV_EXCL_START
    UNREACHABLE("xrpl::TxMetaBuilder::getAffectedNode(UInt256) : node not found");
    Throw<std::runtime_error>("Affected node not found");
    // LCOV_EXCL_STOP
}

TxMeta
TxMetaBuilder::build(TransactionResult result, std::uint32_t index) &&
{
    nodes_.sort([](STObject const& o1, STObject const& o2) {
        return o1[sfLedgerIndex] < o2[sfLedgerIndex];
    });

    return {
        transactionID_,
        ledgerSeq_,
        index,
        result,
        std::move(deliveredAmount_),
        std::move(parentBatchID_),
        std::move(nodes_)};
}

}  // namespace xrpl
