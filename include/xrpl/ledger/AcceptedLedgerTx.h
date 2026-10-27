#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxMeta.h>

#include <cstdint>
#include <flat_set>
#include <memory>
#include <utility>

namespace xrpl {

/**
 * A transaction in a closed ledger, with its metadata.
 *
 * Holds the transaction and metadata as deserialized from the ledger,
 * along with the set of accounts the transaction affected, which is
 * computed once here because both the SQL indexer and the subscription
 * publisher need it.
 */
class AcceptedLedgerTx : public CountedObject<AcceptedLedgerTx>
{
public:
    AcceptedLedgerTx(
        std::uint32_t ledgerSeq,
        std::shared_ptr<STTx const> txn,
        std::shared_ptr<STObject const> meta)
        : txn_(std::move(txn))
        , rawMeta_(std::move(meta))
        , meta_(txn_->getTransactionID(), ledgerSeq, *rawMeta_)
        , affected_(meta_.getAffectedAccounts())
    {
    }

    [[nodiscard]] std::shared_ptr<STTx const> const&
    getTxn() const
    {
        return txn_;
    }

    /**
     * The metadata as deserialized from the ledger.
     */
    [[nodiscard]] std::shared_ptr<STObject const> const&
    getRawMeta() const
    {
        return rawMeta_;
    }

    [[nodiscard]] TxMeta const&
    getMeta() const
    {
        return meta_;
    }

    [[nodiscard]] std::flat_set<AccountID> const&
    getAffected() const
    {
        return affected_;
    }

    [[nodiscard]] TxID
    getTransactionID() const
    {
        return txn_->getTransactionID();
    }

    [[nodiscard]] TxType
    getTxnType() const
    {
        return txn_->getTxnType();
    }

    [[nodiscard]] TER
    getResult() const
    {
        return meta_.getResultTER();
    }

    [[nodiscard]] std::uint32_t
    getTxnSeq() const
    {
        return meta_.getIndex();
    }

private:
    std::shared_ptr<STTx const> txn_;
    std::shared_ptr<STObject const> rawMeta_;
    TxMeta meta_;
    std::flat_set<AccountID> affected_;
};

}  // namespace xrpl
