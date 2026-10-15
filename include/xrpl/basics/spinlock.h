// Copyright (c) 2022, Nikolaos D. Bougalis <nikb@bougalis.net>

#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <boost/predef/architecture.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

#if BOOST_ARCH_X86
#include <immintrin.h>
#endif

namespace xrpl {

namespace detail {

/**
 * An unsigned integral type suitable for use as a spinlock.
 *
 * The type must be always lock-free when wrapped in std::atomic, so that lock
 * operations cannot themselves take a (library-level) lock.
 */
template <typename T>
concept SomeSpinlockValue = std::is_unsigned_v<T> && std::atomic<T>::is_always_lock_free;

/**
 * A spinlock value type that additionally supports the atomic bitwise
 * operations required to pack multiple locks into a single integer.
 */
template <typename T>
concept SomePackedSpinlockValue = SomeSpinlockValue<T> && requires(std::atomic<T>& a, T v) {
    { a.fetch_or(v) } -> std::same_as<T>;
    { a.fetch_and(v) } -> std::same_as<T>;
};

/**
 * Inform the processor that we are in a tight spin-wait loop.
 *
 * A core that polls a memory location in a tight loop fills its pipeline with
 * speculative loads and compare-and-branch sequences. When the value finally
 * changes, the in-flight loads are discarded (a memory-ordering machine clear
 * on x86) and the loop's exit branch mispredicts, both of which add latency at
 * precisely the moment the lock becomes available. On SMT parts the polling
 * core also starves its sibling thread of execution resources.
 *
 * This function issues the architecture's spin-wait hint (`pause` on x86,
 * `yield` on ARM), which throttles the loop for an implementation-defined
 * period and mitigates these effects. The hint's latency varies widely across
 * processor generations (from roughly 10 to well over 100 cycles on x86), and
 * `yield` is effectively a no-op on many ARM cores.
 */
inline void
spinPause() noexcept
{
#if BOOST_ARCH_X86
    _mm_pause();
#elif BOOST_ARCH_ARM
    asm volatile("yield" ::: "memory");
#else
#error No implementation available for spinPause to use
#endif
}

/**
 * The number of bare (un-paused) polling iterations @ref spinLock performs
 * before it begins pausing between reads.
 *
 * Each iteration is one L1-resident load plus a compare and branch, and the
 * loop runs at roughly two cycles per iteration on current x86 cores, so this
 * bounds how long a waiter polls at full speed (on the order of 500 cycles)
 * before it assumes the critical section is not about to end and starts
 * pausing between reads.
 *
 * This value should be on the order of the expected hold time of the locks it
 * protects, measured in loop iterations, and not a general-purpose constant.
 */
constexpr std::size_t spinLockBareSpins = 200;

}  // namespace detail

//------------------------------------------------------------------------------

/**
 * Attempt to acquire a spinlock without blocking.
 *
 * On success the exchange has acquire semantics and is the operation that
 * synchronizes the critical section with the previous holder's release.
 *
 * @note This interface is primarily intended for one-shot attempts to acquire
 *       the lock. Avoid calling this function directly from a loop and use
 *       @ref spinLock instead: a compare-exchange always takes the cache line
 *       for writing, even when it fails, so an unfiltered retry loop generates
 *       coherency traffic on every iteration.
 *
 * @tparam T An unsigned integral type.
 * @param lock The atomic variable used as the lock.
 * @return true if the lock was acquired, false if it was already held.
 */
template <detail::SomeSpinlockValue T>
[[nodiscard]] bool
spinTryLock(std::atomic<T>& lock) noexcept
{
    // A compare-exchange is required here, not an unconditional exchange:
    // a failed attempt must not modify the lock word, in case the atomic
    // is shared with PackedSpinlock.
    T expected = 0;

    return lock.compare_exchange_strong(
        expected,
        std::numeric_limits<T>::max(),
        std::memory_order::acquire,
        std::memory_order::relaxed);
}

/**
 * Acquire a spinlock, blocking until it is available.
 *
 * Acquisition proceeds in two phases:
 *
 * 1. A bare polling phase of @ref detail::spinLockBareSpins iterations: the
 *    lock word is read repeatedly with no pause between reads, and an
 *    acquisition is attempted whenever it reads as free. This catches the
 *    common case of a critical section that lasts only a few hundred cycles
 *    without paying the latency of a spin-wait hint, which on some processors
 *    exceeds the hold time itself.
 *
 * 2. A paused polling phase: the lock word is read repeatedly with a spin-wait
 *    hint between reads (see @ref detail::spinPause), and an acquisition is
 *    attempted only after a read sees the lock free. This phase runs until the
 *    lock is acquired.
 *
 * Both phases use a TTAS (test-and-test-and-set) pattern: the read-modify-write
 * that acquires the lock is only issued after a plain load has observed the
 * lock free, so waiters share the cache line read-only and do not bounce it
 * between cores while the lock is held.
 *
 * The polling loads use relaxed ordering and act only as a filter. The acquire
 * that synchronizes the critical section is the successful exchange inside
 * @ref spinTryLock.
 *
 * @note This function never yields to the scheduler. If the thread holding the
 *       lock is preempted, waiters spin for the remainder of their timeslice.
 *       Use it only to protect critical sections that are short and that do not
 *       block.
 *
 * @tparam T An unsigned integral type.
 * @param lock The atomic variable used as the lock.
 */
template <detail::SomeSpinlockValue T>
void
spinLock(std::atomic<T>& lock) noexcept
{
    // Relaxed ordering is sufficient for the polling loads in both phases:
    // they are only a filter. The acquire on the successful exchange inside
    // spinTryLock is what synchronizes the critical section.

    auto spins = detail::spinLockBareSpins;

    while (spins-- != 0)
    {
        if (lock.load(std::memory_order::relaxed) == 0 && spinTryLock(lock))
            return;
    }

    do
    {
        while (lock.load(std::memory_order::relaxed) != 0)
            detail::spinPause();
    } while (!spinTryLock(lock));
}

/**
 * Release a spinlock.
 *
 * The store has release semantics, so all writes made inside the critical
 * section are visible to the next thread whose @ref spinTryLock succeeds.
 *
 * @note The caller must hold the lock. This is not checked.
 *
 * @tparam T An unsigned integral type.
 * @param lock The atomic variable used as the lock.
 */
template <detail::SomeSpinlockValue T>
void
spinUnlock(std::atomic<T>& lock) noexcept
{
    lock.store(0, std::memory_order::release);
}

//------------------------------------------------------------------------------

/**
 * A Lockable interface to a spinlock implemented on top of an atomic.
 *
 * This class is a non-owning handle: it refers to an atomic integer that lives
 * elsewhere and treats the whole integer as a single lock, using the free
 * functions @ref spinTryLock, @ref spinLock and @ref spinUnlock.
 *
 * This class meets the requirements of Lockable:
 *     https://en.cppreference.com/w/cpp/named_req/Lockable
 *
 * @note Using `PackedSpinlock` and `Spinlock` against the same underlying
 *       atomic integer is possible but can result in `Spinlock` not being
 *       able to acquire the lock during periods of high contention due to
 *       the way the two locks operate: `Spinlock` spins and tries to grab
 *       all the bits at once, whereas any given `PackedSpinlock` instance
 *       only tries to grab one bit at a time. Caveat emptor.
 *
 * @tparam T An unsigned integral type.
 */
template <detail::SomeSpinlockValue T>
class Spinlock
{
    std::atomic<T>& lock_;

public:
    Spinlock(Spinlock const&) = delete;
    Spinlock&
    operator=(Spinlock const&) = delete;

