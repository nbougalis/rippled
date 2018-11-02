//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/DeleteAccount.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/ledger/View.h>

namespace ripple {

std::uint64_t
DeleteAccount::calculateBaseFee (
    ReadView const& view,
    STTx const& tx)
{
    return 3 * Transactor::calculateBaseFee (view, tx);
}

NotTEC
DeleteAccount::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureDeletableAccounts))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    auto const ret = preflight1 (ctx);

    if (!isTesSuccess (ret))
        return ret;

    return preflight2 (ctx);
}

TER
DeleteAccount::preclaim (PreclaimContext const& ctx)
{
    auto dst = ctx.view.read(keylet::account(ctx.tx[sfDestination]));

    if (!dst)
        return tecNO_DST;

    if ((*dst)[sfFlags] & lsfRequireDestTag && !ctx.tx[~sfDestinationTag])
        return tecDST_TAG_NEEDED;

    auto src = ctx.view.read(keylet::account(ctx.tx[sfAccount]));
    assert(src);

    // We don't want to allow an account to be deleted as long as its sequence
    // number is ahead of the current ledger since we use the ledger sequence
    // as the account's initial sequence number, to avoid sequence number reuse.
    if (ctx.tx[sfSequence] + 1 >= ctx.view.seq())
        return tecTOO_SOON;

    if ((*src)[sfOwnerCount] != 0)
        return tecOWNERS;

    return tesSUCCESS;
}

TER
DeleteAccount::doApply ()
{
    auto src = view().peek(keylet::account(ctx_.tx[sfAccount]));
    assert(src);

    auto dst = view().peek(keylet::account(ctx_.tx[sfDestination]));
    assert(dst);

    // Transfer any XRP remaining after the fee is paid to the destination:
    (*dst)[sfBalance] = (*dst)[sfBalance] + mSourceBalance;
    (*src)[sfBalance] = (*src)[sfBalance] - mSourceBalance;

    assert ((*src)[sfBalance] == XRPAmount(0));

    // If there's still an owner directory associated with the source account
    // delete it.
    {
        auto const od = keylet::ownerDir(ctx_.tx[sfAccount]);

        if (ctx_.view().exists(od) && !ctx_.view().dirDelete(od))
            return tecDIR_NOT_EMPTY;
    }

    // Re-arm the password change fee if we can and need to.
    if (dst->isFlag(lsfPasswordSpent))
        dst->clearFlag(lsfPasswordSpent);

    ctx_.view().update(dst);
    ctx_.view().erase(src);

    return tesSUCCESS;
}

}
