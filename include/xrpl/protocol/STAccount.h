#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <stdexcept>
#include <string>

namespace xrpl {

/**
 * A serialized type holding an AccountID.
 *
 * For compatibility and historical reasons (i.e. "it was already broken
 * when I got here") the wire format is shared with @ref STBlob; this is
 * now part of the protocol and cannot be easily changed.
 *
 * Note that STAccount has 2 distinct zero states, each with a separate
 * wire encoding:
 *
 * - A @em default STAccount is one where @c isDefault() returns @c true
 *   and which serializes to an empty VL encoding. It can be constructed
 *   by the default constructor or by deserializing an empty VL blob.
 *
 * - An @em explicit-zero STAccount is one where @c isDefault() returns
 *   @c false. Such an instance follows the regular serialization rules.
 *
 * @note Although the two zero states compare equal with @c operator==,
 *       @ref isEquivalent intentionally reports them as not equivalent,
 *       since they serialize differently. Like the wire format, this
 *       distinction traces back to early design decisions and should
 *       not changed.
 *
 * @note STAccount is @c final because @ref STTypedBase relies on the
 *       most-derived type for its lifted @c copy() and @c move()
 *       implementations; see @ref STTypedBase for details.
 *
 * @see AccountID, STTypedBase
 */
class STAccount final : public STTypedBase<STI_ACCOUNT, STAccount>, public CountedObject<STAccount>
{
    // The account ID
    AccountID value_;

    // True if the value is defaulted; false otherwise.
    bool default_ = true;

public:
    using value_type = AccountID;

    STAccount() = default;

    STAccount(SField const& n) : STTypedBase(n)
    {
    }

    STAccount(SerialIter& sit, SField const& name) : STAccount(name)
    {
        if (auto const s = sit.getVL(); !s.empty())
        {
            // Throwing from constructors can be awkward, but this is only
            // called from STVar::STVar (SerialIter&, SField const&) which
            // also throws.
            auto const id = AccountID::fromRaw(s);

            if (!id) [[unlikely]]
                Throw<std::runtime_error>("Invalid STAccount size");

            value_ = *id;
            default_ = false;
        }
    }

    STAccount(SField const& n, AccountID const& v) : STTypedBase(n), value_(v), default_(false)
    {
    }

    [[nodiscard]] std::string
    getText() const override
    {
        if (default_)
            return "";

        return toBase58(value_);
    }

    void
    add(Serializer& s) const override
    {
        XRPL_ASSERT(getFName().isBinary(), "xrpl::STAccount::add : field is binary");
        XRPL_ASSERT(getFName().fieldType == STI_ACCOUNT, "xrpl::STAccount::add : valid field type");

        // In order to preserve the legacy serialization behavior of defaulted
        // instances, this type internally serializes as a zero-sized blob if
        // the value is defaulted, or a fixed size blob otherwise.
        s.addVL(Slice{value_.data(), default_ ? 0 : value_.size()});
    }

    /**
     * Compares two STAccount instances for equivalence.
     *
     * This is overridden because the semantics implemented by STTypedBase
     * do not cleanly support the default-vs-explicit zero semantics which
     * STAccount expects.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override
    {
        if (t.getSType() != kTypeId)
            return false;

        auto const& other = safeDowncast<STAccount const&>(t);

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

}  // namespace xrpl
