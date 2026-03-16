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
    OR in CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json.h>

#include <charconv>
#include <cmath>
#include <cstring>
#include <string>

namespace Json {

//==============================================================================
// Writer implementation
//==============================================================================

namespace {

// clang-format off
inline constexpr std::uint8_t character_space       = 0x01;
inline constexpr std::uint8_t character_line_break  = 0x02;
inline constexpr std::uint8_t character_control     = 0x04;
inline constexpr std::uint8_t character_whitespace  = character_space | character_line_break;
// clang-format on

/** Maps all bytes to a set of flags and its JSON escape sequence.

    If the length field is zero, the character requires no escaping
    and can be passed through as-is. Otherwise, it is the length of
    the escape sequence.

    Named escape sequences (e.g. @c \\n, @c \\t) are always two bytes.
    Unicode escape sequences for control characters below 0x20 are
    six bytes (e.g. @c \\u0001).
 */
inline constexpr auto char_map = []() consteval {
    struct Entry
    {
        char data[6] = {};
        std::uint8_t length = 0;
        std::uint8_t flags = 0;
    };

    std::array<Entry, 256> table{};

    auto set = [&](unsigned char c, char const* s) {
        if (table[c].length == 0)
        {
            while (s[table[c].length] != 0)
            {
                table[c].data[table[c].length] = s[table[c].length];
                table[c].length++;
            }
        }
    };

    set('"', "\\\"");
    set('\\', "\\\\");
    set('\b', "\\b");
    set('\f', "\\f");
    set('\n', "\\n");
    set('\r', "\\r");
    set('\t', "\\t");

    auto xnib = [](unsigned int c) -> char {
        if (c <= 9)
            return static_cast<char>('0' + c);
        if (c < 16)
            return static_cast<char>('A' + (c - 10));
        throw "Invalid nibble";
    };

    char esc[7] = {'\\', 'u', '0', '0', 0, 0, 0};

    for (unsigned char c = 0; c < 0x20; ++c)
    {
        esc[4] = xnib((c & 0xF0) >> 4);
        esc[5] = xnib(c & 0x0F);
        set(c, esc);
    }

    table[' '].flags |= character_space;
    table['\t'].flags |= character_space;
    table['\r'].flags |= character_line_break;
    table['\n'].flags |= character_line_break;

    for (unsigned c = 0; c < 0x20; ++c)
        table[c].flags |= character_control;

    return table;
}();

[[nodiscard]] constexpr bool
is_character(char c, std::uint8_t mask) noexcept
{
    return (char_map[static_cast<unsigned char>(c)].flags & mask) != 0;
}

constexpr std::string_view json_quote = "\"";
constexpr std::string_view json_null = "null";
constexpr std::string_view json_empty_object = "{}";
constexpr std::string_view json_object_open = json_empty_object.substr(0, 1);
constexpr std::string_view json_object_close = json_empty_object.substr(1, 1);
constexpr std::string_view json_empty_array = "[]";
constexpr std::string_view json_array_open = json_empty_array.substr(0, 1);
constexpr std::string_view json_array_close = json_empty_array.substr(1, 1);

void
write_value(
    detail::StreamSink& sink,
    Value const& value,
    unsigned depth,
    FormatOptions const& fmt);

void
write_string(detail::StreamSink& sink, std::string_view value)
{
    sink.append(json_quote);

    std::size_t start = 0;

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (auto const& entry = char_map[static_cast<unsigned char>(value[i])];
            entry.length != 0) [[unlikely]]
        {
            sink.append(value.substr(start, i - start));
            sink.append(std::string_view(entry.data, entry.length));
            start = i + 1;
        }
    }

    if (auto leftover = value.substr(start); !leftover.empty())
        sink.append(leftover);

