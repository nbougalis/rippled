#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace xrpl {

/**
 * Tracks in-flight closures so that shutdown can wait for their completion.
 *
 * This class lets callers register closures (completion handlers for
 * asynchronous operations) and, later, block on them until every one
 * has been destroyed.
 *
 * The lifetime of a ClosureCounter has two distinct phases:
 *
 * - The initial phase begins with a caller registering a closure by
 *   passing it to @ref wrap. This produces a substitute object that
 *   handles reference counting and can be invoked with the same set
 *   of parameters as the original.
 *
 * - The next phase begins when @ref join is called; the call blocks
 *   until the reference count drops to zero. During this phase, any
 *   calls to @ref wrap will return `std::nullopt`.
 *
 * All operations are lock-free and can be called concurrently. The
 * only exception is `join` itself, which blocks by design.
 *
 * @note Destroying the counter performs a join, which will block
 *       until all substitutes referencing the counter finish.
 *
 * @tparam Ret The return type of the closure.
 * @tparam Args The argument types of the closure.
 */
template <typename Ret, typename... Args>
class ClosureCounter
{
    /**
     * The contract a wrapped closure must satisfy.
     *
     * Substitute stores the closure by value and invokes it as an lvalue,
     * so that is the invocation we check: the closure has to be callable
     * as an lvalue with Args... and yield Ret.
     *
     * Note that a functor which is invocable only as an rvalue (i.e.
     * operator()() &&) does not qualify.
     */
    template <typename Closure>
    static constexpr bool kSuitable =
        std::is_invocable_r_v<Ret, std::remove_reference_t<Closure>&, Args...>;

    /**
     * The state of this counter.
     *
     * The high bit is the join indicator. The remaining bits are
     * the in-flight closure counter.
     *
     * @note we explicitly use a 32-bit unsigned integer type for
     *       this so that we can leverage glibc's use of futex on
     *       Linux.
     */
    std::atomic<std::uint32_t> state_{0};

    static constexpr std::uint32_t kJoinedFlag = 0x80000000;
    static constexpr std::uint32_t kCountMask = kJoinedFlag - 1;

    void
    decrement() noexcept
    {
        // Release: the joiner's acquire load must observe everything
        // the closure wrote before it died.
        auto const prev = state_.fetch_sub(1, std::memory_order::acq_rel);
        XRPL_ASSERT((prev & kCountMask) != 0, "xrpl::ClosureCounter::decrement : count was 0");

        // If the closure has been joined and we were the last count, it
        // is our responsibility to wake anyone waiting for the count to
        // drop to zero.
        if (prev == kJoinedFlag + 1)
            state_.notify_all();
    }

    /**
     * Ownership of one unit of a ClosureCounter's in-flight count.
     *
     * Every live, non-inert substitute holds exactly one count, and this
     * base class is where that ownership lives. Its special members define
     * the complete count discipline:
     *
     * - The adopting constructor takes a count the caller has already paid
     *   for (@ref wrap increments as its gate check and hands the count to
     *   the substitute it constructs). It does not increment.
     * - The copy constructor pays for its own count.
     * - The move constructor transfers ownership; the moved-from instance
     *   becomes inert and does not decrement (and should also not permit
     *   invocation).
     * - The destructor releases the count, if one is held.
     *
     * This is a base class instead of a member to ensure that the decrement
     * only happens AFTER the closure's destructor has completed, since join
     * must not return while any part of a closure, including its destructor
     * which may release captured resources, is still executing.
     *
     * A null counter pointer denotes an inert holder: one that was moved
     * from, or a copy of one. Inert holders take no part in counting.
     */
    class CountHolder
    {
    protected:
        ClosureCounter* counter_;

        explicit CountHolder(ClosureCounter* c) noexcept : counter_(c)
        {
        }

        CountHolder(CountHolder const& o) noexcept : counter_(o.counter_)
        {
            if (counter_ != nullptr)
                counter_->state_.fetch_add(1, std::memory_order::relaxed);
        }

        CountHolder(CountHolder&& o) noexcept : counter_(std::exchange(o.counter_, nullptr))
        {
        }

        CountHolder&
        operator=(CountHolder&& other) = delete;

        CountHolder&
        operator=(CountHolder const& other) = delete;

        ~CountHolder()
        {
            if (counter_ != nullptr)
                counter_->decrement();
        }
    };

    template <typename Closure>
        requires kSuitable<Closure>
    class Substitute : CountHolder
    {
        std::remove_reference_t<Closure> closure_;

