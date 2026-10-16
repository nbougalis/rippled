#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <string>
#include <type_traits>

namespace xrpl {
namespace detail {

/**
 * A serialized fixed-width bit string field.
 *
 * Represents a fixed-width bit string of exactly @p Bits bits in the
 * serialization protocol. @p ID identifies the field type.
 *
 * This class is in the @c detail namespace because there is a 1-to-1
 * mapping between the width in bits and the field type, and properly
 * sized type aliases are declared below (@ref STUInt128, etc) making
 * sure that serialization identifiers are consistent and correct.
 *
 * @note As this is a class template, @ref STTypedBase is, itself, a
 *       dependent base. As a result, any members inherited through
 *       it, like @ref getFName, must be accessed via @c this-> or by
 *       being explicitly qualified. This is a consequence of the way
 *       that C++'s two-phase name lookup rules work.
 *
 * @note The first template parameter could be an unsigned type, however
 *       there's a bug in gdb (last checked in gdb 12.1) that prevents gdb
 *       from finding the RTTI information of a template parameterized by
 *       an unsigned type. This RTTI information is needed to write gdb
 *       pretty printers.
 *
 * @tparam Bits The width of the bitstring in bits (e.g. 128, 256).
 * @tparam ID   The @ref SerializedTypeID that identifies this field type
 *              in the binary serialization protocol.
 *
 * @see STTypedBase, STUInt128, STUInt160, STUInt192, STUInt256
 */
template <int Bits, SerializedTypeID ID>
class STBitString final : public STTypedBase<ID, STBitString<Bits, ID>>,
                          public CountedObject<STBitString<Bits, ID>>
{
    static_assert(Bits > 0, "Number of bits must be positive");

    // STBase::downcast relies on the type identifier to select the
    // concrete type, so each width must map to exactly one identifier.
    static_assert(
        (Bits == 96 && ID == STI_UINT96) || (Bits == 128 && ID == STI_UINT128) ||
            (Bits == 160 && ID == STI_UINT160) || (Bits == 192 && ID == STI_UINT192) ||
            (Bits == 256 && ID == STI_UINT256) || (Bits == 384 && ID == STI_UINT384) ||
            (Bits == 512 && ID == STI_UINT512),
        "The type identifier must match the width of the bit string");

public:
    using value_type = BaseUInt<Bits>;

private:
    value_type value_{};

public:
    STBitString() = default;

    STBitString(SField const& n) : STTypedBase<ID, STBitString<Bits, ID>>(n)
    {
    }

    STBitString(value_type const& v) : value_(v)
    {
    }

    template <typename Tag>
        requires(!std::is_void_v<Tag>)
    STBitString(BaseUInt<Bits, Tag> const& v) : value_(v)
    {
    }

    STBitString(SField const& n, value_type const& v)
        : STTypedBase<ID, STBitString<Bits, ID>>(n), value_(v)
    {
    }

    template <typename Tag>
        requires(!std::is_void_v<Tag>)
    STBitString(SField const& n, BaseUInt<Bits, Tag> const& v)
        : STTypedBase<ID, STBitString<Bits, ID>>(n), value_(v)
    {
    }

    STBitString(SerialIter& sit, SField const& name)
        : STBitString(name, sit.getBitString<value_type>())
    {
    }

    [[nodiscard]] std::string
    getText() const override
    {
        return to_string(value_);
    }

    void
    add(Serializer& s) const override
    {
        XRPL_ASSERT(this->getFName().isBinary(), "xrpl::STBitString::add : field is binary");
        XRPL_ASSERT(
            this->getFName().fieldType == this->getSType(),
            "xrpl::STBitString::add : field type match");
        s.addBitString(value_);
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return value_ == beast::kZero;
    }

    template <typename Tag>
    void
    setValue(BaseUInt<Bits, Tag> const& v)
    {
        value_ = value_type{v};
    }

    [[nodiscard]] value_type const&
    value() const noexcept
    {
        return value_;
    }

    operator value_type() const noexcept
    {
        return value_;
    }

    friend bool
    operator==(STBitString const& lhs, STBitString const& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    friend class STVar;
};

}  // namespace detail

using STUInt96 = detail::STBitString<96, STI_UINT96>;
using STUInt128 = detail::STBitString<128, STI_UINT128>;
using STUInt160 = detail::STBitString<160, STI_UINT160>;
using STUInt192 = detail::STBitString<192, STI_UINT192>;
using STUInt256 = detail::STBitString<256, STI_UINT256>;
using STUInt384 = detail::STBitString<384, STI_UINT384>;
using STUInt512 = detail::STBitString<512, STI_UINT512>;

}  // namespace xrpl
