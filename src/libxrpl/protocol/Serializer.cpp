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

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/digest.h>
#include <cstdint>
#include <type_traits>

namespace ripple {

int
Serializer::add16(std::uint16_t i)
{
    int ret = mData.size();
    mData.push_back(static_cast<unsigned char>(i >> 8));
    mData.push_back(static_cast<unsigned char>(i & 0xff));
    return ret;
}

int
Serializer::add32(HashPrefix p)
{
    // This should never trigger; the size & type of a hash prefix are
    // integral parts of the protocol and unlikely to ever change.
    static_assert(
        std::is_same_v<std::uint32_t, std::underlying_type_t<decltype(p)>>);

    return add32(safe_cast<std::uint32_t>(p));
}

template <>
int
Serializer::addInteger(unsigned char i)
{
    return add8(i);
}
template <>
int
Serializer::addInteger(std::uint16_t i)
{
    return add16(i);
}
template <>
int
Serializer::addInteger(std::uint32_t i)
{
    return add32(i);
}
template <>
int
Serializer::addInteger(std::uint64_t i)
{
    return add64(i);
}

int
Serializer::addFieldID(int type, int name)
{
    int ret = mData.size();
    XRPL_ASSERT(
        (type > 0) && (type < 256) && (name > 0) && (name < 256),
        "ripple::Serializer::addFieldID : inputs inside range");

    if (type < 16)
    {
        if (name < 16)  // common type, common name
            mData.push_back(static_cast<unsigned char>((type << 4) | name));
        else
        {
            // common type, uncommon name
            mData.push_back(static_cast<unsigned char>(type << 4));
            mData.push_back(static_cast<unsigned char>(name));
        }
    }
    else if (name < 16)
    {
        // uncommon type, common name
        mData.push_back(static_cast<unsigned char>(name));
        mData.push_back(static_cast<unsigned char>(type));
    }
    else
    {
        // uncommon type, uncommon name
        mData.push_back(static_cast<unsigned char>(0));
        mData.push_back(static_cast<unsigned char>(type));
        mData.push_back(static_cast<unsigned char>(name));
    }

    return ret;
}

int
Serializer::add8(unsigned char byte)
{
    int ret = mData.size();
    mData.push_back(byte);
    return ret;
}

uint256
Serializer::getSHA512Half() const
{
    return sha512Half(makeSlice(mData));
}

int
Serializer::addEncoded(int length)
{
    boost::container::static_vector<std::uint8_t, 4> bytes;

    if (length <= 192)
    {
        bytes.push_back(static_cast<std::uint8_t>(length));
    }
    else if (length <= 12480)
    {
        length -= 193;
        bytes.push_back(193 + static_cast<std::uint8_t>(length >> 8));
        bytes.push_back(static_cast<std::uint8_t>(length & 0xff));
    }
    else if (length <= 918744)
    {
        length -= 12481;
        bytes.push_back(241 + static_cast<std::uint8_t>(length >> 16));
        bytes.push_back(static_cast<std::uint8_t>((length >> 8) & 0xff));
        bytes.push_back(static_cast<std::uint8_t>(length & 0xff));
    }
    else
        Throw<std::overflow_error>("lenlen");

    return addRaw(makeSlice(bytes));
}

//------------------------------------------------------------------------------

unsigned char
SerialIter::get8()
{
    if (remain_ < 1)
        Throw<std::runtime_error>("invalid SerialIter get8");
    unsigned char t = *p_;
    ++p_;
    --remain_;
    return t;
}

std::uint16_t
SerialIter::get16()
{
    if (remain_ < 2)
        Throw<std::runtime_error>("invalid SerialIter get16");
    auto t = p_;
    p_ += 2;
    remain_ -= 2;
    return (std::uint64_t(t[0]) << 8) + std::uint64_t(t[1]);
}

std::uint32_t
SerialIter::get32()
{
    if (remain_ < 4)
        Throw<std::runtime_error>("invalid SerialIter get32");
    auto t = p_;
    p_ += 4;
    remain_ -= 4;
    return (std::uint64_t(t[0]) << 24) + (std::uint64_t(t[1]) << 16) +
        (std::uint64_t(t[2]) << 8) + std::uint64_t(t[3]);
}

std::uint64_t
SerialIter::get64()
{
    if (remain_ < 8)
        Throw<std::runtime_error>("invalid SerialIter get64");
    auto t = p_;
    p_ += 8;
    remain_ -= 8;
    return (std::uint64_t(t[0]) << 56) + (std::uint64_t(t[1]) << 48) +
        (std::uint64_t(t[2]) << 40) + (std::uint64_t(t[3]) << 32) +
        (std::uint64_t(t[4]) << 24) + (std::uint64_t(t[5]) << 16) +
        (std::uint64_t(t[6]) << 8) + std::uint64_t(t[7]);
}

std::int32_t
SerialIter::geti32()
{
    if (remain_ < 4)
        Throw<std::runtime_error>("invalid SerialIter geti32");
    auto t = p_;
    p_ += 4;
    remain_ -= 4;
    return boost::endian::load_big_s32(t);
}

std::int64_t
SerialIter::geti64()
{
    if (remain_ < 8)
        Throw<std::runtime_error>("invalid SerialIter geti64");
    auto t = p_;
    p_ += 8;
    remain_ -= 8;
    return boost::endian::load_big_s64(t);
}

void
SerialIter::getFieldID(int& type, int& name)
{
    type = get8();
    name = type & 15;
    type >>= 4;

    if (type == 0)
    {
        // uncommon type
        type = get8();
        if (type < 16)
            Throw<std::runtime_error>(
                "gFID: uncommon type out of range " + std::to_string(type));
    }

    if (name == 0)
    {
        // uncommon name
        name = get8();
        if (name < 16)
            Throw<std::runtime_error>(
                "gFID: uncommon name out of range " + std::to_string(name));
    }
}

Slice
SerialIter::getSlice(std::size_t bytes)
{
    if (bytes > remain_)
        Throw<std::runtime_error>("invalid SerialIter getSlice");
    Slice s(p_, bytes);
    p_ += bytes;
    remain_ -= bytes;
    return s;
}

Slice
SerialIter::getVL()
{
    return getSlice([this]() {
        std::size_t const b1 = get8();

        if (b1 <= 192)
            return b1;

        if (b1 <= 240)
        {
            std::size_t const b2 = get8();

            return 193 + (b1 - 193) * 256 + b2;
        }

        if (b1 <= 254)
        {
            std::size_t const b2 = get8();
            std::size_t const b3 = get8();

            return 12481 + (b1 - 241) * 65536 + b2 * 256 + b3;
        }

        Throw<std::invalid_argument>("incorrect vl encoding");
    }());
}

}  // namespace ripple
