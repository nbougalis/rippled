#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

#include <boost/endian/conversion.hpp>

#include <bit>
#include <compare>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace xrpl {

/**
 * A serialized issue: identifies which token a value is denominated in.
 *
 * `STIssue` is a serialized type that names an issued token. As of now,
 * the protocol supports three kinds of issues:
 *
 * - **XRP**: The native token.
 * - **IOU**: An issued asset.
 * - **MPT**: A Multi-Purpose Token asset.
 *
 * For reasons that are unclear, the wire format of STIssue is a very
 * weird, wasteful and ambiguous mess:
 *
 *     XRP:  [currency: all zeroes]
 *     IOU:  [currency: non-zero identifier][account: issuer]
 *     MPT:  [account: issuer][account: special sentinel][uint32: sequence]
 *
 * Parsing Logic:
 *     - The first 20 bytes are always read first. If they decode as the
 *       XRP currency, parsing is done.
 *     - Otherwise another 20 bytes are read.
 *         - If those equal a special sentinel value, the first 20 bytes
 *           are the MPT issuer's account and an additional 4 bytes must
 *           be read to retrieve the MPT sequence.
 *         - Otherwise, the first 20 bytes are the IOU currency code and
 *           the next 20 bytes are the issuer's account.
 *
 * Why this mess instead of a tagged format, which could have represented
 * XRP in a single byte and 3 letter ISO-like IOUs with just 4 bytes?
 *
 * @see Asset, Issue, MPTIssue, MPTID, makeMptID
 */
class STIssue final : public STTypedBase<STI_ISSUE, STIssue>, public CountedObject<STIssue>
{
    Asset asset_{xrpIssue()};

public:
    using value_type = Asset;

    STIssue() = default;
    STIssue(STIssue const& rhs) = default;

    explicit STIssue(SerialIter& sit, SField const& name) : STTypedBase(name)
    {
        // Either the currency of an Issue or the issuer of an MPT; the
        // following 160 bits disambiguate.
        auto const currencyOrAccount = sit.get160();

        if (isXRP(Currency{currencyOrAccount}))
        {
            asset_ = xrpIssue();
            return;
        }

        AccountID const account{sit.get160()};

        // If the account is the "noAccount" sentinel, we are dealing with
        // an MPT. Otherwise, we are dealing with a normal IOU.
        if (noAccount() == account)
        {
            // MPT: issuer (160), noAccount() (160), sequence (32). The sequence
            // is stored in little-endian and get32() reads big-endian, so
            // reverse it before building the canonical ID.
            asset_ = MPTIssue{std::byteswap(sit.get32()), AccountID{currencyOrAccount}};
            return;
        }

        Issue const issue{Currency{currencyOrAccount}, account};

        if (!isConsistent(issue))
            Throw<std::runtime_error>("invalid issue: currency and account native mismatch");

        asset_ = issue;
    }

    template <AssetType A>
    explicit STIssue(SField const& name, A const& asset) : STTypedBase(name), asset_{asset}
    {
        if (holds<Issue>() && !isConsistent(asset_.get<Issue>()))
            Throw<std::runtime_error>("Invalid asset: currency and account native mismatch");
    }

    explicit STIssue(SField const& name) : STTypedBase(name)
    {
    }

    STIssue&
    operator=(STIssue const& rhs) = default;

    template <ValidIssueType TIss>
    [[nodiscard]] bool
    holds() const
    {
        return asset_.holds<TIss>();
    }

    template <ValidIssueType TIss>
    [[nodiscard]] TIss const&
    get() const
    {
        if (!holds<TIss>())
            Throw<std::runtime_error>("Asset doesn't hold the requested issue");

        return asset_.get<TIss>();
    }

    [[nodiscard]] value_type const&
    value() const noexcept
    {
        return asset_;
    }

    void
    setValue(Asset const& asset)
    {
        if (holds<Issue>() && !isConsistent(asset_.get<Issue>()))
            Throw<std::runtime_error>("Invalid asset: currency and account native mismatch");

        asset_ = asset;
    }

    [[nodiscard]] std::string
    getText() const override
    {
        return asset_.getText();
    }

    [[nodiscard]] json::Value
    getJson(JsonOptions) const override
    {
        json::Value jv;
        asset_.setJson(jv);
        return jv;
    }

    void
    add(Serializer& s) const override
    {
        asset_.visit(
            [&](Issue const& issue) {
                s.addBitString(issue.currency);

                if (!isXRP(issue.currency))
                    s.addBitString(issue.account);
            },
            [&](MPTIssue const& issue) {
                // Although `MPTID` is, internally, a 192-bit `BaseUInt` and
                // could have reasonably been serialized that way, we instead
                // split it in two pieces, serializing each one separately. A
                // sentinel between them distinguishes MPTs from IOUs.
                s.addBitString(issue.getIssuer());
                s.addBitString(noAccount());

                // The MPTID bytes are canonical big-endian. Interpret those bytes
                // as the legacy LE-host value so add32() writes the preserved
                // STIssue wire bytes on every host endian.
                std::uint32_t sequence = 0;
                std::memcpy(&sequence, issue.getMptID().data(), sizeof(sequence));
                s.add32(boost::endian::little_to_native(sequence));
            });
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return asset_.visit(
            [](Issue const& issue) { return issue == xrpIssue(); },
            [](MPTIssue const&) { return false; });
    }

    friend constexpr bool
    operator==(STIssue const& lhs, STIssue const& rhs) noexcept
    {
        return lhs.asset_ == rhs.asset_;
    }

    friend constexpr std::weak_ordering
    operator<=>(STIssue const& lhs, STIssue const& rhs) noexcept
    {
        return lhs.asset_ <=> rhs.asset_;
    }

    friend constexpr bool
    operator==(STIssue const& lhs, Asset const& rhs) noexcept
    {
        return lhs.asset_ == rhs;
    }

    friend constexpr std::weak_ordering
    operator<=>(STIssue const& lhs, Asset const& rhs) noexcept
    {
        return lhs.asset_ <=> rhs;
    }

    friend class detail::STVar;
};

inline STIssue
issueFromJson(SField const& name, json::Value const& v)
{
    return STIssue{name, assetFromJson(v)};
}

}  // namespace xrpl
