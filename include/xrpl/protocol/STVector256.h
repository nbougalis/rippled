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

#ifndef RIPPLE_PROTOCOL_STVECTOR256_H_INCLUDED
#define RIPPLE_PROTOCOL_STVECTOR256_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STInteger.h>

namespace ripple {

class STVector256 final : public STTypedBase<STI_VECTOR256, STVector256>,
                          public CountedObject<STVector256>
{
    // The container used here must guarantee that all the items are
    // laid down contiguously in memory with no gaps or padding. The
    // object itself must be free of padding and trivially copyable.
    std::vector<uint256> mValue;

    static_assert(
        std::ranges::contiguous_range<decltype(mValue)>,
        "storage must be contiguous");

    static_assert(
        std::has_unique_object_representations_v<
            std::ranges::range_value_t<decltype(mValue)>>,
        "element must be padding-free and trivially copyable");

public:
    using value_type = std::vector<uint256> const&;

    STVector256() = default;

    explicit STVector256(SField const& n) : STTypedBase(n)
    {
    }
    explicit STVector256(std::vector<uint256> const& vector) : mValue(vector)
    {
    }
    STVector256(SField const& n, std::vector<uint256> const& vector)
        : STTypedBase(n), mValue(vector)
    {
    }

    STVector256(SerialIter& sit, SField const& name) : STTypedBase(name)
    {
        auto const slice = sit.getVL();

        if (slice.size() % uint256::size() != 0)
            Throw<std::runtime_error>(
                "Bad serialization for STVector256: " +
                std::to_string(slice.size()));

        auto const cnt = slice.size() / uint256::size();

        mValue.reserve(cnt);

        for (std::size_t i = 0; i != cnt; ++i)
            mValue.emplace_back(
                slice.substr(i * uint256::size(), uint256::size()));
    }

    void
    add(Serializer& s) const override
    {
        XRPL_ASSERT(
            getFName().isBinary(),
            "ripple::STVector256::add : field is binary");
        XRPL_ASSERT(
            getFName().fieldType == STI_VECTOR256,
            "ripple::STVector256::add : valid field type");

        // Because uint256 has no padding and the container stores the values
        // contiguously, they are already in wire layout, so we can serialize
        // them in one go:
        s.addVL(Slice{mValue.data(), mValue.size() * uint256::size()});
    }

    [[nodiscard]] Json::Value
    getJson(JsonOptions) const override
    {
        Json::Value ret(Json::arrayValue);

        for (auto const& vEntry : mValue)
            ret.append(to_string(vEntry));

        return ret;
    }

    [[nodiscard]] bool
    isDefault() const override
    {
        return mValue.empty();
    }

    STVector256&
    operator=(std::vector<uint256> const& v)
    {
        mValue = v;
        return *this;
    }

    STVector256&
    operator=(std::vector<uint256>&& v)
    {
        mValue = std::move(v);
        return *this;
    }

    void
    setValue(const STVector256& v)
    {
        mValue = v.mValue;
    }

    /** Retrieve a copy of the vector we contain */
    explicit
    operator std::vector<uint256>() const
    {
        return mValue;
    }

    [[nodiscard]] std::size_t
    size() const
    {
        return mValue.size();
    }

    void
    resize(std::size_t n)
    {
        return mValue.resize(n);
    }

    [[nodiscard]] bool
    empty() const
    {
        return mValue.empty();
    }

    [[nodiscard]] std::vector<uint256>::reference
    operator[](std::vector<uint256>::size_type n)
    {
        return mValue[n];
    }

    [[nodiscard]] std::vector<uint256>::const_reference
    operator[](std::vector<uint256>::size_type n) const
    {
        return mValue[n];
    }

    [[nodiscard]] std::vector<uint256> const&
    value() const
    {
        return mValue;
    }

    std::vector<uint256>::iterator
    insert(std::vector<uint256>::const_iterator pos, uint256 const& value)
    {
        return mValue.insert(pos, value);
    }

    std::vector<uint256>::iterator
    insert(std::vector<uint256>::const_iterator pos, uint256&& value)
    {
        return mValue.insert(pos, std::move(value));
    }

    void
    push_back(uint256 const& v)
    {
        mValue.push_back(v);
    }

    [[nodiscard]] std::vector<uint256>::iterator
    begin()
    {
        return mValue.begin();
    }

    [[nodiscard]] std::vector<uint256>::const_iterator
    begin() const
    {
        return mValue.begin();
    }

    [[nodiscard]] std::vector<uint256>::iterator
    end()
    {
        return mValue.end();
    }

    [[nodiscard]] std::vector<uint256>::const_iterator
    end() const
    {
        return mValue.end();
    }

    std::vector<uint256>::iterator
    erase(std::vector<uint256>::iterator position)
    {
        return mValue.erase(position);
    }

    void
    clear() noexcept
    {
        return mValue.clear();
    }

    friend bool
    operator==(STVector256 const& lhs, STVector256 const& rhs) noexcept
    {
        return lhs.mValue == rhs.mValue;
    }

    friend class detail::STVar;
};

}  // namespace ripple

#endif
