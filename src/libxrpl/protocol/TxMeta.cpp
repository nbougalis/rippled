#include <xrpl/protocol/TxMeta.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <boost/container/flat_set.hpp>

#include <cstdint>
#include <flat_set>
#include <optional>
#include <stdexcept>
#include <utility>

namespace xrpl {

TxMeta::TxMeta(
    UInt256 const& transactionID,
    std::uint32_t ledger,
    std::uint32_t index,
    TER result,
    std::optional<STAmount> deliveredAmount,
    std::optional<UInt256> parentBatchID,
    STArray nodes)
    : transactionID_(transactionID)
    , ledgerSeq_(ledger)
    , index_(index)
    , result_(result)
    , deliveredAmount_(std::move(deliveredAmount))
    , parentBatchID_(parentBatchID)
    , nodes_(std::move(nodes))
{
}

TxMeta::TxMeta(UInt256 const& txid, std::uint32_t ledger, STObject const& obj)
    : TxMeta(
          txid,
          ledger,
          obj.getFieldU32(sfTransactionIndex),
          TER::fromInt(obj.getFieldU8(sfTransactionResult)),
          obj[~sfDeliveredAmount],
          obj[~sfParentBatchID],
          obj.getFieldArray(sfAffectedNodes))
{
    if (!isTesSuccess(result_) && !isTecClaim(result_))
        Throw<std::runtime_error>("TxMeta::TxMeta : invalid TER result");
}

TxMeta::TxMeta(UInt256 const& txid, std::uint32_t ledger, Slice data)
    : TxMeta(txid, ledger, STObject{SerialIter{data}, sfMetadata})
{
}

std::flat_set<AccountID>
TxMeta::getAffectedAccounts() const
{
    std::flat_set<AccountID> list;

    // This code should match the behavior of the JS method:
    // Meta#getAffectedAccounts
    for (auto const& node : nodes_)
    {
        int const index =
            node.getFieldIndex(node.getFName() == sfCreatedNode ? sfNewFields : sfFinalFields);

        if (index != -1)
        {
            auto const* inner = dynamic_cast<STObject const*>(&node.peekAtIndex(index));
            XRPL_ASSERT(inner, "xrpl::getAffectedAccounts : STObject type cast succeeded");
            if (inner != nullptr)
            {
                for (auto const& field : *inner)
                {
                    if (auto sa = dynamic_cast<STAccount const*>(&field))
                    {
                        XRPL_ASSERT(!sa->isDefault(), "xrpl::getAffectedAccounts : account is set");
                        if (!sa->isDefault())
                            list.insert(sa->value());
                    }
                    else if (
                        field.getFName() == sfLowLimit || field.getFName() == sfHighLimit ||
                        field.getFName() == sfTakerPays || field.getFName() == sfTakerGets)
                    {
                        auto lim = dynamic_cast<STAmount const*>(&field);
                        XRPL_ASSERT(
                            lim,
                            "xrpl::getAffectedAccounts : STAmount type cast "
                            "succeeded");

                        if (lim != nullptr)
                        {
                            if (auto const issuer = lim->getIssuer(); issuer.isNonZero())
                                list.insert(issuer);
                        }
                    }
                    else if (field.getFName() == sfMPTokenIssuanceID)
                    {
                        if (auto mptID = dynamic_cast<STUInt192 const*>(&field))
                        {
                            if (auto const issuer = MPTIssue(mptID->value()).getIssuer();
                                issuer.isNonZero())
                                list.insert(issuer);
                        }
                    }
                }
            }
        }
    }

    return list;
}

std::flat_set<MPTID>
TxMeta::getAffectedMPTs() const
{
    std::flat_set<MPTID> list;

    for (auto const& it : nodes_)
    {
        int const index =
            it.getFieldIndex(it.getFName() == sfCreatedNode ? sfNewFields : sfFinalFields);

        if (index != -1)
        {
            auto inner = dynamic_cast<STObject const*>(&it.peekAtIndex(index));
            XRPL_ASSERT(inner, "xrpl::getAffectedMPTs : STObject type cast succeeded");
            if (inner != nullptr)
            {
                // An MPTokenIssuance entry does not store its own issuance id;
                // the id is derived from the issuer and the sequence that
                // created the issuance.
                if (it.getFieldU16(sfLedgerEntryType) == ltMPTOKEN_ISSUANCE)
                {
                    list.insert(
                        makeMptID(inner->getFieldU32(sfSequence), inner->getAccountID(sfIssuer)));
                }

                for (auto const& field : *inner)
                {
                    if (auto mptID = dynamic_cast<STUInt192 const*>(&field);
                        field.getFName() == sfMPTokenIssuanceID && mptID != nullptr)
                    {
                        list.insert(mptID->value());
                    }
                    else if (
                        auto amount = dynamic_cast<STAmount const*>(&field);
                        amount != nullptr && amount->holds<MPTIssue>())
                    {
                        list.insert(amount->get<MPTIssue>().getMptID());
                    }
                }
            }
        }
    }

    return list;
}

STObject
TxMeta::getAsObject() const
{
    return STObject(sfTransactionMetaData, [&](STObject& md) {
        md.setFieldU8(sfTransactionResult, checkedCast<std::uint8_t>(TERtoInt(result_)));
        md.setFieldU32(sfTransactionIndex, index_);
        md.emplaceBack(nodes_);

        if (deliveredAmount_.has_value())
            md.setFieldAmount(sfDeliveredAmount, *deliveredAmount_);

        if (parentBatchID_.has_value())
            md.setFieldH256(sfParentBatchID, *parentBatchID_);
    });
}

}  // namespace xrpl
