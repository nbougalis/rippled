#include <xrpld/app/misc/detail/AccountTxPaging.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerPersistence.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/Transaction.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/contract.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxMeta.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace xrpl {

void
convertBlobsToTxResult(
    RelationalDatabase::AccountTxs& to,
    std::uint32_t ledgerIndex,
    std::string const& status,
    Slice rawTxn,
    Slice rawMeta,
    Application& app)
{
    auto const txn = std::make_shared<STTx const>(SerialIter{rawTxn});
    std::string reason;

    auto tr = std::make_shared<Transaction>(txn, reason, app);

    auto const metaset = std::make_shared<TxMeta>(tr->getID(), ledgerIndex, rawMeta);

    // The transaction index from the metadata is needed to generate the CTID
    if (metaset->getIndex() == std::numeric_limits<std::uint32_t>::max())
        logicError("convertBlobsToTxResult : TxMeta without a transaction index");

    tr->setStatus(
        Transaction::sqlTransactionStatus(status),
        ledgerIndex,
        metaset->getIndex(),
        app.getNetworkIDService().getNetworkID());

    to.emplace_back(std::move(tr), metaset);
}

void
saveLedgerAsync(Application& app, std::uint32_t seq)
{
    if (auto const l = app.getLedgerMaster().getLedgerBySeq(seq))
        pendSaveValidated(app, l, false, false);
}

}  // namespace xrpl