    sink.append(json_quote);
}

void
write_indent(detail::StreamSink& sink, unsigned depth, FormatOptions const& fmt)
{
    for (unsigned i = 0; i < depth; ++i)
        sink.append(fmt.indent);
}

template <typename WriteElement>
void
write_collection(
    detail::StreamSink& sink,
    Value const& value,
    FormatOptions const& fmt,
    WriteElement&& write_element)
{
    auto it = value.begin();
    XRPL_ASSERT(
        it != value.end(),
        "Json::write_collection : called an empty collection");

    write_element(it);

    for (++it; it != value.end(); ++it)
    {
        sink.append(",");
        sink.append(fmt.newline);
        write_element(it);
    }
}

void
write_array(
    detail::StreamSink& sink,
    Value const& value,
    unsigned depth,
    FormatOptions const& fmt)
{
    if (value.size() == 0)
    {
        sink.append(json_empty_array);
        return;
    }

    sink.append(json_array_open);
    sink.append(fmt.newline);

    write_collection(sink, value, fmt, [&](auto const& it) {
        write_indent(sink, depth + 1, fmt);
        write_value(sink, *it, depth + 1, fmt);
    });

    sink.append(fmt.newline);
    write_indent(sink, depth, fmt);
    sink.append(json_array_close);
}

void
write_object(
    detail::StreamSink& sink,
    Value const& value,
    unsigned depth,
    FormatOptions const& fmt)
{
    if (value.size() == 0)
    {
        sink.append(json_empty_object);
        return;
    }

    sink.append(json_object_open);
    sink.append(fmt.newline);

    write_collection(sink, value, fmt, [&](auto const& it) {
        write_indent(sink, depth + 1, fmt);
        write_string(sink, it.memberName());
        sink.append(fmt.key_sep);
        write_value(sink, *it, depth + 1, fmt);
    });

    sink.append(fmt.newline);
    write_indent(sink, depth, fmt);
    sink.append(json_object_close);
}

void
write_value(
    detail::StreamSink& sink,
    Value const& value,
    unsigned depth,
    FormatOptions const& fmt)
{
    auto const type = value.type();

    if (type == stringValue)
    {
        write_string(sink, value.asStringView());
        return;
    }

    if (type == arrayValue)
    {
        write_array(sink, value, depth, fmt);
        return;
    }

    if (type == objectValue)
    {
        write_object(sink, value, depth, fmt);
        return;
    }

    if (type == nullValue)
    {
        sink.append(json_null);
        return;
    }

    // RFC 8259 numbers are finite decimals and JSON has no representation for
    // NaN or +/-Infinity, so we cannot serialize non-finite double values. So
    // we follow the convention established by JavaScript's JSON.stringify and
    // emit null in its place.
    //
    // Note that asString() deliberately does NOT do this: it will render such
    // values as "nan" or "inf", as appropriate, for debugging purposes, so we
    // do this check at the serialization boundary instead of in the formatter
    // itself.
    if (type == realValue && !std::isfinite(value.asDouble()))
    {
        sink.append(json_null);
        return;
    }

    sink.append(value.asString());
}

}  // namespace

void
stream(Value const& jv, detail::StreamSink& sink, FormatOptions const& fmt)
{
    write_value(sink, jv, 0, fmt);
    // sink.append(fmt.newline);
}

//==============================================================================
// Parser implementation
//==============================================================================

namespace {

/** Determines how strictly we adhere to RFC 8259.

    If set, the presence of C and C++ style comments in the JSON will
    result in an error. If allowed, any comments that are encountered
    will be parsed and syntax-checked, but will otherwise be ignored.
*/
inline constexpr bool strict_rfc8259_compliance = true;

enum TokenType : unsigned int {
    tokenEndOfStream = 0,
    tokenObjectBegin,
    tokenObjectEnd,
    tokenArrayBegin,
    tokenArrayEnd,
    tokenSimpleString,
    tokenComplexString,
    tokenInteger,
    tokenDouble,
    tokenTrue,
    tokenFalse,
    tokenNull,
    tokenComma,
    tokenMemberSeparator,
    tokenError
};

struct Token
{
    TokenType type = tokenError;

    /** The token value.

        For string tokens (tokenSimpleString, tokenComplexString), this
        contains the string content with surrounding quotes excluded.

        For all other tokens, this contains the raw token text.
     */
    std::string_view value;
};

struct ParseState
{
    std::string_view const doc_;
    std::string_view rest_;

    std::uint16_t const nest_limit;
    std::uint16_t depth = 0;

    ParseState(std::string_view document, std::uint16_t nest_limit) noexcept
        : doc_(document), rest_(document), nest_limit(nest_limit)
    {
    }

    std::size_t
    offset() const noexcept
    {
        return doc_.size() - rest_.size();
    }

