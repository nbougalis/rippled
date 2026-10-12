#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/LoadMonitor.h>

#include <chrono>
#include <functional>
#include <string>
#include <utility>

namespace xrpl {

// VFALCO TODO Rename LoadEvent to ScopedLoadSample
class LoadEvent
{
public:
    LoadEvent(
        std::reference_wrapper<LoadSampler const> callback,
        std::string name,
        bool shouldStart) noexcept
        : name_(std::move(name))
        , callback_(callback)
        , mark_(std::chrono::steady_clock::now())
        , running_(shouldStart)
    {
    }

    LoadEvent(LoadEvent&& other) noexcept
        : name_(std::move(other.name_))
        , callback_(other.callback_)
        , mark_(other.mark_)
        , timeWaiting_(other.timeWaiting_)
        , timeRunning_(other.timeRunning_)
        , running_(other.running_)
        , neutered_(other.neutered_)
    {
        other.running_ = false;
        other.neutered_ = true;
    }

    LoadEvent&
    operator=(LoadEvent&& other) noexcept
    {
        if (this != &other)
        {
            // Report whatever we were measuring before we are overwritten.
            if (running_)
                stop();

            name_ = std::move(other.name_);
            callback_ = other.callback_;
            running_ = other.running_;
            neutered_ = other.neutered_;
            mark_ = other.mark_;
            timeWaiting_ = other.timeWaiting_;
            timeRunning_ = other.timeRunning_;

            // Leave the moved-from object in a sane but "neutered" state.
            other.running_ = false;
            other.neutered_ = true;
        }

        return *this;
    }

    LoadEvent(LoadEvent const&) = delete;
    LoadEvent&
    operator=(LoadEvent const&) = delete;

    ~LoadEvent()
    {
        if (running_)
            stop();
    }

    [[nodiscard]] std::string const&
    name() const noexcept
    {
        return name_;
    }

    // Start the measurement. If already started, then
    // restart, assigning the elapsed time to the "waiting"
    // state.
    void
    start() noexcept
    {
        XRPL_ASSERT(!neutered_, "xrpl::LoadEvent::start : is neutered");

        auto const now = std::chrono::steady_clock::now();

        // If we had already called start, this call will
        // replace the previous one. Any time accumulated will
        // be counted as "waiting".
        timeWaiting_ += now - mark_;
        mark_ = now;
        running_ = true;
    }

    // Stop the measurement and report the results. The
    // time reported is measured from the last call to
    // start.
    void
    stop()
    {
        XRPL_ASSERT(running_, "xrpl::LoadEvent::stop : is running");

        auto const now = std::chrono::steady_clock::now();

        timeRunning_ += now - mark_;
        mark_ = now;
        running_ = false;

        if (!neutered_)
            callback_(name_.c_str(), timeRunning_, timeWaiting_);
    }

private:
    // The name for this event.
    std::string name_;

    // The callback to invoke when we stop. This will only
    // be invoked if `neutered_` is `false`.
    std::reference_wrapper<LoadSampler const> callback_;

    // Represents the time we last transitioned states
    std::chrono::steady_clock::time_point mark_;

    // The time we spent waiting and running respectively
    std::chrono::steady_clock::duration timeWaiting_{};
    std::chrono::steady_clock::duration timeRunning_{};

    // Represents our current state
    bool running_;

    // Determines whether the callback should be invoked
    bool neutered_{false};
};

}  // namespace xrpl
