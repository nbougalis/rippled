//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpl/protocol/LedgerHeader.h>

namespace ripple {

void
addRaw(LedgerHeader const& info, Serializer& s, bool includeHash)
{
    s.add32(info.seq);
    s.add64(info.drops.drops());
    s.addBitString(info.parentHash);
    s.addBitString(info.txHash);
    s.addBitString(info.accountHash);
    s.add32(info.parentCloseTime.time_since_epoch().count());
    s.add32(info.closeTime.time_since_epoch().count());
    s.add8(info.closeTimeResolution.count());
    s.add8(info.closeFlags);

    if (includeHash)
        s.addBitString(info.hash);
}

static std::optional<LedgerHeader>
deserializeHeaderImpl(SerialIter sit, bool hasHash)
{
    std::optional<LedgerHeader> header;

    try
    {
        header.emplace();

        header->seq = sit.get32();
        header->drops = sit.get64();
        header->parentHash = sit.get256();
        header->txHash = sit.get256();
        header->accountHash = sit.get256();
        header->parentCloseTime =
            NetClock::time_point{NetClock::duration{sit.get32()}};
        header->closeTime =
            NetClock::time_point{NetClock::duration{sit.get32()}};
        header->closeTimeResolution = NetClock::duration{sit.get8()};
        header->closeFlags = sit.get8();

        if (hasHash)
            header->hash = sit.get256();
    }
    catch (...)
    {
    }

    return header;
}

std::optional<LedgerHeader>
deserializeHeader(Slice data, bool hasHash)
{
    return deserializeHeaderImpl(SerialIter{data}, hasHash);
}

std::optional<LedgerHeader>
deserializePrefixedHeader(Slice data, bool hasHash)
{
    try
    {
        SerialIter sit{data};

        // Previous versions of this code consumed the prefix but did not
        // verify (or even look at) the value. We check to make sure that
        // it contains the value we expect, and if it does not, we refuse
        // to deserialize any data remaining in the buffer.
        auto const prefix = safe_cast<HashPrefix>(sit.get32());

        if (prefix == HashPrefix::ledgerMaster)
            return deserializeHeaderImpl(sit, hasHash);

        Throw<std::runtime_error>(
            "Wrong prefix: " +
            std::to_string(static_cast<std::uint32_t>(prefix)));
    }
    catch (std::exception const& ex)
    {
        return std::nullopt;
    }
}

}  // namespace ripple