    std::nullopt_t
    error(std::string& into, std::string_view message) noexcept
    {
        into = "Parse error (" + std::to_string(offset()) +
            "): " + std::string(message);
        return std::nullopt;
    }

    bool
    match(std::string_view pattern) noexcept
    {
        if (rest_.starts_with(pattern))
        {
            rest_.remove_prefix(pattern.size());
            return true;
        }

        return false;
    }

    struct Descend
    {
        ParseState* state;

        explicit Descend(ParseState* state) noexcept : state(state)
        {
            ++state->depth;
        }

        ~Descend() noexcept
        {
            if (state)
                --state->depth;
        }

        Descend(Descend&& other) noexcept : state(other.state)
        {
            other.state = nullptr;
        }

        Descend&
        operator=(Descend&&) = delete;
        Descend(Descend const&) = delete;
        Descend&
        operator=(Descend const&) = delete;
    };

    std::optional<Descend>
    descend(std::string& into)
    {
        if (depth == nest_limit)
            return error(into, "Nesting limit exceeded");

        return Descend{this};
    }

    Token
    lex_string() noexcept
    {
        TokenType type = tokenSimpleString;
        auto const orig = rest_;

        while (!rest_.empty())
        {
            char const c = rest_.front();
            rest_.remove_prefix(1);

            if (c == '"')
                return {type, orig.substr(0, orig.size() - rest_.size() - 1)};

            if (c == '\\')
            {
                type = tokenComplexString;

                // Whether the escaped sequence is valid is not important
                // here. We just need to consume the escaped character to
                // avoid treating it as non-escaped in the next iteration.
                if (!rest_.empty())
                    rest_.remove_prefix(1);

                continue;
            }

            // An unescaped, embedded NUL is not allowed per RFC 8259, and
            // we do not accept it even when strict compliance mode is not
            // enabled. We do this because CZString assumes that an object
            // key will collate as a C string; an embedded NUL would break
            // that and corrupt comparison semantics.
            if (c == 0)
                return {tokenError, orig};

            if constexpr (strict_rfc8259_compliance)
            {
                // In strict compliance mode, we reject any unescaped
                // control characters.
                if (is_character(c, character_control))
                    return {tokenError, orig};
            }
        }

        return {tokenError, orig};
    }

    Token
    lex_number() noexcept
    {
        std::string_view const token = rest_;
        TokenType type = tokenInteger;

        if (rest_[0] == '-')
            rest_.remove_prefix(1);

        // Returns the number of continuous digits present in the input.
        auto count_digits = [&]() {
            std::size_t dc = 0;

            while (dc < rest_.size() && rest_[dc] >= '0' && rest_[dc] <= '9')
                ++dc;

            return dc;
        };

        std::size_t dc = count_digits();

        if (dc == 0)
            return {tokenError, token};

        // RFC 8259 does not allow leading zeroes in the integer part, so if
        // the leading character is a zero, then no other digits must follow.
        if (rest_[0] == '0' && dc != 1)
            return {tokenError, token};

        rest_.remove_prefix(dc);

        if (!rest_.empty() && rest_[0] == '.')
        {
            type = tokenDouble;

            rest_.remove_prefix(1);

            dc = count_digits();

            if (dc == 0)
                return {tokenError, token};

            rest_.remove_prefix(dc);
        }

        if (!rest_.empty() && (rest_[0] == 'E' || rest_[0] == 'e'))
        {
            type = tokenDouble;

            rest_.remove_prefix(1);

            if (!rest_.empty() && (rest_[0] == '+' || rest_[0] == '-'))
                rest_.remove_prefix(1);

            dc = count_digits();

            if (dc == 0)
                return {tokenError, token};

            rest_.remove_prefix(dc);
        }

        return {type, token.substr(0, token.size() - rest_.size())};
    }

