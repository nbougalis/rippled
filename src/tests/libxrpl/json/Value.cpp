#include <xrpl/basics/Number.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace xrpl {

namespace {

[[nodiscard]] json::Value
testCopy(json::ValueType typ)
{
    json::Value val{typ};
    json::Value const cpy{val};
    EXPECT_EQ(val.type(), typ);
    EXPECT_EQ(cpy.type(), typ);
    return val;
}

constexpr std::uint32_t kMaxUInt = std::numeric_limits<std::uint32_t>::max();
constexpr std::int32_t kMaxInt = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t kMinInt = std::numeric_limits<std::int32_t>::min();
constexpr std::uint64_t kUIntOverflow = std::uint64_t(kMaxUInt) + 1;

}  // namespace

TEST(JsonValue, limits)
{
    using namespace json;
    static_assert(Value::kMinInt == Int(~(UInt(-1) / 2)));
    static_assert(Value::kMaxInt == Int(UInt(-1) / 2));
    static_assert(Value::kMaxUInt == UInt(-1));
}

TEST(JsonValue, type_null)
{
    json::Value const nullV{testCopy(json::ValueType::Null)};
    EXPECT_TRUE(nullV.isNull());
    EXPECT_FALSE(nullV.isBool());
    EXPECT_FALSE(nullV.isInt());
    EXPECT_FALSE(nullV.isUInt());
    EXPECT_FALSE(nullV.isIntegral());
    EXPECT_FALSE(nullV.isDouble());
    EXPECT_FALSE(nullV.isNumeric());
    EXPECT_FALSE(nullV.isString());
    EXPECT_FALSE(nullV.isArray());
    EXPECT_TRUE(nullV.isArrayOrNull());
    EXPECT_FALSE(nullV.isObject());
    EXPECT_TRUE(nullV.isObjectOrNull());
}

TEST(JsonValue, type_int)
{
    json::Value const intV{testCopy(json::ValueType::Int)};
    EXPECT_FALSE(intV.isNull());
    EXPECT_FALSE(intV.isBool());
    EXPECT_TRUE(intV.isInt());
    EXPECT_FALSE(intV.isUInt());
    EXPECT_TRUE(intV.isIntegral());
    EXPECT_FALSE(intV.isDouble());
    EXPECT_TRUE(intV.isNumeric());
    EXPECT_FALSE(intV.isString());
    EXPECT_FALSE(intV.isArray());
    EXPECT_FALSE(intV.isArrayOrNull());
    EXPECT_FALSE(intV.isObject());
    EXPECT_FALSE(intV.isObjectOrNull());
}

TEST(JsonValue, type_uint)
{
    json::Value const uintV{testCopy(json::ValueType::UInt)};
    EXPECT_FALSE(uintV.isNull());
    EXPECT_FALSE(uintV.isBool());
    EXPECT_FALSE(uintV.isInt());
    EXPECT_TRUE(uintV.isUInt());
    EXPECT_TRUE(uintV.isIntegral());
    EXPECT_FALSE(uintV.isDouble());
    EXPECT_TRUE(uintV.isNumeric());
    EXPECT_FALSE(uintV.isString());
    EXPECT_FALSE(uintV.isArray());
    EXPECT_FALSE(uintV.isArrayOrNull());
    EXPECT_FALSE(uintV.isObject());
    EXPECT_FALSE(uintV.isObjectOrNull());
}

TEST(JsonValue, type_real)
{
    json::Value const realV{testCopy(json::ValueType::Real)};
    EXPECT_FALSE(realV.isNull());
    EXPECT_FALSE(realV.isBool());
    EXPECT_FALSE(realV.isInt());
    EXPECT_FALSE(realV.isUInt());
    EXPECT_FALSE(realV.isIntegral());
    EXPECT_TRUE(realV.isDouble());
    EXPECT_TRUE(realV.isNumeric());
    EXPECT_FALSE(realV.isString());
    EXPECT_FALSE(realV.isArray());
    EXPECT_FALSE(realV.isArrayOrNull());
    EXPECT_FALSE(realV.isObject());
    EXPECT_FALSE(realV.isObjectOrNull());
}

TEST(JsonValue, type_string)
{
    json::Value const stringV{testCopy(json::ValueType::String)};
    EXPECT_FALSE(stringV.isNull());
    EXPECT_FALSE(stringV.isBool());
    EXPECT_FALSE(stringV.isInt());
    EXPECT_FALSE(stringV.isUInt());
    EXPECT_FALSE(stringV.isIntegral());
    EXPECT_FALSE(stringV.isDouble());
    EXPECT_FALSE(stringV.isNumeric());
    EXPECT_TRUE(stringV.isString());
    EXPECT_FALSE(stringV.isArray());
    EXPECT_FALSE(stringV.isArrayOrNull());
    EXPECT_FALSE(stringV.isObject());
    EXPECT_FALSE(stringV.isObjectOrNull());
}

TEST(JsonValue, type_static_string)
{
    static constexpr json::StaticString kStaticStr{"staticStr"};

    json::Value const staticStrV{kStaticStr};
    {
        json::Value const cpy{staticStrV};
        EXPECT_EQ(staticStrV.type(), json::ValueType::String);
        EXPECT_EQ(cpy.type(), json::ValueType::String);
    }
    EXPECT_FALSE(staticStrV.isNull());
    EXPECT_FALSE(staticStrV.isBool());
    EXPECT_FALSE(staticStrV.isInt());
    EXPECT_FALSE(staticStrV.isUInt());
    EXPECT_FALSE(staticStrV.isIntegral());
    EXPECT_FALSE(staticStrV.isDouble());
    EXPECT_FALSE(staticStrV.isNumeric());
    EXPECT_TRUE(staticStrV.isString());
    EXPECT_FALSE(staticStrV.isArray());
    EXPECT_FALSE(staticStrV.isArrayOrNull());
    EXPECT_FALSE(staticStrV.isObject());
    EXPECT_FALSE(staticStrV.isObjectOrNull());
}

TEST(JsonValue, type_bool)
{
    json::Value const boolV{testCopy(json::ValueType::Boolean)};
    EXPECT_FALSE(boolV.isNull());
    EXPECT_TRUE(boolV.isBool());
    EXPECT_FALSE(boolV.isInt());
    EXPECT_FALSE(boolV.isUInt());
    EXPECT_TRUE(boolV.isIntegral());
    EXPECT_FALSE(boolV.isDouble());
    EXPECT_TRUE(boolV.isNumeric());
    EXPECT_FALSE(boolV.isString());
    EXPECT_FALSE(boolV.isArray());
    EXPECT_FALSE(boolV.isArrayOrNull());
    EXPECT_FALSE(boolV.isObject());
    EXPECT_FALSE(boolV.isObjectOrNull());
}

TEST(JsonValue, type_array)
{
    json::Value const arrayV{testCopy(json::ValueType::Array)};
    EXPECT_FALSE(arrayV.isNull());
    EXPECT_FALSE(arrayV.isBool());
    EXPECT_FALSE(arrayV.isInt());
    EXPECT_FALSE(arrayV.isUInt());
    EXPECT_FALSE(arrayV.isIntegral());
    EXPECT_FALSE(arrayV.isDouble());
    EXPECT_FALSE(arrayV.isNumeric());
    EXPECT_FALSE(arrayV.isString());
    EXPECT_TRUE(arrayV.isArray());
    EXPECT_TRUE(arrayV.isArrayOrNull());
    EXPECT_FALSE(arrayV.isObject());
    EXPECT_FALSE(arrayV.isObjectOrNull());
}

TEST(JsonValue, type_object)
{
    json::Value const objectV{testCopy(json::ValueType::Object)};
    EXPECT_FALSE(objectV.isNull());
    EXPECT_FALSE(objectV.isBool());
    EXPECT_FALSE(objectV.isInt());
    EXPECT_FALSE(objectV.isUInt());
    EXPECT_FALSE(objectV.isIntegral());
    EXPECT_FALSE(objectV.isDouble());
    EXPECT_FALSE(objectV.isNumeric());
    EXPECT_FALSE(objectV.isString());
    EXPECT_FALSE(objectV.isArray());
    EXPECT_FALSE(objectV.isArrayOrNull());
    EXPECT_TRUE(objectV.isObject());
    EXPECT_TRUE(objectV.isObjectOrNull());
}

