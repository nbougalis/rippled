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

#ifndef RIPPLE_PROTOCOL_STINTEGER_H_INCLUDED
#define RIPPLE_PROTOCOL_STINTEGER_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/STBase.h>

namespace ripple {
namespace detail {

/** Custom text and JSON rendering for fields with special display semantics.

    Most integer fields render their value directly as a number, but a small
    number of fields require special handling:

    - @ref sfTransactionResult renders as a TER description or token
    - @ref sfLedgerEntryType renders as the name of the ledger entry type
    - @ref sfTransactionType renders as the name of the transaction type.

    If @p field does not require special handling, @p def is returned.

    @param field The field being rendered.
    @param value The field's value, as a @c uint16_t (safe for both
                 @c uint8_t and @c uint16_t fields).
    @param def   The default string to return if no special handling applies.
 */
std::string
specializedText(SField const& field, std::uint16_t value, std::string def);
Json::Value
specializedJson(SField const& field, std::uint16_t value);

/** A serialized integer field.

    Represents a fixed-width unsigned integer value in the serialization
    protocol. The @p Integer template parameter specifies the underlying
    type to use (and thus, indirectly, the width).

    Because there is a 1-to-1 mapping between the underlying integer and
    the serialization type, this class is placed in the detail namespace
    and type aliases are declared below (e.g. @ref STUInt8) for ease.

    @note Because this is a class template, @ref STTypedBase<ID> ends up
          being a dependent base. As a result, inherited members such as
          @ref getFName and @ref type_id are not directly visible to the
          compiler during the first phase of template instantiation, and
          must be accessed via @c this-> or be explicitly qualified.
          This is a consequence of C++'s two-phase name lookup rules for
          templates and cannot be avoided. For more details, please read
          https://en.cppreference.com/cpp/language/dependent_name

    @tparam Integer The underlying integer type (e.g. @c std::uint32_t).
    @tparam ID      The @ref SerializedTypeID that identifies this field
                    type in the binary serialization protocol.

    @see STTypedBase, STUInt8, STUInt16, STUInt32, STUInt64
*/
template <typename Integer, SerializedTypeID ID>
    requires(std::is_same_v<Integer, std::uint8_t> ||
             std::is_same_v<Integer, std::uint16_t> ||
             std::is_same_v<Integer, std::uint32_t> ||
             std::is_same_v<Integer, std::uint64_t>)
class STInteger final : public STTypedBase<ID, STInteger<Integer, ID>>,
                        public CountedObject<STInteger<Integer, ID>>
{
    // If additional types are added the constructor which reads from the
    // serialized iterator should be updated to handle them as well.
    static_assert(
        (std::is_same_v<Integer, std::uint8_t> && ID == STI_UINT8) ||
        (std::is_same_v<Integer, std::uint16_t> && ID == STI_UINT16) ||
        (std::is_same_v<Integer, std::uint32_t> && ID == STI_UINT32) ||
        (std::is_same_v<Integer, std::uint64_t> && ID == STI_UINT64));

public:
    using value_type = Integer;

private:
    Integer value_ = 0;

public:
    explicit STInteger(Integer v) : value_(v)
    {
    }

    STInteger(SField const& n, Integer v = 0)
        : STTypedBase<ID, STInteger<Integer, ID>>(n), value_(v)
    {
    }

    STInteger(SerialIter& sit, SField const& name)
        : STInteger(name, [&sit]() -> Integer {
            if constexpr (std::is_same_v<Integer, std::uint8_t>)
                return sit.get8();

            if constexpr (std::is_same_v<Integer, std::uint16_t>)
                return sit.get16();

            if constexpr (std::is_same_v<Integer, std::uint32_t>)
                return sit.get32();

            if constexpr (std::is_same_v<Integer, std::uint64_t>)
                return sit.get64();
        }())
    {
    }

    [[nodiscard]] Json::Value
    getJson(JsonOptions) const override
    {
        if constexpr (ID == STI_UINT64)
        {
            char buf[32] = {};

            auto [ptr, ec] = std::to_chars(
                std::begin(buf),
                std::end(buf),
                value_,
                this->getFName().shouldMeta(SField::sMD_BaseTen) ? 10 : 16);

            return std::string_view{buf, ptr};
        }
        else
        {
            if constexpr (ID == STI_UINT8 || ID == STI_UINT16)
                return specializedJson(this->getFName(), value_);

            return static_cast<Json::UInt>(value_);
        }
    }

    [[nodiscard]] std::string
    getText() const override
    {
        auto ret = std::to_string(value_);

        if constexpr (ID == STI_UINT8 || ID == STI_UINT16)
            return specializedText(this->getFName(), value_, std::move(ret));

        return ret;
    }

    void
    add(Serializer& s) const override
    {
        XRPL_ASSERT(
            this->getFName().isBinary(),
            "ripple::STInteger::add : field is binary");
        XRPL_ASSERT(
            this->getFName().fieldType == this->getSType(),
            "ripple::STInteger::add : field type match");
        s.addInteger(value_);
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return value_ == 0;
    }

    STInteger&
    operator=(value_type v)
    {
        value_ = v;
        return *this;
    }

    [[nodiscard]] value_type
    value() const noexcept
    {
        return value_;
    }

    void
    setValue(Integer v)
    {
        value_ = v;
    }

    operator Integer() const
    {
        return value_;
    }

    friend bool
    operator==(STInteger const& lhs, STInteger const& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    friend class STVar;
};

}  // namespace detail

using STUInt8 = detail::STInteger<std::uint8_t, STI_UINT8>;
using STUInt16 = detail::STInteger<std::uint16_t, STI_UINT16>;
using STUInt32 = detail::STInteger<std::uint32_t, STI_UINT32>;
using STUInt64 = detail::STInteger<std::uint64_t, STI_UINT64>;

}  // namespace ripple

#endif
