#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/SlabAllocator.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace json {

namespace {

// Not all standard library implementations support std::to_chars for
// floating point types and the __cpp_lib_to_chars feature test macro
// is not reliable, as some standard library versions will define the
// macro but only provide the integer overloads. This expression will
// check whether the specific overload we need actually compiles.
constexpr bool kSupportFloatingPointToChars =
    requires(char* p, double v) { std::to_chars(p, p, v, std::chars_format::general, 16); };

// When true, the code will assert when an attempt to convert a JSON
// double to an integer type via asInt() or asUInt() would result in
// truncation of the fractional part of the double.
//
// This is off by default, but it can be useful when trying to clean
// up existing unintentionally lossy code or when tracking down bugs
// that could be caused by this behavior.
constexpr bool kPedanticFloatToIntConversions = false;

// When false, the code will NOT use the slab allocator for string
// buffers; it will fall back to on-demand heap allocations, which
// have significant performance overhead.
constexpr bool kUseSlabAllocator = true;

// Profiling done on November 2026 on a mainnet node found that the
// length of strings falls into the following buckets:
//
//    0-15 characters: 19%  (fit the in-place buffer; never allocated)
//   16-23 characters:  9%  (mostly 64-bit fields as 16 hex digits)
//   24-47 characters: 35%  (classic addresses (33-35) and 160-bit hex (40))
//   48-71 characters: 32%  (256-bit hex (64) and public keys (66))
//   72 and over:       5%
//
// We leverage this information to optimize the bucket sizes for the
// slab allocator.
struct Buffer
{
    alignas(8) char buffer[24];
};

// Item sizes are sizeof(Buffer) plus the extra bytes, so the classes
// hold strings of up to 23, 47 and 71 characters (plus the terminator).
constinit xrpl::slab::Allocator<
    Buffer,
    xrpl::slab::NoFallback,
    xrpl::slab::Config<xrpl::megabytes(2) / 24>,
    xrpl::slab::Config<xrpl::megabytes(4) / 48, 24>,
    xrpl::slab::Config<xrpl::megabytes(6) / 72, 48>>
    gSlabber;

// The number of bytes beyond sizeof(Buffer) that a string of this length
// needs. We use this to route the request directly to the correct size
// class.
[[nodiscard]] constexpr std::size_t
slabExtra(std::size_t len) noexcept
{
    return std::max(len + 1, sizeof(Buffer)) - sizeof(Buffer);
}

[[nodiscard]] char const*
allocString(std::string_view str) noexcept
{
    XRPL_ASSERT(!str.empty(), "json::allocString : empty strings are never allocated");

    char* ret = nullptr;

    if constexpr (kUseSlabAllocator)
        ret = static_cast<char*>(gSlabber.allocate(slabExtra(str.size())));

    // Fall back to the default if we can't grab memory from the slab
    // allocators.
    if (ret == nullptr)
    {
        ret = new (std::nothrow) char[str.size() + 1];

        if (ret == nullptr) [[unlikely]]
            xrpl::logicError("JSON: memory allocation failure");
    }

    // Copy the string and add the required NUL terminator.
    ret[str.copy(ret, str.size())] = 0;

    return ret;
}

void
freeString(std::string_view str) noexcept
{
    XRPL_ASSERT(!str.empty(), "json::freeString : empty strings are never allocated");

    if constexpr (kUseSlabAllocator)
    {
        if (gSlabber.deallocate(str.data(), slabExtra(str.size())))
            return;
    }

    delete[] str.data();
}

}  // namespace

//-------------------------------------------------------------------

Value::CZString::CZString(std::string_view s) noexcept
{
    if (!s.empty()) [[likely]]
        data_ = reinterpret_cast<std::uintptr_t>(allocString(s));
}

Value::CZString::~CZString()
{
    if (!isStatic())
        freeString(cStr());
}

//-------------------------------------------------------------------

// We need a single instance of a null type, so that we can return
// a reference from things like `Value::operator[](...) const`.
constinit Value const kNull;

bool
Value::initSSO(std::string_view str) noexcept
{
    if (str.size() > data_.buffer.size())
        return false;

    // For the longest string the type byte (zero) is the terminator.
    if (auto const n = str.copy(data_.buffer.data(), str.size()); n < kSsoCapacity)
        data_.buffer[n] = '\0';

    data_.type = xrpl::checkedCast<std::uint8_t>(data_.buffer.size() - str.size());
    return true;
}