TEST(JsonValue, compare_strings)
{
    auto const doCompare = [&](json::Value const& lhs,
                               json::Value const& rhs,
                               bool lhsEqRhs,
                               bool lhsLtRhs,
                               int line) {
        SCOPED_TRACE(line);
        EXPECT_EQ((lhs == rhs), lhsEqRhs);
        EXPECT_NE((lhs != rhs), lhsEqRhs);
        EXPECT_EQ((lhs < rhs), (!lhsEqRhs && lhsLtRhs));
        EXPECT_EQ((lhs <= rhs), (lhsEqRhs || lhsLtRhs));
        EXPECT_EQ((lhs >= rhs), (lhsEqRhs || !lhsLtRhs));
        EXPECT_EQ((lhs > rhs), (!lhsEqRhs && !lhsLtRhs));
    };

    json::Value const null0;
    json::Value const intNeg1{-1};
    json::Value const int0{json::ValueType::Int};
    json::Value const intPos1{1};
    json::Value const uint0{json::ValueType::UInt};
    json::Value const uint1{1u};
    json::Value const realNeg1{-1.0};
    json::Value const real0{json::ValueType::Real};
    json::Value const realPos1{1.0};
    json::Value const str0{json::ValueType::String};
    json::Value const str1{"1"};
    json::Value const boolF{false};
    json::Value const boolT{true};
    json::Value const array0{json::ValueType::Array};
    json::Value const array1{
        [] {
            json::Value array1;
            array1[0u] = 1;
            return array1;
        }(),
    };
    json::Value const obj0{json::ValueType::Object};
    json::Value const obj1{
        [] {
            json::Value obj1;
            obj1["one"] = 1;
            return obj1;
        }(),
    };

#pragma push_macro("DO_COMPARE")
    // DO_COMPARE(lhs, rhs, lhsEqualsToRhs lhsLessThanRhs)
#define DO_COMPARE(lhs, rhs, eq, lt) doCompare(lhs, rhs, eq, lt, __LINE__)
    DO_COMPARE(null0, json::Value{}, true, false);
    DO_COMPARE(null0, intNeg1, false, true);
    DO_COMPARE(null0, int0, false, true);
    DO_COMPARE(null0, intPos1, false, true);
    DO_COMPARE(null0, uint0, false, true);
    DO_COMPARE(null0, uint1, false, true);
    DO_COMPARE(null0, realNeg1, false, true);
    DO_COMPARE(null0, real0, false, true);
    DO_COMPARE(null0, realPos1, false, true);
    DO_COMPARE(null0, str0, false, true);
    DO_COMPARE(null0, str1, false, true);
    DO_COMPARE(null0, boolF, false, true);
    DO_COMPARE(null0, boolT, false, true);
    DO_COMPARE(null0, array0, false, true);
    DO_COMPARE(null0, array1, false, true);
    DO_COMPARE(null0, obj0, false, true);
    DO_COMPARE(null0, obj1, false, true);

    DO_COMPARE(intNeg1, null0, false, false);
    DO_COMPARE(intNeg1, intNeg1, true, false);
    DO_COMPARE(intNeg1, int0, false, true);
    DO_COMPARE(intNeg1, intPos1, false, true);
    DO_COMPARE(intNeg1, uint0, false, true);
    DO_COMPARE(intNeg1, uint1, false, true);
    DO_COMPARE(intNeg1, realNeg1, false, true);
    DO_COMPARE(intNeg1, real0, false, true);
    DO_COMPARE(intNeg1, realPos1, false, true);
    DO_COMPARE(intNeg1, str0, false, true);
    DO_COMPARE(intNeg1, str1, false, true);
    DO_COMPARE(intNeg1, boolF, false, true);
    DO_COMPARE(intNeg1, boolT, false, true);
    DO_COMPARE(intNeg1, array0, false, true);
    DO_COMPARE(intNeg1, array1, false, true);
    DO_COMPARE(intNeg1, obj0, false, true);
    DO_COMPARE(intNeg1, obj1, false, true);

    DO_COMPARE(int0, null0, false, false);
    DO_COMPARE(int0, intNeg1, false, false);
    DO_COMPARE(int0, int0, true, false);
    DO_COMPARE(int0, intPos1, false, true);
    DO_COMPARE(int0, uint0, true, false);
    DO_COMPARE(int0, uint1, false, true);
    DO_COMPARE(int0, realNeg1, false, true);
    DO_COMPARE(int0, real0, false, true);
    DO_COMPARE(int0, realPos1, false, true);
    DO_COMPARE(int0, str0, false, true);
    DO_COMPARE(int0, str1, false, true);
    DO_COMPARE(int0, boolF, false, true);
    DO_COMPARE(int0, boolT, false, true);
    DO_COMPARE(int0, array0, false, true);
    DO_COMPARE(int0, array1, false, true);
    DO_COMPARE(int0, obj0, false, true);
    DO_COMPARE(int0, obj1, false, true);

    DO_COMPARE(intPos1, null0, false, false);
    DO_COMPARE(intPos1, intNeg1, false, false);
    DO_COMPARE(intPos1, int0, false, false);
    DO_COMPARE(intPos1, intPos1, true, false);
    DO_COMPARE(intPos1, uint0, false, false);
    DO_COMPARE(intPos1, uint1, true, false);
    DO_COMPARE(intPos1, realNeg1, false, true);
    DO_COMPARE(intPos1, real0, false, true);
    DO_COMPARE(intPos1, realPos1, false, true);
    DO_COMPARE(intPos1, str0, false, true);
    DO_COMPARE(intPos1, str1, false, true);
    DO_COMPARE(intPos1, boolF, false, true);
    DO_COMPARE(intPos1, boolT, false, true);
    DO_COMPARE(intPos1, array0, false, true);
    DO_COMPARE(intPos1, array1, false, true);
    DO_COMPARE(intPos1, obj0, false, true);
    DO_COMPARE(intPos1, obj1, false, true);

    DO_COMPARE(uint0, null0, false, false);
    DO_COMPARE(uint0, intNeg1, false, false);
    DO_COMPARE(uint0, int0, true, false);
    DO_COMPARE(uint0, intPos1, false, true);
    DO_COMPARE(uint0, uint0, true, false);
    DO_COMPARE(uint0, uint1, false, true);
    DO_COMPARE(uint0, realNeg1, false, true);
    DO_COMPARE(uint0, real0, false, true);
    DO_COMPARE(uint0, realPos1, false, true);
    DO_COMPARE(uint0, str0, false, true);
    DO_COMPARE(uint0, str1, false, true);
    DO_COMPARE(uint0, boolF, false, true);
    DO_COMPARE(uint0, boolT, false, true);
    DO_COMPARE(uint0, array0, false, true);
    DO_COMPARE(uint0, array1, false, true);
    DO_COMPARE(uint0, obj0, false, true);
    DO_COMPARE(uint0, obj1, false, true);

    DO_COMPARE(uint1, null0, false, false);
    DO_COMPARE(uint1, intNeg1, false, false);
    DO_COMPARE(uint1, int0, false, false);
    DO_COMPARE(uint1, intPos1, true, false);
    DO_COMPARE(uint1, uint0, false, false);
    DO_COMPARE(uint1, uint1, true, false);
    DO_COMPARE(uint1, realNeg1, false, true);
    DO_COMPARE(uint1, real0, false, true);
    DO_COMPARE(uint1, realPos1, false, true);
    DO_COMPARE(uint1, str0, false, true);
    DO_COMPARE(uint1, str1, false, true);
    DO_COMPARE(uint1, boolF, false, true);
    DO_COMPARE(uint1, boolT, false, true);
    DO_COMPARE(uint1, array0, false, true);
    DO_COMPARE(uint1, array1, false, true);
    DO_COMPARE(uint1, obj0, false, true);
    DO_COMPARE(uint1, obj1, false, true);

    DO_COMPARE(realNeg1, null0, false, false);
    DO_COMPARE(realNeg1, intNeg1, false, false);
    DO_COMPARE(realNeg1, int0, false, false);
    DO_COMPARE(realNeg1, intPos1, false, false);
    DO_COMPARE(realNeg1, uint0, false, false);
    DO_COMPARE(realNeg1, uint1, false, false);
    DO_COMPARE(realNeg1, realNeg1, true, false);
    DO_COMPARE(realNeg1, real0, false, true);
    DO_COMPARE(realNeg1, realPos1, false, true);
    DO_COMPARE(realNeg1, str0, false, true);
    DO_COMPARE(realNeg1, str1, false, true);
    DO_COMPARE(realNeg1, boolF, false, true);
    DO_COMPARE(realNeg1, boolT, false, true);
    DO_COMPARE(realNeg1, array0, false, true);
    DO_COMPARE(realNeg1, array1, false, true);
    DO_COMPARE(realNeg1, obj0, false, true);
    DO_COMPARE(realNeg1, obj1, false, true);

    DO_COMPARE(real0, null0, false, false);
    DO_COMPARE(real0, intNeg1, false, false);
    DO_COMPARE(real0, int0, false, false);
    DO_COMPARE(real0, intPos1, false, false);
    DO_COMPARE(real0, uint0, false, false);
    DO_COMPARE(real0, uint1, false, false);
    DO_COMPARE(real0, realNeg1, false, false);
    DO_COMPARE(real0, real0, true, false);
    DO_COMPARE(real0, realPos1, false, true);
    DO_COMPARE(real0, str0, false, true);
    DO_COMPARE(real0, str1, false, true);
    DO_COMPARE(real0, boolF, false, true);
    DO_COMPARE(real0, boolT, false, true);
    DO_COMPARE(real0, array0, false, true);
    DO_COMPARE(real0, array1, false, true);
    DO_COMPARE(real0, obj0, false, true);
    DO_COMPARE(real0, obj1, false, true);

    DO_COMPARE(realPos1, null0, false, false);
    DO_COMPARE(realPos1, intNeg1, false, false);
    DO_COMPARE(realPos1, int0, false, false);
    DO_COMPARE(realPos1, intPos1, false, false);
    DO_COMPARE(realPos1, uint0, false, false);
    DO_COMPARE(realPos1, uint1, false, false);
    DO_COMPARE(realPos1, realNeg1, false, false);
    DO_COMPARE(realPos1, real0, false, false);
    DO_COMPARE(realPos1, realPos1, true, false);
    DO_COMPARE(realPos1, str0, false, true);
    DO_COMPARE(realPos1, str1, false, true);
    DO_COMPARE(realPos1, boolF, false, true);
    DO_COMPARE(realPos1, boolT, false, true);
    DO_COMPARE(realPos1, array0, false, true);
    DO_COMPARE(realPos1, array1, false, true);
    DO_COMPARE(realPos1, obj0, false, true);
    DO_COMPARE(realPos1, obj1, false, true);

    DO_COMPARE(str0, null0, false, false);
    DO_COMPARE(str0, intNeg1, false, false);
    DO_COMPARE(str0, int0, false, false);
    DO_COMPARE(str0, intPos1, false, false);
    DO_COMPARE(str0, uint0, false, false);
    DO_COMPARE(str0, uint1, false, false);
    DO_COMPARE(str0, realNeg1, false, false);
    DO_COMPARE(str0, real0, false, false);
    DO_COMPARE(str0, realPos1, false, false);
    DO_COMPARE(str0, str0, true, false);
    DO_COMPARE(str0, str1, false, true);
    DO_COMPARE(str0, boolF, false, true);
    DO_COMPARE(str0, boolT, false, true);
    DO_COMPARE(str0, array0, false, true);
    DO_COMPARE(str0, array1, false, true);
    DO_COMPARE(str0, obj0, false, true);
    DO_COMPARE(str0, obj1, false, true);

    DO_COMPARE(str1, null0, false, false);
    DO_COMPARE(str1, intNeg1, false, false);
    DO_COMPARE(str1, int0, false, false);
    DO_COMPARE(str1, intPos1, false, false);
    DO_COMPARE(str1, uint0, false, false);
    DO_COMPARE(str1, uint1, false, false);
    DO_COMPARE(str1, realNeg1, false, false);
    DO_COMPARE(str1, real0, false, false);
    DO_COMPARE(str1, realPos1, false, false);
    DO_COMPARE(str1, str0, false, false);
    DO_COMPARE(str1, str1, true, false);
    DO_COMPARE(str1, boolF, false, true);
    DO_COMPARE(str1, boolT, false, true);
    DO_COMPARE(str1, array0, false, true);
    DO_COMPARE(str1, array1, false, true);
    DO_COMPARE(str1, obj0, false, true);
    DO_COMPARE(str1, obj1, false, true);

    DO_COMPARE(boolF, null0, false, false);
    DO_COMPARE(boolF, intNeg1, false, false);
    DO_COMPARE(boolF, int0, false, false);
    DO_COMPARE(boolF, intPos1, false, false);
    DO_COMPARE(boolF, uint0, false, false);
    DO_COMPARE(boolF, uint1, false, false);
    DO_COMPARE(boolF, realNeg1, false, false);
    DO_COMPARE(boolF, real0, false, false);
    DO_COMPARE(boolF, realPos1, false, false);
    DO_COMPARE(boolF, str0, false, false);
    DO_COMPARE(boolF, str1, false, false);
    DO_COMPARE(boolF, boolF, true, false);
    DO_COMPARE(boolF, boolT, false, true);
    DO_COMPARE(boolF, array0, false, true);
    DO_COMPARE(boolF, array1, false, true);
    DO_COMPARE(boolF, obj0, false, true);
    DO_COMPARE(boolF, obj1, false, true);

    DO_COMPARE(boolT, null0, false, false);
    DO_COMPARE(boolT, intNeg1, false, false);
    DO_COMPARE(boolT, int0, false, false);
    DO_COMPARE(boolT, intPos1, false, false);
    DO_COMPARE(boolT, uint0, false, false);
    DO_COMPARE(boolT, uint1, false, false);
    DO_COMPARE(boolT, realNeg1, false, false);
    DO_COMPARE(boolT, real0, false, false);
    DO_COMPARE(boolT, realPos1, false, false);
    DO_COMPARE(boolT, str0, false, false);
    DO_COMPARE(boolT, str1, false, false);
    DO_COMPARE(boolT, boolF, false, false);
    DO_COMPARE(boolT, boolT, true, false);
    DO_COMPARE(boolT, array0, false, true);
    DO_COMPARE(boolT, array1, false, true);
    DO_COMPARE(boolT, obj0, false, true);
    DO_COMPARE(boolT, obj1, false, true);

    DO_COMPARE(array0, null0, false, false);
    DO_COMPARE(array0, intNeg1, false, false);
    DO_COMPARE(array0, int0, false, false);
    DO_COMPARE(array0, intPos1, false, false);
    DO_COMPARE(array0, uint0, false, false);
    DO_COMPARE(array0, uint1, false, false);
    DO_COMPARE(array0, realNeg1, false, false);
    DO_COMPARE(array0, real0, false, false);
    DO_COMPARE(array0, realPos1, false, false);
    DO_COMPARE(array0, str0, false, false);
    DO_COMPARE(array0, str1, false, false);
    DO_COMPARE(array0, boolF, false, false);
    DO_COMPARE(array0, boolT, false, false);
    DO_COMPARE(array0, array0, true, false);
    DO_COMPARE(array0, array1, false, true);
    DO_COMPARE(array0, obj0, false, true);
    DO_COMPARE(array0, obj1, false, true);

    DO_COMPARE(array1, null0, false, false);
    DO_COMPARE(array1, intNeg1, false, false);
    DO_COMPARE(array1, int0, false, false);
    DO_COMPARE(array1, intPos1, false, false);
    DO_COMPARE(array1, uint0, false, false);
    DO_COMPARE(array1, uint1, false, false);
    DO_COMPARE(array1, realNeg1, false, false);
    DO_COMPARE(array1, real0, false, false);
    DO_COMPARE(array1, realPos1, false, false);
    DO_COMPARE(array1, str0, false, false);
    DO_COMPARE(array1, str1, false, false);
    DO_COMPARE(array1, boolF, false, false);
    DO_COMPARE(array1, boolT, false, false);
    DO_COMPARE(array1, array0, false, false);
    DO_COMPARE(array1, array1, true, false);
    DO_COMPARE(array1, obj0, false, true);
    DO_COMPARE(array1, obj1, false, true);

    DO_COMPARE(obj0, null0, false, false);
    DO_COMPARE(obj0, intNeg1, false, false);
    DO_COMPARE(obj0, int0, false, false);
    DO_COMPARE(obj0, intPos1, false, false);
    DO_COMPARE(obj0, uint0, false, false);
    DO_COMPARE(obj0, uint1, false, false);
    DO_COMPARE(obj0, realNeg1, false, false);
    DO_COMPARE(obj0, real0, false, false);
    DO_COMPARE(obj0, realPos1, false, false);
    DO_COMPARE(obj0, str0, false, false);
    DO_COMPARE(obj0, str1, false, false);
    DO_COMPARE(obj0, boolF, false, false);
    DO_COMPARE(obj0, boolT, false, false);
    DO_COMPARE(obj0, array0, false, false);
    DO_COMPARE(obj0, array1, false, false);
    DO_COMPARE(obj0, obj0, true, false);
    DO_COMPARE(obj0, obj1, false, true);

    DO_COMPARE(obj1, null0, false, false);
    DO_COMPARE(obj1, intNeg1, false, false);
    DO_COMPARE(obj1, int0, false, false);
    DO_COMPARE(obj1, intPos1, false, false);
    DO_COMPARE(obj1, uint0, false, false);
    DO_COMPARE(obj1, uint1, false, false);
    DO_COMPARE(obj1, realNeg1, false, false);
    DO_COMPARE(obj1, real0, false, false);
    DO_COMPARE(obj1, realPos1, false, false);
    DO_COMPARE(obj1, str0, false, false);
    DO_COMPARE(obj1, str1, false, false);
    DO_COMPARE(obj1, boolF, false, false);
    DO_COMPARE(obj1, boolT, false, false);
    DO_COMPARE(obj1, array0, false, false);
    DO_COMPARE(obj1, array1, false, false);
    DO_COMPARE(obj1, obj0, false, false);
    DO_COMPARE(obj1, obj1, true, false);
#undef DO_COMPARE
#pragma pop_macro("DO_COMPARE")
}