    public:
        Substitute(
            ClosureCounter& counter,
            Closure&& closure)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
            noexcept(std::is_nothrow_constructible_v<decltype(closure_), Closure&&>)
            : CountHolder(&counter), closure_(std::forward<Closure>(closure))
        {
        }

        Substitute(Substitute const&) = default;
        Substitute&
        operator=(Substitute const&) = delete;

        Substitute(Substitute&&) = default;
        Substitute&
        operator=(Substitute&&) = delete;

        // Note that Args is not deduced, it is explicit.  So Args&&
        // would be an rvalue reference, not a forwarding reference.  We
        // want to forward exactly what the user declared.
        Ret
        operator()(Args... args) noexcept(
            std::is_nothrow_invocable_r_v<Ret, decltype(closure_)&, Args...>)
        {
            XRPL_ASSERT(
                this->counter_ != nullptr,
                "xrpl::ClosureCounter::Substitute::operator() : not inert");
            return closure_(std::forward<Args>(args)...);
        }
    };

public:
    ClosureCounter() = default;
    // Not copyable or movable.  Outstanding counts would be hard to sort out.
    ClosureCounter(ClosureCounter const&) = delete;

    ClosureCounter&
    operator=(ClosureCounter const&) = delete;

    /**
     * Destructor verifies all in-flight closures are complete.
     */
    ~ClosureCounter()
    {
        join("ClosureCounter", std::chrono::seconds(1), debugLog());
    }

    /**
     * Returns once all counted in-flight closures are destroyed.
     *
     * @return The time spent waiting for the closures to be destroyed.
     */
    std::chrono::milliseconds
    join() noexcept
    {
        auto s = state_.fetch_or(kJoinedFlag, std::memory_order::acq_rel);

        if ((s & kCountMask) == 0)
            return {};

        auto const start = std::chrono::steady_clock::now();

        // fetch_or returns the value the atomic previously held, which may
        // not have had the flag set. We normalize the result to match that
        // or the first wait() would compare against a value the atomic no
        // longer holds and return immediately.
        s |= kJoinedFlag;

        do
        {
            state_.wait(s, std::memory_order::acquire);

            s = state_.load(std::memory_order::acquire);
        } while ((s & kCountMask) != 0);

        return std::chrono::ceil<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
    }

    /**
     * Returns once all counted in-flight closures are destroyed.
     *
     * @param name Name reported if join time exceeds wait.
     * @param wait If join() exceeds this duration report to Journal.
     * @param j Journal written to if wait is exceeded.
     */
    void
    join(char const* name, std::chrono::milliseconds wait, beast::Journal j)
    {
        if (auto const elapsed = join(); elapsed >= wait)
        {
            if (auto stream = j.error())
                stream << name << " took " << elapsed.count() << "ms in ClosureCounter::join().";
        }
    }

    /**
     * Wrap the passed closure with a reference counter.
     *
     * @param closure Closure that accepts Args parameters and returns Ret.
     * @return If join() has been called returns std::nullopt.  Otherwise
     *         returns a std::optional that wraps closure with a
     *         reference counter.
     */
    template <typename Closure>
        requires kSuitable<Closure>
    std::optional<Substitute<Closure>>
    wrap(Closure&& closure)
    {
        // Optimistically increment; the same RMW tells us whether the
        // gate is closed. Relaxed is fine: we do not publish anything
        // here.
        if (auto const prev = state_.fetch_add(1, std::memory_order::relaxed);
            (prev & kJoinedFlag) != 0)
        {
            // Roll back through the normal path, because our transient
            // increment above may be what a joiner is waiting on.
            decrement();
            return std::nullopt;
        }

        return std::optional<Substitute<Closure>>(
            std::in_place, *this, std::forward<Closure>(closure));
    }

    /**
     * Current number of Closures outstanding.  Only useful for testing.
     */
    [[nodiscard]] std::uint32_t
    count() const noexcept
    {
        return state_.load(std::memory_order::relaxed) & kCountMask;
    }

    /**
     * Returns true if this has been joined.
     *
     * Even if true is returned, counted closures may still be in flight.
     * However if (joined() && (count() == 0)) there should be no more
     * counted closures in flight.
     */
    [[nodiscard]] bool
    joined() const noexcept
    {
        return (state_.load(std::memory_order::relaxed) & kJoinedFlag) != 0;
    }
};

}  // namespace xrpl