    /**
     * Construct a spinlock handle.
     *
     * @param lock The atomic integer to spin against.
     *
     * @note For performance reasons, you should strive to have `lock` be
     *       on a cacheline by itself.
     */
    explicit Spinlock(std::atomic<T>& lock) noexcept : lock_(lock)
    {
    }

    /**
     * Attempt to acquire the lock without blocking.
     *
     * @return true if the lock was acquired, false if it was already held.
     */
    [[nodiscard]] bool
    try_lock() noexcept  // NOLINT(readability-identifier-naming)
    {
        return spinTryLock(lock_);
    }

    /**
     * Acquire the lock, blocking until it is available.
     *
     * @see spinLock for the polling strategy.
     */
    void
    lock() noexcept
    {
        spinLock(lock_);
    }

    /**
     * Release the lock.
     *
     * @note The caller must hold the lock. This is not checked.
     */
    void
    unlock() noexcept
    {
        spinUnlock(lock_);
    }
};

//------------------------------------------------------------------------------

/**
 * A Lockable interface to a packed spinlock implemented on top of an atomic.
 *
 * Packed spinlocks offer tremendous space-efficient lock-sharding but
 * they come at a cost.
 *
 * First, the implementation is necessarily low-level and uses advanced
 * features like memory ordering and highly platform-specific tricks to
 * maximize performance. This imposes a significant and ongoing cost to
 * developers.
 *
 * Second, and perhaps most important, packing multiple locks into a single
 * integer, albeit space-efficient, has performance implications stemming
 * from data dependencies, increased cache-coherency traffic between
 * processors and heavier loads on the processor's load/store units.
 *
 * To be sure, these locks can have advantages but they are definitely
 * not general purpose locks and should not be thought of or used that
 * way. The use cases for them are likely few and far between; without
 * a compelling reason to use them, backed by profiling data, it might
 * be best to use one of the standard locking primitives instead. Note
 * that in most common platforms, `std::mutex` is so heavily optimized
 * that it can, usually, outperform spinlocks.
 *
 * This class meets the requirements of Lockable:
 *     https://en.cppreference.com/w/cpp/named_req/Lockable
 *
 * @note Unlike @ref spinLock, `lock()` does not perform a bare polling phase;
 *       it pauses between reads from the outset.
 *
 * @tparam T An unsigned integral type (e.g. std::uint16_t)
 */
template <detail::SomePackedSpinlockValue T>
class PackedSpinlock
{
    std::atomic<T>& bits_;
    T const mask_;

public:
    PackedSpinlock(PackedSpinlock const&) = delete;
    PackedSpinlock&
    operator=(PackedSpinlock const&) = delete;

