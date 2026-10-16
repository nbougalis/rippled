#pragma once

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace xrpl {

// variable length byte string
class STBlob final : public STTypedBase<STI_VL, STBlob>, public CountedObject<STBlob>
{
    Buffer value_;

public:
    using value_type = Slice;

    STBlob() = default;

    STBlob(STBlob const&) = default;
    STBlob&
    operator=(STBlob const&) = default;

    STBlob(STBlob&&) noexcept = default;
    STBlob&
    operator=(STBlob&&) noexcept = default;

    STBlob(SField const& f, void const* data, std::size_t size) : STTypedBase(f), value_(data, size)
    {
    }

    STBlob(SField const& f, Buffer&& b) : STTypedBase(f), value_(std::move(b))
    {
    }

    STBlob(SField const& n) : STTypedBase(n)
    {
    }

    STBlob(SerialIter& sit, SField const& name = sfGeneric) : STTypedBase(name), value_(sit.getVL())
    {
    }

    [[nodiscard]] std::size_t
    size() const noexcept
    {
        return value_.size();
    }

    [[nodiscard]] std::uint8_t const*
    data() const noexcept
    {
        return value_.data();
    }

    [[nodiscard]] std::string
    getText() const override
    {
        return strHex(value_);
    }

    void
    add(Serializer& s) const override
    {
        XRPL_ASSERT(getFName().isBinary(), "xrpl::STBlob::add : field is binary");
        XRPL_ASSERT(
            (getFName().fieldType == STI_VL) || (getFName().fieldType == STI_ACCOUNT),
            "xrpl::STBlob::add : valid field type");
        s.addVL(Slice{value_.data(), value_.size()});
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return value_.empty();
    }

    STBlob&
    operator=(Slice const& slice)
    {
        value_ = Buffer(slice.data(), slice.size());
        return *this;
    }

    [[nodiscard]] value_type
    value() const noexcept
    {
        return value_;
    }

    STBlob&
    operator=(Buffer&& buffer)
    {
        value_ = std::move(buffer);
        return *this;
    }

    void
    setValue(Buffer&& b)
    {
        value_ = std::move(b);
    }

    friend class detail::STVar;

    friend bool
    operator==(STBlob const& lhs, STBlob const& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }
};

// An STVar that holds an STBlob in its internal buffer relocates it using
// the move constructor, so that must neither allocate nor throw.
static_assert(std::is_nothrow_move_constructible_v<STBlob>);
static_assert(std::is_nothrow_move_assignable_v<STBlob>);

}  // namespace xrpl

