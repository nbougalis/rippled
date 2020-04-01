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

#ifndef RIPPLE_PROTOCOL_INDEXES_H_INCLUDED
#define RIPPLE_PROTOCOL_INDEXES_H_INCLUDED

#include <ripple/protocol/Keylet.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/basics/base_uint.h>
#include <ripple/protocol/Book.h>
#include <cstdint>

namespace ripple {

uint256
getBookBase (Book const& book);

uint256
getOfferIndex (AccountID const& account, std::uint32_t uSequence);

uint256
getOwnerDirIndex (AccountID const& account);

uint256
getDirNodeIndex (uint256 const& uDirRoot, const std::uint64_t uNodeIndex);

uint256
getQualityIndex (uint256 const& uBase, const std::uint64_t uNodeDir = 0);

uint256
getQualityNext (uint256 const& uBase);

// VFALCO This name could be better
std::uint64_t
getQuality (uint256 const& uBase);

uint256
getTicketIndex (AccountID const& account, std::uint32_t uSequence);

uint256
getSignerListIndex (AccountID const& account);

uint256
getCheckIndex (AccountID const& account, std::uint32_t uSequence);

uint256
getDepositPreauthIndex (AccountID const& owner, AccountID const& preauthorized);

//------------------------------------------------------------------------------

/* VFALCO TODO
    For each of these operators that take just the uin256 and
    only attach the LedgerEntryType, we can comment out that
    operator to see what breaks, and those call sites are
    candidates for having the Keylet either passed in as a
    parameter, or having a data member that stores the keylet.
*/

/** Keylet computation funclets. */
namespace keylet {

/** AccountID root */
struct account_t
{
    explicit account_t() = default;

    Keylet operator()(AccountID const& id) const;
};
static account_t const account {};

/** The index of the amendment table */
Keylet const& amendments() noexcept;

/** Any item that can be in an owner dir. */
Keylet child (uint256 const& key);

/** The index of the "short" skip list

    The "short" skip list is a node (at a fixed index) that holds the hashes
    of ledgers since the last flag ledger. It will contain, at most, 256 hashes.
*/
Keylet const& skip() noexcept;

/** The index of "long" skip list for the given ledger.

    The "long" skip list is a node that holds the hashes of (up to) 256 flag
    ledgers.

    It can be used to efficiently skip back to any ledger using only two hops:
    the first hop gets the "long" skip list for the ledger it wants to retrieve
    and uses it to get the hash of the flag ledger whose short skip list will
    contain the hash of the requested ledger.
*/
Keylet skip(LedgerIndex ledger) noexcept;

/** The (fixed) index of the object containing the ledger fees. */
Keylet const& fees() noexcept;

/** The beginning of an order book */
struct book_t
{
    explicit book_t() = default;

    Keylet operator()(Book const& b) const;
};
static book_t const book {};

/** The index of a trust line for a given currency

    Note that a trustline is *shared* between two accounts (commonly referred
    to as the issuer and the holder); if Alice sets up a trust line to Bob for
    BTC, and Bob trusts Alice for BTC, here is only a single BTC trust line
    between them.
 * */
/** @{ */
Keylet line(
    AccountID const& id0,
    AccountID const& id1,
    Currency const& currency) noexcept;

inline
Keylet line(
    AccountID const& id,
    Issue const& issue) noexcept
{
    return line(id, issue.account, issue.currency);
}
/** @} */

/** An offer from an account */
struct offer_t
{
    explicit offer_t() = default;

    Keylet operator()(AccountID const& id,
        std::uint32_t seq) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltOFFER, key };
    }
};
static offer_t const offer {};

/** The initial directory page for a specific quality */
struct quality_t
{
    explicit quality_t() = default;

    Keylet operator()(Keylet const& k,
        std::uint64_t q) const;
};
static quality_t const quality {};

/** The directory for the next lower quality */
struct next_t
{
    explicit next_t() = default;

    Keylet operator()(Keylet const& k) const;
};
static next_t const next {};

/** A ticket belonging to an account */
struct ticket_t
{
    explicit ticket_t() = default;

    Keylet operator()(AccountID const& id,
        std::uint32_t seq) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltTICKET, key };
    }
};
static ticket_t const ticket {};

/** A SignerList */
struct signers_t
{
    explicit signers_t() = default;

    Keylet operator()(AccountID const& id) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltSIGNER_LIST, key };
    }
};
static signers_t const signers {};

/** A Check */
struct check_t
{
    explicit check_t() = default;

    Keylet operator()(AccountID const& id,
        std::uint32_t seq) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltCHECK, key };
    }
};
static check_t const check {};

/** A DepositPreauth */
struct depositPreauth_t
{
    explicit depositPreauth_t() = default;

    Keylet operator()(AccountID const& owner,
        AccountID const& preauthorized) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltDEPOSIT_PREAUTH, key };
    }
};
static depositPreauth_t const depositPreauth {};

//------------------------------------------------------------------------------

/** Any ledger entry */
Keylet unchecked(uint256 const& key);

/** The root page of an account's directory */
Keylet ownerDir (AccountID const& id);

/** A page in a directory */
/** @{ */
Keylet page (uint256 const& root, std::uint64_t index);
Keylet page (Keylet const& root, std::uint64_t index);
/** @} */

// DEPRECATED
inline
Keylet page (uint256 const& key)
{
    return { ltDIR_NODE, key };
}

/** An escrow entry */
Keylet
escrow (AccountID const& source, std::uint32_t seq);

/** A PaymentChannel */
Keylet
payChan (AccountID const& source, AccountID const& dst, std::uint32_t seq);

} // keylet

}

#endif
