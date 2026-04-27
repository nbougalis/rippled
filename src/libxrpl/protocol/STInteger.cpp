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

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <charconv>

namespace ripple {

namespace detail {

std::string
specializedText(SField const& field, std::uint16_t value, std::string def)
{
    if (field == sfTransactionResult)
    {
        if (auto ret = transHuman(TER::fromInt(value)); !ret.empty())
            def = std::move(ret);
    }
    else if (field == sfLedgerEntryType)
    {
        if (auto item = LedgerFormats::getInstance().findByType(
                safe_cast<LedgerEntryType>(value)))
            def = item->getName();
    }
    else if (field == sfTransactionType)
    {
        if (auto item =
                TxFormats::getInstance().findByType(safe_cast<TxType>(value)))
            def = item->getName();
    }

    return def;
}

Json::Value
specializedJson(SField const& field, std::uint16_t value)
{
    if (field == sfTransactionResult)
        return transToken(TER::fromInt(value), std::to_string(value));

    if (field == sfLedgerEntryType)
    {
        if (auto item = LedgerFormats::getInstance().findByType(
                safe_cast<LedgerEntryType>(value)))
            return item->getName();
    }
    else if (field == sfTransactionType)
    {
        if (auto item =
                TxFormats::getInstance().findByType(safe_cast<TxType>(value)))
            return item->getName();
    }

    return static_cast<Json::UInt>(value);
}

}

}  // namespace ripple