TEST(JsonValue, construct_from_string_view)
{
    using namespace std::string_view_literals;

    // The view need not end in a null character.
    EXPECT_EQ(json::Value{"2.0"sv}.asString(), "2.0");
    EXPECT_EQ(json::Value{"2.0 and more"sv.substr(0, 3)}.asString(), "2.0");
    EXPECT_EQ(json::Value{""sv}.asString(), "");

    // A default-constructed view has a null `data()` and yields an empty string value.
    std::string_view const empty;
    json::Value const fromEmpty{empty};
    EXPECT_TRUE(fromEmpty.isString());
    EXPECT_EQ(fromEmpty.asString(), "");
    EXPECT_EQ(fromEmpty, ""sv);

    // The characters are copied, not aliased: overwriting the owner leaves the value unchanged.
    std::string owner{"borrowed"};
    json::Value const copied{std::string_view{owner}};
    owner.assign("replaced");
    EXPECT_EQ(copied.asString(), "borrowed");

    // The length of the view is kept, so an embedded null character is part of the value.
    json::Value const embedded{"a\0b"sv};
    EXPECT_EQ(embedded.asStringView(), "a\0b"sv);
    EXPECT_EQ(embedded.asString(), std::string("a\0b", 3));
    EXPECT_EQ(embedded, "a\0b"sv);
    EXPECT_NE(embedded, "a"sv);
    EXPECT_EQ(embedded, json::Value{"a\0b"sv});
}

