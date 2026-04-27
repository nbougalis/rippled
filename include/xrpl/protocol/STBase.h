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

#ifndef RIPPLE_PROTOCOL_STBASE_H_INCLUDED
#define RIPPLE_PROTOCOL_STBASE_H_INCLUDED

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/Serializer.h>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
namespace ripple {

/// Note, should be treated as flags that can be | and &
struct JsonOptions
{
    using underlying_t = unsigned int;
    underlying_t value;

    enum values : underlying_t {
        // clang-format off
        none                        = 0b0000'0000,
        include_date                = 0b0000'0001,
        disable_API_prior_V2        = 0b0000'0010,

        // IMPORTANT `_all` must be union of all of the above; see also operator~
        _all                        = 0b0000'0011
        // clang-format on
    };

    constexpr JsonOptions(underlying_t v) noexcept : value(v)
    {
    }

    [[nodiscard]] constexpr explicit
    operator underlying_t() const noexcept
    {
        return value;
    }
    [[nodiscard]] constexpr explicit
    operator bool() const noexcept
    {
        return value != 0u;
    }
    [[nodiscard]] constexpr auto friend
    operator==(JsonOptions lh, JsonOptions rh) noexcept -> bool = default;
    [[nodiscard]] constexpr auto friend
    operator!=(JsonOptions lh, JsonOptions rh) noexcept -> bool = default;

    /// Returns JsonOptions union of lh and rh
    [[nodiscard]] constexpr JsonOptions friend
    operator|(JsonOptions lh, JsonOptions rh) noexcept
    {
        return {lh.value | rh.value};
    }

    /// Returns JsonOptions intersection of lh and rh
    [[nodiscard]] constexpr JsonOptions friend
    operator&(JsonOptions lh, JsonOptions rh) noexcept
    {
        return {lh.value & rh.value};
    }

    /// Returns JsonOptions binary negation, can be used with & (above) for set
    /// difference e.g. `(options & ~JsonOptions::include_date)`
    [[nodiscard]] constexpr JsonOptions friend
    operator~(JsonOptions v) noexcept
    {
        return {~v.value & static_cast<underlying_t>(_all)};
    }
};

namespace detail {
class STVar;
}

// VFALCO TODO fix this restriction on copy assignment.
//
// CAUTION: Do not create a vector (or similar container) of any object derived
// from STBase. Use Boost ptr_* containers. The copy assignment operator
// of STBase has semantics that will cause contained types to change
// their names when an object is deleted because copy assignment is used to
// "slide down" the remaining types and this will not copy the field
// name. Changing the copy assignment operator to copy the field name breaks the
// use of copy assignment just to copy values, which is used in the transaction
// engine code.

//------------------------------------------------------------------------------

/** Base class for all serialized types.

    A serialized type is a self-describing value that carries both data
    and metadata: a field name that identifies what specific value this
    object represents within its enclosing object.

    Serialized types can be nested: @ref STObject and @ref STArray will
    contain other serialized types, subject to the constraints that any
    associated @ref SOTemplate imposes.

    Like JSON works, it is effectively a basket which has rules on what
    it can hold, and has a well-defined binary representation.

    All concrete serialized types derive from this class and additionally
    inherit from @ref STType to expose a compile-time @ref SerializedTypeID.

    @note "ST" stands for "Serialized Type."
*/
class STBase
{
    SField const* fName;

public:
    virtual ~STBase() = default;

    explicit STBase(SField const& n) noexcept : fName(&n)
    {
    }

    STBase() noexcept : STBase(sfGeneric)
    {
    }

    STBase(const STBase&) = default;

    STBase&
    operator=(const STBase& t) noexcept
    {
        if (!fName->isUseful())
            fName = t.fName;

        return *this;
    }

    template <class D>
    [[nodiscard]] D&
    downcast();

    template <class D>
    [[nodiscard]] D const&
    downcast() const;

