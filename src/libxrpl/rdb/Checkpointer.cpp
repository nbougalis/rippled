#include <xrpl/rdb/Checkpointer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/rdb/detail/SqliteConnection.h>

#include <soci/session.h>
#include <soci/sqlite3/soci-sqlite3.h>  // IWYU pragma: keep

#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

namespace xrpl {

namespace {

class SQLiteCheckpointer final : public Checkpointer
{
    /**
     * WAL size, in pages, at which a checkpoint is scheduled.
     */
    static constexpr int kCheckpointPageCount = 1000;

    // The session is owned by whoever owns this checkpointer, and outlives it
    // (see the destructor). It is held weakly anyway, so that a checkpoint
    // already in flight cannot be left holding a reference to a closed
    // connection if that ordering is ever violated.
    std::weak_ptr<soci::session> const session_;

    CheckpointScheduler const schedule_;
    beast::Journal const j_;

    std::mutex mutex_;
    std::condition_variable idle_;
    bool running_ = false;
    bool draining_ = false;

public:
    SQLiteCheckpointer(
        std::weak_ptr<soci::session> session,
        CheckpointScheduler schedule,
        beast::Journal journal)
        : session_(std::move(session)), schedule_(std::move(schedule)), j_(journal)
    {
        auto const s = session_.lock();
        if (!s)
            return;

        auto* const conn = detail::getConnection(*s);
        if (conn == nullptr)
            return;

        // sqlite invokes this on the thread that committed, from inside the
        // commit, with the current WAL size. It gets `this` as its user data:
        // nothing can reach a destroyed checkpointer through it, because the
        // destructor uninstalls the hook, and a commit cannot be in progress
        // while the owner is being destroyed.
        //
        // Captureless, so it converts to the plain function pointer sqlite
        // wants, and being local to a member function it has our access.
        sqlite_api::sqlite3_wal_hook(
            conn,
            [](void* p, sqlite_api::sqlite3*, char const*, int walPageCount) {
                if (walPageCount < kCheckpointPageCount)
                    return SQLITE_OK;

                auto* const self = static_cast<SQLiteCheckpointer*>(p);
                {
                    std::scoped_lock const lock(self->mutex_);
                    if (self->running_ || self->draining_)
                        return SQLITE_OK;
                    self->running_ = true;
                }

                if (!self->schedule_([self] { self->checkpoint(); }))
                {
                    // The work will never run, so we are not running after all.
                    std::scoped_lock const lock(self->mutex_);
                    self->running_ = false;
                    self->idle_.notify_all();
                }

                return SQLITE_OK;
            },
            this);
    }

    ~SQLiteCheckpointer() override
    {
        // Refuse further work and wait for anything already scheduled. The
        // flag is set at scheduling time, not when the work starts, so this
        // also covers a checkpoint that has been handed over but not yet run.
        {
            std::unique_lock lock(mutex_);
            draining_ = true;
            idle_.wait(lock, [this] { return !running_; });
        }

        // Nothing is in flight and nothing new can start, so the hook is the
        // last way in. Uninstalling it needs the connection, which is why the
        // session has to outlive us.
        if (auto const s = session_.lock())
        {
            if (auto* const conn = detail::getConnection(*s))
                sqlite_api::sqlite3_wal_hook(conn, nullptr, nullptr);
        }
    }

private:
    void
    checkpoint()
    {
        if (auto const s = session_.lock())
        {
            if (auto* const conn = detail::getConnection(*s))
            {
                int log = 0, ckpt = 0;
                int const ret = sqlite_api::sqlite3_wal_checkpoint_v2(
                    conn, nullptr, SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);

                auto const* const fname = sqlite_api::sqlite3_db_filename(conn, "main");
                if (ret != SQLITE_OK)
                {
                    // Losing the race against another writer is routine under
                    // a passive checkpoint. Anything else means the WAL is
                    // growing and nothing else will say so.
                    auto const jm = (ret == SQLITE_LOCKED) ? j_.trace() : j_.warn();
                    JLOG(jm) << "WAL(" << fname << "): error " << ret;
                }
                else
                {
                    JLOG(j_.trace())
                        << "WAL(" << fname << "): frames=" << log << ", written=" << ckpt;
                }
            }
        }

        // Notifying while holding the lock is deliberate: the destructor
        // cannot reacquire the mutex, and so cannot return and destroy the
        // condition variable, until this thread is done touching both.
        std::scoped_lock const lock(mutex_);
        running_ = false;
        idle_.notify_all();
    }
};

}  // namespace

std::unique_ptr<Checkpointer>
makeCheckpointer(
    std::weak_ptr<soci::session> session,
    CheckpointScheduler schedule,
    beast::Journal journal)
{
    return std::make_unique<SQLiteCheckpointer>(std::move(session), std::move(schedule), journal);
}

}  // namespace xrpl