TEST(JsonValue, compare_string_view)
{
    using namespace std::string_view_literals;

    EXPECT_EQ(json::Value{"2.0"}, "2.0"sv);
    EXPECT_NE(json::Value{"2.0"}, "2.00"sv);
    EXPECT_NE(json::Value{"2.0"}, ""sv);

    // The comparison stops at the view's end, not at a null character.
    EXPECT_EQ(json::Value{"2.0"}, "2.0 and more"sv.substr(0, 3));

    EXPECT_EQ(json::Value{""}, ""sv);

    // A default-constructed string value is the empty string.
    EXPECT_EQ(json::Value{json::ValueType::String}, ""sv);
    EXPECT_NE(json::Value{json::ValueType::String}, "2.0"sv);
    EXPECT_EQ(json::Value{json::ValueType::String}, json::Value{""});

    // Nothing that is not a string equals one. Null is what an absent member reads as.
    EXPECT_NE(json::Value{json::ValueType::Null}, ""sv);
    EXPECT_NE(json::Value{json::ValueType::Null}, "2.0"sv);
    EXPECT_NE(json::Value{2}, "2"sv);
    EXPECT_NE(json::Value{true}, "true"sv);
    EXPECT_NE(json::Value{json::ValueType::Object}, ""sv);

    // A literal, a `char const*` and a `std::string` take the same path.
    EXPECT_EQ(json::Value{"2.0"}, "2.0");
    EXPECT_NE(json::Value{"2.0"}, "2.1");
    EXPECT_NE(json::Value{}, "2.0");
    EXPECT_EQ("2.0", json::Value{"2.0"});

    char const* const version = "2.0";
    EXPECT_EQ(json::Value{"2.0"}, version);
    EXPECT_NE(json::Value{"2.1"}, version);

    // A null `char const*` reads as empty text, as it does when it builds a Value.
    char const* const none = nullptr;
    EXPECT_EQ(json::Value{""}, none);
    EXPECT_EQ(json::Value{""}, json::Value{none});
    EXPECT_NE(json::Value{"2.0"}, none);
    EXPECT_EQ(json::Value{json::ValueType::String}, none);
    EXPECT_NE(json::Value{}, none);

    EXPECT_EQ(json::Value{"2.0"}, std::string{"2.0"});
    EXPECT_NE(json::Value{"2.0"}, std::string{"2.1"});
    EXPECT_NE(json::Value{json::ValueType::Null}, std::string{"2.0"});
    std::string const withNul{"a\0b", 3};
    EXPECT_EQ(json::Value{"a\0b"sv}, withNul);
    EXPECT_EQ(json::Value{"a\0b"sv}, json::Value{withNul});

    // A `StaticString` compares through Value.
    static constexpr json::StaticString kVersion{"2.0"};
    EXPECT_EQ(json::Value{"2.0"}, kVersion);
    EXPECT_NE(json::Value{"2.1"}, kVersion);
    EXPECT_NE(json::Value{}, kVersion);

    // A `nullptr` literal does not build a Value: Value(std::nullptr_t) is deleted.
    static_assert(!std::is_constructible_v<json::Value, std::nullptr_t>);

    // A literal `0` deduces as `int`, which no view accepts, so it compares as a number.
    // RPCCall.cpp rejects a `transaction_entry` ledger index this way.
    EXPECT_EQ(json::Value{0}, 0);
    EXPECT_NE(json::Value{4294967295u}, 0);
    EXPECT_NE(json::Value{""}, 0);
    EXPECT_NE(json::Value{2}, 0);

    // A null and an Int are different types, so the comparison is false.
    EXPECT_NE(json::Value{}, 0);
}

TEST(JsonValue, bool)
{
    EXPECT_FALSE(json::Value());

    EXPECT_FALSE(json::Value(""));

    EXPECT_TRUE(bool(json::Value("empty")));
    EXPECT_TRUE(bool(json::Value(false)));
    EXPECT_TRUE(bool(json::Value(true)));
    EXPECT_TRUE(bool(json::Value(0)));
    EXPECT_TRUE(bool(json::Value(1)));

    json::Value array(json::ValueType::Array);
    EXPECT_FALSE(array);
    array.append(0);
    EXPECT_TRUE(bool(array));

    json::Value object(json::ValueType::Object);
    EXPECT_FALSE(object);
    object[""] = false;
    EXPECT_TRUE(bool(object));
}

TEST(JsonValue, bad_json)
{
    EXPECT_FALSE(json::load("\"method\""));
    EXPECT_FALSE(json::load("1978"));
    EXPECT_FALSE(json::load("true"));
    EXPECT_FALSE(json::load("{1978]"));
    EXPECT_FALSE(json::load("[1,2,3]potato"));
    EXPECT_FALSE(json::load("[1,2,3],[4,5,6]"));
    EXPECT_FALSE(json::load(R"({"method":"ledger","params":[{"ledger_index":1e738}]})"));
    EXPECT_FALSE(json::load(R"({"method":"ledger","params":[{"ledger_index"}]})"));
    EXPECT_FALSE(json::load(R"({"method":"ledger","params":[{"ledger_index"]]})"));
    EXPECT_FALSE(json::load(R"({"method":ledger,"params":[{"ledger_index"]]})"));
    EXPECT_FALSE(json::load(R"({method:"ledger","params":[{"ledger_index"]]})"));
    EXPECT_FALSE(json::load(R"({method:"ledger","params":[{"ledger_index":9012345678]]})"));

    // A large but representable double is accepted.
    EXPECT_TRUE(json::load(R"({"method":"ledger","params":[{"ledger_index":1e300}]})"));
}

namespace {

[[nodiscard]] std::optional<json::Value>
parseValue(std::string const& doc)
{
    json::Value j;
    if (!json::load("{\"v\":" + doc + "}", j))
        return std::nullopt;
    return j["v"];
}

}  // namespace

TEST(JsonValue, parse_double_valid)
{
    // 1e300 is large but still representable, so it parses (unlike the out-of-range cases below).
    for (auto const& [text, expected] : {
             std::pair{"2.5", 2.5},
             std::pair{"-3.25e2", -325.0},
             std::pair{"0.0", 0.0},
             std::pair{"1E3", 1000.0},
             std::pair{"-0.5e-1", -0.05},
             std::pair{"1e300", 1e300},
         })
    {
        auto const v = parseValue(text);
        ASSERT_TRUE(v.has_value()) << text;
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        EXPECT_TRUE(v->isDouble()) << text;
        EXPECT_EQ(v->asDouble(), expected) << text;
        // NOLINTEND(bugprone-unchecked-optional-access)
    }
}

TEST(JsonValue, parse_double_out_of_range)
{
    // Magnitudes with no finite double representation are rejected.
    for (char const* oor : {"1e400", "-1e400", "0.001e500", "1e-400", "-1e-400", "123e-500"})
        EXPECT_FALSE(parseValue(oor).has_value()) << oor;
}

TEST(JsonValue, parse_double_malformed)
{
    // Each of these has a valid leading prefix that from_chars would accept
    // on its own; the lexer only accepts the RFC 8259 number grammar, so the
    // trailing characters are rejected instead of being silently dropped.
    for (char const* bad : {"1+2", "1-2", "1.2.3", "1e5e6", "1..2", "++5", "1e", "1e+", ".", "-"})
        EXPECT_FALSE(parseValue(bad).has_value()) << bad;
}

