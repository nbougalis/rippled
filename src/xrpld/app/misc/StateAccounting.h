//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_STATEACCOUNTING_H_INCLUDED
#define RIPPLE_APP_MISC_STATEACCOUNTING_H_INCLUDED

#include <xrpl/basics/safe_cast.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace ripple {

/** Specifies the mode under which the server believes it's operating.

    The mode has implications about how the server processes transactions
    and how it responds to requests (e.g. account balance request).

    @note The numerical values double as indices into a per-state array of
          counters. External code also depends not only on the numerical
          values but on the relative ordering of these constants.
*/
enum class OperatingMode : std::size_t {
    DISCONNECTED = 0,  //!< not ready to process requests
    CONNECTED = 1,     //!< convinced we are talking to the network
    SYNCING = 2,       //!< fallen slightly behind
    TRACKING = 3,      //!< convinced we agree with the network
    FULL = 4           //!< we have the ledger and can even validate
};

[[nodiscard]] inline constexpr std::string_view
to_string(OperatingMode om) noexcept
{
    if (om == OperatingMode::DISCONNECTED)
        return "disconnected";

    if (om == OperatingMode::CONNECTED)
        return "connected";

    if (om == OperatingMode::SYNCING)
        return "syncing";

    if (om == OperatingMode::TRACKING)
        return "tracking";

    if (om == OperatingMode::FULL)
        return "full";

    return "unknown";
}

/** Tracks time spent in, and transitions between, OperatingModes.

    This owns the authoritative current OperatingMode. The value is read
    far more often than it changes, so it is stored in a std::atomic and
    read lock-free. The mutex guards only the cold-path bookkeeping: the
    per-state durations and transition counts.
*/
class StateAccounting
{
public:
    struct Counters
    {
        std::uint64_t transitions = 0;
        std::chrono::microseconds dur = std::chrono::microseconds(0);
    };

private:
    mutable std::mutex mutex_;

    /** Authoritative current mode.

        This is read lock-free, but is written under mutex_ via exchange() so
        the transition bookkeeping below stays consistent with it.
     */
    std::atomic<OperatingMode> mode_;

    /** The previous operating mode (same as current if no transitions)

        This is protected by the mutex and is primarily used for debugging
        purposes, since we don't expose this information anywhere.
     */
    OperatingMode last_;

    /** The counters for each of the modes that we can be in.

        These are protected by the mutex.
     */
    std::array<Counters, 5> counters_{};

    /** The time at which that the current state began. Always non-zero.

        This is protected by the mutex.
     */
    std::chrono::steady_clock::time_point start_;

    /** The wall time in microseconds it took to transition into the full state.

        Note that if the initial mode is full, then the value remains 0 for
        the lifetime of the process.

        This is protected by the mutex.
     */
    std::chrono::microseconds initial_sync = {};

public:
    explicit StateAccounting(
        OperatingMode initial = OperatingMode::DISCONNECTED) noexcept
        : mode_(initial)
        , last_(OperatingMode::DISCONNECTED)
        , start_(std::chrono::steady_clock::now())
    {
        counters_[safe_cast(initial)].transitions = 1;
    }

    StateAccounting(StateAccounting const&) = delete;
    StateAccounting&
    operator=(StateAccounting const&) = delete;

    /** The current operating mode.

        Lock-free. This is the value other subsystems branch on; it is
        deliberately cheap to read.
    */
    [[nodiscard]] OperatingMode
    get() const noexcept
    {
        return mode_.load(std::memory_order_relaxed);
    }

    /** Record a mode transition, banking the time spent in the prior mode.

        While this method internally serializes its own bookkeeping, it does
        not (and, indeed, cannot) arbitrate which transition is correct. The
        mode is set unconditionally; if there are concurrent calls, the last
        one to acquire the mutex wins, regardless of whether its target mode
        is the semantically right one.

        Ensuring that mode transitions are decided and committed atomically
        is the caller's responsibility and the logic that decides this must
        itself be synchronized.

        @param om The new operating mode.

        @note A transition to the mode we are already in is a no-op: the
              in-state clock is not reset, so no duration is banked, and
              no transition is counted.

        @return the previous mode.
    */
    OperatingMode
    set(OperatingMode om)
    {
        std::lock_guard lock(mutex_);

        auto const prev = mode_.exchange(om, std::memory_order_relaxed);

        if (prev != om)
        {
            auto const now = std::chrono::steady_clock::now();

            // Update time accounting for the mode we just exited.
            counters_[safe_cast(prev)].dur +=
                std::chrono::duration_cast<std::chrono::microseconds>(
                    now - start_);

            // Update the last mode.
            last_ = prev;

            auto& target = counters_[safe_cast(om)];

            ++target.transitions;

            // Record the wall time that it took to get to the "full" mode for
            // the first time. Note that if we were created in the "full" mode
            // we will never get here, since the number of transitions will be
            // non-zero.
            if (target.transitions == 1 && om == OperatingMode::FULL)
            {
                for (auto const& c : counters_)
                    initial_sync +=
                        std::chrono::ceil<std::chrono::microseconds>(c.dur);
            }

            start_ = now;
        }

        return prev;
    }

    struct Snapshot
    {
        std::array<Counters, 5> counters;

        /** Time spent in `mode` so far, not yet banked into the counters. */
        std::chrono::microseconds current;

        /** Time it took to get to the FULL state. May be 0. */
        std::chrono::microseconds initial_sync;

        /** The current operating mode. */
        OperatingMode mode;
    };

    /** A consistent snapshot of the accounting state. */
    [[nodiscard]] Snapshot
    snapshot() const
    {
        std::lock_guard lock(mutex_);
        auto const now = std::chrono::steady_clock::now();

        return Snapshot{
            counters_,
            std::chrono::duration_cast<std::chrono::microseconds>(now - start_),
            initial_sync,
            mode_.load(std::memory_order_relaxed)};
    }
};

}  // namespace ripple

#endif
