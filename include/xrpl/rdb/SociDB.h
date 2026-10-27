#pragma once

/**
 * An embedded database wrapper with an intuitive, type-safe interface.
 *
 * This collection of classes let's you access embedded SQLite databases
 * using C++ syntax that is very similar to regular SQL.
 *
 * This module requires the @ref beast_sqlite external module.
 */

#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>

#include <soci/blob.h>
#include <soci/session.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <string>
#include <type_traits>

namespace xrpl {

class BasicConfig;

/**
 * DBConfig is used when a client wants to delay opening a soci::session after
 * parsing the config parameters. If a client want to open a session
 * immediately, use the free function "open" below.
 */
class DBConfig
{
    std::string connectionString_;
    explicit DBConfig(std::string dbPath);

public:
    DBConfig(BasicConfig const& config, std::string const& dbName);
    [[nodiscard]] std::string
    connectionString() const;
    void
    open(soci::session& s) const;
};

/**
 * Open a soci session.
 *
 * @param s Session to open.
 *
 * @param config Parameters to pick the soci backend and how to connect to that
 *               backend.
 *
 * @param dbName Name of the database. This has different meaning for different
 *               backends. Sometimes it is part of a filename (sqlite3),
 *               other times it is a database name (postgresql).
 */
void
open(soci::session& s, BasicConfig const& config, std::string const& dbName);

/**
 * Open a soci session.
 *
 * @param s Session to open.
 * @param beName Backend name.
 * @param connectionString Connection string to forward to soci::open.
 *        see the soci::open documentation for how to use this.
 *
 */
void
open(soci::session& s, std::string const& beName, std::string const& connectionString);

std::uint32_t
getKBUsedAll(soci::session& s);
std::uint32_t
getKBUsedDB(soci::session& s);

namespace detail {

template <class R>
concept ByteRange = std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
    sizeof(std::ranges::range_value_t<R>) == 1 &&
    std::is_trivially_copyable_v<std::ranges::range_value_t<R>>;

template <class C>
concept ByteBuffer = ByteRange<C> && std::ranges::output_range<C, std::ranges::range_value_t<C>> &&
    requires(C& c, std::size_t n) { c.resize(n); };

}  // namespace detail

template <detail::ByteRange R>
void
convert(R const& from, soci::blob& to)
{
    to.trim(0);

    if (auto const size = std::ranges::size(from); size != 0)
        to.write(0, reinterpret_cast<char const*>(std::ranges::data(from)), size);
}

template <detail::ByteBuffer C>
void
convert(soci::blob& from, C& to)
{
    to.resize(from.get_len());

    if (!std::ranges::empty(to))
        from.read(0, reinterpret_cast<char*>(std::ranges::data(to)), std::ranges::size(to));
}

class Checkpointer : public std::enable_shared_from_this<Checkpointer>
{
public:
    virtual std::uintptr_t
    id() const = 0;
    virtual ~Checkpointer() = default;

    virtual void
    schedule() = 0;

    virtual void
    checkpoint() = 0;
};

/**
 * Returns a new checkpointer which makes checkpoints of a
 * soci database every checkpointPageCount pages, using a job on the job queue.
 *
 * The checkpointer contains references to the session and job queue
 * and so must outlive them both.
 */
std::shared_ptr<Checkpointer>
makeCheckpointer(std::uintptr_t id, std::weak_ptr<soci::session>, JobQueue&, ServiceRegistry&);

}  // namespace xrpl