    [[nodiscard]] virtual SerializedTypeID
    getSType() const noexcept
    {
        return STI_NOTPRESENT;
    }

    [[nodiscard]] virtual std::string
    getFullText() const
    {
        std::string ret;

        if (getSType() != STI_NOTPRESENT)
        {
            if (fName->hasName())
            {
                ret = fName->fieldName;
                ret += " = ";
            }

            ret += getText();
        }

        return ret;
    }

    [[nodiscard]] virtual std::string
    getText() const
    {
        return {};
    }

    [[nodiscard]] virtual Json::Value
    getJson(JsonOptions /*options*/) const
    {
        return getText();
    }

    virtual void
    add(Serializer& s) const
    {
        // It is impossible for a naked STBase object to be serialized and
        // all derived classes have to override this function. This should
        // NEVER be invoked at runtime, unless things have gone very wrong
        // and the only thing we can sensibly do is to terminate.

        LogicError("Attempt to serialize a naked STBase");
    }

    [[nodiscard]] virtual bool
    isEquivalent(STBase const& t) const
    {
        XRPL_ASSERT(
            getSType() == STI_NOTPRESENT,
            "ripple::STBase::isEquivalent : type not present");
        return t.getSType() == STI_NOTPRESENT;
    }

    [[nodiscard]] virtual bool
    isDefault() const
    {
        return true;
    }

    /** A STBase is a field.
        This sets the name.
    */
    void
    setFName(SField const& n) noexcept
    {
        fName = &n;
    }

    [[nodiscard]] SField const&
    getFName() const noexcept
    {
        return *fName;
    }

    void
    addFieldID(Serializer& s) const
    {
        XRPL_ASSERT(
            fName->isBinary(), "ripple::STBase::addFieldID : field is binary");
        s.addFieldID(fName->fieldType, fName->fieldValue);
    }

    friend std::ostream&
    operator<<(std::ostream& out, const STBase& t)
    {
        return out << t.getFullText();
    }

protected:
    template <class T>
        requires std::derived_from<std::decay_t<T>, STBase>
    [[nodiscard]] static STBase*
    emplace(std::size_t n, void* buf, T&& val)
    {
        using U = std::decay_t<T>;

        if (sizeof(U) > n)
            return new U(std::forward<T>(val));

        return new (buf) U(std::forward<T>(val));
    }

private:
    virtual STBase*
    copy(std::size_t n, void* buf) const
    {
        return emplace(n, buf, *this);
    }

    virtual STBase*
    move(std::size_t n, void* buf)
    {
        return emplace(n, buf, std::move(*this));
    }

    friend class detail::STVar;

    friend bool
    operator==(STBase const& lhs, STBase const& rhs)
    {
        return lhs.getSType() == rhs.getSType() && lhs.isEquivalent(rhs);
    }
};

/** An @ref STBase with a compile-time serialized type identifier.

    Deriving from this class provides the same interface and behavior as
    @ref STBase and makes the @ref SerializedTypeID for the derived type
    as a compile-time constant in a consistent way.

    This can allow generic code to dispatch on the SerializedTypeID from
    the type both during compilation and at runtime.

    @note This class has no members, so it has no impact on either the
          layout or size of derived classes, beyond the overhead which
          is imposed by STBase itself.

    @tparam ID The @ref SerializedTypeID that identifies this type in
               the binary serialization protocol.
 */
template <SerializedTypeID ID, typename Derived>
struct STTypedBase : STBase
{
    using derived_type_t = Derived;

    static constexpr SerializedTypeID type_id = ID;

    STTypedBase() = default;

    explicit STTypedBase(SField const& n) : STBase(n)
    {
        // This can only happen as a result of a programmer error: you are
        // constructing an object of a given type (e.g. STArray) but using
        // a field of a different type (e.g. sfMetadata).
        if (n != sfGeneric && n.fieldType != type_id) [[unlikely]]
            Throw<std::runtime_error>(
                "Field '" + std::string(n.fieldName) + "' has type " +
                std::to_string(n.fieldType) + " but expected " +
                std::to_string(ID));
    }

