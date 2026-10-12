#include <xrpl/core/detail/Workers.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>  // IWYU pragma: keep
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

using namespace xrpl;

namespace {

using namespace std::chrono_literals;

// Generous enough to finish on any system; only reached if the pool
// fails to dispatch a task.
constexpr auto kTimeout = 30s;

/**
 * A callback that counts the tasks it is asked to perform.
 */
struct CountingCallback : Workers::Callback
{
    std::mutex mutex;
    std::condition_variable cv;
    std::size_t processed = 0;
    unsigned int maxInstance = 0;

    void
    processTask(unsigned int instance) override
    {
        std::scoped_lock const lock(mutex);
        maxInstance = std::max(maxInstance, instance);
        ++processed;
        cv.notify_all();
    }

    /**
     * Wait until the given number of tasks has been processed.
     *
     * @param expected The number of tasks to wait for.
     * @return true if the tasks were processed before the timeout.
     */
    [[nodiscard]] bool
    waitFor(std::size_t expected)
    {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, kTimeout, [&] { return processed >= expected; });
    }
};

/**
 * A callback whose tasks block until they are released.
 */
struct BlockingCallback : Workers::Callback
{
    std::mutex mutex;
    std::condition_variable cv;
    std::size_t started = 0;
    std::size_t finished = 0;
    bool released = false;

    void
    processTask(unsigned int) override
    {
        std::unique_lock lock(mutex);
        ++started;
        cv.notify_all();
        cv.wait(lock, [&] { return released; });
        ++finished;
    }

    [[nodiscard]] bool
    waitForStart()
    {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, kTimeout, [&] { return started != 0; });
    }

    void
    release()
    {
        {
            std::scoped_lock const lock(mutex);
            released = true;
        }
        cv.notify_all();
    }
};

/**
 * A callback whose first task throws.
 */
struct ThrowingCallback : CountingCallback
{
    std::atomic<bool> thrown = false;
    std::atomic<unsigned int> uncaught = 0;
    std::atomic<bool> matched = false;

    void
    processTask(unsigned int instance) override
    {
        if (!thrown.exchange(true))
            throw std::runtime_error("Workers test");

        CountingCallback::processTask(instance);
    }

    void
    uncaughtException(unsigned int instance, std::exception_ptr eptr) override
    {
        try
        {
            std::rethrow_exception(eptr);
        }
        catch (std::runtime_error const& e)
        {
            matched = (std::string_view{e.what()} == "Workers test");
        }
        catch (...)  // NOLINT(bugprone-empty-catch)
        {
        }

        ++uncaught;

        // Count this as a processed task, so that waiters are released.
        CountingCallback::processTask(instance);
    }
};

}  // namespace

TEST(Workers, zero_threads)
{
    CountingCallback cb;
    EXPECT_THROW(Workers(cb, "Test", 0), std::logic_error);
}

TEST(Workers, thread_count)
{
    for (unsigned int const threads : {1u, 2u, 4u, 16u, 64u})
    {
        CountingCallback cb;
        Workers const w(cb, "Test", threads);
        EXPECT_EQ(w.count(), threads);
    }
}

TEST(Workers, one_call_per_task)
{
    for (auto const policy : {Workers::WakePolicy::Lazy, Workers::WakePolicy::Eager})
    {
        for (unsigned int const threads : {1u, 2u, 4u, 16u})
        {
            constexpr std::size_t kTasks = 1000;

            CountingCallback cb;
            Workers w(cb, "Test", threads, policy);

            for (std::size_t i = 0; i != kTasks; ++i)
                w.addTask();

            EXPECT_TRUE(cb.waitFor(kTasks));
            w.stop();

            // Every task was dispatched before the pool stopped, so each
            // one must have resulted in precisely one call.
            EXPECT_EQ(cb.processed, kTasks);
            EXPECT_LT(cb.maxInstance, threads);
        }
    }
}

TEST(Workers, concurrent_producers)
{
    constexpr std::size_t kProducers = 4;
    constexpr std::size_t kTasksPerProducer = 5000;

    CountingCallback cb;
    Workers w(cb, "Test", 4);

    {
        std::vector<std::jthread> producers;
        producers.reserve(kProducers);

        for (std::size_t p = 0; p != kProducers; ++p)
        {
            producers.emplace_back([&w] {
                for (std::size_t i = 0; i != kTasksPerProducer; ++i)
                    w.addTask();
            });
        }
    }

    EXPECT_TRUE(cb.waitFor(kProducers * kTasksPerProducer));
    w.stop();
    EXPECT_EQ(cb.processed, kProducers * kTasksPerProducer);
}

TEST(Workers, bursts_after_idle)
{
    // Let the workers go back to sleep between bursts, to exercise
    // the path that wakes them up.
    CountingCallback cb;
    Workers w(cb, "Test", 4);

    std::size_t expected = 0;

    for (int burst = 0; burst != 20; ++burst)
    {
        for (int i = 0; i != 3; ++i)
            w.addTask();

        expected += 3;
        EXPECT_TRUE(cb.waitFor(expected));

        std::this_thread::sleep_for(1ms);
    }

    w.stop();
    EXPECT_EQ(cb.processed, expected);
}

TEST(Workers, stop_waits_for_running_task)
{
    BlockingCallback cb;
    Workers w(cb, "Test", 2);

    w.addTask();
    ASSERT_TRUE(cb.waitForStart());

    std::atomic<bool> stopped = false;

    {
        std::jthread const stopper([&] {
            w.stop();
            stopped = true;
        });

        // The task is still running, so stop() must not return.
        std::this_thread::sleep_for(50ms);
        EXPECT_FALSE(stopped);

        cb.release();
    }

    EXPECT_TRUE(stopped);
    EXPECT_EQ(cb.started, 1u);
    EXPECT_EQ(cb.finished, 1u);
}

TEST(Workers, stop_is_idempotent)
{
    CountingCallback cb;
    Workers w(cb, "Test", 4);

    w.addTask();
    EXPECT_TRUE(cb.waitFor(1));

    {
        // Concurrent calls must all block until the pool has stopped.
        std::vector<std::jthread> stoppers;
        stoppers.reserve(8);

        for (int i = 0; i != 8; ++i)
            stoppers.emplace_back([&w] { w.stop(); });
    }

    w.stop();

    // Once stopped, the pool does not restart and new tasks are ignored.
    w.addTask();
    std::this_thread::sleep_for(10ms);
    EXPECT_EQ(cb.processed, 1u);
}

TEST(Workers, uncaught_exception)
{
    ThrowingCallback cb;
    Workers w(cb, "Test", 1);

    w.addTask();
    EXPECT_TRUE(cb.waitFor(1));
    EXPECT_EQ(cb.uncaught, 1u);
    EXPECT_TRUE(cb.matched);

    // The worker survives the exception and keeps processing tasks.
    w.addTask();
    w.addTask();
    EXPECT_TRUE(cb.waitFor(3));
    w.stop();

    EXPECT_EQ(cb.processed, 3u);
    EXPECT_EQ(cb.uncaught, 1u);
}