    /**
     * Construct a packed spinlock handle for a single bit.
     *
     * @param lock The atomic integer inside which the spinlock is packed.
     * @param index The index of the spinlock this object acquires.
     *
     * @note For performance reasons, you should strive to have `lock` be
     *       on a cacheline by itself.
     */
    template <std::unsigned_integral U>
        requires(!std::same_as<U, bool>)
    PackedSpinlock(std::atomic<T>& lock, U index) noexcept
        : bits_(lock), mask_([index] {
            XRPL_ASSERT(
                std::cmp_less(index, std::numeric_limits<T>::digits),
                "xrpl::PackedSpinlock::PackedSpinlock : valid index");
            return static_cast<T>(T{1} << index);
        }())
    {
    }

    /**
     * Attempt to acquire the lock without blocking.
     *
     * The attempt is an unconditional atomic OR of this lock's bit, so a
     * failed attempt still performs a read-modify-write on the shared word.
     * Avoid calling this function from a loop; use `lock()`, which filters
     * with a plain load first.
     *
     * @return true if the lock was acquired, false if it was already held.
     */
    [[nodiscard]] bool
    try_lock() noexcept  // NOLINT(readability-identifier-naming)
    {
        return (bits_.fetch_or(mask_, std::memory_order::acquire) & mask_) == 0;
    }

    /**
     * Acquire the lock, blocking until it is available.
     *
     * Uses a TTAS (test-and-test-and-set) pattern with a spin-wait hint
     * between reads: the atomic OR is only issued after a plain load has
     * observed this lock's bit clear.
     */
    void
    lock() noexcept
    {
        do
        {
            // Relaxed ordering is sufficient for the polling load: it is only
            // a filter that avoids read-modify-writes which are unlikely to
            // acquire the lock. The acquire on the successful fetch_or in
            // try_lock is what synchronizes the critical section.
            while ((bits_.load(std::memory_order::relaxed) & mask_) != 0)
                detail::spinPause();
        } while (!try_lock());
    }

    /**
     * Release the lock.
     *
     * The atomic AND has release semantics and clears only this lock's bit;
     * the other locks packed in the same word are unaffected.
     *
     * @note The caller must hold the lock. This is not checked.
     */
    void
    unlock() noexcept
    {
        bits_.fetch_and(~mask_, std::memory_order::release);
    }
};

}  // namespace xrpl
