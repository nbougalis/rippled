#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/beast/core/multi_buffer.hpp>
#include <boost/container/static_vector.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace json {

/**
 * Type of the value held by a Value object.
 *
 * These values are actually _meaningful_ and form part of the public
 * API for a surprising-but-not-surprising reason: when comparing two
 * Value instances, the one whose ValueType is numerically less comes
 * first (except: ValueType::Int and ValueType::UInt are treated as equal
 * in that case and we simply compare the underlying integers).
 */
enum class ValueType : std::uint8_t {
    Null = 0,     ///< 'null' value
    Int = 1,      ///< signed integer value
    UInt = 2,     ///< unsigned integer value
    Real = 3,     ///< double value
    String = 4,   ///< UTF-8 string value
    Boolean = 5,  ///< bool value
    Array = 6,    ///< array value (ordered list)
    Object = 7    ///< object value (collection of name/value pairs).
};

/**
 * Lightweight wrapper around nul-terminated string literals.
 *
 * We use this type of string to avoid the cost of string duplication
 * when storing the string and/or member name. They will always point
 * to a valid, nul-terminated array of immutable characters.
 */
class StaticString : public std::string_view
{
public:
    // This constructor is needed temporarily because of the way the macros
    // that handle instantiation of SField objects are structured. It helps
    // to keep the scope of changes minimal and targeted. Future changes to
    // SField will eliminate it again. The 'bool' parameter is just a guard
    // against unintended invocations in the meantime.
    explicit consteval StaticString(bool isSField, std::string_view sv) : std::string_view(sv)
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

    /**
     * Return a plain-old pointer to a null-terminated string.
     *
     * @note std::string_view does not require nul termination, but our
     *       constructor requires that it be, so this addition is safe.
     */
    [[nodiscard]] constexpr char const*
    cStr() const noexcept
    {
        return data();
    }
};

/**
 * Whether object member names may borrow a pointer to static storage.
 *
 * When true, a CZString built from a StaticString keeps a pointer to the
 * literal, marked with a flag in bit 63; allocated names are stored as
 * the allocator returned them. HWASan keeps its allocation tag in the top
 * byte of every pointer, so under it the flag cannot be used and every
 * name is copied instead. ARM MTE uses bits 59:56 and is unaffected.
 */
inline constexpr bool kBorrowStaticKeys = [] consteval {
    bool borrow = true;

#ifdef __SANITIZE_HWADDRESS__
    borrow = false;
#endif

#ifdef __has_feature
#if __has_feature(hwaddress_sanitizer)
    borrow = false;
#endif
#endif

    return borrow;
}();

/**
 * A simple exception type used for JSON errors.
 *
 * This type is used, instead of the several std-provided classes,
 * to distinguish errors that are JSON-specific. This is important
 * in some RPC-related contexts (e.g. in doLedgerEntry) where this
 * exception, if caught, results in different handling between the
 * v1 and v2+ responses that are generated. Ask me how I know :D
 *
 * Code that throws or catches this exception, should not be moved
 * over to a more general exception type without due consideration
 * as doing so has potentially breaking client-facing impact.
 */
class Error : public std::runtime_error
{
public:
    Error(std::string const& msg) : std::runtime_error(msg)
    {
    }
};

/**
 * Represents a JSON value.
 *
 * This class is a discriminated union wrapper that can represent a:
 * - signed 32-bit integer
 * - unsigned 32-bit integer
 * - double
 * - UTF-8 string
 * - boolean
 * - 'null'
 * - an ordered list of Value
 * - collection of name/value pairs (javascript object)
 *
 * The type of the held value is represented by a ValueType and
 * can be obtained using type().
 *
 * Values of an ValueType::Object or ValueType::Array can be accessed using operator[]()
 * methods. Non-const methods will automatically create a ValueType::Null element
 * if it does not exist.
 *
 * @note Small string optimization: strings of 15 characters or fewer are
 *       stored inline without allocation.
 *
 * @note Integer values (both signed and unsigned) are stored internally as
 *       std::int64_t. Range checking is performed on extraction.
 */