void
Value::initString(char const* str, std::size_t len) noexcept
{
    // This is an arbitrary limit that is unlikely to be hit in
    // real world use, but if it happens in a DEBUG build, it's
    // worth making some noise over.
    XRPL_ASSERT(
        str != nullptr && len != 0 && len <= kDefaultStringSizeLimit,
        "json::Value::initString : oversized string allocation attempted");

    std::construct_at(reinterpret_cast<char const**>(data_.buffer.data()), str);

    std::construct_at(
        reinterpret_cast<std::uint32_t*>(data_.buffer.data() + sizeof(char const*)),
        static_cast<std::uint32_t>(len));
}

void
Value::initAllocatedString(std::string_view str) noexcept
{
    initString(allocString(str), str.length());
    data_.type = TypeAllocatedString;
}

void
Value::initStaticString(std::string_view str) noexcept
{
    initString(str.data(), str.length());
    data_.type = TypeStaticString;
}

Value::Value(ValueType type)
{
    switch (type)
    {
        case ValueType::Null:
            data_.type = TypeNull;
            break;

        case ValueType::Int:
            std::construct_at(
                reinterpret_cast<std::int64_t*>(data_.buffer.data()), std::int64_t{0});
            data_.type = TypeInt;
            break;

        case ValueType::UInt:
            std::construct_at(
                reinterpret_cast<std::int64_t*>(data_.buffer.data()), std::int64_t{0});
            data_.type = TypeUInt;
            break;

        case ValueType::Boolean:
            std::construct_at(
                reinterpret_cast<std::int64_t*>(data_.buffer.data()), std::int64_t{0});
            data_.type = TypeBoolean;
            break;

        case ValueType::Real:
            std::construct_at(reinterpret_cast<double*>(data_.buffer.data()), 0.0);
            data_.type = TypeReal;
            break;

        case ValueType::String:
            initSSO({});
            break;

        case ValueType::Array:
            std::construct_at(
                reinterpret_cast<ArrayStorage**>(data_.buffer.data()), new ArrayStorage());
            data_.type = TypeArray;
            break;

        case ValueType::Object:
            std::construct_at(
                reinterpret_cast<ObjectStorage**>(data_.buffer.data()), new ObjectStorage());
            data_.type = TypeObject;
            break;

        default:
            UNREACHABLE("json::Value::Value(ValueType) : invalid type");
    }
}

Value::Value(Int value) noexcept
{
    std::construct_at(
        reinterpret_cast<std::int64_t*>(data_.buffer.data()), static_cast<std::int64_t>(value));
    data_.type = TypeInt;
}

Value::Value(UInt value) noexcept
{
    std::construct_at(
        reinterpret_cast<std::int64_t*>(data_.buffer.data()), static_cast<std::int64_t>(value));
    data_.type = TypeUInt;
}

Value::Value(double value) noexcept
{
    std::construct_at(reinterpret_cast<double*>(data_.buffer.data()), value);
    data_.type = TypeReal;
}

Value::Value(xrpl::Number const& value) : Value(to_string(value))
{
}

Value::Value(bool value) noexcept
{
    std::construct_at(
        reinterpret_cast<std::int64_t*>(data_.buffer.data()),
        static_cast<std::int64_t>(value ? 1 : 0));
    data_.type = TypeBoolean;
}

Value::Value(Value const& other)
{
    if (other.data_.type == TypeAllocatedString)
    {
        initAllocatedString(other.asStringView());
        return;
    }

    if (other.data_.type == TypeArray)
    {
        std::construct_at(
            reinterpret_cast<ArrayStorage**>(data_.buffer.data()),
            new ArrayStorage(*other.as<ArrayStorage*>()));
        data_.type = TypeArray;
        return;
    }

    if (other.data_.type == TypeObject)
    {
        std::construct_at(
            reinterpret_cast<ObjectStorage**>(data_.buffer.data()),
            new ObjectStorage(*other.as<ObjectStorage*>()));
        data_.type = TypeObject;
        return;
    }

    data_ = other.data_;
}

Value::~Value() noexcept
{
    if (data_.type == TypeAllocatedString)
    {
        freeString(asStringView());
    }
    else if (data_.type == TypeArray)
    {
        delete as<ArrayStorage*>();
    }
    else if (data_.type == TypeObject)
    {
        delete as<ObjectStorage*>();
    }
}

ValueType
Value::type() const noexcept
{
    if (data_.type <= TypeAllocatedString)
        return ValueType::String;

    switch (data_.type)
    {
        case TypeInt:
            return ValueType::Int;

        case TypeUInt:
            return ValueType::UInt;

        case TypeBoolean:
            return ValueType::Boolean;

        case TypeReal:
            return ValueType::Real;

        case TypeArray:
            return ValueType::Array;

        case TypeObject:
            return ValueType::Object;

        case TypeNull:
        default:
            return ValueType::Null;
    }
}

