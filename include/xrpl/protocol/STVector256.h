#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

class STVector256 final : public STTypedBase<STI_VECTOR256, STVector256>,
                          public CountedObject<STVector256>
{
    std::vector<UInt256> value_;

public:
    using value_type = std::vector<UInt256> const&;

    STVector256() = default;

    explicit STVector256(SField const& n) : STTypedBase(n)
    {
    }

    explicit STVector256(std::vector<UInt256> vector) : value_(std::move(vector))
    {
    }

    STVector256(SField const& n, std::vector<UInt256> vector)
        : STTypedBase(n), value_(std::move(vector))
    {
    }

    STVector256(SerialIter& sit, SField const& name) : STTypedBase(name)
    {
        auto const slice = sit.getVL();

        if (slice.size() % UInt256::size() != 0)
        {
            Throw<std::runtime_error>(
                std::format("Bad serialization for STVector256: {}", slice.size()));
        }

        value_.reserve(slice.size() / UInt256::kBytes);

        SerialIter inner{slice};

        while (!inner.empty())
            value_.push_back(inner.get256());
    }

    void
    add(Serializer& s) const override
    {
        XRPL_ASSERT(getFName().isBinary(), "xrpl::STVector256::add : field is binary");
        XRPL_ASSERT(
            getFName().fieldType == STI_VECTOR256, "xrpl::STVector256::add : valid field type");

        // Because uint256 has no padding and the container stores the values
        // contiguously, they are already in wire layout, so we can serialize
        // them in one go:
        s.addVL(Slice{value_.data(), value_.size() * UInt256::size()});
    }

    [[nodiscard]] json::Value
    getJson(JsonOptions) const override
    {
        json::Value ret(json::ValueType::Array);

        for (auto const& vEntry : value_)
            ret.append(to_string(vEntry));

        return ret;
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return value_.empty();
    }

    STVector256&
    operator=(std::vector<UInt256> const& v)
    {
        value_ = v;
        return *this;
    }

    STVector256&
    operator=(std::vector<UInt256>&& v)
    {
        value_ = std::move(v);
        return *this;
    }

    void
    setValue(STVector256 const& v)
    {
        value_ = v.value_;
    }

    /**
     * Retrieve a copy of the vector we contain
     */
    explicit
    operator std::vector<UInt256>() const
    {
        return value_;
    }

    [[nodiscard]] std::size_t
    size() const
    {
        return value_.size();
    }

    void
    resize(std::size_t n)
    {
        value_.resize(n);
    }

    [[nodiscard]] bool
    empty() const
    {
        return value_.empty();
    }

    [[nodiscard]] auto&
    operator[](this auto&& self, std::vector<UInt256>::size_type n)
    {
        return self.value_[n];
    }

    [[nodiscard]] std::vector<UInt256> const&
    value() const
    {
        return value_;
    }

    std::vector<UInt256>::iterator
    insert(std::vector<UInt256>::const_iterator pos, UInt256 const& value)
    {
        return value_.insert(pos, value);
    }

    void
    pushBack(UInt256 const& v)
    {
        value_.push_back(v);
    }

    /**
     * Iteration
     *
     * The iterator type follows the constness of the vector: a mutable
     * iterator for `STVector256&` and a const iterator for
     * `STVector256 const&`.
     */
    /** @{ */
    [[nodiscard]] auto
    begin(this auto&& self)
    {
        return self.value_.begin();
    }

    [[nodiscard]] auto
    end(this auto&& self)
    {
        return self.value_.end();
    }
    /** @} */

    std::vector<UInt256>::iterator
    erase(std::vector<UInt256>::iterator position)
    {
        return value_.erase(position);
    }

    void
    clear() noexcept
    {
        value_.clear();
    }

    friend bool
    operator==(STVector256 const& lhs, STVector256 const& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    friend class detail::STVar;
};

}  // namespace xrpl
