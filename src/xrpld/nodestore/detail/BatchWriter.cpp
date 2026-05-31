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

#include <xrpld/nodestore/detail/BatchWriter.h>

namespace ripple {
namespace NodeStore {

BatchWriter::BatchWriter(Callback& callback, Scheduler& scheduler)
    : m_callback(callback)
    , m_scheduler(scheduler)
    , mWriteLoad(0)
    , mWritePending(false)
{
    mWriteSet.reserve(batchWritePreallocationSize);
}

BatchWriter::~BatchWriter()
{
    // Block until any scheduled write task has fully drained.
    std::unique_lock sl(mWriteMutex);

    while (mWritePending)
        mWriteCondition.wait(sl);
}

void
BatchWriter::store(boost::intrusive_ptr<NodeObject> object)
{
    std::unique_lock sl(mWriteMutex);

    // If the batch has reached its limit, we wait until the batch
    // writer is finished:
    while (mWriteSet.size() >= batchWriteLimitSize)
        mWriteCondition.wait(sl);

    mWriteSet.emplace_back(std::move(object));

    if (!mWritePending)
    {
        mWritePending = true;

        // This is why we need to use a recursive mutex here: we hold the
        // lock at this point, and if the scheduler cannot queue the task
        // it will invoke performScheduledTask synchronously, which tries
        // to acquire the mutex which we already hold.
        //
        // Long story short: do not move this call out from under the lock
        // or downgrade the mutex without first addressing the synchronous
        // fallback, otherwise we can deadlock or strand writes.
        m_scheduler.scheduleTask(*this);
    }
}

int
BatchWriter::getWriteLoad()
{
    std::lock_guard sl(mWriteMutex);

    return std::max(mWriteLoad, static_cast<int>(mWriteSet.size()));
}

void
BatchWriter::performScheduledTask()
{
    while (true)
    {
        Batch set;
        set.reserve(batchWritePreallocationSize);

        {
            std::lock_guard sl(mWriteMutex);

            mWriteSet.swap(set);
            mWriteLoad = set.size();

            // This covers both the backpressured store case and the
            // destructor waiting for writes to complete.
            mWriteCondition.notify_all();

            if (set.empty())
            {
                mWritePending = false;
                return;
            }
        }

        BatchWriteReport report;
        report.writeCount = set.size();

        auto const before = std::chrono::steady_clock::now();

        m_callback.writeBatch(set);

        report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - before);

        m_scheduler.onBatchWrite(report);
    }
}

}  // namespace NodeStore
}  // namespace ripple
