#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace xrpl {

namespace detail {

/**
 * The entire mutable state of a FixedPoolFreelist.
 *
 * Default-constructed, this is all-zero bits: empty freelist, nothing
 * handed out, which is the state of a fresh list. It is kept in the list
 * packed into a plain integer (see the converting constructor and
 * packed()) rather than as a `std::atomic<FixedPoolHead>`: Clang lowers
 * compare-and-swap on a struct to a libatomic call, and GCC routes it
 * through vector registers; on an integer both emit a single
 * `lock cmpxchg`.
 */
struct FixedPoolHead
{
    /**
     * One plus the index of the chunk at the head of the freelist, or
     * zero if the list is empty. The offset is so that an all-zero
     * FixedPoolHead means "empty".
     */
    std::uint16_t slot = 0;

    /**
     * Chunks at or beyond this index have never been handed out.
     */
    std::uint16_t used = 0;

    /**
     * Incremented on every successful update, so that a compare-and-swap
     * fails if the list was modified and restored in between (the ABA
     * problem).
     */
    std::uint32_t tag = 0;

    /**
     * The largest capacity a list can have: one value of `slot` is reserved
     * to mark the end of the freelist.
     */
    static constexpr std::size_t kMaxCapacity = std::numeric_limits<decltype(slot)>::max();

    constexpr FixedPoolHead() noexcept = default;

    /**
     * Unpack a FixedPoolHead from its packed form.
     */
    constexpr explicit FixedPoolHead(std::uint64_t packed) noexcept
        : slot(static_cast<std::uint16_t>(packed))
        , used(static_cast<std::uint16_t>(packed >> 16))
        , tag(static_cast<std::uint32_t>(packed >> 32))
    {
    }

    /**
     * Pack this FixedPoolHead into the form held in the list.
     */
    [[nodiscard]] constexpr std::uint64_t
    packed() const noexcept
    {
        return std::uint64_t{slot} | (std::uint64_t{used} << 16) | (std::uint64_t{tag} << 32);
    }
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint16_t>::is_always_lock_free);

/**
 * A lock-free freelist over the indices `[0, Capacity)`.
 *
 * Indices are handed out in order the first time around and, once
 * released, recycled in LIFO order. The links live in a side array and the
 * rest of the state in a single 64-bit word updated with compare-and-swap,
 * so no operation blocks. That word is one cache line shared by every
 * thread, so throughput does not scale with core count.
 *
 * The list is constructible in a constant expression and is trivially
 * destructible, and a fresh list is all-zero bits, so it can live in a
 * `constinit` global in .bss. This is why the list is not pre-built: the
 * all-zero state must be the initial one, and the links could not be
 * constructed in a constant expression anyway. Indices are instead handed
 * out by a high-water mark and only enter the list when they are released.
 *
 * This is the part of FixedPool that does not depend on the chunk type;
 * every pool of a given capacity shares one instantiation.
 *
 * @tparam Capacity The number of indices; must be non-zero and at most
 *                  FixedPoolHead::kMaxCapacity.
 */
template <std::size_t Capacity>
    requires(Capacity != 0 && Capacity <= FixedPoolHead::kMaxCapacity)
class FixedPoolFreelist
{
    using Head = FixedPoolHead;

    /**
     * The links: next_[i] is the slot (in the sense of Head::slot) that
     * follows index i in the list. Only meaningful while i is in the list;
     * only ever written by the list.
     */
    std::atomic<std::uint16_t> next_[Capacity] = {};

    /**
     * The packed Head.
     */
    std::atomic<std::uint64_t> head_;

public:
    constexpr FixedPoolFreelist() noexcept = default;
    ~FixedPoolFreelist() = default;

    FixedPoolFreelist(FixedPoolFreelist const&) = delete;
    FixedPoolFreelist&
    operator=(FixedPoolFreelist const&) = delete;
    FixedPoolFreelist(FixedPoolFreelist&&) = delete;
    FixedPoolFreelist&
    operator=(FixedPoolFreelist&&) = delete;

    /**
     * Take an index out of the list.
     *
     * @return An index in `[0, Capacity)` that is not held by anyone else
     *         until released, or nullopt if every index is held.
     */
    [[nodiscard]] std::optional<std::size_t>
    acquire() noexcept
    {
        // The acquire pairs with the release in release(), so that the
        // link written there, and whatever the previous holder did with
        // the index before releasing it, are visible here.
        auto old = head_.load(std::memory_order_acquire);

        for (;;)
        {
            Head h(old);

            if (h.slot == 0 && h.used == Capacity)
                return std::nullopt;

            std::size_t const index = h.slot != 0 ? h.slot - 1 : h.used++;

            // May be stale if another thread gets there first; the CAS
            // below will then fail on the tag and we start over.
            if (h.slot != 0)
                h.slot = next_[index].load(std::memory_order_relaxed);

            ++h.tag;

            if (head_.compare_exchange_weak(
                    old, h.packed(), std::memory_order_acquire, std::memory_order_acquire))
                return index;
        }
    }

