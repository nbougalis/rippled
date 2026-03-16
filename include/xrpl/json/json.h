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

#ifndef RIPPLE_JSON_JSON_VALUE_H_INCLUDED
#define RIPPLE_JSON_JSON_VALUE_H_INCLUDED

#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/beast/core/multi_buffer.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <compare>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Json {

/** Type of the value held by a Value object.

    These values are actually _meaningful_ and form part of the public
    API for a surprising-but-not-surprising reason: when comparing two
    Value instances, the one whose ValueType is numerically less comes
    first (except: intValue and uintValue are treated as equal in that
    case and we simply compare the underlying integers).
 */
enum ValueType : std::uint8_t {
    nullValue = 0,
    intValue = 1,
    uintValue = 2,
    realValue = 3,
    stringValue = 4,
    booleanValue = 5,
    arrayValue = 6,
    objectValue = 7
};

/** Lightweight wrapper around nul-terminated string literals.

    We use this type of string to avoid the cost of string duplication
    when storing the string and/or member name. They will always point
    to a valid, nul-terminated array of immutable characters.
 */
class StaticString : public std::string_view
{
public:
    // This constructor is needed temporarily because of the way the macros
    // that handle instantiation of SField objects are structured. It helps
    // to keep the scope of changes minimal and targeted. Future changes to
    // SField will eliminate it again. The 'bool' parameter is just a guard
    // against unintended invocations in the meantime.
    explicit consteval StaticString(bool isSField, std::string_view sv)
        : std::string_view(sv)
    {
        if (!isSField)
            throw "StaticString must be used with SField objects";
    }

    template <std::size_t N>
    explicit consteval StaticString(char const (&literal)[N]) noexcept
        : std::string_view(literal, N - 1)
    {
        if (literal[N - 1] != '\0')
            throw "StaticString must be NUL-terminated";

        for (std::size_t i = 0; i < N - 1; ++i)
        {
            if (literal[i] == '\0')
                throw "StaticString cannot contain embedded NUL characters";

            if (literal[i] < 0x20 || literal[i] > 0x7E)
                throw "StaticString must contain only printable ASCII characters";
        }
    }

    constexpr StaticString(StaticString const&) = default;
    constexpr StaticString&
    operator=(StaticString const&) = default;

    /** Return a plain-old pointer to a null-terminated string.

        @note std::string_view does not require nul termination, but our
              constructor requires that it be, so this addition is safe.
     */
    [[nodiscard]] constexpr char const*
    c_str() const noexcept
    {
        return data();
    }
};

/** A single empty string, used whenever an empty string is requested. */
inline constexpr char const* const json_empty_string = "";

/** A simple exception type used for JSON errors.

    This type is used, instead of the several std-provided classes,
    to distinguish errors that are JSON-specific. This is important
    in some RPC-related contexts (e.g. in doLedgerEntry) where this
    exception, if caught, results in different handling between the
    v1 and v2+ responses that are generated. Ask me how I know :D

    Code that throws or catches this exception, should not be moved
    over to a more general exception type without due consideration
    as doing so has potentially breaking client-facing impact.
 */
class error : public std::runtime_error
{
public:
    error(std::string const& msg) : std::runtime_error(msg)
    {
    }
};

/** Represents a JSON value.

    This class is a discriminated union wrapper that can represent a:
    - signed 32-bit integer
    - unsigned 32-bit integer
    - double
    - UTF-8 string
    - boolean
    - 'null'
    - an ordered list of Value
    - collection of name/value pairs (javascript object)

    The type of the held value is represented by a ValueType and
    can be obtained using type().

    Values of an objectValue or arrayValue can be accessed using operator[]()
    methods. Non-const methods will automatically create a nullValue element
    if it does not exist.

    @note Small string optimization: strings of 15 characters or fewer are
          stored inline without allocation.

    @note Integer values (both signed and unsigned) are stored internally as
          std::int64_t. Range checking is performed on extraction.
 */
