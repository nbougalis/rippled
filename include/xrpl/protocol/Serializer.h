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

#include <array>
#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/endian/conversion.hpp>

namespace ripple {

namespace detail {

/** Constants for the XRPL variable-length (VL) encoding.

    The length of a variable-length field is encoded in one to three
    bytes; the value of the first ("lead") byte determines how many
    additional bytes follow, and the encoded length is:

        lead in [  0, 192]: 1 byte;  length = lead
        lead in [193, 240]: 2 bytes; length = 193 +
                                       ((lead - 193) << 8) + b2
        lead in [241, 254]: 3 bytes; length = 12481 +
                                       ((lead - 241) << 16) +
                                       (b2 << 8) + b3
        lead == 255:        reserved

    Note: The maximum allowed size for a VL encoded field is 929,984 bytes
          but, in what appears to have been an arithmetic error that dates
          back to the original implementation, the encoder would fail when
          a field was longer than 918,744 bytes.

          This implementation fixes the original arithmetic mistake, so it
          now correctly encodes fields up to the maximum supported length.
*/
inline constexpr std::size_t vlLead2 = 193;
inline constexpr std::size_t vlLead3 = 241;
inline constexpr std::size_t vlLeadReserved = 255;

inline constexpr std::size_t vlMax1 = vlLead2 - 1;
inline constexpr std::size_t vlMax2 = vlMax1 + ((vlLead3 - vlLead2) << 8);
inline constexpr std::size_t vlMax3 =
    vlMax2 + ((vlLeadReserved - vlLead3) << 16);

static_assert(vlMax1 == 192 && vlMax2 == 12480 && vlMax3 == 929984);

/** An integer type whose serialized representation is unambiguous.

    This intentionally excludes bool, and the character types whose
    signedness is implementation-defined, and would determine, in a
    platform-dependent way, whether a widened value is sign-extended.
*/
template <class T>
concept serializable_integer = std::integral<T> && !std::same_as<T, bool> &&
    !std::same_as<T, char> && !std::same_as<T, wchar_t>;

/** A serializable integer that fits within the given number of bytes. */
template <class T, std::size_t Bytes>
concept sized_serializable_integer =
    serializable_integer<T> && (sizeof(T) <= Bytes);

}  // namespace detail

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

    /** Append an integer to the buffer in the canonical XRPL encoding.

        The integer is serialized in network byte order (i.e. with the most
        significant byte first), regardless of the endianness of the system
        that we are running on, and occupies exactly sizeof(T) bytes.

        Signed values are serialized in their two's complement representation.

        We explicitly reject parameters of type `char` because the signedness
        of `char` is implementation defined.

        @tparam T An integral type.
        @param v The value to append.

        @note The buffer grows by sizeof(T) bytes; any iterators or slices
              referring to the buffer may be invalidated.
     */
    template <detail::serializable_integer T>
        requires(sizeof(T) <= 8)
    void
    addInteger(T v)
    {
        std::array<unsigned char, sizeof(T)> buf{};
        boost::endian::endian_store<T, sizeof(T), boost::endian::order::big>(
            buf.data(), v);
        mData.insert(mData.end(), buf.begin(), buf.end());
    }

    /** @{ */
    /** Append explicitly-sized integers to the buffer.

        @note We explicitly reject parameters of type `char` because the
              signedness of `char` is implementation defined.
     */
    void
    add8(unsigned char i)
    {
        mData.push_back(i);
    }

    template <detail::sized_serializable_integer<2> T>
    void
    add16(T i)
    {
        if constexpr (std::signed_integral<T>)
            addInteger(static_cast<std::int16_t>(i));
        else
            addInteger(static_cast<std::uint16_t>(i));
    }

    template <detail::sized_serializable_integer<4> T>
    void
    add32(T i)
    {
        if constexpr (std::signed_integral<T>)
            addInteger(static_cast<std::int32_t>(i));
        else
            addInteger(static_cast<std::uint32_t>(i));
    }

    void
    add32(HashPrefix p)
    {
        // This should never trigger; the size & type of a hash prefix are
        // integral parts of the protocol and unlikely to ever change.
        static_assert(
            std::is_same_v<std::uint32_t, std::underlying_type_t<decltype(p)>>);

        addInteger(safe_cast<std::uint32_t>(p));
    }

    template <detail::sized_serializable_integer<8> T>
    void
    add64(T i)
    {
        if constexpr (std::signed_integral<T>)
            addInteger(static_cast<std::int64_t>(i));
        else
            addInteger(static_cast<std::uint64_t>(i));
    }
    /** @} */

    template <std::size_t Bits, class Tag>
    void
    addBitString(base_uint<Bits, Tag> const& v)
    {
        addRaw(makeSlice(v));
    }

    void
    addRaw(Slice slice)
    {
        mData.insert(mData.end(), slice.begin(), slice.end());
    }

    void
    addRaw(Blob const& vector)
    {
        addRaw(makeSlice(vector));
    }

    void
    addRaw(const Serializer& s)
    {
        addRaw(s.slice());
    }

