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

#include <ripple/protocol/digest.h>
#include <ripple/protocol/Indexes.h>
#include <boost/endian/conversion.hpp>
#include <algorithm>
#include <cassert>

namespace ripple {

uint256
getBookBase (Book const& book)
{
    assert (isConsistent (book));
    // Return with quality 0.
    return getQualityIndex(sha512Half(
        std::uint16_t(spaceBookDir),
        book.in.currency,
        book.out.currency,
        book.in.account,
        book.out.account));
}

uint256
getOfferIndex (AccountID const& account, std::uint32_t uSequence)
{
    return sha512Half(
        std::uint16_t(spaceOffer),
        account,
        std::uint32_t(uSequence));
}

uint256
getOwnerDirIndex (AccountID const& account)
{
    return sha512Half(
        std::uint16_t(spaceOwnerDir),
        account);
}


uint256
getDirNodeIndex (uint256 const& uDirRoot, const std::uint64_t uNodeIndex)
{
    if (uNodeIndex == 0)
        return uDirRoot;

    return sha512Half(
        std::uint16_t(spaceDirNode),
        uDirRoot,
        std::uint64_t(uNodeIndex));
}

uint256
getQualityIndex (uint256 const& uBase, const std::uint64_t uNodeDir)
{
    // Indexes are stored in big endian format: they print as hex as stored.
    // Most significant bytes are first.  Least significant bytes represent
    // adjacent entries.  We place uNodeDir in the 8 right most bytes to be
    // adjacent.  Want uNodeDir in big endian format so ++ goes to the next
    // entry for indexes.
    uint256 uNode (uBase);

    // TODO(tom): there must be a better way.
    // VFALCO [base_uint] This assumes a certain storage format
    ((std::uint64_t*) uNode.end ())[-1] = boost::endian::native_to_big (uNodeDir);

    return uNode;
}

uint256
getQualityNext (uint256 const& uBase)
{
    static uint256 const uNext (
        from_hex_text<uint256>("10000000000000000"));
    return uBase + uNext;
}

std::uint64_t
getQuality (uint256 const& uBase)
{
    // VFALCO [base_uint] This assumes a certain storage format
    return boost::endian::big_to_native (((std::uint64_t*) uBase.end ())[-1]);
}

uint256
getTicketIndex (AccountID const& account, std::uint32_t uSequence)
{
    return sha512Half(
        std::uint16_t(spaceTicket),
        account,
        std::uint32_t(uSequence));
}

uint256
getCheckIndex (AccountID const& account, std::uint32_t uSequence)
{
    return sha512Half(
        std::uint16_t(spaceCheck),
        account,
        std::uint32_t(uSequence));
}

uint256
getDepositPreauthIndex (AccountID const& owner, AccountID const& preauthorized)
{
    return sha512Half(
        std::uint16_t(spaceDepositPreauth),
        owner,
        preauthorized);
}

//------------------------------------------------------------------------------

namespace keylet {

Keylet account_t::operator()(
    AccountID const& id) const
{
    return { ltACCOUNT_ROOT, sha512Half(std::uint16_t(spaceAccount), id) };
}

Keylet child (uint256 const& key)
{
    return { ltCHILD, key };
}

Keylet const& skip() noexcept
{
    static Keylet const ret { ltLEDGER_HASHES,
        sha512Half(std::uint16_t(spaceSkipList)) };
    return ret;
}

Keylet skip(LedgerIndex ledger) noexcept
{
    return { ltLEDGER_HASHES,
        sha512Half(
            std::uint16_t(spaceSkipList),
            std::uint32_t(static_cast<std::uint32_t>(ledger) >> 16)) };
}

Keylet const& amendments() noexcept
{
    static Keylet const ret { ltAMENDMENTS,
        sha512Half(std::uint16_t(spaceAmendment)) };
    return ret;
}

Keylet const& fees() noexcept
{
    static Keylet const ret { ltFEE_SETTINGS,
        sha512Half(std::uint16_t(spaceFee)) };
    return ret;
}

Keylet book_t::operator()(Book const& b) const
{
    return { ltDIR_NODE,
        getBookBase(b) };
}

Keylet line(
    AccountID const& id0,
    AccountID const& id1,
    Currency const& currency) noexcept
{
    // A trust line is shared between two accounts; while we typically think
    // of this as an "issuer" and a "holder" the relationship is actually fully
    // bidirectional.
    //
    // We define a "canonical" order for the two accounts by sorting them, from
    // smallest to largest and hashing them in that sorted order:
    auto const accounts = std::minmax(id0, id1);

    return { ltRIPPLE_STATE,
        sha512Half(std::uint16_t(spaceRipple),
            accounts.first, accounts.second, currency) };
}

Keylet offer_t::operator()(AccountID const& id,
    std::uint32_t seq) const
{
    return { ltOFFER,
        getOfferIndex(id, seq) };
}

Keylet quality_t::operator()(Keylet const& k,
    std::uint64_t q) const
{
    assert(k.type == ltDIR_NODE);
    return { ltDIR_NODE,
        getQualityIndex(k.key, q) };
}

Keylet next_t::operator()(Keylet const& k) const
{
    assert(k.type == ltDIR_NODE);
    return { ltDIR_NODE,
        getQualityNext(k.key) };
}

Keylet ticket_t::operator()(AccountID const& id,
    std::uint32_t seq) const
{
    return { ltTICKET,
        getTicketIndex(id, seq) };
}

// This function is presently static, since it's never accessed from anywhere
// else. If we ever support multiple pages of signer lists, this would be the
// keylet used to locate them.
static
Keylet signers(
    AccountID const& account,
    std::uint32_t page) noexcept
{
    return { ltSIGNER_LIST,
        sha512Half(std::uint16_t(spaceSignerList), account, page) };
}

Keylet signers(
    AccountID const& account) noexcept
{
    return signers (account, 0);
}

Keylet check_t::operator()(AccountID const& id,
    std::uint32_t seq) const
{
    return { ltCHECK,
        getCheckIndex(id, seq) };
}

Keylet depositPreauth_t::operator()(AccountID const& owner,
    AccountID const& preauthorized) const
{
    return { ltDEPOSIT_PREAUTH,
        getDepositPreauthIndex(owner, preauthorized) };
}

//------------------------------------------------------------------------------

Keylet unchecked (uint256 const& key)
{
    return { ltANY, key };
}

Keylet ownerDir(AccountID const& id)
{
    return { ltDIR_NODE,
        getOwnerDirIndex(id) };
}

Keylet page(uint256 const& key,
    std::uint64_t index)
{
    return { ltDIR_NODE,
        getDirNodeIndex(key, index) };
}

Keylet page(Keylet const& root,
    std::uint64_t index)
{
    assert(root.type == ltDIR_NODE);
    return page(root.key, index);
}

Keylet
escrow (AccountID const& source, std::uint32_t seq)
{
    sha512_half_hasher h;
    using beast::hash_append;
    hash_append(h, std::uint16_t(spaceEscrow));
    hash_append(h, source);
    hash_append(h, seq);
    return { ltESCROW, static_cast<uint256>(h) };
}

Keylet
payChan (AccountID const& source, AccountID const& dst, std::uint32_t seq)
{
    sha512_half_hasher h;
    using beast::hash_append;
    hash_append(h, std::uint16_t(spaceXRPUChannel));
    hash_append(h, source);
    hash_append(h, dst);
    hash_append(h, seq);
    return { ltPAYCHAN, static_cast<uint256>(h) };
}

} // keylet

} // ripple
