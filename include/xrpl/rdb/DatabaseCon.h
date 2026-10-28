#pragma once

#include <xrpl/core/PerfLog.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/rdb/Checkpointer.h>
#include <xrpl/rdb/SociDB.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace soci {
class session;
}  // namespace soci

namespace xrpl {

class LockedSociSession
{
public:
    using Mutex = std::recursive_mutex;

private:
    std::shared_ptr<soci::session> session_;
    std::unique_lock<Mutex> lock_;

public:
    LockedSociSession() = delete;

    LockedSociSession(std::shared_ptr<soci::session> it, Mutex& m)
        : session_(std::move(it)), lock_(m)
    {
    }

    LockedSociSession(LockedSociSession&& rhs) noexcept = default;

    LockedSociSession(LockedSociSession const& rhs) = delete;
    LockedSociSession&
    operator=(LockedSociSession const& rhs) = delete;

    soci::session*
    get()
    {
        return session_.get();
    }
    soci::session&
    operator*()
    {
        return *session_;
    }
    soci::session*
    operator->()
    {
        return session_.get();
    }
    explicit
    operator bool() const
    {
        return bool(session_);
    }
};

class DatabaseCon
{
public:
    struct Setup
    {
        explicit Setup() = default;

        StartUpType startUp = StartUpType::Normal;
        bool standAlone = false;
        std::filesystem::path dataDir;

        // Pragmas for this database. commonPragma is a copy of globalPragma
        // for the databases that take it, and empty for those that do not.
        std::vector<std::string> commonPragma;
        std::vector<std::string> txPragma;
        std::vector<std::string> lgrPragma;

        // Built once per process and cached here, so that repeated calls to
        // setupDatabaseCon() do not re-derive them from the config.
        inline static std::unique_ptr<std::vector<std::string> const> globalPragma;
    };

    DatabaseCon(
        Setup const& setup,
        std::string_view dbName,
        std::span<std::string const> pragma,
        std::span<std::string_view const> initSQL,
        beast::Journal journal);

    // Use this constructor to setup checkpointing
    DatabaseCon(
        Setup const& setup,
        std::string_view dbName,
        std::span<std::string const> pragma,
        std::span<std::string_view const> initSQL,
        CheckpointScheduler schedule,
        beast::Journal journal);

    soci::session&
    getSession()
    {
        return *session_;
    }

    LockedSociSession
    checkoutDb()
    {
        using namespace std::chrono_literals;
        LockedSociSession session = perf::measureDurationAndLog(
            [&] { return LockedSociSession(session_, lock_); }, "checkoutDb", 10ms, j_);

        return session;
    }

private:
    void setupCheckpointing(CheckpointScheduler);

    DatabaseCon(
        std::filesystem::path const& dbPath,
        std::span<std::string const> commonPragma,
        std::span<std::string const> pragma,
        std::span<std::string_view const> initSQL,
        beast::Journal journal);

    LockedSociSession::Mutex lock_;

    // Declaration order matters: members are destroyed in reverse, so the
    // checkpointer goes first and can still reach the session to disarm
    // itself and wait for any checkpoint in flight. Do not reorder these.
    // session_ will never be null.
    std::shared_ptr<soci::session> const session_;
    std::unique_ptr<Checkpointer> checkpointer_;

    beast::Journal const j_;
};

}  // namespace xrpl
