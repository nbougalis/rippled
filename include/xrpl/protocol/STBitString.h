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

#ifndef RIPPLE_PROTOCOL_STBITSTRING_H_INCLUDED
#define RIPPLE_PROTOCOL_STBITSTRING_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/STBase.h>

namespace ripple {
namespace detail {

/** A serialized fixed-width bit string field.

    Represents a fixed-width bit string of exactly @p Bits bits in the
    serialization protocol. @p ID identifies the field type.

    This class is in the @c detail namespace because there is a 1-to-1
    mapping between the width in bits and the field type, and properly
    sized type aliases are declared below (@ref STUInt128, etc) making
    sure that serialization identifers are consistent and correct.

    @note As this is a class template, @ref STTypedBase<ID> is, itself,
          a dependent base. As a result, any members inherited through
          it, like @ref getFName, must be accessed via @c this-> or by
          being explicitly qualified. This is a consequence of the way
          that C++'s two-phase name lookup rules work.

    @tparam Bits The width of the bitstring in bits (e.g. 128, 256).
    @tparam ID   The @ref SerializedTypeID that identifies this field type
                 in the binary serialization protocol.

    @see STTypedBase, STUInt128, STUInt160, STUInt192, STUInt256
*/
template <int Bits, SerializedTypeID ID>
class STBitString final : public STTypedBase<ID, STBitString<Bits, ID>>,
                          public CountedObject<STBitString<Bits, ID>>
{
    static_assert(Bits > 0, "Number of bits must be positive");

public:
    using value_type = base_uint<Bits>;

private:
    value_type value_;

public:
    STBitString() = default;

    STBitString(SField const& n) : STTypedBase<ID, STBitString<Bits, ID>>(n)
    {
    }

    STBitString(const value_type& v) : value_(v)
    {
    }

    STBitString(SField const& n, const value_type& v)
        : STTypedBase<ID, STBitString<Bits, ID>>(n), value_(v)
    {
    }

    STBitString(SerialIter& sit, SField const& name)
        : STBitString(name, sit.getBitString<Bits>())
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
        XRPL_ASSERT(
            this->getFName().isBinary(),
            "ripple::STBitString::add : field is binary");
        XRPL_ASSERT(
            this->getFName().fieldType == this->getSType(),
            "ripple::STBitString::add : field type match");
        s.addBitString(value_);
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return value_ == beast::zero;
    }

    template <typename Tag>
    void
    setValue(base_uint<Bits, Tag> const& v)
    {
        value_ = v;
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
        return lhs.value() == rhs.value();
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

}  // namespace ripple

#endif