TEST(JsonValue, parses_integers_at_the_edges_of_the_32_bit_range)
{
    std::uint32_t const aUInt = kMaxUInt - 1978;
    std::int32_t const aLargeInt = kMaxInt - 1978;
    std::int32_t const aSmallInt = kMinInt + 1978;

    std::string const json = std::format(
        R"JSON({{
            "max_uint": {},
            "max_int": {},
            "min_int": {},
            "a_uint": {},
            "a_large_int": {},
            "a_small_int": {}
        }})JSON",
        kMaxUInt,
        kMaxInt,
        kMinInt,
        aUInt,
        aLargeInt,
        aSmallInt);

    json::Value j1;

    EXPECT_TRUE(json::load(json, j1));
    EXPECT_EQ(j1["max_uint"].asUInt(), kMaxUInt);
    EXPECT_EQ(j1["max_uint"].asAbsUInt(), kMaxUInt);
    EXPECT_EQ(j1["max_int"].asInt(), kMaxInt);
    EXPECT_EQ(j1["max_int"].asAbsUInt(), kMaxInt);
    EXPECT_EQ(j1["min_int"].asInt(), kMinInt);
    EXPECT_EQ(j1["min_int"].asAbsUInt(), static_cast<std::int64_t>(kMinInt) * -1);
    EXPECT_EQ(j1["a_uint"].asUInt(), aUInt);
    EXPECT_EQ(j1["a_uint"].asAbsUInt(), aUInt);
    EXPECT_GT(j1["a_uint"], aLargeInt);
    EXPECT_GT(j1["a_uint"], aSmallInt);
    EXPECT_EQ(j1["a_large_int"].asInt(), aLargeInt);
    EXPECT_EQ(j1["a_large_int"].asAbsUInt(), aLargeInt);
    EXPECT_EQ(j1["a_large_int"].asUInt(), aLargeInt);
    EXPECT_LT(j1["a_large_int"], aUInt);
    EXPECT_EQ(j1["a_small_int"].asInt(), aSmallInt);
    EXPECT_EQ(j1["a_small_int"].asAbsUInt(), static_cast<std::int64_t>(aSmallInt) * -1);
    EXPECT_LT(j1["a_small_int"], aUInt);
}

TEST(JsonValue, rejects_an_unsigned_value_one_past_the_maximum)
{
    std::string const json = std::format(R"JSON({{"overflow": {}}})JSON", kUIntOverflow);

    json::Value j2;

    EXPECT_FALSE(json::load(json, j2));
}

TEST(JsonValue, rejects_a_signed_value_one_below_the_minimum)
{
    std::int64_t const underflow = std::int64_t(kMinInt) - 1;

    std::string const json = std::format(R"JSON({{"underflow": {}}})JSON", underflow);

    json::Value j3;

    EXPECT_FALSE(json::load(json, j3));
}

TEST(JsonValue, converting_a_string_valued_json_number)
{
    json::Value intString{std::to_string(kUIntOverflow)};
    EXPECT_THROW([&] { return intString.asUInt(); }(), beast::BadLexicalCast);
    EXPECT_THROW([&] { return intString.asAbsUInt(); }(), json::Error);

    intString = "4294967295";
    EXPECT_EQ(intString.asUInt(), 4294967295u);
    EXPECT_EQ(intString.asAbsUInt(), 4294967295u);

    intString = "0";
    EXPECT_EQ(intString.asUInt(), 0);
    EXPECT_EQ(intString.asAbsUInt(), 0);

    intString = "-1";
    EXPECT_THROW([&] { return intString.asUInt(); }(), beast::BadLexicalCast);
    EXPECT_EQ(intString.asAbsUInt(), 1);

    intString = "-4294967295";
    EXPECT_EQ(intString.asAbsUInt(), 4294967295);

    intString = "-4294967296";
    EXPECT_THROW([&] { return intString.asAbsUInt(); }(), json::Error);

    intString = "2147483648";
    EXPECT_THROW([&] { return intString.asInt(); }(), beast::BadLexicalCast);
    EXPECT_EQ(intString.asAbsUInt(), 2147483648);

    intString = "2147483647";
    EXPECT_EQ(intString.asInt(), 2147483647);
    EXPECT_EQ(intString.asAbsUInt(), 2147483647);

    intString = "-2147483648";
    EXPECT_EQ(intString.asInt(), -2147483648LL);  // MSVC wants the LL
    EXPECT_EQ(intString.asAbsUInt(), 2147483648LL);

    intString = "-2147483649";
    EXPECT_THROW([&] { return intString.asInt(); }(), beast::BadLexicalCast);
    EXPECT_EQ(intString.asAbsUInt(), 2147483649);
}

TEST(JsonValue, converting_a_real_valued_json_number)
{
    json::Value intReal{4294967297.0};
    EXPECT_THROW([&] { return intReal.asUInt(); }(), json::Error);
    EXPECT_THROW([&] { return intReal.asAbsUInt(); }(), json::Error);

    intReal = 4294967295.0;
    EXPECT_EQ(intReal.asUInt(), 4294967295u);
    EXPECT_EQ(intReal.asAbsUInt(), 4294967295u);

    intReal = 0.0;
    EXPECT_EQ(intReal.asUInt(), 0);
    EXPECT_EQ(intReal.asAbsUInt(), 0);

    intReal = -1.0;
    EXPECT_THROW([&] { return intReal.asUInt(); }(), json::Error);
    EXPECT_EQ(intReal.asAbsUInt(), 1);

    intReal = -4294967295.0;
    EXPECT_EQ(intReal.asAbsUInt(), 4294967295);

    intReal = -4294967296.0;
    EXPECT_THROW([&] { return intReal.asAbsUInt(); }(), json::Error);

    intReal = 2147483648.0;
    EXPECT_THROW([&] { return intReal.asInt(); }(), json::Error);
    EXPECT_EQ(intReal.asAbsUInt(), 2147483648);

    intReal = 2147483647.0;
    EXPECT_EQ(intReal.asInt(), 2147483647);
    EXPECT_EQ(intReal.asAbsUInt(), 2147483647);

    intReal = -2147483648.0;
    EXPECT_EQ(intReal.asInt(), -2147483648LL);  // MSVC wants the LL
    EXPECT_EQ(intReal.asAbsUInt(), 2147483648LL);

    intReal = -2147483649.0;
    EXPECT_THROW([&] { return intReal.asInt(); }(), json::Error);
    EXPECT_EQ(intReal.asAbsUInt(), 2147483649);
}

TEST(JsonValue, copy)
{
    json::Value v1{2.5};
    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), 2.5);

    json::Value const v2 = v1;
    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), 2.5);
    EXPECT_TRUE(v2.isDouble());
    EXPECT_EQ(v2.asDouble(), 2.5);
    EXPECT_EQ(v1, v2);

    v1 = v2;
    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), 2.5);
    EXPECT_TRUE(v2.isDouble());
    EXPECT_EQ(v2.asDouble(), 2.5);
    EXPECT_EQ(v1, v2);
}

TEST(JsonValue, move)
{
    // This value is _precisely_ representable in a double, which is why
    // the equality comparisons checks below are correct and do not need
    // epsilon.
    constexpr double value = 2.5;

    json::Value v1{value};
    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), value);

    json::Value v2 = std::move(v1);

    EXPECT_TRUE(v2.isDouble());
    EXPECT_EQ(v2.asDouble(), value);

    // The check here depends on the move constructor setting the type
    // of the moved-from object to "null". That's true now, but it may
    // change in the future.
    // NOLINTBEGIN(bugprone-use-after-move)
    EXPECT_EQ(v1.type(), json::ValueType::Null);
    EXPECT_NE(v1, v2);

    v1 = 7803;
    EXPECT_NE(v1, v2);

    v1 = std::move(v2);
    EXPECT_EQ(v2.type(), json::ValueType::Null);
    EXPECT_NE(v1, v2);
    // NOLINTEND(bugprone-use-after-move)

    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), value);
}

TEST(JsonValue, comparisons)
{
    json::Value a, b;
    auto const testEquals = [&](std::string const& name) {
        EXPECT_TRUE(a == b);
        EXPECT_TRUE(a <= b);
        EXPECT_TRUE(a >= b);

        EXPECT_FALSE(a != b);
        EXPECT_FALSE(a < b);
        EXPECT_FALSE(a > b);

        EXPECT_TRUE(b == a);
        EXPECT_TRUE(b <= a);
        EXPECT_TRUE(b >= a);

        EXPECT_FALSE(b != a);
        EXPECT_FALSE(b < a);
        EXPECT_FALSE(b > a);
    };

    auto const testGreaterThan = [&](std::string const& name) {
        EXPECT_FALSE(a == b);
        EXPECT_FALSE(a <= b);
        EXPECT_TRUE(a >= b);

        EXPECT_TRUE(a != b);
        EXPECT_FALSE(a < b);
        EXPECT_TRUE(a > b);

        EXPECT_FALSE(b == a);
        EXPECT_TRUE(b <= a);
        EXPECT_FALSE(b >= a);

        EXPECT_TRUE(b != a);
        EXPECT_TRUE(b < a);
        EXPECT_FALSE(b > a);
    };

    a["a"] = json::UInt(0);
    b["a"] = json::Int(0);
    testEquals("zero");

    b["a"] = json::Int(-1);
    testGreaterThan("negative");

    json::Int const big = std::numeric_limits<int>::max();
    json::UInt bigger = big;
    bigger++;

    a["a"] = bigger;
    b["a"] = big;
    testGreaterThan("big");
}

