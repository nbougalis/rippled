#ifndef PROTOCOL_GET_OR_THROW_H_
#define PROTOCOL_GET_OR_THROW_H_

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/exception_buffer.h>
#include <xrpl/json/json.h>
#include <xrpl/protocol/SField.h>

#include <charconv>
#include <exception>
#include <optional>
#include <string>

namespace Json {

struct MissingKeyError : std::exception, protected ripple::exception_buffer
{
    MissingKeyError(StaticString const& k) noexcept
        : exception_buffer("Missing JSON key")
    {
        append(" '");
        append(k);
        append("'");
    }

    const char*
    what() const noexcept override
    {
        return c_str();
    }
};

struct TypeMismatchError : std::exception, protected ripple::exception_buffer
{
    TypeMismatchError(StaticString const& k, std::string_view et) noexcept
        : exception_buffer("Type mismatch on JSON key")
    {
        append(" '");
        append(k);
        append("'");

        if (!et.empty())
        {
            append("; expected type: ");
            append(et);
        }
    }

    const char*
    what() const noexcept override
    {
        return c_str();
    }
};

template <class T>
T
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    static_assert(sizeof(T) == -1, "This function must be specialized");
}

template <>
inline std::string
getOrThrow(Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<MissingKeyError>(key);

    Value const& inner = v[key];
    if (!inner.isString())
        Throw<TypeMismatchError>(key, "string");
    return inner.asString();
}

// Note, this allows integer numeric fields to act as bools
template <>
inline bool
getOrThrow(Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<MissingKeyError>(key);
    Value const& inner = v[key];
    if (inner.isBool())
        return inner.asBool();
    if (!inner.isIntegral())
        Throw<TypeMismatchError>(key, "bool");

    return inner.asInt() != 0;
}

template <>
inline std::uint64_t
getOrThrow(Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<MissingKeyError>(key);
    Value const& inner = v[key];
    if (inner.isUInt())
        return inner.asUInt();
    if (inner.isInt())
    {
        auto const r = inner.asInt();
        if (r < 0)
            Throw<TypeMismatchError>(key, "uint64");
        return r;
    }
    if (inner.isString())
    {
        auto const s = inner.asString();
        // parse as hex
        std::uint64_t val;

        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val, 16);

        if (ec != std::errc() || (p != s.data() + s.size()))
            Throw<TypeMismatchError>(key, "uint64");
        return val;
    }
    Throw<TypeMismatchError>(key, "uint64");
}

template <>
inline ripple::Buffer
getOrThrow(Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    std::string const hex = getOrThrow<std::string>(v, field);
    if (auto const r = strUnHex(hex))
    {
        // TODO: mismatch between a buffer and a blob
        return Buffer{r->data(), r->size()};
    }
    Throw<TypeMismatchError>(field.getJsonName(), "Buffer");
}

// This function may be used by external projects (like the witness server).
template <class T>
std::optional<T>
getOptional(Value const& v, ripple::SField const& field)
{
    try
    {
        return getOrThrow<T>(v, field);
    }
    catch (...)
    {
    }
    return {};
}

}  // namespace Json

#endif  // PROTOCOL_GET_OR_THROW_H_
