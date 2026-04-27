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

#ifndef RIPPLE_PROTOCOL_STACCOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_STACCOUNT_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STBase.h>

#include <string>

namespace ripple {

/** A serialized type holding an AccountID.

    For compatibility and historical reasons (i.e. "it was already broken
    when I got here") the wire format is shared with @ref STBlob and this
    cannot easily change at this point.

    Note that STAccount has two distinct zero states:

    - A @em default STAccount is one where @c isDefault() returns @c true
      and which serializes to an empty VL encoding. It can be constructed
      by the default constructor or by deserializing an empty VL blob.

    - An @em explicit-zero STAccount is one where @c isDefault() returns
      @c false. Such an instance follows the regular serialization rules.

    These two zero states are byte-distinct on the wire and deliberately
    treated as inequivalent by @ref isEquivalent, even though they would
    directly compare as equal.

    The serialization and distinction between equivalence and comparison
    trace their origin back to early design decisions, which now form an
    immutable part of the protocol. The serialization wire format cannot
    be changed without impacting any already existing objects containing
    STAccount fields.

    @note STAccount is @c final because @ref STTypedBase relies on the
          most-derived type for its lifted @c copy() and @c move()
          implementations; see @ref STTypedBase for details.

    @see AccountID, STTypedBase
*/
class STAccount final : public STTypedBase<STI_ACCOUNT, STAccount>,
                        public CountedObject<STAccount>
{
    static constexpr std::uint32_t actual_size = static_cast<std::uint32_t>(AccountID::bytes);
    static constexpr std::uint32_t default_size = 0;

    /** The account ID */
    AccountID value_;

    /** True if the value is defaulted; false otherwise. */
    bool default_ = true;

public:
    using value_type = AccountID;

    STAccount() : value_(beast::zero)
    {
    }

    STAccount(SField const& n) : STTypedBase(n), value_(beast::zero)
    {
    }

    STAccount(SerialIter& sit, SField const& name) : STAccount(name)
    {
        if (auto s = sit.getVL(); s.size() != 0)
        {
            default_ = false;

            // Throwing from constructors can be awkward, but this is only
            // called from STVar::STVar (SerialIter&, SField const&) which
            // also throws.
            if (s.size() != value_.size()) [[unlikely]]
                Throw<std::runtime_error>("Invalid STAccount size");

            std::copy_n(s.data(), s.size(), value_.data());
        }
    }

    STAccount(SField const& n, AccountID const& v)
        : STTypedBase(n), value_(v), default_(false)
    {
    }

    [[nodiscard]] std::string
    getText() const override
    {
        if (default_)
            return "";

        return toBase58(value());
    }

    void
    add(Serializer& s) const override
    {
        XRPL_ASSERT(
            getFName().isBinary(), "ripple::STAccount::add : field is binary");
        XRPL_ASSERT(
            getFName().fieldType == STI_ACCOUNT,
            "ripple::STAccount::add : valid field type");

        // In order to present the legacy serialization behavior of defaulted
        // instances, this type internally serializes as a zero-sized blob if
        // the value is defaulted, or a fixed size blob otherwise.
        s.addVL(value_.data(), default_ ? 0 : value_.size());
    }

    /** Compares two STAccount instances for equivalence.

        This is overloaded because the semantics implemented by STTypedBase
        do not cleanly support the default-vs-explicit zero semantics which
        STAccount expects.
     */
    [[nodiscard]] bool
    isEquivalent(const STBase& t) const override
    {
        if (t.getSType() != type_id)
            return false;

        auto const& other = static_cast<STAccount const&>(t);

        if (default_)
            return other.default_;

        return !other.default_ && (value_ == other.value_);
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return default_;
    }

    STAccount&
    operator=(AccountID const& value)
    {
        setValue(value);
        return *this;
    }

    [[nodiscard]] AccountID const&
    value() const noexcept
    {
        return value_;
    }

    void
    setValue(AccountID const& v)
    {
        value_ = v;
        default_ = false;
    }

    friend bool
    operator==(STAccount const& lhs, STAccount const& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    friend auto
    operator<=>(STAccount const& lhs, STAccount const& rhs) noexcept
    {
        return lhs.value_ <=> rhs.value_;
    }

    friend bool
    operator==(STAccount const& lhs, AccountID const& rhs) noexcept
    {
        return lhs.value_ == rhs;
    }

    friend auto
    operator<=>(STAccount const& lhs, AccountID const& rhs) noexcept
    {
        return lhs.value_ <=> rhs;
    }

    friend class detail::STVar;
};

}  // namespace ripple

#endif
