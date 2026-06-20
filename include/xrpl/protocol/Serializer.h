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

#ifndef RIPPLE_PROTOCOL_SERIALIZER_H_INCLUDED
#define RIPPLE_PROTOCOL_SERIALIZER_H_INCLUDED

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/SField.h>
#include <cstdint>

#include <type_traits>

namespace ripple {

class Serializer
{
    Blob mData;

public:
    explicit Serializer(std::size_t n = 256)
    {
        mData.reserve(n);
    }

    Slice
    slice() const noexcept
    {
        return Slice(mData.data(), mData.size());
    }

    std::size_t
    size() const noexcept
    {
        return mData.size();
    }

    void const*
    data() const noexcept
    {
        return mData.data();
    }

    // assemble functions
    int
    add8(unsigned char i);

    int
    add16(std::uint16_t i);

    template <typename T>
        requires(std::is_same_v<
                 std::make_unsigned_t<std::remove_cv_t<T>>,
                 std::uint32_t>)
    int
    add32(T i)
    {
        int ret = mData.size();
        mData.push_back(static_cast<unsigned char>((i >> 24) & 0xff));
        mData.push_back(static_cast<unsigned char>((i >> 16) & 0xff));
        mData.push_back(static_cast<unsigned char>((i >> 8) & 0xff));
        mData.push_back(static_cast<unsigned char>(i & 0xff));
        return ret;
    }

    int
    add32(HashPrefix p);

    template <typename T>
        requires(std::is_same_v<
                 std::make_unsigned_t<std::remove_cv_t<T>>,
                 std::uint64_t>)
    int
    add64(T i)
    {
        int ret = mData.size();
        mData.push_back(static_cast<unsigned char>((i >> 56) & 0xff));
        mData.push_back(static_cast<unsigned char>((i >> 48) & 0xff));
        mData.push_back(static_cast<unsigned char>((i >> 40) & 0xff));
        mData.push_back(static_cast<unsigned char>((i >> 32) & 0xff));
        mData.push_back(static_cast<unsigned char>((i >> 24) & 0xff));
        mData.push_back(static_cast<unsigned char>((i >> 16) & 0xff));
        mData.push_back(static_cast<unsigned char>((i >> 8) & 0xff));
        mData.push_back(static_cast<unsigned char>(i & 0xff));
        return ret;
    }

    template <typename Integer>
    int addInteger(Integer);

    template <std::size_t Bits, class Tag>
    int
    addBitString(base_uint<Bits, Tag> const& v)
    {
        return addRaw(makeSlice(v));
    }

    int
    addRaw(Slice slice)
    {
        int ret = mData.size();
        mData.insert(mData.end(), slice.begin(), slice.end());
        return ret;
    }

    int
    addRaw(Blob const& vector)
    {
        return addRaw(makeSlice(vector));
    }

    int
    addRaw(const Serializer& s)
    {
        return addRaw(s.slice());
    }

    int
    addVL(Slice const& slice)
    {
        int ret = addEncoded(slice.size());
        addRaw(slice);
        return ret;
    }

    int
    addVL(Blob const& vector)
    {
        return addVL(makeSlice(vector));
    }

    int
    addVL(const void* ptr, int len)
    {
        if (len < 0) [[unlikely]]
            Throw<std::logic_error>("Negative length");

        return addVL(Slice{ptr, checked_cast<std::size_t>(len)});
    }

    int
    addFieldID(int type, int name);
    int
    addFieldID(SerializedTypeID type, int name)
    {
        return addFieldID(safe_cast<int>(type), name);
    }

    // DEPRECATED
    uint256
    getSHA512Half() const;

    // totality functions
    Blob const&
    peekData() const
    {
        return mData;
    }
    Blob
    getData() const
    {
        return mData;
    }
    Blob&
    modData()
    {
        return mData;
    }

    int
    getDataLength() const
    {
        return mData.size();
    }
    const void*
    getDataPtr() const
    {
        return mData.data();
    }
    void*
    getDataPtr()
    {
        return mData.data();
    }
    int
    getLength() const
    {
        return mData.size();
    }
    std::string
    getString() const
    {
        return std::string(reinterpret_cast<const char*>(mData.data()), mData.size());
    }
    void
    erase()
    {
        mData.clear();
    }

    // vector-like functions
    Blob ::iterator
    begin()
    {
        return mData.begin();
    }

    Blob ::iterator
    end()
    {
        return mData.end();
    }

    Blob ::const_iterator
    begin() const
    {
        return mData.begin();
    }

    Blob ::const_iterator
    end() const
    {
        return mData.end();
    }

private:
    int
    addEncoded(int length);

    friend bool
    operator==(Serializer const& lhs, Serializer const& rhs)
    {
        return lhs.mData == rhs.mData;
    }

    friend bool
    operator==(Serializer const& lhs, Blob const& rhs)
    {
        return lhs.mData == rhs;
    }
};

//------------------------------------------------------------------------------

// DEPRECATED
// Transitional adapter to new serialization interfaces
class SerialIter
{
    std::uint8_t const* p_;
    std::size_t remain_;

public:
    SerialIter(void const* data, std::size_t size) noexcept
        : p_(reinterpret_cast<std::uint8_t const*>(data)), remain_(size)
    {
    }

    SerialIter(Slice const& slice) : SerialIter(slice.data(), slice.size())
    {
    }

    // Infer the size of the data based on the size of the passed array.
    template <int N>
    explicit SerialIter(std::uint8_t const (&data)[N]) : SerialIter(&data[0], N)
    {
        static_assert(N > 0, "");
    }

    std::size_t
    empty() const noexcept
    {
        return remain_ == 0;
    }

    std::size_t
    getBytesLeft() const noexcept
    {
        return remain_;
    }

    // get functions throw on error
    unsigned char
    get8();

    std::uint16_t
    get16();

    std::uint32_t
    get32();
    std::int32_t
    geti32();

    std::uint64_t
    get64();
    std::int64_t
    geti64();

    template <std::size_t Bits, class Tag = void>
    base_uint<Bits, Tag>
    getBitString()
    {
        auto constexpr N = base_uint<Bits, Tag>::bytes;

        if (remain_ < N)
            Throw<std::runtime_error>("invalid SerialIter getBitString");

        auto const x = p_;

        p_ += N;
        remain_ -= N;

        return base_uint<Bits, Tag>(std::span<uint8_t const, N>{x, N});
    }

    uint128
    get128()
    {
        return getBitString<128>();
    }

    uint160
    get160()
    {
        return getBitString<160>();
    }

    uint192
    get192()
    {
        return getBitString<192>();
    }

    uint256
    get256()
    {
        return getBitString<256>();
    }

    void
    getFieldID(int& type, int& name);

    Slice
    getSlice(std::size_t bytes);

    Slice
    getVL();
};

}  // namespace ripple

#endif