TEST(JsonValue, compact)
{
    auto countLines = [](std::string const& str) {
        return 1 + std::count_if(str.begin(), str.end(), [](char c) { return c == '\n'; });
    };

    json::Value j;
    EXPECT_TRUE(json::load(R"({"array":[{"12":23},{},null,false,0.5]})", j));

    auto const styled = toStyledString(j);
    auto const compact = toCompactString(j);

    EXPECT_GT(countLines(styled), 1);
    EXPECT_EQ(j, json::load(styled));

    EXPECT_EQ(countLines(compact), 1);
    EXPECT_EQ(j, json::load(compact));

    // Streaming a value produces the compact representation.
    std::stringstream ss;
    ss << j;
    EXPECT_EQ(ss.str(), compact);
}

TEST(JsonValue, converts_null)
{
    json::Value const val;
    EXPECT_TRUE(val.isNull());
    EXPECT_EQ(val.asString(), "");
    EXPECT_EQ(val.asInt(), 0);
    EXPECT_EQ(val.asUInt(), 0);
    EXPECT_EQ(val.asAbsUInt(), 0);
    EXPECT_EQ(val.asDouble(), 0.0);
    EXPECT_FALSE(val.asBool());
}

TEST(JsonValue, converts_int)
{
    json::Value const val = -1234;
    EXPECT_TRUE(val.isInt());
    EXPECT_EQ(val.asString(), "-1234");
    EXPECT_EQ(val.asInt(), -1234);
    EXPECT_THROW([&] { return val.asUInt(); }(), json::Error);
    EXPECT_EQ(val.asAbsUInt(), 1234u);
    EXPECT_EQ(val.asDouble(), -1234.0);
    EXPECT_TRUE(val.asBool());
}

TEST(JsonValue, converts_uint)
{
    json::Value const val = 1234U;
    EXPECT_TRUE(val.isUInt());
    EXPECT_EQ(val.asString(), "1234");
    EXPECT_EQ(val.asInt(), 1234);
    EXPECT_EQ(val.asUInt(), 1234u);
    EXPECT_EQ(val.asAbsUInt(), 1234u);
    EXPECT_EQ(val.asDouble(), 1234.0);
    EXPECT_TRUE(val.asBool());
}

TEST(JsonValue, converts_real)
{
    json::Value const val = 2.0;
    EXPECT_TRUE(val.isDouble());
    EXPECT_TRUE(std::regex_match(val.asString(), std::regex("^2\\.0*$")));
    EXPECT_EQ(val.asInt(), 2);
    EXPECT_EQ(val.asUInt(), 2u);
    EXPECT_EQ(val.asAbsUInt(), 2u);
    EXPECT_EQ(val.asDouble(), 2.0);
    EXPECT_TRUE(val.asBool());
}

TEST(JsonValue, converts_numeric_string)
{
    json::Value const val = "54321";
    EXPECT_TRUE(val.isString());
    EXPECT_EQ(val.asStringView(), "54321");
    EXPECT_EQ(val.asString(), "54321");
    EXPECT_EQ(val.asInt(), 54321);
    EXPECT_EQ(val.asUInt(), 54321u);
    EXPECT_EQ(val.asAbsUInt(), 54321);
    EXPECT_THROW([&] { return val.asDouble(); }(), json::Error);
    EXPECT_TRUE(val.asBool());
}

TEST(JsonValue, converts_non_numeric_string)
{
    json::Value const val(json::ValueType::String);
    EXPECT_TRUE(val.isString());
    EXPECT_TRUE(val.asStringView().empty());
    EXPECT_EQ(val.asString(), "");
    EXPECT_THROW([&] { return val.asInt(); }(), std::exception);
    EXPECT_THROW([&] { return val.asUInt(); }(), std::exception);
    EXPECT_THROW([&] { return val.asAbsUInt(); }(), std::exception);
    EXPECT_THROW([&] { return val.asDouble(); }(), std::exception);
    EXPECT_TRUE(val.asBool() == false);
}

TEST(JsonValue, converts_bool_false)
{
    json::Value const val = false;
    EXPECT_TRUE(val.isBool());
    EXPECT_EQ(val.asString(), "false");
    EXPECT_EQ(val.asInt(), 0);
    EXPECT_EQ(val.asUInt(), 0);
    EXPECT_EQ(val.asAbsUInt(), 0);
    EXPECT_EQ(val.asDouble(), 0.0);
    EXPECT_FALSE(val.asBool());
}

TEST(JsonValue, converts_bool_true)
{
    json::Value const val = true;
    EXPECT_TRUE(val.isBool());
    EXPECT_EQ(val.asString(), "true");
    EXPECT_EQ(val.asInt(), 1);
    EXPECT_EQ(val.asUInt(), 1);
    EXPECT_EQ(val.asAbsUInt(), 1);
    EXPECT_EQ(val.asDouble(), 1.0);
    EXPECT_TRUE(val.asBool());
}

TEST(JsonValue, converts_array_type)
{
    json::Value const val(json::ValueType::Array);
    EXPECT_TRUE(val.isArray());
    EXPECT_THROW([&] { return val.asString(); }(), json::Error);
    EXPECT_THROW([&] { return val.asInt(); }(), json::Error);
    EXPECT_THROW([&] { return val.asUInt(); }(), json::Error);
    EXPECT_THROW([&] { return val.asAbsUInt(); }(), json::Error);
    EXPECT_THROW([&] { return val.asDouble(); }(), json::Error);
    EXPECT_FALSE(val.asBool());  // empty or not
}

TEST(JsonValue, converts_object_type)
{
    json::Value const val(json::ValueType::Object);
    EXPECT_TRUE(val.isObject());
    EXPECT_THROW([&] { return val.asString(); }(), json::Error);
    EXPECT_THROW([&] { return val.asInt(); }(), json::Error);
    EXPECT_THROW([&] { return val.asUInt(); }(), json::Error);
    EXPECT_THROW([&] { return val.asAbsUInt(); }(), json::Error);
    EXPECT_THROW([&] { return val.asDouble(); }(), json::Error);
    EXPECT_FALSE(val.asBool());  // empty or not
}

TEST(JsonValue, as_bool)
{
    // Null and empty containers are false; non-empty containers are true.
    EXPECT_FALSE(json::Value().asBool());
    EXPECT_FALSE(json::Value(json::ValueType::Array).asBool());
    EXPECT_FALSE(json::Value(json::ValueType::Object).asBool());
    {
        json::Value arr(json::ValueType::Array);
        arr.append(1);
        EXPECT_TRUE(arr.asBool());
    }
    {
        json::Value obj(json::ValueType::Object);
        obj["key"] = 1;
        EXPECT_TRUE(obj.asBool());
    }

    EXPECT_TRUE(json::Value(true).asBool());
    EXPECT_FALSE(json::Value(false).asBool());

    EXPECT_FALSE(json::Value(json::Value::Int{0}).asBool());
    EXPECT_TRUE(json::Value(json::Value::Int{1}).asBool());
    EXPECT_TRUE(json::Value(json::Value::Int{-1}).asBool());

    // UInt values are read through the unsigned member, including values
    // above the signed range.
    EXPECT_FALSE(json::Value(json::Value::UInt{0}).asBool());
    EXPECT_TRUE(json::Value(json::Value::UInt{1}).asBool());
    EXPECT_TRUE(json::Value(json::Value::kMaxUInt).asBool());

    // The type-tag constructor initializes the member each type reads.
    EXPECT_FALSE(json::Value(json::ValueType::Int).asBool());
    EXPECT_FALSE(json::Value(json::ValueType::UInt).asBool());
    EXPECT_EQ(json::Value(json::ValueType::UInt).asUInt(), 0u);

    EXPECT_FALSE(json::Value(0.0).asBool());
    EXPECT_TRUE(json::Value(1.5).asBool());

    // Only the empty string is false.
    EXPECT_FALSE(json::Value("").asBool());
    EXPECT_TRUE(json::Value("false").asBool());
}