class Value
{
    static_assert(sizeof(std::uintptr_t) == sizeof(std::uint64_t));

public:
    /** Key type for object members: a wrapper around a pointer to a C string.

        The pointer is internally mangled to distinguish between memory that
        was allocated by us vs. memory we just point to.

        @note A CZString will never point to NULL. Attempting to instantiate
              such a string will result in an instance pointing to the empty
              string (and an assert in debug builds).
     */
    class CZString
    {
        static constexpr std::uintptr_t allocated_flag = 0x8000000000000000;
        static constexpr std::uintptr_t allocated_mask = ~allocated_flag;

        /** A tagged pointer to the key string.

            If the high bit is set, then the string pointed to by this value,
            after the high bit is cleared, was dynamically allocated and we
            are responsible for deleting it. Otherwise, it is a pointer to
            a string that is guaranteed to exist as long as there is a CZString
            instance pointing to it.

            @note We assume that the high bit is never set on pointers on any
                  of the platforms that we support. This assumption currently
                  holds and that is unlikely to change in the future.
         */
        std::uintptr_t data_ =
            reinterpret_cast<std::uintptr_t>(json_empty_string);

    public:
        CZString() noexcept = default;

        CZString(std::string_view s);

        // The ternary is needed because, unfortunately, it is UB to construct
        // a std::string_view from nullptr.
        CZString(char const* s) noexcept
            : CZString(std::string_view{s ? s : ""})
        {
        }

        CZString(StaticString s) noexcept
            : data_(reinterpret_cast<std::uintptr_t>(s.c_str()))
        {
        }

        CZString(nullptr_t) = delete;

        CZString(const CZString& other);
        CZString&
        operator=(const CZString&);

        CZString(CZString&& other) noexcept
        {
            std::swap(data_, other.data_);
        }

        CZString&
        operator=(CZString&& other) noexcept
        {
            if (this != &other)
                std::swap(data_, other.data_);

            return *this;
        }

        ~CZString();

        [[nodiscard]] char const*
        c_str() const noexcept
        {
            return reinterpret_cast<char const*>(data_ & allocated_mask);
        }

        [[nodiscard]] bool
        isStatic() const noexcept
        {
            return (data_ & allocated_flag) == 0;
        }

        // Heterogeneous comparison operators against std::string_view make
        // comparing a CZString and a std::string_view more efficient since
        // they avoid having to first construct a temporary CZString, which
        // saves a memory allocation.
        //
        // Additionally, we route all comparison operators down to a single
        // implementation, ensuring consistency.
        //
        // Note that we restrict keys to printable ASCII (enforced at parse
        // time and by convention for programmatic keys), which lets us use
        // the collation semantics that std::string_view has.
        [[nodiscard]] std::strong_ordering
        operator<=>(std::string_view other) const noexcept
        {
            return std::string_view(c_str()) <=> other;
        }

        [[nodiscard]] bool
        operator==(std::string_view other) const noexcept
        {
            return std::string_view(c_str()) == other;
        }

        [[nodiscard]] std::strong_ordering
        operator<=>(CZString const& other) const noexcept
        {
            // While identical pointers must, necessarily, be the same string,
            // the converse does not hold: different address may point to the
            // same content.
            if (data_ == other.data_)
                return std::strong_ordering::equal;

            return std::string_view(c_str()) <=>
                std::string_view(other.c_str());
        }

        [[nodiscard]] bool
        operator==(CZString const& other) const noexcept
        {
            // While identical pointers must, necessarily, be the same string,
            // the converse does not hold: different address may point to the
            // same content.
            if (data_ == other.data_)
                return true;

            return std::string_view(c_str()) == std::string_view(other.c_str());
        }
    };

    static_assert(sizeof(CZString) == 8);

    // Storage types - defined after Value is complete
    struct ArrayStorage;
    struct ObjectStorage;

private:
    template <bool IsConst>
    class ValueIteratorImpl;

    friend std::partial_ordering
    operator<=>(const Value&, const Value&) noexcept;
    friend bool
    operator==(const Value&, const Value&) noexcept;

public:
    using UInt = std::uint32_t;
    using Int = std::int32_t;

    using Members = std::vector<std::string>;
    using iterator = ValueIteratorImpl<false>;
    using const_iterator = ValueIteratorImpl<true>;
    using ArrayIndex = UInt;

