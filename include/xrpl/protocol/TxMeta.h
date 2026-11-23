#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <flat_set>
#include <optional>

namespace xrpl {

class TxMetaBuilder;

/**
 * The metadata produced by applying a transaction to a ledger.
 *
 * Metadata records the outcome of a transaction: its result, its position
 * within the ledger, and the set of ledger entries it touched, along with
 * their state before and after.
 *
 * A TxMeta is complete from the moment it exists, and does not change. It
 * is either deserialized from a ledger or the database, or produced by a
 * TxMetaBuilder once a transaction has been applied.
 */
class TxMeta
{
    friend class TxMetaBuilder;

    UInt256 transactionID_;
    std::uint32_t ledgerSeq_;
    std::uint32_t index_;
    TER result_;

    std::optional<STAmount> deliveredAmount_;
    std::optional<UInt256> parentBatchID_;

    STArray nodes_;

    /**
     * Assemble metadata from its parts.
     *
     * Used by TxMetaBuilder, which vouches for the result and has already
     * put the nodes in order.
     *
     * @param transactionID The transaction's ID.
     * @param ledger The sequence of the ledger the transaction is in.
     * @param index The transaction's position within the ledger.
     * @param result The transaction's result; must be tes or tec.
     * @param deliveredAmount The amount delivered, if recorded.
     * @param parentBatchID The ID of the enclosing batch, if any.
     * @param nodes The affected nodes, sorted by key.
     */
    TxMeta(
        UInt256 const& transactionID,
        std::uint32_t ledger,
        std::uint32_t index,
        TER result,
        std::optional<STAmount> deliveredAmount,
        std::optional<UInt256> parentBatchID,
        STArray nodes);

public:
    /**
     * Construct from serialized metadata.
     *
     * @throws std::runtime_error if the result is neither tes nor tec.
     */
    TxMeta(UInt256 const& txID, std::uint32_t ledger, Slice);

    /**
     * Construct from a deserialized metadata object.
     *
     * @throws std::runtime_error if the result is neither tes nor tec.
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
        return TERtoInt(result_);
    }

    /**
     * The result code.
     */
    [[nodiscard]] TER
    getResultTER() const
    {
        return result_;
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
     * Return the metadata as a serializable object.
     */
    [[nodiscard]] STObject
    getAsObject() const;

    /**
     * The affected nodes, sorted by key.
     */
    [[nodiscard]] STArray const&
    getNodes() const
    {
        return nodes_;
    }

    [[nodiscard]] std::optional<STAmount> const&
    getDeliveredAmount() const
    {
        return deliveredAmount_;
    }
};

}  // namespace xrpl