class Value
{
    static_assert(sizeof(std::uintptr_t) == sizeof(std::uint64_t));

public:
    /**
     * Key type for object members: a wrapper around a pointer to a C string.
     *
     * The pointer is internally mangled to distinguish between memory that
     * was allocated by us vs. memory we just point to.
     *
     * @note A CZString will never point to NULL. Attempting to instantiate
     *       such a string will result in an instance pointing to the empty
     *       string (and an assert in debug builds).
     */
    class CZString
    {
        /**
         * This must be a named object, not a string literal. A constexpr
         * pointer initialized from a literal is folded at each use to be
         * the address of the literal, as evaluated in *that* TU. And the
         * standard does not specify whether two evaluations of a literal
         * yield one object or two. Normally the linker merges the copies
         * and the folded addresses are the same; but in some cases, like
         * when running under ASan, they are not. Given:
         *
         *     inline constexpr char const* const kEmpty = "";
         *
         * the value of kEmpty as seen in one translation unit may not be
         * equal to the value of kEmpty as seen in another!
         *
         * A static constexpr array member is itself the object, and must
         * have one address, however it is reached.
         *
         * @note All empty keys point to this string. Nothing other than
         *       the default constructor will hand out a pointer to this
         *       array.
         */
        static constexpr char kEmpty[] = "";

        // Set on a pointer we borrowed from a string literal. Clear on one we
        // allocated, which is therefore stored exactly as the allocator
        // returned it, and on kEmpty. In the no-borrow mode the flag is zero
        // and nothing is ever borrowed.
        static constexpr std::uintptr_t kStaticFlag =
            kBorrowStaticKeys ? std::uintptr_t{1} << 63 : std::uintptr_t{0};

        /**
         * A tagged pointer to the key string.
         *
         * If kStaticFlag is set, the pointer (with the flag cleared) refers
         * to a string literal that outlives every CZString pointing to it.
         * Otherwise it is either kEmpty or a string we allocated and must
         * free; isStatic() tells those apart.
         */
        std::uintptr_t data_ = reinterpret_cast<std::uintptr_t>(kEmpty);

    public:
        CZString() noexcept = default;

        CZString(std::string_view s) noexcept;

        // The ternary is needed because, unfortunately, it is UB to construct
        // a std::string_view from nullptr.
        CZString(char const* s) noexcept : CZString(std::string_view{(s != nullptr) ? s : ""})
        {
        }

        CZString(StaticString s) noexcept
        {
            if constexpr (kBorrowStaticKeys)
            {
                data_ = kStaticFlag | reinterpret_cast<std::uintptr_t>(s.cStr());
            }
            else
            {
                *this = CZString(std::string_view(s));
            }
        }

        CZString(std::nullptr_t) = delete;

        CZString(CZString const& other) noexcept
        {
            if (other.isStatic())
            {
                data_ = other.data_;
                return;
            }

            *this = CZString(std::string_view{other.cStr()});
        }

        CZString(CZString&& other) noexcept
        {
            std::swap(data_, other.data_);
        }

        // Unified copy/move assignment
        CZString&
        operator=(CZString other) noexcept
        {
            std::swap(data_, other.data_);
            return *this;
        }

        ~CZString();

        [[nodiscard]] char const*
        cStr() const noexcept
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            return reinterpret_cast<char const*>(data_ & ~kStaticFlag);
        }

        [[nodiscard]] bool
        isStatic() const noexcept
        {
            // The empty key is always static.
            if (data_ == reinterpret_cast<std::uintptr_t>(kEmpty))
                return true;

            // The flag marks a borrowed literal. It is zero in the no-borrow
            // mode, so this test will fold to false.
            return (data_ & kStaticFlag) != 0;
        }

        // The comparison against std::string_view is what std::less<> selects
        // for lookups, so a lookup never builds a temporary CZString (which
        // would allocate). Both forms make a single pass over the bytes: the
        // key's terminator bounds the key and the view's size bounds the view.
        //
        // Keys are printable ASCII (enforced at parse time and by convention
        // for programmatic keys) and never contain NUL, and neither does any
        // string they are compared against; this is what lets strncmp stand in
        // for a length-aware comparison.
        friend std::strong_ordering
        operator<=>(CZString const& lhs, std::string_view rhs) noexcept
        {
            XRPL_ASSERT(
                rhs.find('\0') == std::string_view::npos,
                "json::Value::CZString : comparison against a string with an embedded NUL");

            if (auto const c = std::strncmp(lhs.cStr(), rhs.data(), rhs.size()); c != 0)
                return c <=> 0;

            // Equal over the length of rhs: equal if the key ends here, and
            // greater if it continues.
            return lhs.cStr()[rhs.size()] <=> '\0';
        }