    static constexpr std::size_t defaultArrayCapacity = 8;
    static constexpr std::size_t defaultObjectCapacity = 16;

    static constexpr auto maxUInt = std::numeric_limits<UInt>::max();
    static constexpr auto maxInt = std::numeric_limits<Int>::max();
    static constexpr auto minInt = std::numeric_limits<Int>::min();

private:
    // Internal storage types. We can't directly use ValueType
    // because we need the values [0, 15] to serve double duty
    // as small string and length identifiers and, in the case
    // of 0, as the NUL for the longest possible small string.
    static constexpr std::uint8_t type_staticString = 16;
    static constexpr std::uint8_t type_allocatedString = 17;
    static constexpr std::uint8_t type_int = 18;
    static constexpr std::uint8_t type_uint = 19;
    static constexpr std::uint8_t type_boolean = 20;
    static constexpr std::uint8_t type_real = 21;
    static constexpr std::uint8_t type_array = 22;
    static constexpr std::uint8_t type_object = 23;
    static constexpr std::uint8_t type_null = 24;

public:
    /** Default constructor. Creates a null value. */
    constexpr Value() noexcept = default;

    /** Create a Value of the given type. */
    Value(ValueType type);

    /** Create a signed integer value. */
    Value(Int value) noexcept;

    /** Create an unsigned integer value. */
    Value(UInt value) noexcept;

    /** Create a double value. */
    Value(double value) noexcept;

    /** @{ */
    /** Create a string value.

        Uses small string optimization for strings <= 15 characters.
     */
    Value(std::string_view value) noexcept
    {
        if (!initSSO(value))
            initAllocatedString(value);
    }

    Value(std::string const& value) noexcept : Value(std::string_view{value})
    {
    }

    Value(char const* value) noexcept
        : Value(std::string_view{value ? value : ""})
    {
    }
    /** @} */

    /** Prevent construction from nullptr. */
    Value(std::nullptr_t) = delete;

    /** @{ */
    /** Create a string value from a static string.

        Does not duplicate the string for internal storage. The given string
        must remain alive for the lifetime of this Value.

        @note If the string is <= 15 characters, it will be copied into
              SSO storage regardless.
     */
    Value(StaticString value) noexcept
    {
        if (!initSSO(value))
            initStaticString(value);
    }

    Value(CZString value) noexcept
    {
        std::string_view str{value.c_str(), std::strlen(value.c_str())};

        if (!initSSO(str))
        {
            if (value.isStatic())
                initStaticString(str);
            else
                initAllocatedString(str);
        }
    }
    /** @} */

    /** Create a boolean value. */
    Value(bool value) noexcept;

    /** Copy constructor. */
    Value(const Value& other);

    /** Move constructor. Left in null state. */
    Value(Value&& other) noexcept : data_(other.data_)
    {
        other.data_.type = type_null;
    }

    ~Value() noexcept;

    /** Unified copy and move assignment operator.

        Takes other by value, allowing the compiler to select copy or move
        construction at the call site. The assignment itself is a single swap.
     */
    Value&
    operator=(Value other) noexcept
    {
        std::swap(data_, other.data_);
        return *this;
    }

    /** Returns the type of the held value.

        @note All internal string representations return stringValue.
     */
    [[nodiscard]] ValueType
    type() const noexcept;

    /** Returns the value as a string_view.

        @return The string value, or empty string_view if not a string type.
     */
    [[nodiscard]] std::string_view
    asStringView() const noexcept;

    /** Returns the value as a std::string.

        Numeric types are converted to their string representation.
     */
    [[nodiscard]] std::string
    asString() const;

    /** Returns the value as a signed integer.

        @note Asserts if the stored value is outside [minInt, maxInt].
     */
    [[nodiscard]] Int
    asInt() const;

    /** Returns the value as an unsigned integer.

        @note Asserts if the stored value is negative or exceeds maxUInt.
     */
    [[nodiscard]] UInt
    asUInt() const;

    /** Returns the value as a double. */
    [[nodiscard]] double
    asDouble() const;

    /** Returns the value as a boolean. */
    [[nodiscard]] bool
    asBool() const noexcept;

