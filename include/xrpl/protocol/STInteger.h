#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <charconv>
#include <cstdint>
#include <iterator>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace xrpl {
namespace detail {

/**
 * Custom text rendering for fields with special display semantics.
 *
 * Most integer fields render their value directly as a number, but a small
 * number of fields require special handling:
 *
 * - @ref sfTransactionResult renders as a TER description;
 * - @ref sfLedgerEntryType renders as the name of the ledger entry type;
 * - @ref sfTransactionType renders as the name of the transaction type;
 * - @ref sfPermissionValue renders as the name of the permission.
 *
 * If @p field does not require special handling, or if @p value has no
 * known name, @p def is returned.
 *
 * @param field The field being rendered.
 * @param value The field's value, as a @c std::uint32_t (safe for 8, 16
 *              and 32 bit unsigned fields).
 * @param def   The text to return if no special handling applies.
 * @return The text to display for @p value.
 */
std::string
specializedText(SField const& field, std::uint32_t value, std::string def);

/**
 * Custom JSON rendering for fields with special display semantics.
 *
 * The fields which require special handling are those described in
 * @ref specializedText, except that @ref sfTransactionResult renders as
 * a TER token.
 *
 * @param field The field being rendered.
 * @param value The field's value, as a @c std::uint32_t (safe for 8, 16
 *              and 32 bit unsigned fields).
 * @param def   The JSON to return if no special handling applies.
 * @return The JSON to display for @p value.
 */
json::Value
specializedJson(SField const& field, std::uint32_t value, json::Value def);

/**
 * A serialized integer field.
 *
 * Represents a fixed-width integer value in the serialization protocol.
 * The @p Integer template parameter specifies the underlying type to use
 * (and thus, indirectly, the width).
 *
 * Because there is a 1-to-1 mapping between the underlying integer and
 * the serialization type, this class is placed in the detail namespace
 * and type aliases are declared below (e.g. @ref STUInt8) for ease.
 *
 * @note Because this is a class template, @ref STTypedBase ends up
 *       being a dependent base. As a result, inherited members such as
 *       @ref getFName and @ref kTypeId are not directly visible to the
 *       compiler during the first phase of template instantiation, and
 *       must be accessed via @c this-> or be explicitly qualified.
 *       This is a consequence of C++'s two-phase name lookup rules for
 *       templates and cannot be avoided. For more details, please read
 *       https://en.cppreference.com/cpp/language/dependent_name
 *
 * @tparam Integer The underlying integer type (e.g. @c std::uint32_t).
 * @tparam ID      The @ref SerializedTypeID that identifies this field
 *                 type in the binary serialization protocol.
 *
 * @see STTypedBase, STUInt8, STUInt16, STUInt32, STUInt64, STInt32
 */
template <typename Integer, SerializedTypeID ID>
    requires(std::is_same_v<Integer, std::uint8_t> || std::is_same_v<Integer, std::uint16_t> ||
             std::is_same_v<Integer, std::uint32_t> || std::is_same_v<Integer, std::uint64_t> ||
             std::is_same_v<Integer, std::int32_t> || std::is_same_v<Integer, std::int64_t>)
class STInteger final : public STTypedBase<ID, STInteger<Integer, ID>>,
                        public CountedObject<STInteger<Integer, ID>>
{
    // If additional types are added the constructor which reads from the
    // serialized iterator should be updated to handle them as well.
    static_assert(
        (std::is_same_v<Integer, std::uint8_t> && ID == STI_UINT8) ||
            (std::is_same_v<Integer, std::uint16_t> && ID == STI_UINT16) ||
            (std::is_same_v<Integer, std::uint32_t> && ID == STI_UINT32) ||
            (std::is_same_v<Integer, std::uint64_t> && ID == STI_UINT64) ||
            (std::is_same_v<Integer, std::int32_t> && ID == STI_INT32),
        "The type identifier must match a supported underlying integer type");

    static_assert(
        SerializableInteger<Integer>,
        "STInteger requires an integer type that Serializer can append");

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
            {
                return sit.get8();
            }
            else if constexpr (std::is_same_v<Integer, std::uint16_t>)
            {
                return sit.get16();
            }
            else if constexpr (std::is_same_v<Integer, std::uint32_t>)
            {
                return sit.get32();
            }
            else if constexpr (std::is_same_v<Integer, std::uint64_t>)
            {
                return sit.get64();
            }
            else
            {
                return sit.geti32();
            }
        }())
    {
    }

    [[nodiscard]] json::Value
    getJson(JsonOptions) const override
    {
        if constexpr (ID == STI_UINT64)
        {
            char buf[32] = {};

            auto const [ptr, ec] = std::to_chars(
                std::begin(buf),
                std::end(buf),
                value_,
                this->getFName().shouldMeta(SField::kSmdBaseTen) ? 10 : 16);

            XRPL_ASSERT(ec == std::errc(), "xrpl::STUInt64::getJson : to_chars succeeded");

            return std::string{std::begin(buf), ptr};
        }
        else if constexpr (ID == STI_UINT8 || ID == STI_UINT16 || ID == STI_UINT32)
        {
            return specializedJson(this->getFName(), value_, value_);
        }
        else
        {
            return value_;
        }
    }

    [[nodiscard]] std::string
    getText() const override
    {
        auto ret = std::to_string(value_);

        if constexpr (ID == STI_UINT8 || ID == STI_UINT16 || ID == STI_UINT32)
            return specializedText(this->getFName(), value_, std::move(ret));

        return ret;
    }

    void
    add(Serializer& s) const override
    {
        XRPL_ASSERT(this->getFName().isBinary(), "xrpl::STInteger::add : field is binary");
        XRPL_ASSERT(
            this->getFName().fieldType == this->getSType(),
            "xrpl::STInteger::add : field type match");

        if constexpr (sizeof(Integer) == 1)
        {
            s.add8(value_);
        }
        else if constexpr (sizeof(Integer) == 2)
        {
            s.add16(value_);
        }
        else if constexpr (sizeof(Integer) == 4)
        {
            s.add32(value_);
        }
        else
        {
            s.add64(value_);
        }
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

using STInt32 = detail::STInteger<std::int32_t, STI_INT32>;

}  // namespace xrpl