    void
    addVL(Slice const& slice)
    {
        auto length = slice.size();

        if (length > detail::vlMax3) [[unlikely]]
            Throw<std::overflow_error>("Excessive length for VL field");

        if (length <= detail::vlMax1)
        {
            mData.push_back(static_cast<std::uint8_t>(length));
        }
        else if (length <= detail::vlMax2)
        {
            length -= detail::vlLead2;
            mData.push_back(
                static_cast<std::uint8_t>(detail::vlLead2 + (length >> 8)));
            mData.push_back(static_cast<std::uint8_t>(length & 0xff));
        }
        else
        {
            length -= detail::vlMax2 + 1;
            mData.push_back(
                static_cast<std::uint8_t>(detail::vlLead3 + (length >> 16)));
            mData.push_back(static_cast<std::uint8_t>((length >> 8) & 0xff));
            mData.push_back(static_cast<std::uint8_t>(length & 0xff));
        }

        addRaw(slice);
    }

    void
    addVL(Blob const& vector)
    {
        addVL(makeSlice(vector));
    }

    void
    addVL(const void* ptr, std::size_t len)
    {
        addVL(Slice{ptr, len});
    }

    void
    addFieldID(int type, int name)
    {
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
    }

    void
    addFieldID(SerializedTypeID type, int name)
    {
        addFieldID(safe_cast<int>(type), name);
    }

    // totality functions
    Blob
    getData() const
    {
        return mData;
    }

    // Temporary bridge until the Serializer stops owning a Blob
    Blob
    takeData() noexcept
    {
        return std::exchange(mData, Blob{});
    }

    int
    getDataLength() const noexcept
    {
        return unsafe_cast<int>(mData.size());
    }
    const void*
    getDataPtr() const noexcept
    {
        return mData.data();
    }
    void
    erase() noexcept
    {
        mData.clear();
    }

    // vector-like functions
    Blob::const_iterator
    begin() const noexcept
    {
        return mData.cbegin();
    }

    Blob::const_iterator
    end() const noexcept
    {
        return mData.cend();
    }

    Blob::const_iterator
    cbegin() const noexcept
    {
        return mData.cbegin();
    }

    Blob::const_iterator
    cend() const noexcept
    {
        return mData.cend();
    }

    friend bool
    operator==(Serializer const& lhs, Serializer const& rhs) noexcept
    {
        return lhs.mData == rhs.mData;
    }

    friend bool
    operator==(Serializer const& lhs, Blob const& rhs) noexcept
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

    /** Read an integer from the buffer, decoded from big-endian.

        The integer is deserialized from network byte order (most
        significant byte first), regardless of the endianness of the
        host platform, consuming exactly sizeof(T) bytes.

        @tparam T An integral type.
        @return The decoded value.

        @throws std::runtime_error if fewer than sizeof(T) bytes remain.
     */
    template <std::integral T>
        requires(sizeof(T) <= 8) && (!std::same_as<T, bool>)
    T
    readInteger()
    {
        if (remain_ < sizeof(T)) [[unlikely]]
            Throw<std::runtime_error>("short buffer for integer read");

        auto const v =
            boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(
                p_);

        p_ += sizeof(T);
        remain_ -= sizeof(T);

        return v;
    }

public:
    SerialIter(void const* data, std::size_t size) noexcept
        : p_(reinterpret_cast<std::uint8_t const*>(data)), remain_(size)
    {
    }

    SerialIter(Slice const& slice) noexcept
        : SerialIter(slice.data(), slice.size())
    {
    }

    // Infer the size of the data based on the size of the passed array.
    template <std::size_t N>
    explicit SerialIter(std::uint8_t const (&data)[N]) noexcept
        : SerialIter(&data[0], N)
    {
        static_assert(N > 0, "");
    }

    bool
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
    get8()
    {
        if (remain_ < 1) [[unlikely]]
            Throw<std::runtime_error>("short buffer for byte read");
        unsigned char t = *p_;
        ++p_;
        --remain_;
        return t;
    }

    std::uint16_t
    get16()
    {
        return readInteger<std::uint16_t>();
    }

    std::uint32_t
    get32()
    {
        return readInteger<std::uint32_t>();
    }

    std::int32_t
    geti32()
    {
        return readInteger<std::int32_t>();
    }

    std::uint64_t
    get64()
    {
        return readInteger<std::uint64_t>();
    }

    std::int64_t
    geti64()
    {
        return readInteger<std::int64_t>();
    }

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
    getFieldID(int& type, int& name)
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
    getSlice(std::size_t bytes)
    {
        if (bytes > remain_)
            Throw<std::runtime_error>("invalid SerialIter getSlice");
        Slice s(p_, bytes);
        p_ += bytes;
        remain_ -= bytes;
        return s;
    }

    Slice
    getVL()
    {
        return getSlice([this]() {
            std::size_t const b1 = get8();

            if (b1 <= detail::vlMax1)
                return b1;

            if (b1 < detail::vlLead3)
            {
                std::size_t const b2 = get8();

                return detail::vlLead2 + ((b1 - detail::vlLead2) << 8) + b2;
            }

            if (b1 < detail::vlLeadReserved)
            {
                std::size_t const b2 = get8();
                std::size_t const b3 = get8();

                return (detail::vlMax2 + 1) + ((b1 - detail::vlLead3) << 16) +
                    (b2 << 8) + b3;
            }

            Throw<std::invalid_argument>("incorrect vl encoding");
        }());
    }
};

}  // namespace ripple

#endif