    [[nodiscard]] bool
    isNull() const noexcept
    {
        return data_.type == type_null;
    }

    [[nodiscard]] bool
    isBool() const noexcept
    {
        return data_.type == type_boolean;
    }

    [[nodiscard]] bool
    isInt() const noexcept
    {
        return data_.type == type_int;
    }

    [[nodiscard]] bool
    isUInt() const noexcept
    {
        return data_.type == type_uint;
    }

    [[nodiscard]] bool
    isIntegral() const noexcept
    {
        return data_.type == type_int || data_.type == type_uint ||
            data_.type == type_boolean;
    }

    [[nodiscard]] bool
    isDouble() const noexcept
    {
        return data_.type == type_real;
    }

    [[nodiscard]] bool
    isNumeric() const noexcept
    {
        return isIntegral() || isDouble();
    }

    [[nodiscard]] bool
    isString() const noexcept
    {
        return data_.type <= type_allocatedString;
    }

    [[nodiscard]] bool
    isArray() const noexcept
    {
        return data_.type == type_array;
    }

    [[nodiscard]] bool
    isArrayOrNull() const noexcept
    {
        return data_.type == type_null || data_.type == type_array;
    }

    [[nodiscard]] bool
    isObject() const noexcept
    {
        return data_.type == type_object;
    }

    [[nodiscard]] bool
    isObjectOrNull() const noexcept
    {
        return data_.type == type_null || data_.type == type_object;
    }

    /** Returns the length of the string, if the type is a string. */
    std::uint32_t
    strlen() const noexcept
    {
        assert(data_.type <= type_allocatedString);

        if (data_.type < type_staticString)
            return static_cast<std::uint32_t>(data_.buffer.size()) - data_.type;

        if (data_.type <= type_allocatedString)
            return *std::launder(
                reinterpret_cast<uint32_t const*>(
                    data_.buffer.data() + sizeof(char const*)));

        return 0;
    }

    /** Returns the number of elements in an array or object. */
    [[nodiscard]] UInt
    size() const noexcept;

    /** Returns false if this is null, empty array/object, or empty string. */
    explicit
    operator bool() const noexcept;

    /** Access an array element by index.

        If the array contains fewer than index+1 elements, null values are
        inserted to extend the array.

        @note You may need to write value[0u] to disambiguate from the
              string key overload.
     */
    [[nodiscard]] Value&
    operator[](UInt index);

    /** Access an array element by index (const). */
    [[nodiscard]] const Value&
    operator[](UInt index) const;

    /** Append value to array at the end.

        This is a unified version that handles both rvalues and lvalues
        optimally.

        @param value The value to append. The argument is materialized
                     before the array is modified, so it may safely be
                     an element of (or reference into) this array.

        @return A reference to the newly appended value. The reference
                is guaranteed to remain valid until the array is
                modified.


        @throws Json::error if this Value is neither an array nor null.
     */
    Value&
    append(Value value)
    {
        return ((*this)[size()] = std::move(value));
    }

    /** Access an object member by key.

        Creates the member as a null Value if it does not exist.

        @note We use StaticString to avoid allocating memory for string
              literals stored inside a Value.
     */
    template <typename T>
        requires std::convertible_to<T, std::string_view>
    [[nodiscard]] Value&
    operator[](T const& key);

    /** Access an object member by key (const). */
    [[nodiscard]] const Value&
    operator[](std::string_view key) const;

    /** Get an object member with default. */
    [[nodiscard]] Value
    get(std::string_view key, Value defaultValue = {}) const;

    [[nodiscard]] Value
    get(std::size_t index, Value defaultValue = {}) const;

    /** Remove and return a member.

        @param key The key identifying the member to remove.
        @retun The removed member, or std::nullopt if the member does not exist.

        @note type() must be objectValue or nullValue.
     */
    std::optional<Value>
    removeMember(std::string_view key);

    /** Check if a member exists.

        @param type An optional expected type for the member.
     */
    [[nodiscard]] bool
    isMember(std::string_view key, std::optional<ValueType> type = {})
        const noexcept;

