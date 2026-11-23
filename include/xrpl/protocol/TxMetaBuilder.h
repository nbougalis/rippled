#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

/**
 * Builds the metadata of a transaction that is being applied.
 *
 * While the transaction is applied, ApplyStateTable records the affected
 * nodes through setAffectedNode and getAffectedNode. Once the result is
 * known, build turns what was recorded into a TxMeta.
 *
 * A builder is used once: build consumes it.
 */
class TxMetaBuilder
{
    UInt256 transactionID_;
    std::uint32_t ledgerSeq_;

    std::optional<STAmount> deliveredAmount_;
    std::optional<UInt256> parentBatchID_;

    STArray nodes_;

public:
    /**
     * Start the metadata of a transaction about to be applied.
     *
     * @param transactionID The transaction's ID.
     * @param ledger The sequence of the ledger the transaction is applied to.
     */
    TxMetaBuilder(
        UInt256 const& transactionID,
        std::uint32_t ledger,
        std::optional<STAmount> deliveredAmount,
        std::optional<UInt256> parentBatchID);

    [[nodiscard]] UInt256 const&
    getTxID() const
    {
        return transactionID_;
    }

    [[nodiscard]] std::uint32_t
    getLgrSeq() const
    {
        return ledgerSeq_;
    }

    /**
     * Record an affected node, or change the type of an existing one.
     *
     * @param key The ledger entry's key.
     * @param type One of sfCreatedNode, sfModifiedNode or sfDeletedNode.
     * @param nodeType The ledger entry type.
     */
    void
    setAffectedNode(UInt256 const& key, SField const& type, std::uint16_t nodeType);

    /**
     * Return the node for a ledger entry, creating it if needed.
     *
     * A newly created node is given the type passed in; an existing
     * node's type is left unchanged.
     */
    STObject&
    getAffectedNode(std::shared_ptr<SLE> const& node, SField const& type);

    /**
     * Return the node for a ledger entry.
     *
     * @throws std::runtime_error if no node exists for the key.
     */
    STObject&
    getAffectedNode(UInt256 const& key);

    void
    setDeliveredAmount(std::optional<STAmount> const& amount)
    {
        deliveredAmount_ = amount;
    }

    void
    setParentBatchID(std::optional<UInt256> const& id)
    {
        parentBatchID_ = id;
    }

    /**
     * Finish the metadata.
     *
     * Sorts the affected nodes by key and returns the metadata. Call this
     * once all affected nodes have been recorded; it consumes the builder.
     *
     * @param result The transaction's result.
     * @param index The transaction's position within the ledger.
     * @returns The finished metadata.
     */
    [[nodiscard]] TxMeta
    build(TransactionResult result, std::uint32_t index) &&;
};

}  // namespace xrpl
