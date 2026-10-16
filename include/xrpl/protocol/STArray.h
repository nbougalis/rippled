#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

class STArray final : public STTypedBase<STI_ARRAY, STArray>, public CountedObject<STArray>
{
    using ListType = std::vector<STObject>;

    ListType v_;

public:
    using value_type = STObject;
    using size_type = ListType::size_type;
    using iterator = ListType::iterator;
    using const_iterator = ListType::const_iterator;

    STArray() = default;
    STArray(STArray const&) = default;

    template <std::input_iterator Iter>
        requires std::convertible_to<std::iter_reference_t<Iter>, STObject>
    explicit STArray(Iter first, Iter last) : v_(first, last)
    {
    }

    template <std::input_iterator Iter>
        requires std::convertible_to<std::iter_reference_t<Iter>, STObject>
    STArray(SField const& f, Iter first, Iter last) : STTypedBase(f), v_(first, last)
    {
    }

    STArray&
    operator=(STArray const&) = default;

    STArray(STArray&& other) noexcept : STTypedBase(other), v_(std::move(other.v_))
    {
    }

    STArray&
    operator=(STArray&& other) noexcept
    {
        if (this != &other)
        {
            setFName(other.getFName());
            v_ = std::move(other.v_);
        }

        return *this;
    }

    STArray(SField const& f, std::size_t n) : STTypedBase(f)
    {
        v_.reserve(n);
    }

    STArray(SerialIter& sit, SField const& f, int depth = 0) : STTypedBase(f)
    {
        while (!sit.empty())
        {
            int type = 0, field = 0;
            sit.getFieldID(type, field);

            if ((type == STI_ARRAY) && (field == 1))
                break;

            if ((type == STI_OBJECT) && (field == 1))
                Throw<std::runtime_error>("Illegal terminator in array");

            auto const& fn = SField::getField(type, field);

            if (fn.isInvalid())
                Throw<std::runtime_error>("Unknown field");

            if (fn.fieldType != STI_OBJECT)
                Throw<std::runtime_error>("Non-object in array");

            v_.emplace_back(sit, fn, depth + 1);

            v_.back().applyTemplateFromSField(fn);  // May throw
        }
    }

    explicit STArray(std::size_t n)
    {
        v_.reserve(n);
    }

    explicit STArray(SField const& f) : STTypedBase(f)
    {
    }

    [[nodiscard]] STObject&
    operator[](std::size_t j)
    {
        return v_[j];
    }

    [[nodiscard]] STObject const&
    operator[](std::size_t j) const
    {
        return v_[j];
    }

    [[nodiscard]] STObject&
    back()
    {
        return v_.back();
    }

    [[nodiscard]] STObject const&
    back() const
    {
        return v_.back();
    }

    template <class... Args>
    void
    emplaceBack(Args&&... args)
    {
        v_.emplace_back(std::forward<Args>(args)...);
    }

    void
    pushBack(STObject const& object)
    {
        v_.push_back(object);
    }

    void
    pushBack(STObject&& object)
    {
        v_.push_back(std::move(object));
    }

    // STL-compatible alias required by std::back_insert_iterator
    void
    // NOLINTNEXTLINE(readability-identifier-naming)
    push_back(STObject const& object)
    {
        pushBack(object);
    }

    void
    // NOLINTNEXTLINE(readability-identifier-naming)
    push_back(STObject&& object)
    {
        pushBack(std::move(object));
    }

    [[nodiscard]] iterator
    begin()
    {
        return v_.begin();
    }

    [[nodiscard]] iterator
    end()
    {
        return v_.end();
    }

    [[nodiscard]] const_iterator
    begin() const
    {
        return v_.begin();
    }

    [[nodiscard]] const_iterator
    end() const
    {
        return v_.end();
    }

    [[nodiscard]] size_type
    size() const noexcept
    {
        return v_.size();
    }

    [[nodiscard]] bool
    empty() const noexcept
    {
        return v_.empty();
    }

    void
    clear()
    {
        v_.clear();
    }

    void
    reserve(std::size_t n)
    {
        v_.reserve(n);
    }

    void
    swap(STArray& a) noexcept
    {
        v_.swap(a.v_);
    }

    [[nodiscard]] std::string
    getFullText() const override
    {
        return "[" + rangeToString(v_, [](STObject const& o) { return o.getFullText(); }) + "]";
    }

    [[nodiscard]] std::string
    getText() const override
    {
        return "[" + rangeToString(v_, [](STObject const& o) { return o.getText(); }) + "]";
    }

    [[nodiscard]] json::Value
    getJson(JsonOptions options) const override
    {
        json::Value v = json::ValueType::Array;

        for (auto const& object : v_)
        {
            if (object.getSType() != STI_NOTPRESENT)
            {
                json::Value& inner = v.append(json::ValueType::Object);
                inner[object.getFName().getJsonName()] = object.getJson(options);
            }
        }

        return v;
    }

    void
    add(Serializer& s) const override
    {
        for (STObject const& object : v_)
        {
            object.addFieldID(s);
            object.add(s);
            s.addFieldID(STI_OBJECT, 1);
        }
    }

    /**
     * Sort the elements of the array in place.
     *
     * @tparam Compare A callable that imposes a strict weak ordering on
     *                 @ref STObject instances. The callable must satisfy
     *                 `std::strict_weak_order<Compare, STObject const&,
     *                 STObject const&>`.
     *
     * @param compare The comparison function object to use for sorting.
     */
    template <std::strict_weak_order<STObject const&, STObject const&> Compare>
    void
    sort(Compare compare)
    {
        std::ranges::sort(v_, std::move(compare));
    }

    iterator
    erase(iterator pos)
    {
        return v_.erase(pos);
    }

    iterator
    erase(const_iterator pos)
    {
        return v_.erase(pos);
    }

    iterator
    erase(iterator first, iterator last)
    {
        return v_.erase(first, last);
    }

    iterator
    erase(const_iterator first, const_iterator last)
    {
        return v_.erase(first, last);
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return v_.empty();
    }

    friend bool
    operator==(STArray const& lhs, STArray const& rhs)
    {
        return lhs.v_ == rhs.v_;
    }

    friend class detail::STVar;
};

}  // namespace xrpl