TEST(JsonValue, randomized_conversions)
{
    auto testInteger = []<typename T>(T x) {
        json::Value v(x);

        EXPECT_TRUE(v.isInt() || v.isUInt());
        EXPECT_EQ(v.asString(), std::to_string(x));
        EXPECT_EQ(json::toInteger<T>(v), x);
        EXPECT_EQ(v.asBool(), x != 0);

        // The same checks must hold for the value itself and for its decimal
        // string representation.
        auto check = [x](json::Value const& v) {
            if constexpr (std::is_signed_v<T>)
            {
                EXPECT_EQ(v.asInt(), x);

                if (x < 0)
                {
                    EXPECT_ANY_THROW((void)v.asUInt());
                }
                else
                {
                    EXPECT_EQ(v.asUInt(), static_cast<std::uint32_t>(x));
                }
            }
            else
            {
                EXPECT_EQ(v.asUInt(), x);

                if (std::in_range<json::Int>(x))
                {
                    EXPECT_EQ(v.asInt(), static_cast<std::int32_t>(x));
                }
                else
                {
                    EXPECT_ANY_THROW((void)v.asInt());
                }
            }
        };

        check(v);

        v = json::Value{std::to_string(x)};

        EXPECT_TRUE(v.isString());
        EXPECT_EQ(v.asString(), std::to_string(x));
        EXPECT_EQ(json::toInteger<T>(v), x);

        check(v);
    };

    // Signed and unsigned integers
    for (int i = 0; i != 1024; ++i)
    {
        testInteger(randInt<std::int32_t>(-100, 100));
        testInteger(randInt<std::uint32_t>(0, 256));
    }

    testInteger(std::numeric_limits<std::int32_t>::min());
    testInteger(std::numeric_limits<std::int32_t>::max());
    testInteger(std::numeric_limits<std::uint32_t>::max());

    // Real values
    for (int i = 0; i != 1024; ++i)
    {
        auto const x = randInt<int>(-100, 100);

        json::Value const val{x * 1.0};

        EXPECT_TRUE(val.isDouble());
        EXPECT_EQ(val.asDouble(), x * 1.0);
        EXPECT_EQ(val.asBool(), x != 0);

        // Integer conversion: exact for this range
        EXPECT_EQ(val.asInt(), x);

        // String form must look like a double, not a bare integer
        auto const s = val.asString();
        EXPECT_TRUE(std::regex_match(s, std::regex(R"(^-?\d+\.\d*$)"))) << s;

        // Round-trip: parsing the string back gives the same double
        EXPECT_EQ(std::stod(s), x * 1.0);
    }

    // Non-numeric strings
    for (std::size_t i = 0; i != 1024; ++i)
    {
        auto s = std::to_string(randInt<int>(-100000, 100000));

        // Corrupt one digit, turning it into a non-digit
        s[i % s.size()] += randInt<int>(16, 64);

        json::Value const val(s);
        EXPECT_TRUE(val.isString());
        EXPECT_EQ(val.asString(), s);

        EXPECT_FALSE(json::toInteger<std::int32_t>(val)) << s;
        EXPECT_FALSE(json::toInteger<std::uint32_t>(val)) << s;

        EXPECT_ANY_THROW((void)val.asInt()) << s;
        EXPECT_ANY_THROW((void)val.asUInt()) << s;
    }

    {
        // Array type
        json::Value val{json::ValueType::Array};
        EXPECT_TRUE(val.isArray());
        EXPECT_FALSE(val.isObject());
        EXPECT_FALSE(val.asBool());
        val[0] = "n";
        EXPECT_TRUE(val[0].isString());
        EXPECT_EQ(val[0].asStringView(), "n");
        EXPECT_TRUE(val.asBool());
    }

    {
        // Object type
        json::Value val{json::ValueType::Object};
        EXPECT_TRUE(val.isObject());
        EXPECT_FALSE(val.isArray());
        EXPECT_FALSE(val.asBool());

        static constexpr json::StaticString kKey("an obscenely long key goes here");
        val[kKey] = "b";
        std::string const sk = kKey.cStr();
        EXPECT_TRUE(val[kKey].isString());
        EXPECT_EQ(val[sk].asStringView(), "b");
        EXPECT_TRUE(val.asBool());
    }

    EXPECT_TRUE(json::Value(true).asBool());
    EXPECT_FALSE(json::Value(false).asBool());

    EXPECT_FALSE(json::Value(json::Value::Int{0}).asBool());
    EXPECT_TRUE(json::Value(json::Value::Int{1}).asBool());
    EXPECT_TRUE(json::Value(json::Value::Int{-1}).asBool());

    // UInt values are read through the unsigned member, including values
    // above the signed range.
    EXPECT_FALSE(json::Value(json::Value::UInt{0}).asBool());
    EXPECT_TRUE(json::Value(json::Value::UInt{1}).asBool());
    EXPECT_TRUE(json::Value(json::Value::kMaxUInt).asBool());

    // The type-tag constructor initializes the member each type reads.
    EXPECT_FALSE(json::Value(json::ValueType::Int).asBool());
    EXPECT_FALSE(json::Value(json::ValueType::UInt).asBool());
    EXPECT_EQ(json::Value(json::ValueType::UInt).asUInt(), 0u);

    EXPECT_FALSE(json::Value(0.0).asBool());
    EXPECT_TRUE(json::Value(1.5).asBool());

    // Only the empty string is false.
    EXPECT_FALSE(json::Value("").asBool());
    EXPECT_TRUE(json::Value("false").asBool());
}

TEST(JsonValue, strings)
{
    // Strings of up to 15 bytes are stored inline; longer strings are either
    // allocated or, for a StaticString, referenced. The observable behavior
    // must be identical for all three representations.
    static constexpr json::StaticString kShort("short");
    static constexpr json::StaticString kLong("a string that is too long to store inline");

    for (std::size_t len = 0; len != 64; ++len)
    {
        std::string const s(len, 'x');

        json::Value const v{s};
        EXPECT_TRUE(v.isString());
        EXPECT_EQ(v.asStringView(), s);
        EXPECT_EQ(v.asString(), s);
        EXPECT_EQ(bool(v), len != 0);

        json::Value const copy{v};
        EXPECT_EQ(copy, v);
        EXPECT_EQ(copy.asStringView(), s);

        json::Value moved{copy};
        json::Value const target{std::move(moved)};
        EXPECT_EQ(target.asStringView(), s);

        auto const parsed = json::load("[" + to_string(v) + "]");
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ((*parsed)[0u], v);  // NOLINT(bugprone-unchecked-optional-access)
    }

    for (auto const& ss : {kShort, kLong})
    {
        json::Value const v{ss};
        EXPECT_TRUE(v.isString());
        EXPECT_EQ(v.asStringView(), ss);
        EXPECT_EQ(v, json::Value{std::string(ss)});

        json::Value const copy{v};
        EXPECT_EQ(copy.asStringView(), ss);
    }

    // Escaping round trips.
    {
        std::string const tricky{"quote\" backslash\\ tab\t newline\n bell\a unicode\xC3\xA9"};
        json::Value arr{json::ValueType::Array};
        arr.append(tricky);

        auto const parsed = json::load(to_string(arr));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ((*parsed)[0u].asString(), tricky);  // NOLINT(bugprone-unchecked-optional-access)
    }
}

TEST(JsonValue, number)
{
    // A Number is stored as its decimal string representation.
    Number const n{12345, -2};
    json::Value const v{n};
    EXPECT_TRUE(v.isString());
    EXPECT_EQ(v.asString(), to_string(n));
    EXPECT_EQ(json::toJson(n), v);
}

TEST(JsonValue, access_members)
{
    json::Value val;
    EXPECT_EQ(val.type(), json::ValueType::Null);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isMember("key"));
    {
        json::Value const constVal = val;
        EXPECT_EQ(constVal[7u].type(), json::ValueType::Null);
        EXPECT_FALSE(constVal.isMember("key"));
        EXPECT_EQ(constVal["key"].type(), json::ValueType::Null);
        EXPECT_TRUE(constVal.getMemberNames().empty());
        EXPECT_EQ(constVal.get(1u, "default0"), "default0");
        EXPECT_EQ(constVal.get(std::string("not"), "oh"), "oh");
        EXPECT_EQ(constVal.get("missing", "default2"), "default2");
    }

    val = -7;
    EXPECT_EQ(val.type(), json::ValueType::Int);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isMember("key"));

    val = 42u;
    EXPECT_EQ(val.type(), json::ValueType::UInt);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isMember("key"));

    val = std::numbers::pi;
    EXPECT_EQ(val.type(), json::ValueType::Real);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isMember("key"));

    val = true;
    EXPECT_EQ(val.type(), json::ValueType::Boolean);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isMember("key"));

    val = "string";
    EXPECT_EQ(val.type(), json::ValueType::String);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isMember("key"));

    val = json::Value(json::ValueType::Object);
    EXPECT_EQ(val.type(), json::ValueType::Object);
    EXPECT_EQ(val.size(), 0);
    static json::StaticString const kStaticThree("three");
    val[kStaticThree] = 3;
    val["two"] = 2;
    EXPECT_EQ(val.size(), 2);
    EXPECT_EQ(val[kStaticThree], 3);
    EXPECT_TRUE(val.isMember("two"));
    EXPECT_TRUE(val.isMember(kStaticThree));
    EXPECT_FALSE(val.isMember("key"));
    {
        json::Value const constVal = val;
        EXPECT_EQ(constVal["two"], 2);
        EXPECT_EQ(constVal["four"].type(), json::ValueType::Null);
        EXPECT_EQ(constVal[kStaticThree], 3);
        EXPECT_TRUE(constVal.isMember("two"));
        EXPECT_TRUE(constVal.isMember(kStaticThree));
        EXPECT_FALSE(constVal.isMember("key"));
        EXPECT_EQ(val.get(std::string("two"), "backup"), 2);
        EXPECT_EQ(val.get("missing", "default2"), "default2");
    }

    val = json::Value(json::ValueType::Array);
    EXPECT_EQ(val.type(), json::ValueType::Array);
    EXPECT_EQ(val.size(), 0);
    val[0u] = "zero";
    val[1u] = "one";
    EXPECT_EQ(val.size(), 2);
    // Reading past the end through a const reference yields null. (Doing so
    // through a non-const reference would extend the array, which asserts.)
    EXPECT_EQ(std::as_const(val)[20u].type(), json::ValueType::Null);
    EXPECT_EQ(val.size(), 2);
    EXPECT_FALSE(val.isMember("key"));
    {
        json::Value const constVal = val;
        EXPECT_EQ(constVal[0u], "zero");
        EXPECT_EQ(constVal[2u].type(), json::ValueType::Null);
        EXPECT_FALSE(constVal.isMember("key"));
        EXPECT_EQ(val.get(1u, "default0"), "one");
        EXPECT_EQ(val.get(3u, "default1"), "default1");
    }
}

