#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace json {

//==============================================================================
// Writer implementation
//==============================================================================

namespace {

// clang-format off
inline constexpr std::uint8_t kCharacterSpace       = 0x01;
inline constexpr std::uint8_t kCharacterLineBreak  = 0x02;
inline constexpr std::uint8_t kCharacterControl     = 0x04;
inline constexpr std::uint8_t kCharacterWhitespace  = kCharacterSpace | kCharacterLineBreak;
// clang-format on

/**
 * Maps all bytes to a set of flags and its JSON escape sequence.
 *
 * If the length field is zero, the character requires no escaping
 * and can be passed through as-is. Otherwise, it is the length of
 * the escape sequence.
 *
 * Named escape sequences (e.g. @c \\n, @c \\t) are always two bytes.
 * Unicode escape sequences for control characters below 0x20 are
 * six bytes (e.g. @c \\u0001).
 */
inline constexpr auto kCharMap = []() consteval {
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

    table[' '].flags |= kCharacterSpace;
    table['\t'].flags |= kCharacterSpace;
    table['\r'].flags |= kCharacterLineBreak;
    table['\n'].flags |= kCharacterLineBreak;

    for (unsigned c = 0; c < 0x20; ++c)
        table[c].flags |= kCharacterControl;

    return table;
}();

[[nodiscard]] constexpr bool
isCharacter(char c, std::uint8_t mask) noexcept
{
    return (kCharMap[static_cast<unsigned char>(c)].flags & mask) != 0;
}

constexpr std::string_view kJsonQuote = "\"";
constexpr std::string_view kJsonNull = "null";
constexpr std::string_view kJsonEmptyObject = "{}";
constexpr std::string_view kJsonObjectOpen = kJsonEmptyObject.substr(0, 1);
constexpr std::string_view kJsonObjectClose = kJsonEmptyObject.substr(1, 1);
constexpr std::string_view kJsonEmptyArray = "[]";
constexpr std::string_view kJsonArrayOpen = kJsonEmptyArray.substr(0, 1);
constexpr std::string_view kJsonArrayClose = kJsonEmptyArray.substr(1, 1);

void
writeValue(detail::StreamSink& sink, Value const& value, unsigned depth, FormatOptions const& fmt);

void
writeString(detail::StreamSink& sink, std::string_view value)
{
    sink.append(kJsonQuote);

    std::size_t start = 0;

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (auto const& entry = kCharMap[static_cast<unsigned char>(value[i])]; entry.length != 0)
            [[unlikely]]
        {
            sink.append(value.substr(start, i - start));
            sink.append(std::string_view(entry.data, entry.length));
            start = i + 1;
        }
    }

    if (auto leftover = value.substr(start); !leftover.empty())
        sink.append(leftover);

    sink.append(kJsonQuote);
}

void
writeIndent(detail::StreamSink& sink, unsigned depth, FormatOptions const& fmt)
{
    for (unsigned i = 0; i < depth; ++i)
        sink.append(fmt.indent);
}

template <typename WriteElement>
void
writeCollection(
    detail::StreamSink& sink,
    Value const& value,
    FormatOptions const& fmt,
    WriteElement&& writeElement)
{
    auto it = value.begin();
    XRPL_ASSERT(it != value.end(), "json::writeCollection : called an empty collection");

    writeElement(it);

    for (++it; it != value.end(); ++it)
    {
        sink.append(",");
        sink.append(fmt.newline);
        writeElement(it);
    }
}

void
writeArray(detail::StreamSink& sink, Value const& value, unsigned depth, FormatOptions const& fmt)
{
    if (value.size() == 0)
    {
        sink.append(kJsonEmptyArray);
        return;
    }

    sink.append(kJsonArrayOpen);
    sink.append(fmt.newline);

    writeCollection(sink, value, fmt, [&](auto const& it) {
        writeIndent(sink, depth + 1, fmt);
        writeValue(sink, *it, depth + 1, fmt);
    });

    sink.append(fmt.newline);
    writeIndent(sink, depth, fmt);
    sink.append(kJsonArrayClose);
}

void
writeObject(detail::StreamSink& sink, Value const& value, unsigned depth, FormatOptions const& fmt)
{
    if (value.size() == 0)
    {
        sink.append(kJsonEmptyObject);
        return;
    }

    sink.append(kJsonObjectOpen);
    sink.append(fmt.newline);

    writeCollection(sink, value, fmt, [&](auto const& it) {
        writeIndent(sink, depth + 1, fmt);
        writeString(sink, it.memberName());
        sink.append(fmt.keySep);
        writeValue(sink, *it, depth + 1, fmt);
    });

    sink.append(fmt.newline);
    writeIndent(sink, depth, fmt);
    sink.append(kJsonObjectClose);
}