        friend bool
        operator==(CZString const& lhs, std::string_view rhs) noexcept
        {
            return (lhs <=> rhs) == 0;
        }

        friend std::strong_ordering
        operator<=>(CZString const& lhs, CZString const& rhs) noexcept
        {
            // While identical pointers must, necessarily, be the same string,
            // the converse does not hold: different address may point to the
            // same content.
            if (lhs.data_ == rhs.data_)
                return std::strong_ordering::equal;

            return std::strcmp(lhs.cStr(), rhs.cStr()) <=> 0;
        }

        friend bool
        operator==(CZString const& lhs, CZString const& rhs) noexcept
        {
            return (lhs <=> rhs) == 0;
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
    operator<=>(Value const&, Value const&) noexcept;
    friend bool
    operator==(Value const&, Value const&) noexcept;

public:
    using UInt = std::uint32_t;
    using Int = std::int32_t;

    using Members = std::vector<std::string>;
    using iterator = ValueIteratorImpl<false>;
    using const_iterator = ValueIteratorImpl<true>;
    using ArrayIndex = UInt;

    static constexpr auto kMaxUInt = std::numeric_limits<UInt>::max();
    static constexpr auto kMaxInt = std::numeric_limits<Int>::max();
    static constexpr auto kMinInt = std::numeric_limits<Int>::min();

private:
    // The in-place buffer holds strings of up to this many characters; the
    // type byte of an SSO string stores the remaining capacity, so that the
    // longest string's terminator doubles as its type byte (zero).
    static constexpr std::uint8_t kSsoCapacity = 15;

    // Internal type tags. Values [0, kSsoCapacity] are taken by SSO strings
    // (the tag is the remaining capacity), so the tags start just above.
    enum : std::uint8_t {
        TypeStaticString = kSsoCapacity + 1,
        TypeAllocatedString,
        TypeInt,
        TypeUInt,
        TypeBoolean,
        TypeReal,
        TypeArray,
        TypeObject,
        TypeNull,
    };

public:
    /**
     * Default constructor. Creates a null value.
     */
    constexpr Value() noexcept = default;

    /**
     * Create a Value of the given type.
     */
    Value(ValueType type);

    /**
     * Create a signed integer value.
     */
    Value(Int value) noexcept;

    /**
     * Create an unsigned integer value.
     */
    Value(UInt value) noexcept;

    /**
     * Create a double value.
     */
    Value(double value) noexcept;

    /**
     * Create a string value holding the decimal representation of a Number.
     */
    Value(xrpl::Number const& value);

    /** @{ */
    /**
     * Create a string value.
     *
     * Uses small string optimization for strings <= 15 characters.
     */
    Value(std::string_view value) noexcept
    {
        if (!initSSO(value))
            initAllocatedString(value);
    }

    Value(std::string const& value) noexcept : Value(std::string_view{value})
    {
    }

    Value(char const* value) noexcept : Value(std::string_view{(value != nullptr) ? value : ""})
    {
    }
    /** @} */

    /**
     * Prevent construction from nullptr.
     */
    Value(std::nullptr_t) = delete;

    /** @{ */
    /**
     * Create a string value from a static string.
     *
     * Does not duplicate the string for internal storage. The given string
     * must remain alive for the lifetime of this Value.
     *
     * @note If the string is <= 15 characters, it will be copied into
     *       SSO storage regardless.
     */
    Value(StaticString value) noexcept
    {
        if (!initSSO(value))
            initStaticString(value);
    }

    Value(CZString const& value) noexcept
    {
        std::string_view const str{value.cStr()};

        if (!initSSO(str))
        {
            if (value.isStatic())
            {
                initStaticString(str);
            }
            else
            {
                initAllocatedString(str);
            }
        }
    }
    /** @} */

    /**
     * Create a boolean value.
     */
    Value(bool value) noexcept;

    /**
     * Copy constructor.
     */
    Value(Value const& other);

    /**
     * Move constructor. Left in null state.
     */
    Value(Value&& other) noexcept : data_(other.data_)
    {
        other.data_.type = TypeNull;
    }

    ~Value() noexcept;

    /**
     * Unified copy and move assignment operator.
     *
     * Takes other by value, allowing the compiler to select copy or move
     * construction at the call site. The assignment itself is a single swap.
     */
    Value&
    operator=(Value other) noexcept
    {
        std::swap(data_, other.data_);
        return *this;
    }

    /**
     * Returns the type of the held value.
     *
     * @note All internal string representations return ValueType::String.
     */
    [[nodiscard]] ValueType
    type() const noexcept;

    /**
     * Returns the value as a string_view.
     *
     * @return The string value, or empty string_view if not a string type.
     */
    [[nodiscard]] std::string_view
    asStringView() const noexcept;

    /**
     * Returns the value as a std::string.
     *
     * Numeric types are converted to their string representation.
     */
    [[nodiscard]] std::string
    asString() const;

    /**
     * Returns the value as a signed integer.
     *
     * @note Asserts if the stored value is outside [kMinInt, kMaxInt].
     */
    [[nodiscard]] Int
    asInt() const;

    /**
     * Returns the value as an unsigned integer.
     *
     * @note Asserts if the stored value is negative or exceeds kMaxUInt.
     */
    [[nodiscard]] UInt
    asUInt() const;

    /**
     * Returns the absolute value as an unsigned integer.
     *
     * @throws json::Error if the magnitude exceeds kMaxUInt or the type is
     *         not convertible to an integer.
     */
    [[nodiscard]] UInt
    asAbsUInt() const;

    /**
     * Returns the value as a double.
     */
    [[nodiscard]] double
    asDouble() const;

    /**
     * Returns the value as a boolean.
     */
    [[nodiscard]] bool
    asBool() const noexcept;

    [[nodiscard]] bool
    isNull() const noexcept
    {
        return data_.type == TypeNull;
    }

    [[nodiscard]] bool
    isBool() const noexcept
    {
        return data_.type == TypeBoolean;
    }

    [[nodiscard]] bool
    isInt() const noexcept
    {
        return data_.type == TypeInt;
    }

    [[nodiscard]] bool
    isUInt() const noexcept
    {
        return data_.type == TypeUInt;
    }

    [[nodiscard]] bool
    isIntegral() const noexcept
    {
        return data_.type == TypeInt || data_.type == TypeUInt || data_.type == TypeBoolean;
    }

    [[nodiscard]] bool
    isDouble() const noexcept
    {
        return data_.type == TypeReal;
    }

    [[nodiscard]] bool
    isNumeric() const noexcept
    {
        return isIntegral() || isDouble();
    }

    [[nodiscard]] bool
    isString() const noexcept
    {
        return data_.type <= TypeAllocatedString;
    }

    [[nodiscard]] bool
    isArray() const noexcept
    {
        return data_.type == TypeArray;
    }

    [[nodiscard]] bool
    isArrayOrNull() const noexcept
    {
        return data_.type == TypeNull || data_.type == TypeArray;
    }

    [[nodiscard]] bool
    isObject() const noexcept
    {
        return data_.type == TypeObject;
    }

    [[nodiscard]] bool
    isObjectOrNull() const noexcept
    {
        return data_.type == TypeNull || data_.type == TypeObject;
    }

    /**
     * Returns the number of elements in an array or object.
     */
    [[nodiscard]] UInt
    size() const noexcept;

    /**
     * Returns false if this is null, empty array/object, or empty string.
     */
    explicit
    operator bool() const noexcept;

    /**
     * Access an array element by index.
     *
     * If the array contains fewer than index+1 elements, null values are
     * inserted to extend the array.
     *
     * @note You may need to write value[0u] to disambiguate from the
     *       string key overload.
     */
    [[nodiscard]] Value&
    operator[](UInt index);

    /**
     * Access an array element by index (const).
     */
    [[nodiscard]] Value const&
    operator[](UInt index) const;

    /**
     * Append value to array at the end.
     *
     * This is a unified version that handles both rvalues and lvalues
     * optimally.
     *
     * @param value The value to append. The argument is materialized
     *              before the array is modified, so it may safely be
     *              an element of (or reference into) this array.
     *
     * @return A reference to the newly appended value. The reference
     *         is guaranteed to remain valid until the array is
     *         modified.
     *
     *
     * @throws json::Error if this Value is neither an array nor null.
     */
    Value&
    append(Value value)
    {
        return ((*this)[size()] = std::move(value));
    }

    /**
     * Access an object member by key.
     *
     * Creates the member as a null Value if it does not exist.
     *
     * @note We use StaticString to avoid allocating memory for string
     *       literals stored inside a Value.
     */
    template <typename T>
        requires std::convertible_to<T, std::string_view>
    [[nodiscard]] Value&
    operator[](T const& key);

    /**
     * Access an object member by key (const).
     */
    [[nodiscard]] Value const&
    operator[](std::string_view key) const;

    /**
     * Get an object member with default.
     */
    [[nodiscard]] Value
    get(std::string_view key, Value defaultValue = {}) const;

    [[nodiscard]] Value
    get(std::size_t index, Value defaultValue = {}) const;

    /**
     * Remove and return a member.
     *
     * @param key The key identifying the member to remove.
     * @return The removed member, or std::nullopt if the member does not exist.
     *
     * @note type() must be ValueType::Object or ValueType::Null.
     */
    std::optional<Value>
    removeMember(std::string_view key);

    /**
     * Check if a member exists.
     *
     * @param type An optional expected type for the member.
     */
    [[nodiscard]] bool
    isMember(std::string_view key, std::optional<ValueType> type = {}) const noexcept;

    /**
     * Return a list of member names.
     *
     * @note type() must be ValueType::Object or ValueType::Null.
     */
    [[nodiscard]] Members
    getMemberNames() const;

    /** @{ */
    /**
     * Iterators over the elements of an array or the members of an object.
     *
     * For any other type, begin() == end(). The const and non-const forms
     * share one definition through the explicit object parameter: a const
     * Value yields a const_iterator, a mutable one an iterator.
     */
    [[nodiscard]] auto
    begin(this auto& self)
    {
        using It = std::conditional_t<
            std::is_const_v<std::remove_reference_t<decltype(self)>>,
            const_iterator,
            iterator>;

        if (self.data_.type == TypeArray)
            return It(self.template as<ArrayStorage*>()->begin());

        if (self.data_.type == TypeObject)
            return It(self.template as<ObjectStorage*>()->begin());

        return It{};
    }

    [[nodiscard]] auto
    end(this auto& self)
    {
        using It = std::conditional_t<
            std::is_const_v<std::remove_reference_t<decltype(self)>>,
            const_iterator,
            iterator>;

        if (self.data_.type == TypeArray)
            return It(self.template as<ArrayStorage*>()->end());

        if (self.data_.type == TypeObject)
            return It(self.template as<ObjectStorage*>()->end());

        return It{};
    }
    /** @} */

private:
    /**
     * Returns the length of the string, if the type is a string.
     */
    [[nodiscard]] std::uint32_t
    stringLength() const noexcept
    {
        XRPL_ASSERT(
            data_.type <= TypeAllocatedString,
            "json::Value::stringLength : called on a non-string");

        // An SSO string's type byte is its remaining capacity.
        if (data_.type < TypeStaticString)
            return xrpl::checkedCast<std::uint32_t>(kSsoCapacity - data_.type);

        if (data_.type <= TypeAllocatedString)
        {
            // initString constructs the length after the pointer.
            return *std::launder(
                reinterpret_cast<std::uint32_t const*>(data_.buffer.data() + sizeof(char const*)));
        }

        return 0;
    }

    /**
     * Access the buffer as a T.
     *
     * A const Value yields a T const&, a mutable one a T&.
     */
    template <typename T>
    [[nodiscard]] auto&
    as(this auto&& self) noexcept
    {
        static_assert(sizeof(T) <= sizeof(self.data_.buffer));
        static_assert(alignof(T) <= alignof(Data));

        using Target = std::
            conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, T const, T>;

        return *std::launder(reinterpret_cast<Target*>(self.data_.buffer.data()));
    }

    [[nodiscard]] bool
    isSSO() const noexcept
    {
        return data_.type < TypeStaticString;
    }

    [[nodiscard]] bool
    isAllocatedString() const noexcept
    {
        return data_.type == TypeAllocatedString;
    }

    [[nodiscard]] bool
    isStaticString() const noexcept
    {
        return data_.type == TypeStaticString;
    }

    bool
    initSSO(std::string_view str) noexcept;
    void
    initAllocatedString(std::string_view str) noexcept;
    void
    initStaticString(std::string_view str) noexcept;
    void
    initString(char const* str, std::size_t len) noexcept;

    // This structure defines the binary layout of the Value type and
    // is manually and deliberately aligned to ensure that the buffer
    // is, itself, aligned correctly for any of the types that it may
    // contain.
    struct alignas(8) Data
    {
        std::array<char, kSsoCapacity> buffer{};
        std::uint8_t type = TypeNull;

        // This cannot be defaulted: the default member initializers of a
        // nested class cannot be used by the enclosing class until the
        // enclosing class is complete.
        // NOLINTNEXTLINE(modernize-use-equals-default)
        constexpr Data() noexcept
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
    operator<=>(ArrayStorage const& other) const noexcept
    {
        return std::lexicographical_compare_three_way(
            begin(),
            end(),
            other.begin(),
            other.end(),
            [](value_type const& a, value_type const& b) -> std::partial_ordering {
                if (auto const cmp = a.first <=> b.first; cmp != 0)
                    return cmp;
                return a.second <=> b.second;
            });
    }

    bool
    operator==(ArrayStorage const& other) const noexcept
    {
        return size() == other.size() &&
            std::equal(begin(), end(), other.begin(), [](value_type const& a, value_type const& b) {
                   return a.first == b.first && a.second == b.second;
               });
    }
};

struct Value::ObjectStorage : std::map<CZString, Value, std::less<>>
{
    using map::map;

    std::partial_ordering
    operator<=>(ObjectStorage const& other) const noexcept
    {
        return std::lexicographical_compare_three_way(
            begin(),
            end(),
            other.begin(),
            other.end(),
            [](value_type const& a, value_type const& b) -> std::partial_ordering {
                if (auto const cmp = a.first <=> b.first; cmp != 0)
                    return cmp;
                return a.second <=> b.second;
            });
    }

    bool
    operator==(ObjectStorage const& other) const noexcept
    {
        return size() == other.size() &&
            std::equal(begin(), end(), other.begin(), [](value_type const& a, value_type const& b) {
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

    using ArrayIterator =
        std::conditional_t<IsConst, ArrayStorage::const_iterator, ArrayStorage::iterator>;

    using ObjectIterator =
        std::conditional_t<IsConst, ObjectStorage::const_iterator, ObjectStorage::iterator>;

    std::variant<std::monostate, ArrayIterator, ObjectIterator> current_;

public:
    using difference_type = std::ptrdiff_t;
    using value_type = Value;
    using reference = std::conditional_t<IsConst, Value const&, Value&>;
    using pointer = std::conditional_t<IsConst, Value const*, Value*>;
    using iterator_category = std::bidirectional_iterator_tag;

    ValueIteratorImpl() = default;

    template <bool OtherConst>
        requires(IsConst && !OtherConst)
    ValueIteratorImpl(ValueIteratorImpl<OtherConst> const& other)
        : current_(
              std::visit(
                  [](auto const& it) -> decltype(current_) {
                      if constexpr (std::is_same_v<std::decay_t<decltype(it)>, std::monostate>)
                      {
                          return std::monostate{};
                      }
                      else
                      {
                          return it;
                      }
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
                if constexpr (!std::is_same_v<std::decay_t<decltype(it)>, std::monostate>)
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
                if constexpr (!std::is_same_v<std::decay_t<decltype(it)>, std::monostate>)
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
            [](auto const& it) -> reference {
                if constexpr (std::is_same_v<std::decay_t<decltype(it)>, std::monostate>)
                {
                    std::terminate();
                }
                else
                {
                    return it->second;
                }
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
            [](auto const& it) -> char const* {
                if constexpr (std::is_same_v<std::decay_t<decltype(it)>, ObjectIterator>)
                {
                    return it->first.cStr();
                }
                else
                {
                    return "";
                }
            },
            current_);
    }

    [[nodiscard]] Value
    key() const
    {
        return std::visit(
            [](auto const& it) -> Value {
                if constexpr (std::is_same_v<std::decay_t<decltype(it)>, ArrayIterator>)
                {
                    return Value(it->first);
                }
                else if constexpr (std::is_same_v<std::decay_t<decltype(it)>, ObjectIterator>)
                {
                    return Value(it->first.cStr());
                }
                else
                {
                    return {};
                }
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
operator<=>(Value const&, Value const&) noexcept;
bool
operator==(Value const&, Value const&) noexcept;

template <typename T>
    requires std::convertible_to<T, std::string_view>
[[nodiscard]] Value&
Value::operator[](T const& key)
{
    if (data_.type == TypeNull)
        *this = Value(ValueType::Object);

    if (data_.type != TypeObject)
        xrpl::Throw<Error>("Attempt to treat non-object value as object.");

    auto* obj = as<ObjectStorage*>();

    // One traversal: lower_bound finds the member or the place to insert it.
    auto it = obj->lower_bound(std::string_view(key));

    if (it != obj->end() && it->first == std::string_view(key))
        return it->second;

    return obj->try_emplace(it, CZString(key))->second;
}

/**
 * Extract an integer value from a json::Value.
 *
 * Attempts to convert the given JSON value to the requested integral type.
 * The JSON value may be a signed integer, an unsigned integer, or a decimal
 * string representation. Range checking is performed using std::in_range.
 *
 * @return The converted value, or std::nullopt if out of range or wrong type.
 */
template <typename T>
    requires(std::integral<T> && !std::same_as<T, bool>)
std::optional<T>
toInteger(Value const& v) noexcept
{
    if (v.isInt())
    {
        if (auto const num = v.asInt(); std::in_range<T>(num))
            return xrpl::checkedCast<T>(num);

        return std::nullopt;
    }

    if (v.isUInt())
    {
        if (auto const num = v.asUInt(); std::in_range<T>(num))
            return xrpl::checkedCast<T>(num);

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

/**
 * The signed and unsigned integer types that we support.
 */
using UInt = Value::UInt;
using Int = Value::Int;

/**
 * Convert a Number to its JSON (string) representation.
 */
inline Value
toJson(xrpl::Number const& number)
{
    return to_string(number);
}

//------------------------------------------------------------------------------
// Serialization / Deserialization

/**
 * Defines the format options to use when writing JSON output.
 */
struct FormatOptions
{
    std::string_view newline;
    std::string_view keySep;
    std::string_view indent;
};

/**
 * The format specifier used when writing compact/machine output.
 */
inline constexpr FormatOptions kCompactFormat{.newline = "", .keySep = ":", .indent = ""};

/**
 * The format specifier used when writing styled output.
 */
inline constexpr FormatOptions kStyledFormat{.newline = "\n", .keySep = " : ", .indent = "   "};

namespace detail {

/**
 * Amount of memory to use for internal chunked buffering.
 */
inline constexpr std::size_t kDefaultBufferSize = 512;

/**
 * Base class for JSON serialization sinks.
 *
 * We derive classes that implement append() to direct serialized
 * JSON output to a specific destination (e.g. into a std::string
 * or to a boost::beast::multi_buffer).
 *
 * stream() is compiled separately and sees only this interface, so
 * every append() is an indirect call. The sinks that care (the
 * multi_buffer one) buffer internally to keep the number of calls
 * proportional to the output size rather than to the token count.
 */
struct StreamSink
{
    virtual void
    append(std::string_view sv) = 0;

    virtual ~StreamSink() = default;
};

}  // namespace detail

/**
 * Stream JSON to the specified sink.
 *
 * @param jv   The json::Value to serialize.
 * @param sink The output sink to write to.
 * @param fmt  The output format (compact by default).
 */
void
stream(Value const& jv, detail::StreamSink& sink, FormatOptions const& fmt = kCompactFormat);

/**
 * Serialize a JSON value.
 *
 * @tparam T   The output type: either std::string or
 *             boost::beast::multi_buffer.
 * @param jv   The json::Value to serialize.
 * @param fmt  The output format (compact by default).
 */
template <typename T>
    requires(
        std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<char>> ||
        std::is_same_v<T, std::vector<unsigned char>> ||
        std::is_same_v<T, std::vector<std::uint8_t>> ||
        std::is_same_v<T, boost::beast::multi_buffer>)
[[nodiscard]] T
save(Value const& jv, FormatOptions const& fmt = kCompactFormat)
{
    if constexpr (std::is_same_v<T, boost::beast::multi_buffer>)
    {
        struct MultiBufferSink : detail::StreamSink
        {
            boost::container::static_vector<char, detail::kDefaultBufferSize> chunk;
            boost::beast::multi_buffer sb;

            void
            flush()
            {
                auto buf = sb.prepare(chunk.size());
                boost::asio::buffer_copy(buf, boost::asio::buffer(chunk.data(), chunk.size()));
                sb.commit(chunk.size());
                chunk.clear();
            }

            void
            append(std::string_view sv) override
            {
                while (!sv.empty())
                {
                    std::size_t const n = std::min(sv.size(), chunk.capacity() - chunk.size());
                    chunk.insert(chunk.end(), sv.begin(), sv.begin() + n);
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
                out.reserve(detail::kDefaultBufferSize);
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
                out.reserve(detail::kDefaultBufferSize);
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

/**
 * Serialize a JSON value to a compact std::string.
 */
inline std::string
to_string(Value const& jv)
{
    return save<std::string>(jv, kCompactFormat);
}

/**
 * Serialize a JSON value to a styled std::string.
 */
inline std::string
toStyledString(Value const& jv)
{
    return save<std::string>(jv, kStyledFormat);
}

/**
 * Serialize a JSON value to a compact std::string.
 */
inline std::string
toCompactString(Value const& jv)
{
    return save<std::string>(jv, kCompactFormat);
}

/**
 * Serialize a JSON value to a boost::beast::multi_buffer.
 */
[[nodiscard]] inline boost::beast::multi_buffer
toMultiBuffer(Value const& jv, FormatOptions const& fmt = kCompactFormat)
{
    return save<boost::beast::multi_buffer>(jv, fmt);
}

/**
 * Write the given value, in compact format, to the output stream.
 */
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

/**
 * The default maximum size of a JSON member name.
 */
static constexpr std::size_t kDefaultKeySizeLimit = 128;

/**
 * The default maximum size for JSON strings.
 */
static constexpr std::size_t kDefaultStringSizeLimit = 4 * 1024 * 1024;

/**
 * The default maximum size for JSON documents.
 */
static constexpr std::size_t kDefaultDocumentSizeLimit = 64 * 1024 * 1024;

/**
 * The default nesting limit for JSON parsing.
 */
static constexpr std::uint16_t kDefaultNestLimit = 25;

/**
 * Parse a UTF-8 encoded JSON document into a Value.
 *
 * Parsing strictly follows RFC 8259. The document must contain exactly
 * one JSON value; we do not support extensions such as trailing commas
 * or comments.
 *
 * As permitted by section 9 of RFC 8259, we additionally impose limits
 * that are implementation-specific and which go beyond the grammar:
 *
 *   - the top-level value must be an object, an array or null;
 *   - object member names may not contain an escaped or unescaped NUL;
 *   - duplicate member names within an object are rejected; and
 *   - nesting depth is bounded by nestLimit.
 *
 * @param document   The UTF-8 encoded JSON document to parse.
 * @param error      On failure, contains a description of the parse error.
 * @param nestLimit The maximum nesting depth allowed.
 *
 * @return The parsed value on success, or std::nullopt on failure.
 */
std::optional<Value>
load(std::string_view document, std::string& error, std::uint16_t nestLimit = kDefaultNestLimit);

/**
 * Deserialize a JSON document, discarding the error message.
 */
inline std::optional<Value>
load(std::string_view document, std::uint16_t nestLimit = kDefaultNestLimit)
{
    std::string error;
    return load(document, error, nestLimit);
}

/**
 * Deserialize a JSON document into an existing Value.
 *
 * @return true on success, false on failure.
 */
inline bool
load(std::string_view document, Value& result, std::uint16_t nestLimit = kDefaultNestLimit)
{
    if (auto v = load(document, nestLimit))
    {
        result = std::move(*v);
        return true;
    }

    return false;
}

}  // namespace json
