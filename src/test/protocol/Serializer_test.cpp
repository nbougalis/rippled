//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace ripple {

struct Serializer_test : public beast::unit_test::suite
{
    /** Returns the hex representation of everything in the serializer. */
    static std::string
    hex(Serializer const& s)
    {
        return strHex(s.slice());
    }

    /** Returns the hex representation of the first n bytes. */
    static std::string
    hex(Serializer const& s, std::size_t n)
    {
        return strHex(s.slice().substr(0, n));
    }

    void
    testFixedWidth()
    {
        testcase("Fixed width integers");

        {
            Serializer s;
            s.add8(0x12);
            BEAST_EXPECT(s.size() == 1);
            BEAST_EXPECT(hex(s) == "12");
        }

        {
            Serializer s;
            s.add16(std::uint16_t{0x1234});
            BEAST_EXPECT(s.size() == 2);
            BEAST_EXPECT(hex(s) == "1234");
        }

        {
            Serializer s;
            s.add32(std::uint32_t{0x12345678});
            BEAST_EXPECT(s.size() == 4);
            BEAST_EXPECT(hex(s) == "12345678");
        }

        {
            Serializer s;
            s.add64(std::uint64_t{0x123456789ABCDEF0});
            BEAST_EXPECT(s.size() == 8);
            BEAST_EXPECT(hex(s) == "123456789ABCDEF0");
        }

        {
            Serializer s;
            s.add32(HashPrefix::transactionID);
            BEAST_EXPECT(s.size() == 4);
            BEAST_EXPECT(
                hex(s) ==
                strHex(
                    Slice{
                        reinterpret_cast<std::uint8_t const*>(
                            "\x54\x58\x4E\x00"),
                        4}));
        }
    }

    void
    testWidening()
    {
        testcase("Widening preserves value and signedness");

        // A narrower argument is widened to the requested width; the
        // signedness of the argument determines whether the value is
        // sign-extended or zero-extended.
        {
            Serializer s;
            s.add64(4891);  // int literal
            BEAST_EXPECT(hex(s) == "000000000000131B");
        }

        {
            Serializer s;
            s.add32(-7);  // int literal, two's complement
            BEAST_EXPECT(hex(s) == "FFFFFFF9");
        }

        {
            Serializer s;
            s.add32(std::int32_t{-2});
            BEAST_EXPECT(hex(s) == "FFFFFFFE");
        }

        {
            Serializer s;
            s.add64(std::int8_t{-7});  // sign-extended
            BEAST_EXPECT(hex(s) == "FFFFFFFFFFFFFFF9");
        }

        {
            Serializer s;
            s.add64(std::uint8_t{249});  // zero-extended
            BEAST_EXPECT(hex(s) == "00000000000000F9");
        }

        {
            Serializer s;
            s.add16(std::int8_t{-2});
            BEAST_EXPECT(hex(s) == "FFFE");
        }
    }

    void
    testSignedRoundTrip()
    {
        testcase("Signed round trip");

        {
            std::initializer_list<std::int32_t> const values = {
                std::numeric_limits<std::int32_t>::min(),
                -1,
                0,
                1,
                std::numeric_limits<std::int32_t>::max()};

            for (std::int32_t value : values)
            {
                Serializer s;
                s.add32(value);
                BEAST_EXPECT(s.size() == 4);
                SerialIter sit(s.slice());
                BEAST_EXPECT(sit.geti32() == value);
                BEAST_EXPECT(sit.empty());
            }
        }

        {
            std::initializer_list<std::int64_t> const values = {
                std::numeric_limits<std::int64_t>::min(),
                -1,
                0,
                1,
                std::numeric_limits<std::int64_t>::max()};

            for (std::int64_t value : values)
            {
                Serializer s;
                s.add64(value);
                BEAST_EXPECT(s.size() == 8);
                SerialIter sit(s.slice());
                BEAST_EXPECT(sit.geti64() == value);
                BEAST_EXPECT(sit.empty());
            }
        }

        // The shape used by STNumber: a signed mantissa followed by a
        // signed exponent, read back with the signed accessors.
        {
            Serializer s;
            s.add64(std::int64_t{-1234567890123});
            s.add32(-6);

            SerialIter sit(s.slice());
            BEAST_EXPECT(sit.geti64() == -1234567890123);
            BEAST_EXPECT(sit.geti32() == -6);
            BEAST_EXPECT(sit.empty());
        }
    }