void
writeValue(detail::StreamSink& sink, Value const& value, unsigned depth, FormatOptions const& fmt)
{
    auto const type = value.type();

    if (type == ValueType::String)
    {
        writeString(sink, value.asStringView());
        return;
    }

    if (type == ValueType::Array)
    {
        writeArray(sink, value, depth, fmt);
        return;
    }

    if (type == ValueType::Object)
    {
        writeObject(sink, value, depth, fmt);
        return;
    }

    if (type == ValueType::Null)
    {
        sink.append(kJsonNull);
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
    if (type == ValueType::Real && !std::isfinite(value.asDouble()))
    {
        sink.append(kJsonNull);
        return;
    }

    sink.append(value.asString());
}

}  // namespace

void
stream(Value const& jv, detail::StreamSink& sink, FormatOptions const& fmt)
{
    writeValue(sink, jv, 0, fmt);
    // sink.append(fmt.newline);
}

//==============================================================================
// Parser implementation
//==============================================================================

namespace {

/**
 * Determines how strictly we adhere to RFC 8259.
 *
 * If set, the presence of C and C++ style comments in the JSON will
 * result in an error. If allowed, any comments that are encountered
 * will be parsed and syntax-checked, but will otherwise be ignored.
 */
inline constexpr bool kStrictRfc8259Compliance = true;

enum class TokenType : std::uint8_t {
    EndOfStream = 0,
    ObjectBegin,
    ObjectEnd,
    ArrayBegin,
    ArrayEnd,
    SimpleString,
    ComplexString,
    Integer,
    Double,
    True,
    False,
    Null,
    Comma,
    MemberSeparator,
    Error
};

struct Token
{
    TokenType type = TokenType::Error;

    /**
     * The token value.
     *
     * For string tokens (TokenType::SimpleString, TokenType::ComplexString), this
     * contains the string content with surrounding quotes excluded.
     *
     * For all other tokens, this contains the raw token text.
     */
    std::string_view value;
};

struct ParseState
{
    std::string_view const doc;
    std::string_view rest;

    std::uint16_t const nestLimit;
    std::uint16_t depth = 0;

    ParseState(std::string_view document, std::uint16_t nestLimit) noexcept
        : doc(document), rest(document), nestLimit(nestLimit)
    {
    }

    [[nodiscard]] std::size_t
    offset() const noexcept
    {
        return doc.size() - rest.size();
    }

    std::nullopt_t
    error(std::string& into, std::string_view message) const noexcept
    {
        into = "Parse error (" + std::to_string(offset()) + "): " + std::string(message);
        return std::nullopt;
    }

