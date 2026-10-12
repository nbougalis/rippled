#pragma once

#include <xrpl/basics/UptimeClock.h>
#include <xrpl/beast/utility/Journal.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace xrpl {

/**
 * The signature of the function used to report a load sample: the name
 * of the event, the time it spent running and the time it spent waiting.
 */
using LoadSampler = std::function<
    void(char const*, std::chrono::steady_clock::duration, std::chrono::steady_clock::duration)>;

/**
 * Monitors load levels and response times
 *
 * @todo Consider renaming this, since having both a LoadManager and a
 *       LoadMonitor is confusing, especially considering that they do
 *       different things.
 */
class LoadMonitor
{
public:
    struct Stats
    {
        std::uint64_t count = 0;

        std::chrono::milliseconds averageLatency{};
        std::chrono::milliseconds peakLatency{};

        bool isOverloaded = false;
    };

    explicit LoadMonitor(
        std::optional<std::chrono::milliseconds> targetAverageLatency,
        std::optional<std::chrono::milliseconds> targetPeakLatency,
        beast::Journal j);

    LoadMonitor(LoadMonitor const& other) = delete;
    LoadMonitor&
    operator=(LoadMonitor const& other) = delete;

    LoadMonitor(LoadMonitor&& other) = delete;
    LoadMonitor&
    operator=(LoadMonitor&& other) = delete;

    void
    addSamples(int count, std::chrono::milliseconds latency);

    Stats
    getStats();

    bool
    isOver();

    /**
     * A reference to a function object used to report performance events.
     */
    std::reference_wrapper<LoadSampler const>
    sample()
    {
        return std::cref(eventCallback_);
    }

private:
    void
    update();

    std::mutex mutex_;

    std::uint64_t samples_ = 0;
    std::chrono::milliseconds averageLatency_{};
    std::chrono::milliseconds peakLatency_{};
    std::optional<std::chrono::milliseconds> targetAverageLatency_;
    std::optional<std::chrono::milliseconds> targetPeakLatency_;
    UptimeClock::time_point lastUpdate_;
    beast::Journal const j_;
    LoadSampler eventCallback_;
};

}  // namespace xrpl
