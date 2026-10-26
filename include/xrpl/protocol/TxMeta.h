#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <flat_set>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl {

/**
 * The metadata produced by applying a transaction to a ledger.
 *
 * Metadata records the outcome of a transaction: its result, its position
 * within the ledger, and the set of ledger entries it touched, along with
 * their state before and after.
 *
 * A TxMeta is built in two phases: during application, ApplyStateTable
 * records affected nodes through setAffectedNode and getAffectedNode;
 * once the result is known, addRaw finalizes and serializes it.
 *
 * A TxMeta deserialized from a ledger or the database is complete on
 * construction.
 */
class TxMeta
{
    UInt256 transactionID_;
    std::uint32_t ledgerSeq_;
    std::uint32_t index_;
    std::optional<TER> result_;

    std::optional<STAmount> deliveredAmount_;
    std::optional<UInt256> parentBatchID_;

    STArray nodes_;

    void
    setAdditionalFields(STObject const& obj)
    {
        if (obj.isFieldPresent(sfDeliveredAmount))
            deliveredAmount_ = obj.getFieldAmount(sfDeliveredAmount);

        if (obj.isFieldPresent(sfParentBatchID))
            parentBatchID_ = obj.getFieldH256(sfParentBatchID);
    }

public:
    /**
     * Construct empty metadata for a transaction about to be applied.
     *
     * The result is unset until addRaw is called; querying it or
     * serializing the metadata before then is an error.
     */
    TxMeta(UInt256 const& transactionID, std::uint32_t ledger);

    /**
     * Construct from serialized metadata.
     */
    TxMeta(UInt256 const& txID, std::uint32_t ledger, Slice);

    /**
     * Construct from a deserialized metadata object.
     */
    TxMeta(UInt256 const& txID, std::uint32_t ledger, STObject const&);

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
     * The result code as an integer, as it appears in the metadata.
     */
    [[nodiscard]] int
    getResult() const
    {
        return TERtoInt(getResultTER());
    }

    /**
     * The result code.
     *
     * It is an error to call this before the result has been set.
     */
    [[nodiscard]] TER
    getResultTER() const
    {
        if (!result_)
            logicError("TxMeta::getResultTER : result not set");
        return *result_;
    }

    /**
     * The transaction's position within its ledger.
     */
    [[nodiscard]] std::uint32_t
    getIndex() const
    {
        return index_;
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

    /**
     * Return a list of accounts affected by this transaction
     */
    [[nodiscard]] std::flat_set<AccountID>
    getAffectedAccounts() const;

    /**
     * Return the MPT issuances touched by this transaction.
     */
    [[nodiscard]] std::flat_set<MPTID>
    getAffectedMPTs() const;

    [[nodiscard]] json::Value
    getJson(JsonOptions p) const
    {
        return getAsObject().getJson(p);
    }

    /**
     * Finalize the metadata and serialize it.
     *
     * Sets the result and index, sorts the affected nodes by key and
     * appends the serialized metadata to @p s. Must be called exactly
     * once, after all affected nodes have been recorded.
     *
     * @param s The serializer to append to.
     * @param result The transaction's result; must be tes or tec.
     * @param index The transaction's position within the ledger.
     */
    void
    addRaw(Serializer& s, TER result, std::uint32_t index);

    /**
     * Return the metadata as a serializable object.
     *
     * It is an error to call this before the result has been set.
     */
    [[nodiscard]] STObject
    getAsObject() const;

    /**
     * The affected nodes, in the order they were recorded.
     */
    [[nodiscard]] auto&&
    getNodes(this auto&& self)
    {
        return std::forward<decltype(self)>(self).nodes_;
    }

    [[nodiscard]] std::optional<STAmount> const&
    getDeliveredAmount() const
    {
        return deliveredAmount_;
    }

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
};

}  // namespace xrpl