    /** Return a list of member names.

        @note type() must be objectValue or nullValue.
     */
    [[nodiscard]] Members
    getMemberNames() const;

    [[nodiscard]] const_iterator
    cbegin() const;

    [[nodiscard]] const_iterator
    begin() const;

    [[nodiscard]] const_iterator
    cend() const;

    [[nodiscard]] const_iterator
    end() const;

    [[nodiscard]] iterator
    begin();

    [[nodiscard]] iterator
    end();

private:
    template <typename T>
    [[nodiscard]] T&
    as() noexcept
    {
        static_assert(sizeof(T) <= sizeof(data_.buffer));
        static_assert(alignof(T) <= alignof(decltype(data_)));
        return *std::launder(reinterpret_cast<T*>(data_.buffer.data()));
    }

    template <typename T>
    [[nodiscard]] T const&
    as() const noexcept
    {
        static_assert(sizeof(T) <= sizeof(data_.buffer));
        static_assert(alignof(T) <= alignof(decltype(data_)));
        return *std::launder(reinterpret_cast<const T*>(data_.buffer.data()));
    }

    [[nodiscard]] bool
    isSSO() const noexcept
    {
        return data_.type < type_staticString;
    }

    [[nodiscard]] bool
    isAllocatedString() const noexcept
    {
        return data_.type == type_allocatedString;
    }

    [[nodiscard]] bool
    isStaticStringType() const noexcept
    {
        return data_.type == type_staticString;
    }

    bool
    initSSO(std::string_view str) noexcept;
    void
    initAllocatedString(std::string_view str) noexcept;
    void
    initStaticString(std::string_view str) noexcept;
    void
    initString(const char* str, std::size_t len) noexcept;

    // This structure defines the binary layout of the Value type and
    // is manually and deliberately aligned to ensure that the buffer
    // is, itself, aligned correctly for any of the types that it may
    // contain.
    struct alignas(8) Data
    {
        std::array<unsigned char, 15> buffer;
        std::uint8_t type = type_null;

        constexpr Data() noexcept : buffer{}
        {
        }

        explicit Data(std::uint8_t t) noexcept : type(t)
        {
        }
    } data_;
};

static_assert(sizeof(Value) == 16, "Value must be exactly 16 bytes");
static_assert(alignof(Value) == 8, "Value alignment mismatch");

// Define storage types now that Value is complete
struct Value::ArrayStorage : std::map<ArrayIndex, Value>
{
    using map::map;

    std::partial_ordering
    operator<=>(const ArrayStorage& other) const noexcept
    {
        return std::lexicographical_compare_three_way(
            begin(),
            end(),
            other.begin(),
            other.end(),
            [](const value_type& a,
               const value_type& b) -> std::partial_ordering {
                if (auto cmp = a.first <=> b.first; cmp != 0)
                    return cmp;
                return a.second <=> b.second;
            });
    }

    bool
    operator==(const ArrayStorage& other) const noexcept
    {
        return size() == other.size() &&
            std::equal(
                   begin(),
                   end(),
                   other.begin(),
                   [](const value_type& a, const value_type& b) {
                       return a.first == b.first && a.second == b.second;
                   });
    }
};

struct Value::ObjectStorage : std::map<CZString, Value, std::less<>>
{
    using map::map;

    std::partial_ordering
    operator<=>(const ObjectStorage& other) const noexcept
    {
        return std::lexicographical_compare_three_way(
            begin(),
            end(),
            other.begin(),
            other.end(),
            [](const value_type& a,
               const value_type& b) -> std::partial_ordering {
                if (auto cmp = a.first <=> b.first; cmp != 0)
                    return cmp;
                return a.second <=> b.second;
            });
    }

    bool
    operator==(const ObjectStorage& other) const noexcept
    {
        return size() == other.size() &&
            std::equal(
                   begin(),
                   end(),
                   other.begin(),
                   [](const value_type& a, const value_type& b) {
                       return a.first == b.first && a.second == b.second;
                   });
    }
};

// Iterator implementation - must come after storage types are defined
template <bool IsConst>
class Value::ValueIteratorImpl
{
    friend class Value;
    friend class ValueIteratorImpl<!IsConst>;

