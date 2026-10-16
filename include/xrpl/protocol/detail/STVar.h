#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <concepts>
#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace xrpl::detail {

struct DefaultObjectT
{
    explicit DefaultObjectT() = default;
};

struct NonPresentObjectT
{
    explicit NonPresentObjectT() = default;
};

inline constexpr DefaultObjectT kDefaultObject{};
inline constexpr NonPresentObjectT kNonPresentObject{};

// Concept to constrain STVar constructors, which
// instantiate ST* types from SerializedTypeID
template <typename... Args>
concept ValidConstructSTArgs =
    (std::is_same_v<std::tuple<std::remove_cvref_t<Args>...>, std::tuple<SField>> ||
     std::is_same_v<std::tuple<std::remove_cvref_t<Args>...>, std::tuple<SerialIter, SField>>);

/**
 * A "variant" that can hold any STBase-derived object.
 *
 * Objects whose size is less than or equal to @c kMaxSize bytes are
 * constructed in-place in a buffer internal to the instance; larger
 * objects are allocated on the heap. Polymorphic copy and move will
 * be delegated to STBase::copy and STBase::move.
 *
 * @note STVar does not preserve the concrete type of the contained
 *       object statically. Access is through a reference to STBase
 *       via get() and dynamic_cast at the call site.
 *
 * @see STBase::copy, STBase::move, STObject
 */
class STVar : public CountedObject<STVar>
{
public:
    /**
     * This is the largest "small object" that we can accommodate.
     *
     * This is a tradeoff: the larger this is, the more objects can leverage
     * SBO and avoid an allocation, but the more wasteful we are.
     *
     * This value was 72 previously, apparently carefully selected to ensure
     * that all types could be constructed in the internal buffer. As of the
     * time of this writing, the four largest types are:
     *
     *     1. STXChainBridge at 224 bytes;
     *     2. STNumber at 96 bytes;
     *     3. STAmount at 80 bytes (more than doubled as a result of MPT!);
     *     4. STIssue at 64 bytes.
     *
     * Since all other types fit in 48 bytes or fewer, shrinking the size of
     * the internal buffer to that size and accepting that STIssue will also
     * require a dynamic allocation seems like a reasonable trade-off.
     *
     * Increasing kMaxSize to 56 would not allow additional types to fit in
     * the internal buffer but would make each STVar occupy one cacheline
     * which can have some performance benefits. But since we keep all STVar
     * instances in contiguous memory, it is unlikely to be meaningful.
     */
    static constexpr std::size_t kMaxSize = 48;

private:
    /**
     * The pointer to the object contained, if any.
     *
     * If an object is contained, this pointer will either point to
     * heap-allocated memory or to the internal buffer.
     */
    STBase* p_ = nullptr;

    /**
     * The internal buffer used for in-place constructions of small objects.
     *
     * By placing it after the pointer, we artificially enforce the alignment
     * requirement we expect from STBase:
     */
    std::byte d_[kMaxSize] = {};

    static_assert(
        alignof(STBase) == alignof(void*),
        "STBase alignment needs to be the same as the alignment of a pointer");

public:
    ~STVar()
    {
        destroy();
    }

    STVar(STVar const& other) : CountedObject(other)
    {
        if (other.p_ != nullptr)
            p_ = other.p_->copy(kMaxSize, d_);
    }

    STVar&
    operator=(STVar const& rhs)
    {
        if (&rhs != this) [[likely]]
        {
            destroy();

            if (rhs.p_ != nullptr)
                p_ = rhs.p_->copy(kMaxSize, d_);
        }

        return *this;
    }

    STVar(STVar&& other)
    {
        if (other.onHeap())
        {
            p_ = other.p_;
            other.p_ = nullptr;
        }
        else
        {
            p_ = other.p_->move(kMaxSize, d_);
        }
    }

    STVar&
    operator=(STVar&& rhs)
    {
        if (&rhs != this) [[likely]]
        {
            destroy();

            if (rhs.onHeap())
            {
                p_ = rhs.p_;
                rhs.p_ = nullptr;
            }
            else
            {
                p_ = rhs.p_->move(kMaxSize, d_);
            }
        }

        return *this;
    }

    // The pointer is declared ahead of the buffer, so it is initialized
    // first: assigning it in the body of the constructors below ensures
    // that the buffer is initialized before an object is placed in it.

    STVar(STBase const& t)
    {
        p_ = t.copy(kMaxSize, d_);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    STVar(STBase&& t)
    {
        p_ = t.move(kMaxSize, d_);
    }

    STVar(DefaultObjectT, SField const& name);
    STVar(NonPresentObjectT, SField const& name);
    STVar(SerialIter& sit, SField const& name, int depth = 0);

    STBase&
    get()
    {
        return *p_;
    }

    STBase&
    operator*()
    {
        return get();
    }

    STBase*
    operator->()
    {
        return &get();
    }

    [[nodiscard]] STBase const&
    get() const
    {
        return *p_;
    }

    STBase const&
    operator*() const
    {
        return get();
    }

    STBase const*
    operator->() const
    {
        return &get();
    }

    template <class T, class... Args>
        requires std::derived_from<T, STBase>
    friend STVar
    makeStvar(Args&&... args);

private:
    STVar() = default;

    STVar(SerializedTypeID id, SField const& name);

    [[nodiscard]] bool
    onHeap() const noexcept
    {
        return static_cast<void const*>(p_) != static_cast<void const*>(d_);
    }

    void
    destroy()
    {
        if (onHeap())
        {
            delete p_;
        }
        else
        {
            std::destroy_at(p_);
        }

        p_ = nullptr;
    }

    template <class T, class... Args>
        requires std::derived_from<T, STBase>
    void
    construct(Args&&... args)
    {
        // The expectation is that all types constructed here share the
        // same alignment as STBase. It is possible for a derived class
        // to be "overaligned" but constructing such an instance in the
        // internal buffer would result in UB. If handling this case is
        // required without changing the alignment of the buffer, using
        // the heap allocation path is the best option.
        static_assert(alignof(T) == alignof(decltype(p_)), "Misaligned STBase-derived type");

        XRPL_ASSERT(p_ == nullptr, "xrpl::detail::STVar::construct : not already constructed");

        if constexpr (sizeof(T) > kMaxSize)
        {
            p_ = new T(std::forward<Args>(args)...);
        }
        else
        {
            p_ = std::construct_at(reinterpret_cast<T*>(d_), std::forward<Args>(args)...);
        }
    }

    /**
     * Construct the requested type according to the serialized type ID.
     *
     * As of now, the variadic args must either be:
     *   - a single SField; or
     *   - a SerialIter, followed by an SField.
     *
     * The depth is only relevant in the latter case.
     */
    template <typename... Args>
        requires ValidConstructSTArgs<Args...>
    void
    constructST(SerializedTypeID id, int depth, Args&&... arg);

    friend bool
    operator==(STVar const& lhs, STVar const& rhs)
    {
        return lhs.get().isEquivalent(rhs.get());
    }
};

template <class T, class... Args>
    requires std::derived_from<T, STBase>
STVar
makeStvar(Args&&... args)
{
    STVar st;
    st.construct<T>(std::forward<Args>(args)...);
    return st;
}

}  // namespace xrpl::detail