    void
    testUnsignedRoundTrip()
    {
        testcase("Unsigned round trip");

        Serializer s;
        s.add16(std::numeric_limits<std::uint16_t>::min());
        s.add16(std::numeric_limits<std::uint16_t>::max());
        s.add32(std::numeric_limits<std::uint32_t>::min());
        s.add32(std::numeric_limits<std::uint32_t>::max());
        s.add64(std::numeric_limits<std::uint64_t>::min());
        s.add64(std::numeric_limits<std::uint64_t>::max());

        SerialIter sit(s.slice());
        BEAST_EXPECT(sit.get16() == std::numeric_limits<std::uint16_t>::min());
        BEAST_EXPECT(sit.get16() == std::numeric_limits<std::uint16_t>::max());
        BEAST_EXPECT(sit.get32() == std::numeric_limits<std::uint32_t>::min());
        BEAST_EXPECT(sit.get32() == std::numeric_limits<std::uint32_t>::max());
        BEAST_EXPECT(sit.get64() == std::numeric_limits<std::uint64_t>::min());
        BEAST_EXPECT(sit.get64() == std::numeric_limits<std::uint64_t>::max());
        BEAST_EXPECT(sit.empty());
    }

    void
    testVLEncoding()
    {
        testcase("Variable length encoding");

        // Every boundary of the three encoding ranges, including the
        // values around the historical (incorrect) 918,744 byte ceiling.
        std::initializer_list<std::size_t> const sizes = {
            0,
            1,
            detail::vlMax1,      // 192, last 1-byte prefix
            detail::vlMax1 + 1,  // 193, first 2-byte prefix
            detail::vlMax2,      // 12480, last 2-byte prefix
            detail::vlMax2 + 1,  // 12481, first 3-byte prefix
            918744,              // the old encoder's ceiling
            918745,              // the first newly encodable length
            detail::vlMax3};     // 929984, the true maximum

        for (std::size_t size : sizes)
        {
            Blob const payload(size, 0xAB);

            Serializer s;
            s.addVL(payload);

            SerialIter sit(s.slice());
            Slice const got = sit.getVL();

            BEAST_EXPECT(got.size() == size);
            BEAST_EXPECT(sit.empty());

            if (size != 0)
                BEAST_EXPECT(got[0] == 0xAB && got[size - 1] == 0xAB);
        }
    }

    void
    testVLPrefixBytes()
    {
        testcase("Variable length prefix bytes");

        // Pin the exact prefix encoding at each boundary, so that a
        // change to the arithmetic cannot silently alter the wire format.
        auto const prefix = [](std::size_t size, std::size_t prefixLen) {
            Blob const payload(size, 0);
            Serializer s;
            s.addVL(payload);
            return hex(s, prefixLen);
        };

        BEAST_EXPECT(prefix(0, 1) == "00");
        BEAST_EXPECT(prefix(detail::vlMax1, 1) == "C0");
        BEAST_EXPECT(prefix(detail::vlMax1 + 1, 2) == "C100");
        BEAST_EXPECT(prefix(detail::vlMax2, 2) == "F0FF");
        BEAST_EXPECT(prefix(detail::vlMax2 + 1, 3) == "F10000");
        BEAST_EXPECT(prefix(detail::vlMax3, 3) == "FEFFFF");

        // The encoding of a length the original implementation could
        // handle is unchanged.
        BEAST_EXPECT(prefix(918744, 3) == "FED417");
    }