    using ArrayIterator = std::conditional_t<
        IsConst,
        ArrayStorage::const_iterator,
        ArrayStorage::iterator>;

    using ObjectIterator = std::conditional_t<
        IsConst,
        ObjectStorage::const_iterator,
        ObjectStorage::iterator>;

    std::variant<std::monostate, ArrayIterator, ObjectIterator> current_;

public:
    using difference_type = std::ptrdiff_t;
    using value_type = Value;
    using reference = std::conditional_t<IsConst, const Value&, Value&>;
    using pointer = std::conditional_t<IsConst, const Value*, Value*>;
    using iterator_category = std::bidirectional_iterator_tag;

    ValueIteratorImpl() = default;

    template <bool OtherConst>
        requires(IsConst && !OtherConst)
    ValueIteratorImpl(ValueIteratorImpl<OtherConst> const& other)
        : current_(
              std::visit(
                  [](const auto& it) -> decltype(current_) {
                      if constexpr (std::is_same_v<
                                        std::decay_t<decltype(it)>,
                                        std::monostate>)
                          return std::monostate{};
                      else
                          return it;
                  },
                  other.current_))
    {
    }

    [[nodiscard]] bool
    operator==(ValueIteratorImpl const& other) const noexcept
    {
        return current_ == other.current_;
    }

    ValueIteratorImpl&
    operator++()
    {
        std::visit(
            [](auto& it) {
                if constexpr (!std::is_same_v<
                                  std::decay_t<decltype(it)>,
                                  std::monostate>)
                    ++it;
            },
            current_);
        return *this;
    }

    ValueIteratorImpl&
    operator--()
    {
        std::visit(
            [](auto& it) {
                if constexpr (!std::is_same_v<
                                  std::decay_t<decltype(it)>,
                                  std::monostate>)
                    --it;
            },
            current_);
        return *this;
    }

    ValueIteratorImpl
    operator++(int)
    {
        auto tmp = *this;
        ++*this;
        return tmp;
    }

    ValueIteratorImpl
    operator--(int)
    {
        auto tmp = *this;
        --*this;
        return tmp;
    }

    [[nodiscard]] reference
    operator*() const
    {
        return std::visit(
            [](const auto& it) -> reference {
                if constexpr (std::is_same_v<
                                  std::decay_t<decltype(it)>,
                                  std::monostate>)
                    std::terminate();
                else
                    return it->second;
            },
            current_);
    }

    [[nodiscard]] pointer
    operator->() const
    {
        return &**this;
    }

    [[nodiscard]] char const*
    memberName() const
    {
        return std::visit(
            [](const auto& it) -> char const* {
                if constexpr (std::is_same_v<
                                  std::decay_t<decltype(it)>,
                                  ObjectIterator>)
                    return it->first.c_str();
                else
                    return "";
            },
            current_);
    }

    [[nodiscard]] Value
    key() const
    {
        return std::visit(
            [](const auto& it) -> Value {
                if constexpr (std::is_same_v<
                                  std::decay_t<decltype(it)>,
                                  ArrayIterator>)
                    return Value(it->first);
                else if constexpr (std::is_same_v<
                                       std::decay_t<decltype(it)>,
                                       ObjectIterator>)
                    return Value(it->first.c_str());
                else
                    return {};
            },
            current_);
    }

private:
    explicit ValueIteratorImpl(ArrayIterator it) : current_(it)
    {
    }

    explicit ValueIteratorImpl(ObjectIterator it) : current_(it)
    {
    }
};

std::partial_ordering
operator<=>(const Value&, const Value&) noexcept;
bool
operator==(const Value&, const Value&) noexcept;

template <typename T>
    requires std::convertible_to<T, std::string_view>
[[nodiscard]] Value&
Value::operator[](T const& key)
{
    if (data_.type == type_null)
        *this = Value(objectValue);

    if (data_.type != type_object)
        ripple::Throw<error>("Attempt to treat non-object value as object.");

    auto* obj = as<ObjectStorage*>();

    if (auto it = obj->find(std::string_view(key)); it != obj->end())
        return it->second;

    return obj->try_emplace(CZString(key)).first->second;
}