std::string_view
Value::asStringView() const noexcept
{
    if (isSSO())
        return {data_.buffer.data(), stringLength()};

    if (isAllocatedString() || isStaticString())
        return {as<char const*>(), stringLength()};

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
        return (as<std::int64_t>() != 0) ? "true" : "false";

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
        std::size_t len = 0;

        if constexpr (kSupportFloatingPointToChars)
        {
            auto [ptr, ec] =
                std::to_chars(buffer, buffer + sizeof(buffer), d, std::chars_format::general, 16);
            len = static_cast<std::size_t>(ptr - buffer);
        }
        else
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            len = static_cast<std::size_t>(std::snprintf(buffer, sizeof(buffer), "%.16g", d));
        }

        if (std::string_view const tmp(buffer, len);
            tmp.find_first_of(".eE") == std::string_view::npos)
        {
            buffer[len++] = '.';
            buffer[len++] = '0';
        }

        return {buffer, len};
    }

    xrpl::Throw<Error>(
        "Type (" + std::to_string(static_cast<int>(type())) + ") is not convertible to string");
}

Int
Value::asInt() const
{
    if (isIntegral())
    {
        if (auto const v = as<std::int64_t>(); std::in_range<Int>(v))
            return v;

        xrpl::Throw<Error>("integer value out of signed integer range");
    }

    if (isString())
        return beast::lexicalCastThrow<Int>(asStringView());

    if (isDouble())
    {
        auto v = as<double>();

        if (v >= std::numeric_limits<Int>::min() && v <= std::numeric_limits<Int>::max())
        {
            if constexpr (kPedanticFloatToIntConversions)
            {
                XRPL_ASSERT(
                    std::trunc(v) == v, "json::Value::asInt : double value has fractional part");
            }

            // Even if the range check above succeeded, the double can still
            // have a fractional part that casting will truncate toward zero.
            return static_cast<Int>(v);
        }

        xrpl::Throw<Error>("double value out of signed integer range");
    }

    if (isNull())
        return 0;

    xrpl::Throw<Error>("Type is not convertible to an integer");
}

UInt
Value::asUInt() const
{
    if (isIntegral())
    {
        if (auto const v = as<std::int64_t>(); std::in_range<UInt>(v))
            return v;

        xrpl::Throw<Error>("integer value out of unsigned integer range");
    }

    if (isString())
        return beast::lexicalCastThrow<UInt>(asStringView());

    if (isDouble())
    {
        auto v = as<double>();

        if (v >= std::numeric_limits<UInt>::min() && v <= std::numeric_limits<UInt>::max())
        {
            if constexpr (kPedanticFloatToIntConversions)
            {
                XRPL_ASSERT(
                    std::trunc(v) == v, "json::Value::asUInt : double value has fractional part");
            }

            // Even if the range check above succeeded, the double can still
            // have a fractional part that casting will truncate toward zero.
            return static_cast<UInt>(v);
        }

        xrpl::Throw<Error>("double value out of unsigned integer range");
    }

    if (isNull())
        return 0;

    xrpl::Throw<Error>("Type is not convertible to an unsigned integer");
}

UInt
Value::asAbsUInt() const
{
    if (isIntegral())
    {
        // Integers are only ever constructed from Int, UInt or bool so the
        // magnitude of the stored value always fits in a UInt.
        auto const v = as<std::int64_t>();
        return static_cast<UInt>(v < 0 ? -v : v);
    }

    if (isString())
    {
        auto const v = beast::lexicalCastThrow<std::int64_t>(asStringView());

        // Compute the magnitude in unsigned arithmetic, so that negating
        // the most negative value is well defined.
        auto const magnitude = v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v)
                                     : static_cast<std::uint64_t>(v);

        if (magnitude > kMaxUInt)
            xrpl::Throw<Error>("String out of unsigned integer range");

        return static_cast<UInt>(magnitude);
    }

    if (isDouble())
    {
        auto const v = std::fabs(as<double>());

        if (v <= kMaxUInt)
            return static_cast<UInt>(v);

        xrpl::Throw<Error>("Real out of unsigned integer range");
    }

    if (isNull())
        return 0;

    xrpl::Throw<Error>("Type is not convertible to an unsigned integer");
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

    xrpl::Throw<Error>("Type is not convertible to a double");
}

