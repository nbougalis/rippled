//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CHRONO_BASIC_SECONDS_CLOCK_H_INCLUDED
#define BEAST_CHRONO_BASIC_SECONDS_CLOCK_H_INCLUDED

#include <boost/predef/os.h>

#include <chrono>

#if BOOST_OS_LINUX
#include <time.h>
#endif

namespace beast {

/** A monotonically increasing clock with one-second granularity.

    This clock inherits the period of std::chrono::steady_clock, but
    its minimum granularity/resolution is always one second. It will
    always report whole seconds.

    This clock is for measuring elapsed time only: the epoch is left
    unspecified. Differences between time points are meaningful, but
    absolute values are not.

    Time points share std::chrono::steady_clock's type and epoch and
    can be compared or subtracted against that clock's values, with
    up to one second of error in any mixed arithmetic.

    On Linux, reads of this clock are very inexpensive: it leverages
    CLOCK_MONOTONIC_COARSE (which is serviced directly from the vDSO
    and does not require a syscall), falling back to CLOCK_MONOTONIC
    if necessary.

    On other targets, the cost of reads is the same as the cost of a
    call to std::chrono::steady_clock::now().
 */
class basic_seconds_clock
{
public:
    using Clock = std::chrono::steady_clock;

    using rep = Clock::rep;
    using period = Clock::period;
    using duration = Clock::duration;
    using time_point = Clock::time_point;

#if BOOST_OS_LINUX
    static constexpr bool is_steady = true;
#else
    static constexpr bool is_steady = Clock::is_steady;
#endif

    explicit basic_seconds_clock() = default;

    static time_point
    now() noexcept
    {
#if BOOST_OS_LINUX
        // Since libstdc++/libc++ implement steady_clock via CLOCK_MONOTONIC,
        // the time_point is the same with those steady_clock::now() produces.
        // We prefer the coarse clock, which is always vDSO-based, and fall
        // back to the fine-grained one in the unlikely event of a failure:
        if (::timespec ts; ::clock_gettime(CLOCK_MONOTONIC_COARSE, &ts) == 0 ||
            ::clock_gettime(CLOCK_MONOTONIC, &ts) == 0) [[likely]]
        {
            return time_point{std::chrono::seconds{ts.tv_sec}};
        }

        // POSIX mandates CLOCK_MONOTONIC, so reaching here suggests that
        // something with the environment is unusable.
        std::terminate();
#else
        return std::chrono::floor<std::chrono::seconds>(Clock::now());
#endif
    }
};
}  // namespace beast

#endif
