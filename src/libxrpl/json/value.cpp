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

#include <xrpl/basics/SlabAllocator.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string_view>
#include <utility>

namespace Json {

namespace {

// Not all standard library implementations support std::to_chars for
// floating point types and the __cpp_lib_to_chars feature test macro
// is not reliable, as some standard library versions will define the
// macro but only provide the integer overloads. This expression will
// check whether the specific overload we need actually compiles.
constexpr bool support_floating_point_to_chars = requires(char* p, double v) {
    std::to_chars(p, p, v, std::chars_format::general, 16);
};

// When true, the code will assert when an attempt to convert a JSON
// double to an integer type via asInt() or asUInt() would result in
// truncation of the fractional part of the double.
//
// This is off by default, but it can be useful when trying to clean
// up existing unintentionally lossy code or when tracking down bugs
// that could be caused by this behavior.
constexpr bool pedantic_float_to_int_conversions = false;

// When false, the code will NOT use the slab allocator for string
// buffers; it will fall back to on-demand heap allocations, which
// have significant performance overhead.
constexpr bool use_slab_allocator = true;

// In the original commit that implemented slabbed allocation of
// JSON objects, Nik Bougalis stated that:
//
// Real-world data indicates that only 2% of allocation requests
// are over 72 bytes long. The remaining 98% of allocations fall
// into the following 3 buckets, calculated across 9,500,000,000
// allocation calls:
//
// [ 1, 32]: 17% of all allocations
// [33, 48]: 27% of all allocations
// [49, 72]: 57% of all allocations
//
// Note that this commit uses small string optimization, so that
// strings shorter 16 characters don't require a separate memory
// allocation; this will skew the results a little bit.
struct buffer_t
{
    alignas(8) char buffer_[32];
};

constinit ripple::slab::allocator_t<
    buffer_t,
    ripple::slab::no_fallback,
    ripple::slab::config<262144>,
    ripple::slab::config<393216, 16>,
    ripple::slab::config<262144, 40>>
    slabber_;

[[nodiscard]] char const*
alloc_string(std::string_view str) noexcept
{
    if (str.empty()) [[unlikely]]
        return json_empty_string;

    char* ret = nullptr;

    // When using the slabber, we have to carefully calculate how many bytes
    // _above_ sizeof(buffer_t) we need:
    if constexpr (use_slab_allocator)
        ret = reinterpret_cast<char*>(slabber_.allocate(
            std::max(str.size() + 1, sizeof(buffer_t)) - sizeof(buffer_t)));

    // Fall back to the default if we can't grab memory from the slab
    // allocators.
    if (ret == nullptr)
    {
        ret = new (std::nothrow) char[str.size() + 1];

        if (ret == nullptr) [[unlikely]]
            ripple::LogicError("JSON: memory allocation failure");
    }

    auto nul = std::copy_n(str.data(), str.size(), ret);
    *nul = 0;
    return ret;
}

void
free_string(char const* str) noexcept
{
    if (str != json_empty_string) [[likely]]
    {
        if constexpr (use_slab_allocator)
        {
            if (slabber_.deallocate(const_cast<char*>(str)))
                return;
        }

        delete[] str;
    }
}

}  // namespace

//-------------------------------------------------------------------

Value::CZString::CZString(std::string_view s)
{
    if (!s.empty()) [[likely]]
        data_ =
            allocated_flag | reinterpret_cast<std::uintptr_t>(alloc_string(s));
}

Value::CZString::CZString(CZString const& other)
{
    if (other.isStatic())
    {
        data_ = other.data_;
        return;
    }

    data_ = allocated_flag |
        reinterpret_cast<std::uintptr_t>(alloc_string(other.c_str()));
}

Value::CZString&
Value::CZString::operator=(CZString const& other)
{
    if (this != &other)
    {
        if (auto masked = data_ & allocated_mask; masked != data_)
            free_string(reinterpret_cast<char const*>(masked));

        data_ = [&other]() -> std::uintptr_t {
            if (other.isStatic())
                return other.data_;

            return allocated_flag |
                reinterpret_cast<std::uintptr_t>(alloc_string(other.c_str()));
        }();
    }

    return *this;
}

Value::CZString::~CZString()
{
    if (auto masked = data_ & allocated_mask; masked != data_)
        free_string(reinterpret_cast<char const*>(masked));
}

//-------------------------------------------------------------------

// We need a single instance of a null type, so that we can return
// a reference from things like `Value::operator[](...) const`.
constinit Value const null;

bool
Value::initSSO(std::string_view str) noexcept
{
    if (str.size() > data_.buffer.size())
        return false;

    std::copy_n(str.data(), str.size(), data_.buffer.data());
    if (str.size() < data_.buffer.size())
        data_.buffer[str.size()] = 0;
    data_.type = static_cast<std::uint8_t>(data_.buffer.size() - str.size());
    return true;
}

void
Value::initString(const char* str, std::size_t len) noexcept
{
    // This is an arbitrary limit that is unlikely to be hit in
    // real world use, but if it happens in a DEBUG build, it's
    // worth making some noise over.
    XRPL_ASSERT(
        str != nullptr && len != 0 && len <= default_string_size_limit,
        "Json::Value::initString : oversized string allocation attempted");

    std::construct_at(reinterpret_cast<const char**>(data_.buffer.data()), str);

    std::construct_at(
        reinterpret_cast<std::uint32_t*>(
            data_.buffer.data() + sizeof(const char*)),
        static_cast<std::uint32_t>(len));
}

void
Value::initAllocatedString(std::string_view str) noexcept
{
    initString(alloc_string(str), str.length());
    data_.type = type_allocatedString;
}

void
Value::initStaticString(std::string_view str) noexcept
{
    initString(str.data(), str.length());
    data_.type = type_staticString;
}

Value::Value(ValueType type)
{
    switch (type)
    {
        case nullValue:
            data_.type = type_null;
            break;

        case intValue:
            std::construct_at(
                reinterpret_cast<std::int64_t*>(data_.buffer.data()),
                std::int64_t{0});
            data_.type = type_int;
            break;

        case uintValue:
            std::construct_at(
                reinterpret_cast<std::int64_t*>(data_.buffer.data()),
                std::int64_t{0});
            data_.type = type_uint;
            break;

        case booleanValue:
            std::construct_at(
                reinterpret_cast<std::int64_t*>(data_.buffer.data()),
                std::int64_t{0});
            data_.type = type_boolean;
            break;

        case realValue:
            std::construct_at(
                reinterpret_cast<double*>(data_.buffer.data()), 0.0);
            data_.type = type_real;
            break;

        case stringValue:
            initSSO({});
            break;

        case arrayValue:
            std::construct_at(
                reinterpret_cast<ArrayStorage**>(data_.buffer.data()),
                new ArrayStorage());
            data_.type = type_array;
            break;

        case objectValue:
            std::construct_at(
                reinterpret_cast<ObjectStorage**>(data_.buffer.data()),
                new ObjectStorage());
            data_.type = type_object;
            break;

        default:
            UNREACHABLE("ripple::Value::Value : unknown type");
    }
}

Value::Value(Int value) noexcept
{
    std::construct_at(
        reinterpret_cast<std::int64_t*>(data_.buffer.data()),
        static_cast<std::int64_t>(value));
    data_.type = type_int;
}

Value::Value(UInt value) noexcept
{
    std::construct_at(
        reinterpret_cast<std::int64_t*>(data_.buffer.data()),
        static_cast<std::int64_t>(value));
    data_.type = type_uint;
}

Value::Value(double value) noexcept
{
    std::construct_at(reinterpret_cast<double*>(data_.buffer.data()), value);
    data_.type = type_real;
}

Value::Value(bool value) noexcept
{
    std::construct_at(
        reinterpret_cast<std::int64_t*>(data_.buffer.data()),
        static_cast<std::int64_t>(value ? 1 : 0));
    data_.type = type_boolean;
}

Value::Value(Value const& other)
{
    if (other.data_.type == type_allocatedString)
    {
        initAllocatedString(other.asStringView());
        return;
    }

    if (other.data_.type == type_array)
    {
        std::construct_at(
            reinterpret_cast<ArrayStorage**>(data_.buffer.data()),
            new ArrayStorage(*other.as<ArrayStorage*>()));
        data_.type = type_array;
        return;
    }

    if (other.data_.type == type_object)
    {
        std::construct_at(
            reinterpret_cast<ObjectStorage**>(data_.buffer.data()),
            new ObjectStorage(*other.as<ObjectStorage*>()));
        data_.type = type_object;
        return;
    }

    data_ = other.data_;
}

Value::~Value() noexcept
{
    if (data_.type == type_allocatedString)
        free_string(as<const char*>());
    else if (data_.type == type_array)
        delete as<ArrayStorage*>();
    else if (data_.type == type_object)
        delete as<ObjectStorage*>();
}

ValueType
Value::type() const noexcept
{
    if (data_.type <= type_allocatedString)
        return stringValue;

    switch (data_.type)
    {
        case type_int:
            return intValue;

        case type_uint:
            return uintValue;

        case type_boolean:
            return booleanValue;

        case type_real:
            return realValue;

        case type_array:
            return arrayValue;

        case type_object:
            return objectValue;

        case type_null:
            return nullValue;

        default:
            return nullValue;
    }
}

std::string_view
Value::asStringView() const noexcept
{
    if (isSSO())
        return {reinterpret_cast<char const*>(data_.buffer.data()), strlen()};

    if (isAllocatedString() || isStaticStringType())
        return {as<const char*>(), strlen()};

    return {};
}

std::string
Value::asString() const
{
    if (isString())
        return std::string{asStringView()};

    if (isNull())
        return "";

    if (isBool())
        return as<std::int64_t>() ? "true" : "false";

    if (isInt() || isUInt())
        return std::to_string(as<std::int64_t>());

    if (isDouble())
    {
        auto const d = asDouble();

        // There's no way to meaningfully display a non-finite double
        // in a JSON-compatible format. When we serialize such values
        // we render them as null objects, but here we choose to bend
        // the rules a bit, since `asString` is used for debugging.
        if (!std::isfinite(d)) [[unlikely]]
        {
            if (std::isnan(d))
                return "nan";

            return std::signbit(d) ? "-inf" : "inf";
        }

        // We use the general format with 16 digits of precision when
        // displaying double values. Older versions of this code used
        // std::to_string (which reduces to %f) here, but switched to
        // sprintf with %.16g when serializing the value.
        //
        // The problem with std::to_string is that the output is ugly
        // (e.g. the double value 2 is formatted as "2.000000") while
        // also losing precision for very large or very small values.
        //
        // The %g format is correct but strips trailing zeros and the
        // decimal point, so 2.0 becomes "2". This means that we lose
        // the type information (when parsing the resulting value the
        // type is deduced as integer). To preserve type fidelity, we
        // manually append a ".0" when the output does not contain an
        // exponent or a decimal point.
        char buffer[64];
        std::size_t len;

        if constexpr (support_floating_point_to_chars)
        {
            auto [ptr, ec] = std::to_chars(
                buffer,
                buffer + sizeof(buffer),
                d,
                std::chars_format::general,
                16);
            len = static_cast<std::size_t>(ptr - buffer);
        }
        else
        {
            len = static_cast<std::size_t>(
                std::snprintf(buffer, sizeof(buffer), "%.16g", asDouble()));
        }

        if (std::string_view tmp(buffer, len);
            tmp.find_first_of(".eE") == std::string_view::npos)
        {
            buffer[len++] = '.';
            buffer[len++] = '0';
        }

        return {buffer, len};
    }

    ripple::Throw<error>(
        "Type (" + std::to_string(type()) + ") is not convertible to string");
}

Int
Value::asInt() const
{
    if (isIntegral())
    {
        if (auto const v = as<std::int64_t>(); std::in_range<Int>(v))
            return v;

        ripple::Throw<error>("integer value out of signed integer range");
    }

    if (isString())
        return beast::lexicalCastThrow<Int>(asStringView());

    if (isDouble())
    {
        auto v = as<double>();

        if (v >= std::numeric_limits<Int>::min() &&
            v <= std::numeric_limits<Int>::max())
        {
            if constexpr (pedantic_float_to_int_conversions)
                XRPL_ASSERT(
                    std::trunc(v) == v,
                    "ripple::Value::asInt : double value has fractional part");

            // Even if the range check above succeeded, the double can still
            // have a fractional part that casting will truncate toward zero.
            return static_cast<Int>(v);
        }

        ripple::Throw<error>("double value out of signed integer range");
    }

    if (isNull())
        return 0;

    ripple::Throw<error>("Type is not convertible to an integer");
}

UInt
Value::asUInt() const
{
    if (isIntegral())
    {
        if (auto const v = as<std::int64_t>(); std::in_range<UInt>(v))
            return v;

        ripple::Throw<error>("integer value out of unsigned integer range");
    }

    if (isString())
        return beast::lexicalCastThrow<UInt>(asStringView());

    if (isDouble())
    {
        auto v = as<double>();

        if (v >= std::numeric_limits<UInt>::min() &&
            v <= std::numeric_limits<UInt>::max())
        {
            if constexpr (pedantic_float_to_int_conversions)
                XRPL_ASSERT(
                    std::trunc(v) == v,
                    "ripple::Value::asUInt : double value has fractional part");

            // Even if the range check above succeeded, the double can still
            // have a fractional part that casting will truncate toward zero.
            return static_cast<UInt>(v);
        }

        ripple::Throw<error>("double value out of unsigned integer range");
    }

    if (isNull())
        return 0;

    ripple::Throw<error>("Type is not convertible to an unsigned integer");
}

double
Value::asDouble() const
{
    if (isDouble())
        return as<double>();

    if (isIntegral())
        return static_cast<double>(as<std::int64_t>());

    if (isNull())
        return 0.0;

    ripple::Throw<error>("Type is not convertible to a double");
}

bool
Value::asBool() const noexcept
{
    if (isString())
        return strlen() != 0;

    if (isIntegral())
        return as<std::int64_t>() != 0;

    if (isDouble())
        return as<double>() != 0.0;

    if (isArray())
        return !as<ArrayStorage*>()->empty();

    if (isObject())
        return !as<ObjectStorage*>()->empty();

    return false;
}

UInt
Value::size() const noexcept
{
    switch (data_.type)
    {
        case type_array:
            return static_cast<UInt>(as<ArrayStorage*>()->size());

        case type_object:
            return static_cast<UInt>(as<ObjectStorage*>()->size());

        default:
            return 0;
    }
}

Value::
operator bool() const noexcept
{
    if (isNull())
        return false;

    if (isString())
        return strlen() != 0;

    if (isArray())
        return !as<ArrayStorage*>()->empty();

    if (isObject())
        return !as<ObjectStorage*>()->empty();

    return true;
}

Value&
Value::operator[](UInt index)
{
    if (data_.type == type_null)
        *this = Value(arrayValue);

    if (data_.type != type_array)
        ripple::Throw<error>("Attempt to treat non-array value as array.");

    auto arr = as<ArrayStorage*>();

    // While JSON cannot represent sparse arrays (at least not as arrays)
    // previous versions of this code would allow a hole to be created by
    // pushing an entry at an index that was greater than the size of the
    // current array. It would then "patch" things in, by pretending that
    // there were null values in the hole.
    //
    // We assert if this is attempted on a debug build. But to ensure the
    // code is backwards compatible, in release mode we fill in the slots
    // that would be empty with a null value.
    XRPL_ASSERT(
        index <= arr->size(),
        "ripple::Value::operator[](UInt) : index out of bounds");

    if (index > arr->size()) [[unlikely]]
    {
        for (auto i = arr->size(); i < index; ++i)
            arr->try_emplace(i);
    }

    return arr->try_emplace(index).first->second;
}

Value const&
Value::operator[](UInt index) const
{
    XRPL_ASSERT(
        data_.type == type_array || data_.type == type_null,
        "Json::Value::operator[] : Attempt to treat non-array value as array.");

    if (data_.type == type_array)
    {
        auto* arr = as<ArrayStorage const*>();

        if (auto it = arr->find(index); it != arr->end())
            return it->second;
    }

    return null;
}

Value const&
Value::operator[](std::string_view key) const
{
    XRPL_ASSERT(
        data_.type == type_object || data_.type == type_null,
        "Json::Value::operator[] : Attempt to treat non-object value as "
        "object.");

    if (data_.type == type_object)
    {
        auto* obj = as<ObjectStorage const*>();
        auto it = obj->find(key);

        if (it != obj->end())
            return it->second;
    }

    return null;
}

Value
Value::get(std::size_t index, Value defaultValue) const
{
    if (data_.type == type_array)
    {
        auto* arr = as<ArrayStorage const*>();

        if (auto it = arr->find(index); it != arr->end())
            return it->second;
    }

    return defaultValue;
}

Value
Value::get(std::string_view key, Value defaultValue) const
{
    if (data_.type == type_object)
    {
        auto* obj = as<ObjectStorage const*>();

        if (auto it = obj->find(key); it != obj->end())
            return it->second;
    }

    return defaultValue;
}

std::optional<Value>
Value::removeMember(std::string_view key)
{
    XRPL_ASSERT(
        data_.type == type_null || data_.type == type_object,
        "ripple::Value::removeMember : wrong type");

    if (data_.type == type_object)
    {
        auto* obj = as<ObjectStorage*>();

        if (auto it = obj->find(key); it != obj->end())
        {
            Value old(std::move(it->second));
            obj->erase(it);
            return old;
        }
    }

    return std::nullopt;
}

bool
Value::isMember(std::string_view key, std::optional<ValueType> type)
    const noexcept
{
    if (data_.type != type_object)
        return false;

    auto* obj = as<ObjectStorage const*>();

    if (auto it = obj->find(key); it != obj->end())
    {
        if (type)
            return it->second.type() == *type;

        return true;
    }

    return false;
}

Value::Members
Value::getMemberNames() const
{
    XRPL_ASSERT(
        data_.type == type_null || data_.type == type_object,
        "ripple::Value::getMemberNames : wrong type");

    Members members;

    if (data_.type == type_object)
    {
        auto* obj = as<ObjectStorage const*>();

        if (!obj->empty())
        {
            members.reserve(obj->size());

            for (const auto& [k, v] : *obj)
                members.push_back(std::string(k.c_str()));
        }
    }

    return members;
}

Value::const_iterator
Value::cbegin() const
{
    if (data_.type == type_array)
        return const_iterator(as<ArrayStorage*>()->cbegin());

    if (data_.type == type_object)
        return const_iterator(as<ObjectStorage*>()->cbegin());

    return {};
}

Value::const_iterator
Value::begin() const
{
    if (data_.type == type_array)
        return const_iterator(as<ArrayStorage*>()->cbegin());

    if (data_.type == type_object)
        return const_iterator(as<ObjectStorage*>()->cbegin());

    return {};
}

Value::const_iterator
Value::cend() const
{
    if (data_.type == type_array)
        return const_iterator(as<ArrayStorage*>()->cend());

    if (data_.type == type_object)
        return const_iterator(as<ObjectStorage*>()->cend());

    return {};
}

Value::const_iterator
Value::end() const
{
    if (data_.type == type_array)
        return const_iterator(as<ArrayStorage*>()->cend());

    if (data_.type == type_object)
        return const_iterator(as<ObjectStorage*>()->cend());

    return {};
}

Value::iterator
Value::begin()
{
    if (data_.type == type_array)
        return iterator(as<ArrayStorage*>()->begin());

    if (data_.type == type_object)
        return iterator(as<ObjectStorage*>()->begin());

    return {};
}

Value::iterator
Value::end()
{
    if (data_.type == type_array)
        return iterator(as<ArrayStorage*>()->end());

    if (data_.type == type_object)
        return iterator(as<ObjectStorage*>()->end());

    return {};
}

std::partial_ordering
operator<=>(Value const& x, Value const& y) noexcept
{
    auto const xType = x.type();
    auto const yType = y.type();

    if ((xType == intValue || xType == uintValue) &&
        (yType == intValue || yType == uintValue))
        return x.as<std::int64_t>() <=> y.as<std::int64_t>();

    if (xType != yType)
        return xType <=> yType;

    if (xType == nullValue)
        return std::partial_ordering::equivalent;

    if (xType == realValue)
        return x.as<double>() <=> y.as<double>();

    if (xType == booleanValue)
        return x.as<std::int64_t>() <=> y.as<std::int64_t>();

    if (xType == stringValue)
        return x.asStringView() <=> y.asStringView();

    if (xType == arrayValue)
        return *x.as<Value::ArrayStorage const*>() <=>
            *y.as<Value::ArrayStorage const*>();

    if (xType == objectValue)
        return *x.as<Value::ObjectStorage const*>() <=>
            *y.as<Value::ObjectStorage const*>();

    UNREACHABLE("ripple::Value::operator<=> : impossible comparison");
    return std::partial_ordering::unordered;
}

bool
operator==(Value const& x, Value const& y) noexcept
{
    auto const xType = x.type();
    auto const yType = y.type();

    if ((xType == intValue || xType == uintValue) &&
        (yType == intValue || yType == uintValue))
        return x.as<std::int64_t>() == y.as<std::int64_t>();

    if (xType != yType)
        return false;

    if (xType == nullValue)
        return true;

    if (xType == realValue)
        return x.as<double>() == y.as<double>();

    if (xType == booleanValue)
        return x.as<std::int64_t>() == y.as<std::int64_t>();

    if (xType == stringValue)
        return x.asStringView() == y.asStringView();

    if (xType == arrayValue)
        return *x.as<Value::ArrayStorage const*>() ==
            *y.as<Value::ArrayStorage const*>();

    if (xType == objectValue)
        return *x.as<Value::ObjectStorage const*>() ==
            *y.as<Value::ObjectStorage const*>();

    UNREACHABLE("ripple::Value::operator== : impossible comparison");
    return false;
}

}  // namespace Json
