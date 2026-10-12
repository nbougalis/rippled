#include <test/jtx/Env.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>

#include <atomic>
#include <memory>

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
            BEAST_EXPECT(
                jQueue.addJob(JtClient, "JobAddTest1", [&jobRan]() { jobRan = true; }) == true);

            // Wait for the Job to run.
            while (!jobRan)
                ;
        }

        // If the JobQueue is stopped or in the process of stopping, attempting
        // to add a job should fail (and calling addJob() should return false).
        jQueue.stop();
        BEAST_EXPECT(jQueue.isStopping());

        BEAST_EXPECT(jQueue.addJob(JtClient, "JobAddTest2", []() {
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
                JtClient,
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
                JtClient,
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

        auto const coro =
            jQueue.postCoro(JtClient, "PostCoroTest3", [](std::shared_ptr<JobQueue::Coro> const&) {
                logicError("Unscheduled coroutine improperly invoked!");
            });
        BEAST_EXPECT(coro == nullptr);
    }

public:
    void
    run() override
    {
        testAddJob();
        testPostCoro();
    }
};

BEAST_DEFINE_TESTSUITE(JobQueue, core, xrpl);

}  // namespace xrpl::test
