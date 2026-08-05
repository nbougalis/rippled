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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/LoadManager.h>
#include <xrpld/app/main/Watchdog.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpl/beast/clock/basic_seconds_clock.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <memory>
#include <thread>

namespace ripple {

LoadManager::LoadManager(Application& app, beast::Journal journal)
    : app_(app), journal_(journal)
{
}

LoadManager::~LoadManager()
{
    try
    {
        stop();
    }
    catch (std::exception const& ex)
    {
        // Swallow any exception in a destructor.
        JLOG(journal_.warn())
            << "std::exception in ~LoadManager.  " << ex.what();
    }
}

void
LoadManager::start()
{
    if (!run_.exchange(true, std::memory_order_relaxed))
    {
        thread_ = std::thread([this]() {
            beast::setCurrentThreadName("LoadManager");

            using namespace std::chrono_literals;

            // Limits how often we log (and dump the job queue) while
            // overloaded.
            static constexpr auto overloadLogInterval = 60s;

            // Backdated by one interval so the first overload event always
            // logs.
            auto lastOverloadLog =
                beast::basic_seconds_clock::now() - overloadLogInterval;

            auto checkOverload = [this, &lastOverloadLog]() {
                if (app_.getJobQueue().isOverloaded())
                {
                    if (auto const now = beast::basic_seconds_clock::now();
                        now - lastOverloadLog >= overloadLogInterval)
                    {
                        JLOG(journal_.info())
                            << "Raising local fee (JQ overload): "
                            << app_.getJobQueue().getJson(0);
                        lastOverloadLog = now;
                    }

                    return app_.getFeeTrack().raiseLocalFee();
                }

                return app_.getFeeTrack().lowerLocalFee();
            };

            auto t = beast::basic_seconds_clock::now();

            while (run_.load(std::memory_order_relaxed))
            {
                // Escalation thresholds, denominated in observation ticks (the
                // loop runs at 1 Hz, so nominally seconds).
                constexpr std::uint64_t reportingInterval = 10;
                constexpr std::uint64_t fatalLogLimit = 90;
                constexpr std::uint64_t logicErrorLimit = 600;

                // Check for stalls first: this must not sit behind anything
                // that could itself block on a wedge (e.g. the fee check below,
                // which takes JobQueue and LoadFeeTrack locks).
                if (auto const stalled = app_.watchdog().observe();
                    stalled >= reportingInterval)
                {
                    // stalled advances exactly once per observation, so this
                    // modulo reliably fires every reportingInterval ticks.
                    if ((stalled % reportingInterval) == 0)
                    {
                        if (stalled < fatalLogLimit)
                        {
                            JLOG(journal_.warn()) << "Server stalled for "
                                                  << stalled << " seconds.";
                            if (app_.getJobQueue().isOverloaded())
                            {
                                JLOG(journal_.warn())
                                    << app_.getJobQueue().getJson(0);
                            }
                        }
                        else
                        {
                            JLOG(journal_.fatal())
                                << "Deadlock detected. Deadlocked time: "
                                << stalled << "s";
                            JLOG(journal_.fatal())
                                << "JobQueue: "
                                << app_.getJobQueue().getJson(0);
                        }
                    }

                    // The stall has gone on long enough for us to conclude that
                    // the server is wedged. Terminate unconditionally.
                    if (stalled >= logicErrorLimit)
                        LogicError("Deadlock detected");
                }

                // Instead of a direct call, we should have a way for NetworkOPs
                // to subscribe to the load manager to intercept this event.
                if (checkOverload())
                    app_.getOPs().reportFeeChange();

                t += 1s;

                std::this_thread::sleep_until(t);
            }
        });
    }
}

void
LoadManager::stop()
{
    run_.store(false, std::memory_order_relaxed);

    if (thread_.joinable())
        thread_.join();
}

}  // namespace ripple