/** Extract an integer value from a Json::Value.

    Attempts to convert the given JSON value to the requested integral type.
    The JSON value may be a signed integer, an unsigned integer, or a decimal
    string representation. Range checking is performed using std::in_range.

    @return The converted value, or std::nullopt if out of range or wrong type.
*/
template <typename T>
    requires(std::integral<T> && !std::same_as<T, bool>)
std::optional<T>
to_integer(Value const& v) noexcept
{
    if (v.isInt())
    {
        if (auto const num = v.asInt(); std::in_range<T>(num))
            return static_cast<T>(num);

        return std::nullopt;
    }

    if (v.isUInt())
    {
        if (auto const num = v.asUInt(); std::in_range<T>(num))
            return static_cast<T>(num);

        return std::nullopt;
    }

    if (auto sv = v.asStringView(); !sv.empty())
    {
        T num;

        auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), num);

        if (ec == std::errc() && p == sv.data() + sv.size())
            return num;
    }

    return std::nullopt;
}

/** The signed and unsigned integer types that we support. */
using UInt = Value::UInt;
using Int = Value::Int;

//------------------------------------------------------------------------------
// Serialization / Deserialization

/** Defines the format options to use when writing JSON output. */
struct FormatOptions
{
    std::string_view newline;
    std::string_view key_sep;
    std::string_view indent;
};

/** The format specifier used when writing compact/machine output. */
inline constexpr FormatOptions compactFormat{"", ":", ""};

/** The format specifier used when writing styled output. */
inline constexpr FormatOptions styledFormat{"\n", " : ", "   "};

namespace detail {

/** Amount of memory to use for internal chunked buffering. */
inline constexpr std::size_t default_buffer_size = 512;

/** Base class for JSON serialization sinks.

    We derive classes that implement append() to direct serialized
    JSON output to a specific destination (e.g. into a std::string
    or to a boost::beast::multi_buffer).

    Our implementation is such that the compiler should be able to
    devirtualize the call since the concrete type is known when we
    call stream() from the call site.
 */
struct StreamSink
{
    virtual void
    append(std::string_view sv) = 0;

    virtual ~StreamSink() = default;
};

}  // namespace detail

/** Stream JSON to the specified sink.

    @param jv   The Json::Value to serialize.
    @param sink The output sink to write to.
    @param fmt  The output format (compact by default).
*/
void
stream(
    Value const& jv,
    detail::StreamSink& sink,
    FormatOptions const& fmt = compactFormat);

/** Serialize a JSON value.

    @tparam T   The output type: either std::string or
                boost::beast::multi_buffer.
    @param jv   The Json::Value to serialize.
    @param fmt  The output format (compact by default).
*/
template <typename T>
    requires(
        std::is_same_v<T, std::string> ||
        std::is_same_v<T, std::vector<char>> ||
        std::is_same_v<T, std::vector<unsigned char>> ||
        std::is_same_v<T, std::vector<std::uint8_t>> ||
        std::is_same_v<T, boost::beast::multi_buffer>)
[[nodiscard]] T
save(Value const& jv, FormatOptions const& fmt = compactFormat)
{
    if constexpr (std::is_same_v<T, boost::beast::multi_buffer>)
    {
        struct MultiBufferSink : detail::StreamSink
        {
            boost::container::static_vector<char, detail::default_buffer_size>
                chunk;
            boost::beast::multi_buffer sb;

            void
            flush()
            {
                auto buf = sb.prepare(chunk.size());
                boost::asio::buffer_copy(
                    buf, boost::asio::buffer(chunk.data(), chunk.size()));
                sb.commit(chunk.size());
                chunk.clear();
            }

            void
            append(std::string_view sv) override
            {
                while (!sv.empty())
                {
                    std::size_t const n =
                        std::min(sv.size(), chunk.capacity() - chunk.size());
                    chunk.insert(chunk.end(), sv.data(), sv.data() + n);
                    sv.remove_prefix(n);

                    if (chunk.size() == chunk.capacity())
                        flush();
                }
            }
        };

        MultiBufferSink sink;
        stream(jv, sink, fmt);
        if (!sink.chunk.empty())
            sink.flush();
        return std::move(sink.sb);
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        struct StringSink : detail::StreamSink
        {
            std::string out;

            StringSink()
            {
                out.reserve(detail::default_buffer_size);
            }

            void
            append(std::string_view sv) override
            {
                out.append(sv);
            }
        };

        StringSink sink;
        stream(jv, sink, fmt);
        return std::move(sink.out);
    }
    else
    {
        struct ByteSink : detail::StreamSink
        {
            T out;

            ByteSink()
            {
                out.reserve(detail::default_buffer_size);
            }

            void
            append(std::string_view sv) override
            {
                out.insert(out.end(), sv.begin(), sv.end());
            }
        };

        ByteSink sink;
        stream(jv, sink, fmt);
        return std::move(sink.out);
    }
}

