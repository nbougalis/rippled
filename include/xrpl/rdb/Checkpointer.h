#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <functional>
#include <memory>

namespace soci {
class session;
}  // namespace soci

namespace xrpl {

/**
 * Hands a unit of work to something that will run it off the calling thread.
 *
 * @return true if the work was accepted and will run; false if it will never
 *         run, in which case the checkpointer re-arms so that a later trigger
 *         can try again.
 */
using CheckpointScheduler = std::function<bool(std::function<void()>)>;

/**
 * Checkpoints the write-ahead log of a database session off the writing
 * thread, for as long as it is alive.
 *
 * There is nothing to call: a checkpointer arms itself on construction and,
 * on destruction, stops and waits for any checkpoint still in flight. The
 * owner only has to destroy it before the session it was built from, which
 * is why it must be declared after that session.
 */
class Checkpointer
{
public:
    virtual ~Checkpointer() = default;
};

/**
 * Returns a checkpointer for a soci session.
 *
 * Because sqlite internally implements wal_autocheckpoint with the same hook,
 * installing ours replaces it: the WAL is not continuously checkpointed inside
 * the committing call, but handed to @p schedule once it reaches the same page
 * count sqlite uses by default. See https://www.sqlite.org/wal.html#ckpt.
 *
 * @note Only sqlite sessions are supported; on any other backend no hook is
 *       installed and the checkpointer is inert.
 *
 * @warning The session must not yet be reachable from another thread: the hook
 *          is installed before this returns, and it refers to an object that
 *          is not fully constructed until it does.
 */
std::unique_ptr<Checkpointer>
    makeCheckpointer(std::weak_ptr<soci::session>, CheckpointScheduler, beast::Journal);

}  // namespace xrpl