    Token
    lex() noexcept
    {
        while (!rest_.empty())
        {
            while (!rest_.empty() &&
                   is_character(rest_.front(), character_whitespace))
                rest_.remove_prefix(1);

            if constexpr (!strict_rfc8259_compliance)
            {
                if (match("//"))
                {
                    while (!rest_.empty() &&
                           !is_character(rest_.front(), character_line_break))
                        rest_.remove_prefix(1);

                    continue;
                }

                if (match("/*"))
                {
                    auto const pos = rest_.find("*/");

                    if (pos == std::string_view::npos)
                        return {tokenError, rest_};

                    rest_.remove_prefix(pos + 2);
                    continue;
                }
            }

            if (rest_.empty())
                break;

            char const c = rest_.front();

            if (c == '-' || (c >= '0' && c <= '9'))
                return lex_number();

            auto const before = rest_;
            rest_.remove_prefix(1);

            switch (c)
            {
                case '{':
                    return {tokenObjectBegin, before.substr(0, 1)};

                case '}':
                    return {tokenObjectEnd, before.substr(0, 1)};

                case '[':
                    return {tokenArrayBegin, before.substr(0, 1)};

                case ']':
                    return {tokenArrayEnd, before.substr(0, 1)};

                case ',':
                    return {tokenComma, before.substr(0, 1)};

                case ':':
                    return {tokenMemberSeparator, before.substr(0, 1)};

                case '"':
                    return lex_string();

                case 't':
                    if (match("rue"))
                        return {tokenTrue, before.substr(0, 4)};
                    break;

                case 'f':
                    if (match("alse"))
                        return {tokenFalse, before.substr(0, 5)};
                    break;

                case 'n':
                    if (match("ull"))
                        return {tokenNull, before.substr(0, 4)};
                    break;

                default:
                    break;
            }

            return {tokenError, before.substr(0, before.size() - rest_.size())};
        }

        return {tokenEndOfStream, rest_.substr(0, 0)};
    }
};

void
appendCodePointAsUTF8(std::string& out, char32_t cp)
{
    if (cp <= 0x7f)
    {
        out += static_cast<char>(cp);
    }
    else if (cp <= 0x7FF)
    {
        out += static_cast<char>(0xC0 | (0x1f & (cp >> 6)));
        out += static_cast<char>(0x80 | (0x3f & cp));
    }
    else if (cp <= 0xFFFF)
    {
        out += static_cast<char>(0xE0 | (0x0f & (cp >> 12)));
        out += static_cast<char>(0x80 | (0x3f & (cp >> 6)));
        out += static_cast<char>(0x80 | (0x3f & cp));
    }
    else if (cp <= 0x10FFFF)
    {
        out += static_cast<char>(0xF0 | (0x07 & (cp >> 18)));
        out += static_cast<char>(0x80 | (0x3f & (cp >> 12)));
        out += static_cast<char>(0x80 | (0x3f & (cp >> 6)));
        out += static_cast<char>(0x80 | (0x3f & cp));
    }
}

std::optional<char32_t>
decodeUnicodeEscapeSequence(
    ParseState& state,
    std::string& error,
    std::string_view& sv)
{
    if (sv.size() < 4)
        return state.error(
            error,
            "Bad unicode escape sequence in string: four digits expected.");

    char32_t unicode = 0;

    for (int index = 0; index < 4; ++index)
    {
        char const c = sv[index];
        unicode *= 16;

        if (c >= '0' && c <= '9')
            unicode += c - '0';
        else if (c >= 'a' && c <= 'f')
            unicode += c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            unicode += c - 'A' + 10;
        else
            return state.error(
                error,
                "Bad unicode escape sequence in string: hexadecimal digit "
                "expected.");
    }

    sv.remove_prefix(4);
    return unicode;
}

std::optional<char32_t>
decodeUnicodeCodePoint(
    ParseState& state,
    std::string& error,
    std::string_view& sv)
{
    auto unicode = decodeUnicodeEscapeSequence(state, error, sv);

    if (!unicode)
        return std::nullopt;

    if (*unicode >= 0xD800 && *unicode <= 0xDBFF)
    {
        if (sv.size() < 6 || sv[0] != '\\' || sv[1] != 'u')
            return state.error(error, "expected surrogate pair");

        sv.remove_prefix(2);

        auto surrogate = decodeUnicodeEscapeSequence(state, error, sv);

        if (!surrogate)
            return std::nullopt;

        if (*surrogate < 0xDC00 || *surrogate > 0xDFFF)
            return state.error(
                error, "expected low surrogate in surrogate pair");

        return char32_t(
            0x10000 + ((*unicode & 0x3FF) << 10) + (*surrogate & 0x3FF));
    }

    if (*unicode >= 0xDC00 && *unicode <= 0xDFFF)
        return state.error(
            error, "unexpected low surrogate without preceding high surrogate");

    return unicode;
}

std::optional<std::string>
decodeString(ParseState& state, std::string& error, std::string_view content)
{
    std::string decoded;
    decoded.reserve(content.size());

    while (!content.empty())
    {
        auto const pos = content.find('\\');

        decoded.append(content.substr(0, pos));

        if (pos == std::string_view::npos)
            break;

        content.remove_prefix(pos + 1);

        if (content.empty())
            return state.error(error, "Empty escape sequence in string");

        char const escape = content.front();
        content.remove_prefix(1);

        switch (escape)
        {
            case '"':
                decoded += '"';
                break;
            case '/':
                decoded += '/';
                break;
            case '\\':
                decoded += '\\';
                break;
            case 'b':
                decoded += '\b';
                break;
            case 'f':
                decoded += '\f';
                break;
            case 'n':
                decoded += '\n';
                break;
            case 'r':
                decoded += '\r';
                break;
            case 't':
                decoded += '\t';
                break;
            case 'u': {
                auto unicode = decodeUnicodeCodePoint(state, error, content);
                if (!unicode)
                    return std::nullopt;
                appendCodePointAsUTF8(decoded, *unicode);
                break;
            }
            default:
                return state.error(error, "Bad escape sequence in string");
        }
    }

    return decoded;
}

std::optional<Value>
decodeNumber(ParseState& state, std::string& error, Token const& token)
{
    std::int64_t v;

    static_assert(
        std::numeric_limits<Value::Int>::min() >=
                std::numeric_limits<std::int64_t>::min() &&
            std::numeric_limits<Value::Int>::max() <=
                std::numeric_limits<std::int64_t>::max() &&
            std::numeric_limits<Value::UInt>::max() <=
                std::numeric_limits<std::int64_t>::max(),
        "The existing integer/unsigned integer disambiguating logic needs "
        "updating.");

    auto [p, ec] = std::from_chars(
        token.value.data(), token.value.data() + token.value.size(), v);

    if (ec != std::errc() || p != token.value.data() + token.value.size())
        return state.error(
            error, "'" + std::string(token.value) + "' is not a valid number.");

    if (std::in_range<Value::Int>(v))
        return Value(static_cast<Value::Int>(v));

    if (std::in_range<Value::UInt>(v))
        return Value(static_cast<Value::UInt>(v));

    return state.error(
        error,
        "'" + std::string(token.value) + "' exceeds the allowable range.");
}

std::optional<Value>
decodeDouble(ParseState& state, std::string& error, Token const& token)
{
    double v;

    auto [p, ec] = std::from_chars(
        token.value.data(), token.value.data() + token.value.size(), v);

    if (ec != std::errc() || p != token.value.data() + token.value.size())
        return state.error(
            error, "'" + std::string(token.value) + "' is not a number.");

    return Value(v);
}

// We need this forward declaration because readValue calls readContainer,
// and the element readers passed into readContainer call readValue.
std::optional<Value>
readValue(ParseState& state, std::string& error, Token const& token);

bool
readObjectValue(
    ParseState& state,
    std::string& error,
    Value& result,
    std::string_view key)
{
    if (auto const colon = state.lex(); colon.type != tokenMemberSeparator)
    {
        state.error(error, "Missing ':' after object member name.");
        return false;
    }

    if (result.isMember(key))
    {
        state.error(error, "Key appears twice.");
        return false;
    }

    if (auto member = readValue(state, error, state.lex()))
    {
        result[key] = std::move(*member);
        return true;
    }

    return false;
}

bool
readObjectKeyValue(
    ParseState& state,
    std::string& error,
    Value& result,
    Token const& token)
{
    // For simplicity and to avoid weirdness involving homoglyphs, control
    // characters and the like, we require that keys be non-empty and only
    // use printable ASCII characters. This limitation does not affect any
    // existing endpoints of schemas that are in use. This is a limitation
    // that we impose, as allowed under RFC 8259 section 9.
    auto verify_key_name = [&](std::string_view name) {
        if (name.empty())
        {
            state.error(error, "Object member names must not be empty.");
            return false;
        }

        if (name.size() >= default_key_size_limit)
        {
            state.error(error, "Object member name too long.");
            return false;
        }

        if (!std::ranges::all_of(
                name, [](unsigned char c) { return c >= 0x20 && c <= 0x7E; }))
        {
            state.error(error, "Object member names must be printable ASCII.");
            return false;
        }

        return true;
    };

    if (token.type == tokenSimpleString)
    {
        if (!verify_key_name(token.value))
            return false;

        return readObjectValue(state, error, result, token.value);
    }

    if (token.type == tokenComplexString)
    {
        auto key = decodeString(state, error, token.value);

        if (!key)
            return false;

        if (!verify_key_name(*key))
            return false;

        return readObjectValue(state, error, result, *key);
    }

    state.error(error, "Missing member name.");
    return false;
}

template <typename ElementReader>
std::optional<Value>
readContainer(
    ParseState& state,
    std::string& error,
    Value initial,
    TokenType endToken,
    std::string_view missingCommaError,
    ElementReader&& readElement)
{
    auto depth = state.descend(error);

    if (!depth)
        return std::nullopt;

    if (auto token = state.lex(); token.type != endToken)
    {
        if (!readElement(state, error, initial, token))
            return std::nullopt;

        for (token = state.lex(); token.type == tokenComma; token = state.lex())
        {
            if (!readElement(state, error, initial, state.lex()))
                return std::nullopt;
        }

        if (token.type != endToken)
            return state.error(error, missingCommaError);
    }

    return initial;
}

std::optional<Value>
readValue(ParseState& state, std::string& error, Token const& token)
{
    auto verifyString = [&](std::string_view s) -> std::optional<Value> {
        if (s.size() < default_string_size_limit) [[likely]]
            return Value(s);

        return state.error(error, "Limit error: string exceeds maximum size.");
    };

    switch (token.type)
    {
        case tokenObjectBegin:
            return readContainer(
                state,
                error,
                Value(objectValue),
                tokenObjectEnd,
                "Missing ',' or '}' in object declaration.",
                [](ParseState& state,
                   std::string& error,
                   Value& result,
                   Token const& token) {
                    return readObjectKeyValue(state, error, result, token);
                });

        case tokenArrayBegin:
            return readContainer(
                state,
                error,
                Value(arrayValue),
                tokenArrayEnd,
                "Missing ',' or ']' in array declaration.",
                [](ParseState& state,
                   std::string& error,
                   Value& result,
                   Token const& token) {
                    if (auto element = readValue(state, error, token))
                    {
                        result[result.size()] = std::move(*element);
                        return true;
                    }

                    return false;
                });

        case tokenInteger:
            return decodeNumber(state, error, token);

        case tokenDouble:
            return decodeDouble(state, error, token);

        case tokenSimpleString:
            return verifyString(token.value);

        case tokenComplexString:
            if (auto s = decodeString(state, error, token.value)) [[likely]]
                return verifyString(*s);

            return state.error(
                error, "Parse error: unable to decode complex string.");

        case tokenTrue:
            return Value(true);

        case tokenFalse:
            return Value(false);

        case tokenNull:
            return Value();

        default:
            return state.error(
                error, "Syntax error: value, object or array expected.");
    }
}

}  // namespace

std::optional<Value>
load(std::string_view document, std::string& error, std::uint16_t nest_limit)
{
    try
    {
        if (nest_limit == 0)
        {
            error = "Invalid nest limit.";
            return std::nullopt;
        }

        ParseState state{document, nest_limit};

        if (document.size() >= default_document_size_limit)
            return state.error(error, "The JSON document is too large.");

        auto root = readValue(state, error, state.lex());

        if (root)
        {
            if (!root->isArray() && !root->isObject() && !root->isNull())
                return state.error(
                    error, "Expected a single array, object, or null.");

            if (state.lex().type != tokenEndOfStream)
                return state.error(error, "Unexpected trailing content.");
        }

        return root;
    }
    catch (...)
    {
        // There's not much we can do about an exception anyways, so
        // we report it back as a regular parsing failure.
        error = "Parsing resulted in an exception";
        return std::nullopt;
    }
}

}  // namespace Json
