//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"
#endif

#include <xrpld/core/Config.h>
#include <xrpld/core/ConfigSections.h>
#include <xrpld/core/DatabaseCon.h>
#include <xrpld/core/SociDB.h>
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/contract.h>
#include <boost/filesystem.hpp>
#include <atomic>
#include <memory>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

static auto checkpointPageCount = 1000;

namespace detail {

std::string
getSociSqliteInit(
    std::string const& name,
    std::string const& dir,
    std::string const& ext)
{
    if (name.empty())
    {
        Throw<std::runtime_error>(
            "Sqlite databases must specify a dir and a name. Name: " + name +
            " Dir: " + dir);
    }
    boost::filesystem::path file(dir);
    if (is_directory(file))
        file /= name + ext;
    return file.string();
}

std::string
getSociInit(BasicConfig const& config, std::string const& dbName)
{
    auto const& section = config.section("sqdb");
    auto const backendName = get(section, "backend", "sqlite");

    if (backendName != "sqlite")
        Throw<std::runtime_error>("Unsupported soci backend: " + backendName);

    auto const path = config.legacy("database_path");
    auto const ext =
        dbName == "validators" || dbName == "peerfinder" ? ".sqlite" : ".db";
    return detail::getSociSqliteInit(dbName, path, ext);
}

}  // namespace detail

DBConfig::DBConfig(std::string const& dbPath) : connectionString_(dbPath)
{
}

DBConfig::DBConfig(BasicConfig const& config, std::string const& dbName)
    : DBConfig(detail::getSociInit(config, dbName))
{
}

std::string
DBConfig::connectionString() const
{
    return connectionString_;
}

void
DBConfig::open(soci::session& s) const
{
    s.open(soci::sqlite3, connectionString());
}

void
open(soci::session& s, BasicConfig const& config, std::string const& dbName)
{
    DBConfig(config, dbName).open(s);
}

void
open(
    soci::session& s,
    std::string const& beName,
    std::string const& connectionString)
{
    if (beName == "sqlite")
        s.open(soci::sqlite3, connectionString);
    else
        Throw<std::runtime_error>("Unsupported soci backend: " + beName);
}

static sqlite_api::sqlite3*
getConnection(soci::session& s)
{
    sqlite_api::sqlite3* result = nullptr;
    auto be = s.get_backend();
    if (auto b = dynamic_cast<soci::sqlite3_session_backend*>(be))
        result = b->conn_;

    if (!result)
        Throw<std::logic_error>("Didn't get a database connection.");

    return result;
}

std::uint32_t
getKBUsedAll(soci::session& s)
{
    if (!getConnection(s))
        Throw<std::logic_error>("No connection found.");
    return static_cast<size_t>(
        sqlite_api::sqlite3_memory_used() / kilobytes(1));
}

std::uint32_t
getKBUsedDB(soci::session& s)
{
    // This function will have to be customized when other backends are added
    if (auto conn = getConnection(s))
    {
        int cur = 0, hiw = 0;
        sqlite_api::sqlite3_db_status(
            conn, SQLITE_DBSTATUS_CACHE_USED, &cur, &hiw, 0);
        return cur / kilobytes(1);
    }
    Throw<std::logic_error>("");
    return 0;  // Silence compiler warning.
}

void
convert(soci::blob& from, std::vector<std::uint8_t>& to)
{
    to.resize(from.get_len());
    if (to.empty())
        return;
    from.read(0, reinterpret_cast<char*>(&to[0]), from.get_len());
}

void
convert(soci::blob& from, std::string& to)
{
    std::vector<std::uint8_t> tmp;
    convert(from, tmp);
    to.assign(tmp.begin(), tmp.end());
}

void
convert(std::vector<std::uint8_t> const& from, soci::blob& to)
{
    if (!from.empty())
        to.write(0, reinterpret_cast<char const*>(&from[0]), from.size());
    else
        to.trim(0);
}

void
convert(std::string const& from, soci::blob& to)
{
    if (!from.empty())
        to.write(0, from.data(), from.size());
    else
        to.trim(0);
}