/** Serialize a JSON value to a compact std::string. */
inline std::string
to_string(Value const& jv)
{
    return save<std::string>(jv, compactFormat);
}

/** Serialize a JSON value to a styled std::string. */
inline std::string
to_styled_string(Value const& jv)
{
    return save<std::string>(jv, styledFormat);
}

/** Serialize a JSON value to a compact std::string. */
inline std::string
to_compact_string(Value const& jv)
{
    return save<std::string>(jv, compactFormat);
}

/** Serialize a JSON value to a boost::beast::multi_buffer. */
[[nodiscard]] inline boost::beast::multi_buffer
to_multi_buffer(Value const& jv, FormatOptions const& fmt = compactFormat)
{
    return save<boost::beast::multi_buffer>(jv, fmt);
}

/** Write the given value, in compact format, to the output stream. */
inline std::ostream&
operator<<(std::ostream& os, Value const& jv)
{
    struct OStreamSink : detail::StreamSink
    {
        std::ostream& os;
        explicit OStreamSink(std::ostream& os) : os(os)
        {
        }

        void
        append(std::string_view sv) override
        {
            os.write(sv.data(), sv.size());
        }
    };

    OStreamSink sink(os);
    stream(jv, sink);
    return os;
}

//------------------------------------------------------------------------------
// Deserialization

/** The default maximum size of a JSON member name. */
static constexpr std::size_t default_key_size_limit = 1024;

/** The default maximum size for JSON strings. */
static constexpr std::size_t default_string_size_limit = 4 * 1024 * 1024;

/** The default maximum size for JSON documents. */
static constexpr std::size_t default_document_size_limit = 64 * 1024 * 1024;

/** The default nesting limit for JSON parsing. */
static constexpr std::uint16_t default_nest_limit = 25;

/** Parse a UTF-8 encoded JSON document into a Value.

    Parsing strictly follows RFC 8259. The document must contain exactly
    one JSON value; we do not support extensions such as trailing commas
    or comments.

    As permitted by section 9 of RFC 8259, we additionally impose limits
    that are implementation-specific and which go beyond the grammar:

      - the top-level value must be an object, an array or null;
      - object member names may not contain an escaped or unescaped NUL;
      - duplicate member names within an object are rejected; and
      - nesting depth is bounded by nest_limit.

    @param document   The UTF-8 encoded JSON document to parse.
    @param error      On failure, contains a description of the parse error.
    @param nest_limit The maximum nesting depth allowed.

    @return The parsed value on success, or std::nullopt on failure.
*/
std::optional<Value>
load(
    std::string_view document,
    std::string& error,
    std::uint16_t nest_limit = default_nest_limit);

/** Deserialize a JSON document, discarding the error message. */
inline std::optional<Value>
load(std::string_view document, std::uint16_t nest_limit = default_nest_limit)
{
    std::string error;
    return load(document, error, nest_limit);
}

/** Deserialize a JSON document into an existing Value.

    @return true on success, false on failure.
*/
inline bool
load(
    std::string_view document,
    Value& result,
    std::uint16_t nest_limit = default_nest_limit)
{
    if (auto v = load(document, nest_limit))
    {
        result = std::move(*v);
        return true;
    }

    return false;
}

}  // namespace Json

#endif  // RIPPLE_JSON_JSON_VALUE_H_INCLUDED
