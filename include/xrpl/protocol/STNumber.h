#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Number.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/Serializer.h>

#include <cstdint>
#include <ostream>
#include <string>

namespace xrpl {

/**
 * A serializable number.
 *
 * This type is-a `Number`, and can be used everywhere that is accepted.
 * This type simply integrates `Number` with the serialization framework,
 * letting it be used for fields in ledger entries and transactions.
 * It is effectively an `STAmount` sans `Asset`: it can represent a value
 * of any token type (XRP, IOU, or MPT) without paying the storage cost of
 * duplicating asset information that may be deduced from the context.
 *
 * STNumber derives from STTakesAsset, so that it can be associated with the
 * related Asset during transaction processing. Which asset is relevant depends
 * on the object and transaction. As of this writing, only Vault, LoanBroker,
 * and Loan objects use STNumber fields. All of those fields represent amounts
 * of the Vault's Asset, so they should be associated with the Vault's Asset.
 *
 * e.g.
 *     associateAsset(*loanSle, asset);
 *     associateAsset(*brokerSle, asset);
 *     associateAsset(*vaultSle, asset);
 */
class STNumber final : public STTakesAsset<STI_NUMBER, STNumber>, public CountedObject<STNumber>
{
    Number value_;

public:
    using value_type = Number;

    STNumber() = default;
    explicit STNumber(SField const& field, Number const& value = Number());
    STNumber(SerialIter& sit, SField const& field);

    [[nodiscard]] std::string
    getText() const override;
    void
    add(Serializer& s) const override;

    [[nodiscard]] Number const&
    value() const;
    void
    setValue(Number const& v);

    STNumber&
    operator=(Number const& rhs)
    {
        setValue(rhs);
        return *this;
    }

    [[nodiscard]] bool
    isDefault() const override;

    /**
     * Associate an asset, rounding the value to the precision of the asset.
     */
    void
    associateAsset(Asset const& a) override;

    operator Number() const
    {
        return value_;
    }

    friend bool
    operator==(STNumber const& lhs, STNumber const& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }
};

std::ostream&
operator<<(std::ostream& out, STNumber const& rhs);

struct NumberParts
{
    std::uint64_t mantissa = 0;
    int exponent = 0;
    bool negative = false;
};

NumberParts
partsFromString(std::string const& number);

STNumber
numberFromJson(SField const& field, json::Value const& value);

}  // namespace xrpl