    /** Destructor

        This is essentially a trivial destructor; there is really nothing
        to do for this shim class at runtime, but we can run compile-time
        correctness checks that are not possible to fit elsewhere.
     */
    ~STTypedBase() override
    {
        // Derived must be final because STTypedBase lifts both copy() and
        // move() from STBase, casting *this to Derived to do this. And if
        // Derived were not the most-derived type, those casts would slice
        // off additional state.
        //
        // Note that these checks live in the destructor body rather than
        // the class body for three reasons:
        //
        // - they require a complete type, but Derived is not complete at
        //   this point.
        // - Bodies of member functions, even when inlined in the body of
        //   the class, get instantiated lazily, at which point the class
        //   is complete.
        // - The destructor is guaranteed to be instantiated, which means
        //   that this compile-time check will always execute.
        static_assert(
            std::is_final_v<Derived>,
            "STTypedBase requires the Derived class to be 'final'");

        static_assert(
            std::is_base_of_v<STTypedBase, Derived>,
            "STTypedBase requires CRTP: Derived must inherit from "
            "STTypedBase<ID, Derived>");
    }

    [[nodiscard]] SerializedTypeID
    getSType() const noexcept final
    {
        return type_id;
    }

    /** Equivalence check: same type-id and `Derived::operator==`.

        This is not marked @c final because there are types whose equivalence
        semantics differ from equality via operator==, namely @ref STAccount,
        which distinguishes default from explicit-zero states). Those classes
        need to be able to override this with their own implementation.
     */
    bool
    isEquivalent(STBase const& t) const override
    {
        if (t.getSType() != type_id)
            return false;

        return static_cast<Derived const&>(*this) ==
            static_cast<Derived const&>(t);
    }

private:
    // These two functions implement copy and move semantics for all
    // classes which derive from STTypedBase. The magic sauce is the
    // static_cast to Derived, so that `emplace` can deduce the type
    // of the object correctly.
    STBase*
    copy(std::size_t n, void* buf) const final
    {
        return emplace(n, buf, static_cast<Derived const&>(*this));
    }

    STBase*
    move(std::size_t n, void* buf) final
    {
        return emplace(n, buf, static_cast<Derived&&>(*this));
    }
};

/** Concept that identifies a concrete serialized type with a compile-time
    type identifier.

    A type satisfies @c STTyped if it:
    - Exposes a @c static @c constexpr @c SerializedTypeID member named
      @c type_id, and
    - Derives from @ref STTypedBase<T::type_id, T>.

    This concept is used to enable optimized dispatch in generic code —
    for example, @ref STBase::downcast can use a cheaper @c static_cast
    instead of @c dynamic_cast for types that satisfy this concept.

    @see STTypedBase
*/
template <typename T>
concept STTyped = requires {
    { T::type_id } -> std::convertible_to<SerializedTypeID>;
} && std::is_base_of_v<STTypedBase<T::type_id, T>, T>;

//------------------------------------------------------------------------------

template <class D>
D&
STBase::downcast()
{
    if constexpr (STTyped<D>)
    {
        if (getSType() == D::type_id)
            return static_cast<D&>(*this);
    }
    else
    {
        D* ptr = dynamic_cast<D*>(this);

        if (ptr)
            return *ptr;
    }

    Throw<std::bad_cast>();
}

template <class D>
D const&
STBase::downcast() const
{
    if constexpr (STTyped<D>)
    {
        if (getSType() == D::type_id)
            return static_cast<D const&>(*this);
    }
    else
    {
        D const* ptr = dynamic_cast<D const*>(this);

        if (ptr)
            return *ptr;
    }

    Throw<std::bad_cast>();
}

}  // namespace ripple

#endif