namespace {

/** Run a thread to checkpoint the write ahead log (wal) for
    the given soci::session every 1000 pages. This is only implemented
    for sqlite databases.

    Note: According to: https://www.sqlite.org/wal.html#ckpt this
    is the default behavior of sqlite. We may be able to remove this
    class.
*/

class WALCheckpointer : public Checkpointer
{
    std::atomic<bool> running_ = false;

    std::uintptr_t const id_;

    // session is owned by the DatabaseCon parent that holds the checkpointer.
    // It is possible (tho rare) for the DatabaseCon class to be destoryed
    // before the checkpointer.
    std::weak_ptr<soci::session> session_;

    JobQueue& jobQueue_;

    beast::Journal const j_;

    class ConnInfo
    {
        /** Used to keep the session alive while `conn` is accessible. */
        std::shared_ptr<soci::session> ref;

        sqlite_api::sqlite3* conn = nullptr;

    public:
        explicit ConnInfo(std::weak_ptr<soci::session> r) : ref(r.lock())
        {
            if (ref)
                conn = getConnection(*ref);
        }

        explicit
        operator bool() const noexcept
        {
            return conn;
        }

        sqlite_api::sqlite3*
        handle() const noexcept
        {
            return conn;
        }
    };

public:
    WALCheckpointer(
        std::uintptr_t id,
        std::weak_ptr<soci::session> session,
        JobQueue& q,
        Logs& logs)
        : id_(id)
        , session_(std::move(session))
        , jobQueue_(q)
        , j_(logs.journal("WALCheckpointer"))
    {
        if (ConnInfo ci{session_})
        {
            sqlite_api::sqlite3_wal_hook(
                ci.handle(),
                [](void* cp,
                   sqlite_api::sqlite3* conn,
                   char const* name,
                   int pages) {
                    if (pages >= checkpointPageCount)
                    {
                        if (auto checkpointer = checkpointerFromId(
                                reinterpret_cast<std::uintptr_t>(cp)))
                        {
                            checkpointer->schedule();
                        }
                        else
                        {
                            sqlite_api::sqlite3_wal_hook(
                                conn, nullptr, nullptr);
                        }
                    }

                    return SQLITE_OK;
                },
                reinterpret_cast<void*>(id_));
        }
    }

    std::uintptr_t
    id() const noexcept override
    {
        return id_;
    }

    void
    schedule() override
    {
        // If another job is not already running, try to queue a job; we take
        // a weak pointer, so that if the owning DatabaseCon can be destroyed
        // we can avoid an unnecessary checkpoint.
        if (!running_.exchange(true, std::memory_order_acq_rel) &&
            !jobQueue_.addJob(jtWAL, "WAL", [wp = weak_from_this()]() {
                // There is a separate check in `checkpoint` to check for
                // connection validity. Here we only care that the object
                // still exists.
                if (auto self = wp.lock())
                    self->checkpoint();
            }))
        {
            // If the Job was not added to the JobQueue then we're not running.
            running_.store(false, std::memory_order_release);
        }
    }

    void
    checkpoint() override
    {
        if (ConnInfo ci{session_})
        {
            int log = 0;
            int ckpt = 0;

            int ret = sqlite3_wal_checkpoint_v2(
                ci.handle(), nullptr, SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);

            auto fname = sqlite3_db_filename(ci.handle(), "main");

            if (fname == nullptr)
                fname = "unknown";

            if (ret != SQLITE_OK)
            {
                auto jm = (ret == SQLITE_LOCKED) ? j_.trace() : j_.warn();
                JLOG(jm) << "WAL(" << fname << "): error " << ret;
            }
            else
            {
                JLOG(j_.trace()) << "WAL(" << fname << "): frames=" << log
                                 << ", written=" << ckpt;
            }
        }

        running_.store(false, std::memory_order_release);
    }
};

}  // namespace

std::shared_ptr<Checkpointer>
makeCheckpointer(
    std::uintptr_t id,
    std::weak_ptr<soci::session> session,
    JobQueue& queue,
    Logs& logs)
{
    return std::make_shared<WALCheckpointer>(
        id, std::move(session), queue, logs);
}

}  // namespace ripple

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
