#include <test/jtx/Env.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace xrpl::test {

//------------------------------------------------------------------------------

class JobQueue_test : public beast::unit_test::Suite
{
    void
    testAddJob()
    {
        jtx::Env env{*this};

        JobQueue& jQueue = env.app().getJobQueue();
        {
            // addJob() should run the Job (and return true).
            std::atomic<bool> jobRan{false};
            BEAST_EXPECT(jQueue.addJob(JobType::Client, "JobAddTest1", [&jobRan]() {
                jobRan = true;
            }) == true);

            // Wait for the Job to run.
            while (!jobRan)
                ;
        }

        // If the JobQueue is stopped or in the process of stopping, attempting
        // to add a job should fail (and calling addJob() should return false).
        jQueue.stop();
        BEAST_EXPECT(jQueue.isStopping());

        BEAST_EXPECT(jQueue.addJob(JobType::Client, "JobAddTest2", []() {
            logicError("Unscheduled job improperly invoked!");
        }) == false);
    }

    void
    testPostCoro()
    {
        jtx::Env env{*this};

        JobQueue& jQueue = env.app().getJobQueue();
        {
            // Test repeated post()s until the Coro completes.
            std::atomic<int> yieldCount{0};
            auto const coro = jQueue.postCoro(
                JobType::Client,
                "PostCoroTest1",
                [&yieldCount](std::shared_ptr<JobQueue::Coro> const& coroCopy) {
                    while (++yieldCount < 4)
                        coroCopy->yield();
                });
            BEAST_EXPECT(coro != nullptr);

            // Wait for the Job to run and yield.
            while (yieldCount == 0)
                ;

            // Now re-post until the Coro says it is done.
            int old = yieldCount;
            while (coro->runnable())
            {
                BEAST_EXPECT(coro->post());
                while (old == yieldCount)
                {
                }
                coro->join();
                BEAST_EXPECT(++old == yieldCount);
            }
            BEAST_EXPECT(yieldCount == 4);
        }
        {
            // Test repeated resume()s until the Coro completes.
            int yieldCount{0};
            auto const coro = jQueue.postCoro(
                JobType::Client,
                "PostCoroTest2",
                [&yieldCount](std::shared_ptr<JobQueue::Coro> const& coroCopy) {
                    while (++yieldCount < 4)
                        coroCopy->yield();
                });
            if (!coro)
            {
                // There's no good reason we should not get a Coro, but we
                // can't continue without one.
                BEAST_EXPECT(false);
                return;
            }

            // Wait for the Job to run and yield.
            coro->join();

            // Now resume until the Coro says it is done.
            int old = yieldCount;
            while (coro->runnable())
            {
                coro->resume();  // Resume runs synchronously on this thread.
                BEAST_EXPECT(++old == yieldCount);
            }
            BEAST_EXPECT(yieldCount == 4);
        }

        // If the JobQueue is stopped or in the process of stopping, attempting
        // to schedule a coroutine should fail.
        jQueue.stop();
        BEAST_EXPECT(jQueue.isStopping());

        auto const coro = jQueue.postCoro(
            JobType::Client, "PostCoroTest3", [](std::shared_ptr<JobQueue::Coro> const&) {
                logicError("Unscheduled coroutine improperly invoked!");
            });
        BEAST_EXPECT(coro == nullptr);
    }

    void
    testJobOrdering()
    {
        jtx::Env env{*this};
        JobQueue& jq = env.app().getJobQueue();

        // Hold all workers while we enqueue, so jobs accumulate in the
        // queue rather than executing as they arrive.
        std::atomic<unsigned> gate{jq.threadCount() + 1};

        auto pass = [&gate] {
            if (gate.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                gate.notify_all();
                return;
            }

            auto v = gate.load(std::memory_order_acquire);

            while (v != 0)
            {
                gate.wait(v, std::memory_order_acquire);
                v = gate.load(std::memory_order_acquire);
            }
        };

        for (unsigned i = 0; i < jq.threadCount(); ++i)
            jq.addJob(JobType::Admin, "gate", pass);

        // Sprinkle jobs across several types, interleaved, tagged with
        // their per-type sequence number.
        std::mutex m;
        std::map<JobType, std::vector<int>> order;

        auto record = [&](JobType t, int seq) {
            return [&, t, seq] {
                std::scoped_lock const lock(m);
                order[t].push_back(seq);
            };
        };

        constexpr JobType types[] = {
            JobType::Pack,
            JobType::Sweep,
            JobType::Transaction,
            JobType::LedgerData,
            JobType::Accept};
        constexpr int perType = 20;

        std::map<JobType, int> seq;

        for (int i = 0; i < perType; ++i)
        {
            for (auto const t : types)
                jq.addJob(t, "ordered", record(t, seq[t]++));
        }

        pass();
        jq.rendezvous();

        {
            std::scoped_lock const lock(m);
            for (auto const t : types)
            {
                if (!BEAST_EXPECT(order[t].size() == perType))
                    continue;
                BEAST_EXPECT(std::ranges::is_sorted(order[t]));
                // Strictly increasing 0..perType-1, i.e. exactly FIFO:
                BEAST_EXPECT(order[t].front() == 0);
                BEAST_EXPECT(order[t].back() == perType - 1);
            }
        }
    }

public:
    void
    run() override
    {
        testAddJob();
        testPostCoro();
        testJobOrdering();
    }
};

BEAST_DEFINE_TESTSUITE(JobQueue, core, xrpl);

}  // namespace xrpl::test