    void
    testVLErrors()
    {
        testcase("Variable length errors");

        // An overlong field is rejected, and the serializer is left
        // exactly as it was.
        {
            Serializer s;
            s.add32(std::uint32_t{0xDEADBEEF});

            Blob const payload(detail::vlMax3 + 1, 0);

            try
            {
                s.addVL(payload);
                fail("An exception should have been thrown");
            }
            catch (std::overflow_error const&)
            {
                pass();
            }

            BEAST_EXPECT(hex(s) == "DEADBEEF");
        }

        // A lead byte of 255 is reserved and must be rejected.
        {
            std::uint8_t const data[] = {0xFF, 0x00, 0x00};
            SerialIter sit(data);

            try
            {
                sit.getVL();
                fail("An exception should have been thrown");
            }
            catch (std::invalid_argument const&)
            {
                pass();
            }
        }

        // A prefix announcing more data than remains is rejected.
        {
            std::uint8_t const data[] = {0x04, 0x01, 0x02};
            SerialIter sit(data);

            try
            {
                sit.getVL();
                fail("An exception should have been thrown");
            }
            catch (std::runtime_error const&)
            {
                pass();
            }
        }
    }

    void
    testShortBuffer()
    {
        testcase("Short buffer");

        std::uint8_t const data[] = {0x01};

        {
            SerialIter sit(data);
            try
            {
                sit.get16();
                fail("An exception should have been thrown");
            }
            catch (std::runtime_error const&)
            {
                pass();
            }
        }

        {
            SerialIter sit(data);
            try
            {
                sit.get32();
                fail("An exception should have been thrown");
            }
            catch (std::runtime_error const&)
            {
                pass();
            }
        }

        {
            SerialIter sit(data);
            try
            {
                sit.get64();
                fail("An exception should have been thrown");
            }
            catch (std::runtime_error const&)
            {
                pass();
            }
        }

        {
            SerialIter sit(data);
            BEAST_EXPECT(sit.get8() == 0x01);
            BEAST_EXPECT(sit.empty());

            try
            {
                sit.get8();
                fail("An exception should have been thrown");
            }
            catch (std::runtime_error const&)
            {
                pass();
            }
        }
    }

    void
    testFieldID()
    {
        testcase("Field identifiers");

        // Exercises all four encodings: common/uncommon type against
        // common/uncommon name.
        struct
        {
            int type;
            int name;
        } const cases[] = {
            {1, 2}, {1, 200}, {200, 2}, {200, 201}, {15, 15}, {16, 16}};

        for (auto const& c : cases)
        {
            Serializer s;
            s.addFieldID(c.type, c.name);

            SerialIter sit(s.slice());

            int type = 0;
            int name = 0;
            sit.getFieldID(type, name);

            BEAST_EXPECT(type == c.type);
            BEAST_EXPECT(name == c.name);
            BEAST_EXPECT(sit.empty());
        }
    }

    void
    testTakeData()
    {
        testcase("Taking data");

        Serializer s;
        s.add32(std::uint32_t{0x01020304});
        BEAST_EXPECT(s.size() == 4);

        Blob const b = s.takeData();

        BEAST_EXPECT(b.size() == 4);
        BEAST_EXPECT(strHex(makeSlice(b)) == "01020304");

        // The serializer is left empty and usable.
        BEAST_EXPECT(s.size() == 0);
        BEAST_EXPECT(s.slice().empty());

        s.add32(std::uint32_t{0x05060708});
        BEAST_EXPECT(hex(s) == "05060708");
    }

    void
    run() override
    {
        testFixedWidth();
        testWidening();
        testSignedRoundTrip();
        testUnsignedRoundTrip();
        testVLEncoding();
        testVLPrefixBytes();
        testVLErrors();
        testShortBuffer();
        testFieldID();
        testTakeData();
    }
};

BEAST_DEFINE_TESTSUITE(Serializer, protocol, ripple);

}  // namespace ripple
