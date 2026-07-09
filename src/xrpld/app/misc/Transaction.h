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

#ifndef RIPPLE_APP_MISC_TRANSACTION_H_INCLUDED
#define RIPPLE_APP_MISC_TRANSACTION_H_INCLUDED

#include <xrpl/basics/RangeSet.h>
#include <xrpl/basics/enum_bitops.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>

#include <optional>
#include <variant>

namespace ripple {

//
// Transactions should be constructed in JSON with. Use STObject::parseJson to
// obtain a binary version.
//

class Application;
class Database;
class Rules;

enum TransStatus : std::int8_t {
    // clang-format off
    NEW         = 0,  // just received / generated
    INVALID     = 1,  // no valid signature, insufficient funds
    INCLUDED    = 2,  // added to the current ledger
    CONFLICTED  = 3,  // losing to a conflicting transaction
    COMMITTED   = 4,  // known to be in a ledger
    HELD        = 5,  // not valid now, maybe later
    REMOVED     = 6,  // taken out of a ledger
    OBSOLETE    = 7,  // a compatible transaction has taken precedence
    INCOMPLETE  = 8   // needs more signatures
    // clang-format on
};

enum class TxSearched { all, some, unknown };

enum class SubmitResult : std::uint8_t {
    // clang-format off
    none        = 0x00,
    queued      = 0x01,
    kept        = 0x02,
    broadcast   = 0x04,
    applied     = 0x08
    // clang-format on
};

/** Enables bitwise operators (&, |, ^, ~) for @ref sfmeta. */
template <>
struct enum_bitops::optin<SubmitResult> : std::true_type
{
};

// This class is for constructing and examining transactions.
// Transactions are static so manipulation functions are unnecessary.
class Transaction : public CountedObject<Transaction>
{
public:
    explicit Transaction(std::shared_ptr<STTx const> const&) noexcept;

    Transaction(Transaction const&) = delete;
    Transaction&
    operator=(Transaction const&) = delete;

    Transaction(Transaction&&) = delete;
    Transaction&
    operator=(Transaction&&) = delete;

    // The two boost::optional parameters are because SOCI requires
    // boost::optional (not std::optional) parameters.
    static std::shared_ptr<Transaction>
    transactionFromSQL(
        boost::optional<std::uint64_t> const& ledgerSeq,
        boost::optional<std::string> const& status,
        Blob const& rawTxn,
        Application& app);

    // The boost::optional parameter is because SOCI requires
    // boost::optional (not std::optional) parameters.
    static TransStatus
    sqlTransactionStatus(boost::optional<std::string> const& status);

    std::shared_ptr<STTx const> const&
    getSTransaction() const noexcept
    {
        return mTransaction;
    }

    uint256 const&
    getID() const noexcept
    {
        return mTransaction->getTransactionID();
    }

    LedgerIndex
    getLedger() const noexcept
    {
        return mLedgerIndex;
    }

    bool
    isValidated() const noexcept
    {
        return mLedgerIndex != 0;
    }

    TransStatus
    getStatus() const noexcept
    {
        return mStatus;
    }

    TER
    getResult() const noexcept
    {
        return mResult;
    }

    void
    setResult(TER terResult) noexcept
    {
        mResult = terResult;
    }

    void
    setStatus(
        TransStatus status,
        std::uint32_t ledgerSeq,
        std::optional<uint32_t> transactionSeq = std::nullopt,
        std::optional<uint16_t> networkID = std::nullopt);

    void
    setStatus(TransStatus status) noexcept
    {
        mStatus = status;
    }

    void
    setLedger(LedgerIndex ledger) noexcept
    {
        mLedgerIndex = ledger;
    }

    /**
     * Set this flag once added to a batch.
     */
    void
    setApplying() noexcept
    {
        mApplying = true;
    }

    /**
     * Detect if transaction is being batched.
     *
     * @return Whether transaction is being applied within a batch.
     */
    bool
    getApplying() noexcept
    {
        return mApplying;
    }

    /**
     * Indicate that transaction application has been attempted.
     */
    void
    clearApplying() noexcept
    {
        mApplying = false;
    }

    /**
     * @brief getSubmitResult Return submit result
     * @return SubmitResult struct
     */
    SubmitResult
    getSubmitResult() const noexcept
    {
        return submitResult_;
    }

    /**
     * @brief clearSubmitResult Clear all flags in SubmitResult
     */
    void
    clearSubmitResult() noexcept
    {
        submitResult_ = SubmitResult::none;
    }

    /**
     * @brief setApplied Set this flag once was applied to open ledger
     */
    void
    setApplied() noexcept
    {
        submitResult_ |= SubmitResult::applied;
    }

