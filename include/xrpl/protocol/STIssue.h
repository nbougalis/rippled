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

#ifndef RIPPLE_PROTOCOL_STISSUE_H_INCLUDED
#define RIPPLE_PROTOCOL_STISSUE_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

namespace ripple {

/** A serialized issue: identifies which token a value is denominated in.

    `STIssue` is a serialized type that names an issued token. As of now,
    the protocol supports three kinds of issues:

    - **XAH**: The native token.
    - **IOU**: An issued asset.
    - **MPT**: A Multi-Purpose Token asset.

    For reasons that are unclear, the wire format of STIssue is a very
    weird, wasteful and ambiguous mess:

        XAH:  [currency: all zeroes]
        IOU:  [currency: non-zero identifier][account: issuer]
        MPT:  [account: issuer][account: special sentinel][uint32: sequence]

    Parsing Logic:
        - The first 20 bytes are always read first. If they decode as the
          XRP currency, parsing is done.
        - Otherwise another 20 bytes are read.
            - If those equal a special sentinel value, the first 20 bytes
              are the MPT issuer's account and an additional 4 bytes must
              be read to retrieve the MPT sequence.
            - Otherwise, the first 20 bytes are the IOU currency code and
              the next 20 bytes are the issuer's account.

    Why this mess instead of a tagged format, which could have represented
    XRP/XAH in a single byte and 3 letter ISO-like IOUs with just 4 bytes?

    @see Asset, Issue, MPTIssue, MPTID, makeMptID
*/
class STIssue final : public STTypedBase<STI_ISSUE, STIssue>,
                      public CountedObject<STIssue>
{
    static_assert(
        std::endian::native == std::endian::little,
        "This code has not fully tested on big-endian platforms and "
        "may have endianess issues. See STIssue::add");

    Asset asset_{xrpIssue()};

public:
    using value_type = Asset;

    STIssue() = default;

    explicit STIssue(SerialIter& sit, SField const& name) : STTypedBase(name)
    {
        auto const currencyOrAccount = sit.get160();

        if (isXRP(static_cast<Currency>(currencyOrAccount)))
        {
            asset_ = xrpIssue();
            return;
        }

        auto const account = static_cast<AccountID>(sit.get160());

        // If the account is the "noAccount" sentinel, we are dealing with
        // an MPT. Otherwise, we are dealing with a normal IOU.

        if (account != noAccount())
        {
            Issue const issue{
                static_cast<Currency>(currencyOrAccount), account};

            if (!isConsistent(issue))
                Throw<std::runtime_error>(
                    "invalid issue: currency and account native mismatch");

            asset_ = issue;
            return;
        }

        // This is an MPT: a 32-bit sequence follows. See STIssue::add for
        // details and an important discussion about the byte-order issues
        // with this code.
        auto const sequence = sit.get32();

        MPTID mptID;

        static_assert(
            sizeof(mptID) == sizeof(sequence) + sizeof(currencyOrAccount));

        std::memcpy(mptID.data(), &sequence, sizeof(sequence));
        std::memcpy(
            mptID.data() + sizeof(sequence),
            currencyOrAccount.data(),
            sizeof(currencyOrAccount));

        asset_ = MPTIssue{mptID};
    }

    template <AssetType A>
    explicit STIssue(SField const& name, A const& asset)
        : STTypedBase(name), asset_{asset}
    {
        if (holds<Issue>() && !isConsistent(asset_.get<Issue>()))
            Throw<std::runtime_error>(
                "Invalid asset: currency and account native mismatch");
    }

    explicit STIssue(SField const& name) : STTypedBase(name)
    {
    }

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
        return std::get<TIss>(asset_);
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
            Throw<std::runtime_error>(
                "Invalid asset: currency and account native mismatch");

        asset_ = asset;
    }

    [[nodiscard]] std::string
    getText() const override
    {
        return asset_.getText();
    }

    [[nodiscard]] Json::Value
    getJson(JsonOptions) const override
    {
        Json::Value jv;
        asset_.setJson(jv);
        return jv;
    }

    void
    add(Serializer& s) const override
    {
        if (holds<Issue>())
        {
            auto const& issue = asset_.get<Issue>();

            s.addBitString(issue.currency);

            if (!isXRP(issue.currency))
                s.addBitString(issue.account);

            return;
        }

        // Although `MPTID` is, internally, a 192-bit `base_uint` and
        // could have reasonably been serialized that way, we instead
        // split it in two pieces, serializing each one separately. A
        // sentinel between them distinguishes between MPTs from IOUs.
        auto const& issue = asset_.get<MPTIssue>();
        s.addBitString(issue.getIssuer());
        s.addBitString(noAccount());

        // WARNING: The fact that we use memcpy to copy raw bytes and
        //          then feed them into add32 (which will convert the
        //          data to big endian) could cause endianness issues
        //          on big-endian platforms. Exercise extreme care if
        //          porting this code.
        std::uint32_t sequence;
        std::memcpy(&sequence, issue.getMptID().data(), sizeof(sequence));
        s.add32(sequence);
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return holds<Issue>() && asset_.get<Issue>() == xrpIssue();
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
issueFromJson(SField const& name, Json::Value const& v)
{
    return STIssue{name, assetFromJson(v)};
}

}  // namespace ripple

#endif