TEST(JsonValue, remove_members)
{
    json::Value val;

    EXPECT_FALSE(val.removeMember(std::string("member")));

    val = json::Value(json::ValueType::Object);

    struct TestCase
    {
        json::StaticString key;
        json::Int value;
    };

    static constexpr TestCase kTestCases[] = {
        {.key = json::StaticString("one"), .value = 1},
        {.key = json::StaticString("two"), .value = 2},
        {.key = json::StaticString("three"), .value = 3},
        {.key = json::StaticString("four"), .value = 4},
        {.key = json::StaticString("five"), .value = 5},
    };

    for (auto const& tc : kTestCases)
    {
        EXPECT_FALSE(val.isMember(tc.key));
        val[tc.key] = tc.value;
        EXPECT_TRUE(val.isMember(tc.key));
    }

    EXPECT_EQ(val.size(), std::size(kTestCases));
    EXPECT_FALSE(val.removeMember("potato"));

    for (auto const& tc : kTestCases)
    {
        EXPECT_FALSE(val.removeMember(std::to_string(tc.value)));

        auto const v1 = val.get(tc.key);

        EXPECT_EQ(v1.type(), json::ValueType::Int);
        EXPECT_EQ(v1.asInt(), tc.value);
    }

    EXPECT_EQ(val.size(), std::size(kTestCases));

    for (auto const& tc : kTestCases)
    {
        auto rm = val.removeMember(tc.key);
        ASSERT_TRUE(rm.has_value());

        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        EXPECT_EQ(rm->type(), json::ValueType::Int);
        EXPECT_EQ(rm->asInt(), tc.value);
        // NOLINTEND(bugprone-unchecked-optional-access)
        EXPECT_FALSE(val.isMember(tc.key));
        EXPECT_FALSE(val.removeMember(tc.key));
    }

    EXPECT_EQ(val.size(), 0);

    EXPECT_FALSE(val.removeMember("potato"));

    for (auto const& tc : kTestCases)
    {
        EXPECT_FALSE(val.removeMember(std::to_string(tc.value)));
        EXPECT_FALSE(val.removeMember(tc.key));
    }
}

TEST(JsonValue, iterator)
{
    {
        // Iterating a non-const array.
        json::Value arr{json::ValueType::Array};
        arr[0u] = "zero";
        arr[1u] = "one";
        arr[2u] = "two";
        arr[3u] = "three";

        auto const b = arr.begin();
        auto const e = arr.end();

        EXPECT_TRUE(b != e);
        EXPECT_FALSE(b == e);

        auto i1 = b;
        auto i2 = e;
        --i2;

        // key() and memberName() on an array iterator.
        EXPECT_EQ(i1.key(), json::Value{0u});
        EXPECT_EQ(i2.key(), json::Value{3u});
        EXPECT_STREQ(i1.memberName(), "");
        EXPECT_STREQ(i2.memberName(), "");

        // Pre and post increment and decrement.
        *i1++ = "0";
        EXPECT_EQ(*i1, "one");
        *i1 = "1";
        ++i1;

        *i2-- = "3";
        EXPECT_EQ(*i2, "two");
        EXPECT_EQ(i1, i2);
        *i2 = "2";
        EXPECT_EQ(*i1, "2");
    }
    {
        // Iterating a const object.
        json::Value const obj{
            [] {
                json::Value obj{json::ValueType::Object};
                obj["0"] = 0;
                obj["1"] = 1;
                obj["2"] = 2;
                obj["3"] = 3;
                return obj;
            }(),
        };

        auto i1 = obj.begin();
        auto i2 = obj.end();
        --i2;

        EXPECT_TRUE(i1 != i2);
        EXPECT_FALSE(i1 == i2);

        // key() and memberName() on an object iterator.
        EXPECT_EQ(i1.key(), json::Value{"0"});
        EXPECT_EQ(i2.key(), json::Value{"3"});
        EXPECT_STREQ(i1.memberName(), "0");
        EXPECT_STREQ(i2.memberName(), "3");

        // Pre and post increment and decrement.
        EXPECT_EQ(*i1++, 0);
        EXPECT_EQ(*i1, 1);
        ++i1;

        EXPECT_EQ(*i2--, 3);
        EXPECT_EQ(*i2, 2);
        EXPECT_EQ(i1, i2);
        EXPECT_EQ(*i1, 2);
    }
    {
        // const_iterator is constructible from iterator.
        json::Value arr{json::ValueType::Array};
        arr[0u] = 0;

        auto it = arr.begin();
        json::Value::const_iterator const cit{it};
        EXPECT_EQ(cit, std::as_const(arr).begin());
    }
    {
        // Iterating a non-const null value yields an empty range.
        json::Value nul{};
        EXPECT_EQ(nul.begin(), nul.end());
    }
    {
        // Iterating a const scalar yields an empty range.
        json::Value const i{-3};
        EXPECT_EQ(i.begin(), i.end());
    }
}

TEST(JsonValue, nest_limits)
{
    {
        constexpr std::uint16_t limit = 5;

        json::Value obj(json::ValueType::Object);

        for (std::uint16_t depth = 1; depth <= limit; ++depth)
        {
            EXPECT_TRUE(json::load(to_string(obj), depth));
            obj[std::to_string(depth)] = obj;
        }

        EXPECT_FALSE(json::load(to_string(obj), limit));
    }

    // A document consisting of a top-level object that contains `depth`
    // additional levels of nested objects.
    auto nestObjects = [](std::uint32_t depth) -> std::string {
        std::string s = "{";
        for (std::uint32_t i{1}; i <= depth; ++i)
            s += "\"obj\":{";
        for (std::uint32_t i{1}; i <= depth; ++i)
            s += "}";
        s += "}";
        return s;
    };

    // Each iteration adds an array and an object, i.e. two levels.
    auto nestArrays = [](std::uint32_t depth) -> std::string {
        std::string s = "{";
        for (std::uint32_t i{1}; i <= depth; ++i)
            s += "\"array\":[{";
        for (std::uint32_t i{1}; i <= depth; ++i)
            s += "}]";
        s += "}";
        return s;
    };

    {
        // Within the default nest limit
        json::Value j;
        EXPECT_TRUE(json::load(nestObjects(10), j));
        EXPECT_TRUE(json::load(nestObjects(json::kDefaultNestLimit - 1), j));
        EXPECT_TRUE(json::load(nestArrays((json::kDefaultNestLimit - 1) / 2), j));
    }

    {
        // Exceed the default nest limit
        json::Value j;
        EXPECT_FALSE(json::load(nestObjects(json::kDefaultNestLimit), j));
        EXPECT_FALSE(json::load(nestArrays((json::kDefaultNestLimit / 2) + 1), j));
    }

    // A nest limit of zero is invalid.
    EXPECT_FALSE(json::load("{}", std::uint16_t{0}));
}

TEST(JsonValue, memory_leak)
{
    // When run with the address sanitizer, this test confirms there is no
    // memory leak with the scenarios below.
    {
        json::Value a;
        a[0u] = 1;
        EXPECT_EQ(a.type(), json::ValueType::Array);
        EXPECT_EQ(a[0u].type(), json::ValueType::Int);
        a = std::move(a[0u]);
        EXPECT_EQ(a.type(), json::ValueType::Int);
    }
    {
        json::Value b;
        json::Value temp;
        temp["a"] = "Probably avoids the small string optimization";
        temp["b"] = "Also probably avoids the small string optimization";
        EXPECT_EQ(temp.type(), json::ValueType::Object);
        b.append(temp);
        EXPECT_EQ(temp.type(), json::ValueType::Object);
        EXPECT_EQ(b.size(), 1);

        b.append(std::move(temp));
        EXPECT_EQ(b.size(), 2);
    }
}

}  // namespace xrpl
