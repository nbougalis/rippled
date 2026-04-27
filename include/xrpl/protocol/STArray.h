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

#ifndef RIPPLE_PROTOCOL_STARRAY_H_INCLUDED
#define RIPPLE_PROTOCOL_STARRAY_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/protocol/STObject.h>

#include <concepts>
#include <iterator>

namespace ripple {

class STArray final : public STTypedBase<STI_ARRAY, STArray>,
                      public CountedObject<STArray>
{
    using list_type = std::vector<STObject>;

    list_type v_;

public:
    using value_type = STObject;
    using size_type = list_type::size_type;
    using iterator = list_type::iterator;
    using const_iterator = list_type::const_iterator;

    template <std::input_iterator Iter>
        requires std::convertible_to<std::iter_reference_t<Iter>, STObject>
    STArray(Iter first, Iter last) : v_(first, last)
    {
    }

    template <std::input_iterator Iter>
        requires std::convertible_to<std::iter_reference_t<Iter>, STObject>
    STArray(SField const& f, Iter first, Iter last)
        : STTypedBase(f), v_(first, last)
    {
    }

    STArray(std::vector<STObject> v, SField const& f)
        : STTypedBase(f), v_(std::move(v))
    {
    }

    STArray() = default;
    STArray(STArray const&) = default;

    STArray&
    operator=(STArray const&) = default;

    STArray(STArray&& other) noexcept
        : STTypedBase(other.getFName()), v_(std::move(other.v_))
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
        uint8_t nop_counter = 0;

        while (!sit.empty())
        {
            int type, field;
            sit.getFieldID(type, field);

            // FIXME: Apparently, 9/9 is a "special" encoding for NOP used by
            //        Xahau. Unfortunately, the Ripple developers chose 9 for
            //        STI_NUMBER. Although (STI_NUMBER,9) is not currently in
            //        use, it could cause hard-to-track down breakage, if the
            //        field ever became used.
            if (type == 9 && field == 9)
            {
                if (++nop_counter == 64)
                    Throw<std::runtime_error>("Too many NOPS");

                continue;
            }

            if ((type == STI_ARRAY) && (field == 1))
                break;

            if ((type == STI_OBJECT) && (field == 1))
                Throw<std::runtime_error>("Illegal terminator in array");

            auto const& fn = SField::getField(type, field);

            if (fn.isInvalid())
                Throw<std::runtime_error>("Unknown field - In Array");

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
    emplace_back(Args&&... args)
    {
        v_.emplace_back(std::forward<Args>(args)...);
    }

    void
    push_back(STObject const& object)
    {
        v_.push_back(object);
    }

    void
    push_back(STObject&& object)
    {
        v_.push_back(std::move(object));
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
        return "[" +
            range_to_string(
                   v_, [](STObject const& o) { return o.getFullText(); }) +
            "]";
    }

    [[nodiscard]] std::string
    getText() const override
    {
        return "[" +
            range_to_string(v_, [](STObject const& o) { return o.getText(); }) +
            "]";
    }

    [[nodiscard]] Json::Value
    getJson(JsonOptions options) const override
    {
        Json::Value v = Json::arrayValue;

        for (auto const& object : v_)
        {
            if (object.getSType() != STI_NOTPRESENT)
            {
                Json::Value& inner = v.append(Json::objectValue);
                inner[object.getFName().getJsonName()] =
                    object.getJson(options);
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

    /** Sort the elements of the array in place.

        @tparam Compare A callable that imposes a strict weak ordering on
                        @ref STObject instances. The callable must satisfy
                        `std::strict_weak_order<Compare, STObject const&,
                        STObject const&>`.

        @param compare The comparison function object to use for sorting.
    */
    template <std::strict_weak_order<STObject const&, STObject const&> Compare>
    void
    sort(Compare compare)
    {
        std::sort(v_.begin(), v_.end(), std::move(compare));
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
    operator==(STArray const& lhs, STArray const& rhs) noexcept
    {
        return lhs.v_ == rhs.v_;
    }

    friend class detail::STVar;
};

}  // namespace ripple

#endif