    /**
     * @brief setQueued Set this flag once was put into heldtxns queue
     */
    void
    setQueued() noexcept
    {
        submitResult_ |= SubmitResult::queued;
    }

    /**
     * @brief setBroadcast Set this flag once was broadcasted via network
     */
    void
    setBroadcast() noexcept
    {
        submitResult_ |= SubmitResult::broadcast;
    }

    /**
     * @brief setKept Set this flag once was put to localtxns queue
     */
    void
    setKept() noexcept
    {
        submitResult_ |= SubmitResult::kept;
    }

    struct CurrentLedgerState
    {
        CurrentLedgerState() noexcept = default;

        CurrentLedgerState(
            LedgerIndex li,
            XRPAmount fee,
            std::uint32_t accSeqNext,
            std::uint32_t accSeqAvail) noexcept
            : minFeeRequired{fee}
            , validatedLedger{li}
            , accountSeqNext{accSeqNext}
            , accountSeqAvail{accSeqAvail}
        {
        }

        XRPAmount minFeeRequired{};
        LedgerIndex validatedLedger = 0;
        std::uint32_t accountSeqNext = 0;
        std::uint32_t accountSeqAvail = 0;
    };

    /**
     * @brief getCurrentLedgerState Get current ledger state of transaction
     * @return Current ledger state
     */
    std::optional<CurrentLedgerState>
    getCurrentLedgerState() const noexcept
    {
        if (currentLedgerState_.validatedLedger == 0)
            return std::nullopt;

        return currentLedgerState_;
    }

    /**
     * @brief setCurrentLedgerState Set current ledger state of transaction
     * @param validatedLedger Number of last validated ledger
     * @param fee minimum Fee required for the transaction
     * @param accountSeq First valid account sequence in current ledger
     * @param availableSeq First available sequence for the transaction
     */
    void
    setCurrentLedgerState(
        LedgerIndex validatedLedger,
        XRPAmount fee,
        std::uint32_t accountSeq,
        std::uint32_t availableSeq)
    {
        currentLedgerState_.validatedLedger = validatedLedger;
        currentLedgerState_.minFeeRequired = fee;
        currentLedgerState_.accountSeqNext = accountSeq;
        currentLedgerState_.accountSeqAvail = availableSeq;
    }

    Json::Value
    getJson(JsonOptions options, Application& app, bool binary = false) const;

    // Information used to locate a transaction.
    // Contains a nodestore hash and ledger sequence pair if the transaction was
    // found. Otherwise, contains the range of ledgers present in the database
    // at the time of search.
    struct Locator
    {
        std::variant<std::pair<uint256, uint32_t>, ClosedInterval<uint32_t>>
            locator;

        // @return true if transaction was found, false otherwise
        //
        // Call this function first to determine the type of the contained info.
        // Calling the wrong getter function will throw an exception.
        // See documentation for the getter functions for more details
        bool
        isFound()
        {
            return std::holds_alternative<std::pair<uint256, uint32_t>>(
                locator);
        }

        // @return key used to find transaction in nodestore
        //
        // Throws if isFound() returns false
        uint256 const&
        getNodestoreHash()
        {
            return std::get<std::pair<uint256, uint32_t>>(locator).first;
        }

        // @return sequence of ledger containing the transaction
        //
        // Throws is isFound() returns false
        uint32_t
        getLedgerSequence()
        {
            return std::get<std::pair<uint256, uint32_t>>(locator).second;
        }

        // @return range of ledgers searched
        //
        // Throws if isFound() returns true
        ClosedInterval<uint32_t> const&
        getLedgerRangeSearched()
        {
            return std::get<ClosedInterval<uint32_t>>(locator);
        }
    };

    static Locator
    locate(uint256 const& id, Application& app);

    static std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearched>
    load(uint256 const& id, Application& app, error_code_i& ec);

    static std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearched>
    load(
        uint256 const& id,
        Application& app,
        ClosedInterval<uint32_t> const& range,
        error_code_i& ec);

private:
    static std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearched>
    load(
        uint256 const& id,
        Application& app,
        std::optional<ClosedInterval<uint32_t>> const& range,
        error_code_i& ec);

    std::shared_ptr<STTx const> mTransaction;

    LedgerIndex mLedgerIndex = 0;
    std::optional<uint32_t> mTxnSeq;
    std::optional<uint16_t> mNetworkID;
    CurrentLedgerState currentLedgerState_{};
    TER mResult = temUNCERTAIN;

    TransStatus mStatus = INVALID;
    bool mApplying = false;

    /** different ways for transaction to be accepted */
    SubmitResult submitResult_;
};

}  // namespace ripple

#endif