bool
Value::asBool() const noexcept
{
    if (isString())
        return stringLength() != 0;

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
        case TypeArray:
            return static_cast<UInt>(as<ArrayStorage*>()->size());

        case TypeObject:
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
        return stringLength() != 0;

    if (isArray())
        return !as<ArrayStorage*>()->empty();

    if (isObject())
        return !as<ObjectStorage*>()->empty();

    return true;
}

Value&
Value::operator[](UInt index)
{
    if (data_.type == TypeNull)
        *this = Value(ValueType::Array);

    if (data_.type != TypeArray)
        xrpl::Throw<Error>("Attempt to treat non-array value as array.");

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
    XRPL_ASSERT(index <= arr->size(), "json::Value::operator[](UInt) : index out of bounds");

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
        data_.type == TypeArray || data_.type == TypeNull,
        "json::Value::operator[] : Attempt to treat non-array value as array.");

    if (data_.type == TypeArray)
    {
        auto* arr = as<ArrayStorage const*>();

        if (auto it = arr->find(index); it != arr->end())
            return it->second;
    }

    return kNull;
}

Value const&
Value::operator[](std::string_view key) const
{
    XRPL_ASSERT(
        data_.type == TypeObject || data_.type == TypeNull,
        "json::Value::operator[] : Attempt to treat non-object value as "
        "object.");

    if (data_.type == TypeObject)
    {
        auto* obj = as<ObjectStorage const*>();
        auto it = obj->find(key);

        if (it != obj->end())
            return it->second;
    }

    return kNull;
}

Value
Value::get(std::size_t index, Value defaultValue) const
{
    if (data_.type == TypeArray)
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
    if (data_.type == TypeObject)
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
        data_.type == TypeNull || data_.type == TypeObject,
        "json::Value::removeMember : wrong type");

    if (data_.type == TypeObject)
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
Value::isMember(std::string_view key, std::optional<ValueType> type) const noexcept
{
    if (data_.type != TypeObject)
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
        data_.type == TypeNull || data_.type == TypeObject,
        "json::Value::getMemberNames : wrong type");

    Members members;

    if (data_.type == TypeObject)
    {
        auto* obj = as<ObjectStorage const*>();

        if (!obj->empty())
        {
            members.reserve(obj->size());

            for (auto const& [k, v] : *obj)
                members.emplace_back(k.cStr());
        }
    }

    return members;
}

std::partial_ordering
operator<=>(Value const& x, Value const& y) noexcept
{
    auto const xType = x.type();
    auto const yType = y.type();

    if ((xType == ValueType::Int || xType == ValueType::UInt) &&
        (yType == ValueType::Int || yType == ValueType::UInt))
        return x.as<std::int64_t>() <=> y.as<std::int64_t>();

    if (xType != yType)
        return xType <=> yType;

    if (xType == ValueType::Null)
        return std::partial_ordering::equivalent;

    if (xType == ValueType::Real)
        return x.as<double>() <=> y.as<double>();

    if (xType == ValueType::Boolean)
        return x.as<std::int64_t>() <=> y.as<std::int64_t>();

    if (xType == ValueType::String)
        return x.asStringView() <=> y.asStringView();

    if (xType == ValueType::Array)
        return *x.as<Value::ArrayStorage const*>() <=> *y.as<Value::ArrayStorage const*>();

    if (xType == ValueType::Object)
        return *x.as<Value::ObjectStorage const*>() <=> *y.as<Value::ObjectStorage const*>();

    UNREACHABLE("json::Value::operator<=> : impossible comparison");
    return std::partial_ordering::unordered;
}

bool
operator==(Value const& x, Value const& y) noexcept
{
    auto const xType = x.type();
    auto const yType = y.type();

    if ((xType == ValueType::Int || xType == ValueType::UInt) &&
        (yType == ValueType::Int || yType == ValueType::UInt))
        return x.as<std::int64_t>() == y.as<std::int64_t>();

    if (xType != yType)
        return false;

    if (xType == ValueType::Null)
        return true;

    if (xType == ValueType::Real)
        return x.as<double>() == y.as<double>();

    if (xType == ValueType::Boolean)
        return x.as<std::int64_t>() == y.as<std::int64_t>();

    if (xType == ValueType::String)
        return x.asStringView() == y.asStringView();

    if (xType == ValueType::Array)
        return *x.as<Value::ArrayStorage const*>() == *y.as<Value::ArrayStorage const*>();

    if (xType == ValueType::Object)
        return *x.as<Value::ObjectStorage const*>() == *y.as<Value::ObjectStorage const*>();

    UNREACHABLE("json::Value::operator== : impossible comparison");
    return false;
}

}  // namespace json