    /**
     * Put an index back into the list.
     *
     * @param index An index previously returned by acquire() and not since
     *              released. Releasing anything else corrupts the list.
     */
    void
    release(std::size_t index) noexcept
    {
        auto old = head_.load(std::memory_order_relaxed);

        for (;;)
        {
            Head h(old);

            // Nobody else may touch next_[index] while we hold the index,
            // so a plain store ahead of the CAS is enough; the release on
            // the CAS publishes it.
            next_[index].store(h.slot, std::memory_order_relaxed);

            h.slot = static_cast<std::uint16_t>(index + 1);
            ++h.tag;

            if (head_.compare_exchange_weak(
                    old, h.packed(), std::memory_order_release, std::memory_order_relaxed))
                return;
        }
    }
};

}  // namespace detail

/**
 * A fixed-capacity, lock-free pool of uniformly sized chunks.
 *
 * The pool owns a single, suitably aligned buffer of @p Capacity chunks,
 * each large enough for one @p Type, and a FixedPoolFreelist over their
 * indices. The pool hands out raw storage: callers construct into it (for
 * example with std::construct_at) and must destroy the object before
 * returning the chunk. When the pool is exhausted, allocate() returns
 * nullptr and the caller must obtain the memory elsewhere; deallocate()
 * reports whether it owned a chunk, so that memory from such a source can
 * be routed back to it.
 *
 * The pool is meant for small populations of short-lived objects where the
 * point is bounded, contiguous storage and cheap churn; see
 * FixedPoolFreelist for its concurrency properties.
 *
 * The pool is constructible in a constant expression and is trivially
 * destructible, so it can be a `constinit` global in .bss that is never
 * torn down. It is neither copyable nor movable, since chunks handed out
 * refer into it.
 *
 * @tparam Type     The type the chunks are sized and aligned for.
 * @tparam Capacity The number of chunks in the pool; must be non-zero and
 *                  at most detail::FixedPoolHead::kMaxCapacity.
 */
template <typename Type, std::size_t Capacity>
    requires(
        std::is_object_v<Type> && Capacity != 0 && Capacity <= detail::FixedPoolHead::kMaxCapacity)
class FixedPool
{
    /**
     * The chunks. Zero-initialized, so that a constinit pool lives in .bss.
     */
    alignas(alignof(Type)) std::byte storage_[sizeof(Type) * Capacity] = {};

    /**
     * Which chunks are available.
     */
    detail::FixedPoolFreelist<Capacity> freelist_;

public:
    constexpr FixedPool() noexcept = default;
    ~FixedPool() = default;

    FixedPool(FixedPool const&) = delete;
    FixedPool&
    operator=(FixedPool const&) = delete;
    FixedPool(FixedPool&&) = delete;
    FixedPool&
    operator=(FixedPool&&) = delete;

    /**
     * Obtain a chunk from the pool.
     *
     * @return Pointer to uninitialized storage for one Type, or nullptr if
     *         every chunk is in use.
     */
    [[nodiscard]] void*
    allocate() noexcept
    {
        if (auto const index = freelist_.acquire())
            return &storage_[sizeof(Type) * *index];

        return nullptr;
    }

    /**
     * Return a chunk to the pool.
     *
     * Any object in the chunk must already have been destroyed.
     *
     * @param ptr A pointer previously returned by allocate(), or a pointer
     *            the pool does not own (including nullptr), which is left
     *            untouched.
     * @return true if the pool owned the chunk and reclaimed it, false if
     *         the pointer did not come from this pool.
     *
     * @note A pointer to a chunk that was not obtained from allocate(), or
     *       has already been returned, is indistinguishable from a valid
     *       one and corrupts the pool.
     */
    [[nodiscard]] bool
    deallocate(void* ptr) noexcept
    {
        auto const b = reinterpret_cast<std::uintptr_t>(storage_);
        auto const p = reinterpret_cast<std::uintptr_t>(ptr);

        // Divide first and multiply back, rather than testing the remainder
        // and then dividing: GCC otherwise emits a hardware divide for the
        // quotient after having tested divisibility by multiplication.
        auto const offset = p - b;

        if (auto const index = offset / sizeof(Type);
            index < Capacity && index * sizeof(Type) == offset)
        {
            freelist_.release(index);
            return true;
        }

        return false;
    }
};

}  // namespace xrpl
