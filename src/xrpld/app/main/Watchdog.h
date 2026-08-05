/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MAIN_WATCHDOG_H_INCLUDED
#define RIPPLE_APP_MAIN_WATCHDOG_H_INCLUDED

#include <atomic>
#include <cstdint>

namespace ripple {

/** Detects server stalls by watching for missing heartbeats.

    The monitored side calls heartbeat() to prove that the server is
    making progress; a single monitoring thread calls observe() at a
    nominally fixed cadence (one second) and acts on the returned stall
    count. This class only measures: all escalation policy (reporting,
    dumping diagnostics and, ultimately, terminating the process) belongs
    to the observer. A wedged server that stays up is worse than a dead
    one: it looks alive to process-level monitoring while silently
    failing to participate in the network.

    The detector is inert until arm() is called. Arming asserts that
    heartbeats are expected from that point on; consequently, a server
    which arms but whose heartbeat machinery never starts will
    (correctly) accumulate a stall count. Standalone mode never arms.

    Thread safety: heartbeat() and arm() are lock-free and safe from any
    thread. observe() must only ever be called from one thread. The
    returned stall count is denominated in calls to observe(), so the
    caller's cadence defines the unit; at 1 Hz it is seconds.
*/
class Watchdog
{
    /** Determines if the watchdog is armed. */
    std::atomic<bool> armed_ = false;

    /** Tracks the number of heartbeats reported by the caller. */
    std::atomic<std::uint64_t> counter_ = 0;

    /** The last counter value we observed.

        @note Only accessed from the thread calling observe().
    */
    std::uint64_t last_ = 0;

    /** The number of consecutive observations without progress.

        @note Only accessed from the thread calling observe().
    */
    std::uint64_t stalled_ = 0;

public:
    Watchdog() = default;

    Watchdog(Watchdog const&) = delete;
    Watchdog&
    operator=(Watchdog const&) = delete;

    /** Prove liveness. Lock-free; safe from any thread. */
    void
    heartbeat() noexcept
    {
        counter_.fetch_add(1, std::memory_order_relaxed);
    }

    /** Begin enforcement.

        Must be called by a supervisor whose own liveness is not under
        test (i.e. not by the monitored machinery), since a stalled
        server cannot be relied on to arm its own detector.

        Lock-free; safe from any thread.
    */
    void
    arm() noexcept
    {
        armed_.store(true, std::memory_order_relaxed);
    }

    /** Sample the heartbeat counter and update the stall measurement.

        This must be called from exactly one thread and at a nominally
        fixed cadence; the returned value will advance by at most one per
        call, so callers may reliably compare it with modular arithmetic
        (e.g. to report every Nth tick).

        @return The number of consecutive observations without progress.
                Zero while disarmed or whenever progress has occurred.
    */
    [[nodiscard]] std::uint64_t
    observe() noexcept
    {
        auto const seen = counter_.load(std::memory_order_relaxed);

        if (seen != last_ || !armed_.load(std::memory_order_relaxed))
        {
            // Progress was made, or we are not yet enforcing. We resync
            // both the progress marker and stall count, so that neither
            // stale heartbeats nor stale counts carry across arming.
            last_ = seen;
            stalled_ = 0;
            return 0;
        }

        return ++stalled_;
    }
};

}  // namespace ripple

#endif
