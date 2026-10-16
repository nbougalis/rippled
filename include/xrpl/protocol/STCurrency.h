#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace xrpl {

class STCurrency final : public STTypedBase<STI_CURRENCY, STCurrency>,
                         public CountedObject<STCurrency>
{
    Currency currency_;

public:
    using value_type = Currency;

    STCurrency() = default;

    explicit STCurrency(SField const& name, Currency const& currency)
        : STTypedBase(name), currency_(currency)
    {
    }

    explicit STCurrency(SerialIter& sit, SField const& name)
        : STCurrency(name, Currency{sit.get160()})
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
currencyFromJson(SField const& name, json::Value const& v)
{
    if (!v.isString())
    {
        Throw<std::runtime_error>("currencyFromJson currency must be a string Json value");
    }

    auto const currency = toCurrency(v.asString());
    if (currency == badCurrency() || currency == noCurrency())
    {
        Throw<std::runtime_error>("currencyFromJson currency must be a valid currency");
    }

    return STCurrency{name, currency};
}

}  // namespace xrpl