    bool
    match(std::string_view pattern) noexcept
    {
        if (rest.starts_with(pattern))
        {
            rest.remove_prefix(pattern.size());
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
            if (state != nullptr)
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
        if (depth == nestLimit)
            return error(into, "Nesting limit exceeded");

        return Descend{this};
    }

    Token
    lexString() noexcept
    {
        TokenType type = TokenType::SimpleString;
        auto const orig = rest;

        while (!rest.empty())
        {
            char const c = rest.front();
            rest.remove_prefix(1);

            if (c == '"')
                return {.type = type, .value = orig.substr(0, orig.size() - rest.size() - 1)};

            if (c == '\\')
            {
                type = TokenType::ComplexString;

                // Whether the escaped sequence is valid is not important
                // here. We just need to consume the escaped character to
                // avoid treating it as non-escaped in the next iteration.
                if (!rest.empty())
                    rest.remove_prefix(1);

                continue;
            }

            // An unescaped, embedded NUL is not allowed per RFC 8259, and
            // we do not accept it even when strict compliance mode is not
            // enabled. We do this because CZString assumes that an object
            // key will collate as a C string; an embedded NUL would break
            // that and corrupt comparison semantics.
            if (c == 0)
                return {.type = TokenType::Error, .value = orig};

            if constexpr (kStrictRfc8259Compliance)
            {
                // In strict compliance mode, we reject any unescaped
                // control characters.
                if (isCharacter(c, kCharacterControl))
                    return {.type = TokenType::Error, .value = orig};
            }
        }

        return {.type = TokenType::Error, .value = orig};
    }

    Token
    lexNumber() noexcept
    {
        std::string_view const token = rest;
        TokenType type = TokenType::Integer;

        if (rest[0] == '-')
            rest.remove_prefix(1);

        // Returns the number of continuous digits present in the input.
        auto countDigits = [&]() {
            std::size_t dc = 0;

            while (dc < rest.size() && rest[dc] >= '0' && rest[dc] <= '9')
                ++dc;

            return dc;
        };

        std::size_t dc = countDigits();

        if (dc == 0)
            return {.type = TokenType::Error, .value = token};

        // RFC 8259 does not allow leading zeroes in the integer part, so if
        // the leading character is a zero, then no other digits must follow.
        if (rest[0] == '0' && dc != 1)
            return {.type = TokenType::Error, .value = token};

        rest.remove_prefix(dc);

        if (!rest.empty() && rest[0] == '.')
        {
            type = TokenType::Double;

            rest.remove_prefix(1);

            dc = countDigits();

            if (dc == 0)
                return {.type = TokenType::Error, .value = token};

            rest.remove_prefix(dc);
        }

        if (!rest.empty() && (rest[0] == 'E' || rest[0] == 'e'))
        {
            type = TokenType::Double;

            rest.remove_prefix(1);

            if (!rest.empty() && (rest[0] == '+' || rest[0] == '-'))
                rest.remove_prefix(1);

            dc = countDigits();

            if (dc == 0)
                return {.type = TokenType::Error, .value = token};

            rest.remove_prefix(dc);
        }

        return {.type = type, .value = token.substr(0, token.size() - rest.size())};
    }

    Token
    lex() noexcept
    {
        while (!rest.empty())
        {
            while (!rest.empty() && isCharacter(rest.front(), kCharacterWhitespace))
                rest.remove_prefix(1);

            if constexpr (!kStrictRfc8259Compliance)
            {
                if (match("//"))
                {
                    while (!rest.empty() && !isCharacter(rest.front(), kCharacterLineBreak))
                        rest.remove_prefix(1);

                    continue;
                }

                if (match("/*"))
                {
                    auto const pos = rest.find("*/");

                    if (pos == std::string_view::npos)
                        return {.type = TokenType::Error, .value = rest};

                    rest.remove_prefix(pos + 2);
                    continue;
                }
            }

            if (rest.empty())
                break;

            char const c = rest.front();

            if (c == '-' || (c >= '0' && c <= '9'))
                return lexNumber();

            auto const before = rest;
            rest.remove_prefix(1);

            switch (c)
            {
                case '{':
                    return {.type = TokenType::ObjectBegin, .value = before.substr(0, 1)};

                case '}':
                    return {.type = TokenType::ObjectEnd, .value = before.substr(0, 1)};

                case '[':
                    return {.type = TokenType::ArrayBegin, .value = before.substr(0, 1)};

                case ']':
                    return {.type = TokenType::ArrayEnd, .value = before.substr(0, 1)};

                case ',':
                    return {.type = TokenType::Comma, .value = before.substr(0, 1)};

                case ':':
                    return {.type = TokenType::MemberSeparator, .value = before.substr(0, 1)};

                case '"':
                    return lexString();

                case 't':
                    if (match("rue"))
                        return {.type = TokenType::True, .value = before.substr(0, 4)};
                    break;

                case 'f':
                    if (match("alse"))
                        return {.type = TokenType::False, .value = before.substr(0, 5)};
                    break;

                case 'n':
                    if (match("ull"))
                        return {.type = TokenType::Null, .value = before.substr(0, 4)};
                    break;

                default:
                    break;
            }

            return {
                .type = TokenType::Error, .value = before.substr(0, before.size() - rest.size())};
        }

        return {.type = TokenType::EndOfStream, .value = rest.substr(0, 0)};
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
decodeUnicodeEscapeSequence(ParseState& state, std::string& error, std::string_view& sv)
{
    if (sv.size() < 4)
        return state.error(error, "Bad unicode escape sequence in string: four digits expected.");

    char32_t unicode = 0;

    for (int index = 0; index < 4; ++index)
    {
        char const c = sv[index];
        unicode *= 16;

        if (c >= '0' && c <= '9')
        {
            unicode += c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            unicode += c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            unicode += c - 'A' + 10;
        }
        else
        {
            return state.error(
                error,
                "Bad unicode escape sequence in string: hexadecimal digit "
                "expected.");
        }
    }

    sv.remove_prefix(4);
    return unicode;
}

std::optional<char32_t>
decodeUnicodeCodePoint(ParseState& state, std::string& error, std::string_view& sv)
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
            return state.error(error, "expected low surrogate in surrogate pair");

        return char32_t(0x10000 + ((*unicode & 0x3FF) << 10) + (*surrogate & 0x3FF));
    }

    if (*unicode >= 0xDC00 && *unicode <= 0xDFFF)
        return state.error(error, "unexpected low surrogate without preceding high surrogate");

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
    std::int64_t v = 0;

    static_assert(
        std::numeric_limits<Value::Int>::min() >= std::numeric_limits<std::int64_t>::min() &&
            std::numeric_limits<Value::Int>::max() <= std::numeric_limits<std::int64_t>::max() &&
            std::numeric_limits<Value::UInt>::max() <= std::numeric_limits<std::int64_t>::max(),
        "The existing integer/unsigned integer disambiguating logic needs "
        "updating.");

    auto [p, ec] = std::from_chars(token.value.data(), token.value.data() + token.value.size(), v);

    if (ec != std::errc() || p != token.value.data() + token.value.size())
        return state.error(error, "'" + std::string(token.value) + "' is not a valid number.");

    if (std::in_range<Value::Int>(v))
        return Value(static_cast<Value::Int>(v));

    if (std::in_range<Value::UInt>(v))
        return Value(static_cast<Value::UInt>(v));

    return state.error(error, "'" + std::string(token.value) + "' exceeds the allowable range.");
}

std::optional<Value>
decodeDouble(ParseState& state, std::string& error, Token const& token)
{
    double v = 0.0;

    auto [p, ec] = std::from_chars(token.value.data(), token.value.data() + token.value.size(), v);

    if (ec != std::errc() || p != token.value.data() + token.value.size())
        return state.error(error, "'" + std::string(token.value) + "' is not a number.");

    return Value(v);
}

// We need this forward declaration because readValue calls readContainer,
// and the element readers passed into readContainer call readValue.
std::optional<Value>
readValue(ParseState& state, std::string& error, Token const& token);

bool
readObjectValue(ParseState& state, std::string& error, Value& result, std::string_view key)
{
    if (auto const colon = state.lex(); colon.type != TokenType::MemberSeparator)
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
readObjectKeyValue(ParseState& state, std::string& error, Value& result, Token const& token)
{
    // For simplicity and to avoid weirdness involving homoglyphs, control
    // characters and the like, we require that keys be non-empty and only
    // use printable ASCII characters. This limitation does not affect any
    // existing endpoints of schemas that are in use. This is a limitation
    // that we impose, as allowed under RFC 8259 section 9.
    auto verifyKeyName = [&](std::string_view name) {
        if (name.empty())
        {
            state.error(error, "Object member names must not be empty.");
            return false;
        }

        if (name.size() >= kDefaultKeySizeLimit)
        {
            state.error(error, "Object member name too long.");
            return false;
        }

        if (!std::ranges::all_of(name, [](unsigned char c) { return c >= 0x20 && c <= 0x7E; }))
        {
            state.error(error, "Object member names must be printable ASCII.");
            return false;
        }

        return true;
    };

    if (token.type == TokenType::SimpleString)
    {
        if (!verifyKeyName(token.value))
            return false;

        return readObjectValue(state, error, result, token.value);
    }

    if (token.type == TokenType::ComplexString)
    {
        auto key = decodeString(state, error, token.value);

        if (!key)
            return false;

        if (!verifyKeyName(*key))
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

        for (token = state.lex(); token.type == TokenType::Comma; token = state.lex())
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
        if (s.size() < kDefaultStringSizeLimit) [[likely]]
            return Value(s);

        return state.error(error, "Limit error: string exceeds maximum size.");
    };

    switch (token.type)
    {
        case TokenType::ObjectBegin:
            return readContainer(
                state,
                error,
                Value(ValueType::Object),
                TokenType::ObjectEnd,
                "Missing ',' or '}' in object declaration.",
                [](ParseState& state, std::string& error, Value& result, Token const& token) {
                    return readObjectKeyValue(state, error, result, token);
                });

        case TokenType::ArrayBegin:
            return readContainer(
                state,
                error,
                Value(ValueType::Array),
                TokenType::ArrayEnd,
                "Missing ',' or ']' in array declaration.",
                [](ParseState& state, std::string& error, Value& result, Token const& token) {
                    if (auto element = readValue(state, error, token))
                    {
                        result[result.size()] = std::move(*element);
                        return true;
                    }

                    return false;
                });

        case TokenType::Integer:
            return decodeNumber(state, error, token);

        case TokenType::Double:
            return decodeDouble(state, error, token);

        case TokenType::SimpleString:
            return verifyString(token.value);

        case TokenType::ComplexString:
            if (auto s = decodeString(state, error, token.value)) [[likely]]
                return verifyString(*s);

            return state.error(error, "Parse error: unable to decode complex string.");

        case TokenType::True:
            return Value(true);

        case TokenType::False:
            return Value(false);

        case TokenType::Null:
            return Value();

        default:
            return state.error(error, "Syntax error: value, object or array expected.");
    }
}

}  // namespace

std::optional<Value>
load(std::string_view document, std::string& error, std::uint16_t nestLimit)
{
    try
    {
        if (nestLimit == 0)
        {
            error = "Invalid nest limit.";
            return std::nullopt;
        }

        ParseState state{document, nestLimit};

        if (document.size() >= kDefaultDocumentSizeLimit)
            return state.error(error, "The JSON document is too large.");

        auto root = readValue(state, error, state.lex());

        if (root)
        {
            if (!root->isArray() && !root->isObject() && !root->isNull())
                return state.error(error, "Expected a single array, object, or null.");

            if (state.lex().type != TokenType::EndOfStream)
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

}  // namespace json
