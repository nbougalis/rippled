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

#ifndef RIPPLE_PROTOCOL_STCURRENCY_H_INCLUDED
#define RIPPLE_PROTOCOL_STCURRENCY_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/contract.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

class STCurrency final : public STTypedBase<STI_CURRENCY, STCurrency>,
                         public CountedObject<STCurrency>
{
    Currency currency_{};

public:
    using value_type = Currency;

    STCurrency() = default;

    explicit STCurrency(SField const& name, Currency const& currency)
        : STTypedBase(name), currency_(currency)
    {
    }

    explicit STCurrency(SerialIter& sit, SField const& name)
        : STTypedBase(name), currency_(sit.get160())
    {
    }

    explicit STCurrency(SField const& name) : STTypedBase(name)
    {
    }

    [[nodiscard]] Currency const&
    value() const noexcept
    {
        return currency_;
    }

    [[nodiscard]] std::string
    getText() const override
    {
        return to_string(currency_);
    }

    void
    add(Serializer& s) const override
    {
        s.addBitString(currency_);
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return isXRP(currency_);
    }

private:
    static std::unique_ptr<STCurrency>
    construct(SerialIter& sit, SField const& name)
    {
        return std::make_unique<STCurrency>(sit, name);
    }

    friend bool
    operator==(STCurrency const& lhs, STCurrency const& rhs) noexcept
    {
        return lhs.currency_ == rhs.currency_;
    }

    friend auto
    operator<=>(STCurrency const& lhs, STCurrency const& rhs) noexcept
    {
        return lhs.currency_ <=> rhs.currency_;
    }

    friend bool
    operator==(STCurrency const& lhs, Currency const& rhs) noexcept
    {
        return lhs.currency_ == rhs;
    }

    friend auto
    operator<=>(STCurrency const& lhs, Currency const& rhs) noexcept
    {
        return lhs.currency_ <=> rhs;
    }

    friend class detail::STVar;
};

inline STCurrency
currencyFromJson(SField const& name, Json::Value const& v)
{
    if (!v.isString())
    {
        Throw<std::runtime_error>(
            "currencyFromJson currency must be a string Json value");
    }

    auto const currency = to_currency(v.asString());
    if (currency == badCurrency() || currency == noCurrency())
    {
        Throw<std::runtime_error>(
            "currencyFromJson currency must be a valid currency");
    }

    return STCurrency{name, currency};
}
}  // namespace ripple

#endif
