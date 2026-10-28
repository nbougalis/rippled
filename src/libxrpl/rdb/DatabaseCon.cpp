#include <xrpl/rdb/DatabaseCon.h>

#include <xrpl/basics/contract.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/rdb/Checkpointer.h>
#include <xrpl/rdb/SociDB.h>

#include <soci/session.h>
#include <soci/statement.h>

#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl {

DatabaseCon::DatabaseCon(
    std::filesystem::path const& dbPath,
    std::span<std::string const> commonPragma,
    std::span<std::string const> pragma,
    std::span<std::string_view const> initSQL,
    beast::Journal journal)
    : session_(std::make_shared<soci::session>()), j_(journal)
{
    openSession(*session_, dbPath.string());

    auto const execute = [this](auto const& sql) {
        soci::statement st = session_->prepare << sql;
        st.execute(true);
    };

    for (auto const& p : pragma)
        execute(p);

    for (auto const& p : commonPragma)
        execute(p);

    for (auto const& sql : initSQL)
        execute(sql);
}

DatabaseCon::DatabaseCon(
    Setup const& setup,
    std::string_view dbName,
    std::span<std::string const> pragma,
    std::span<std::string_view const> initSQL,
    beast::Journal journal)
    // Use temporary files or regular DB files?
    : DatabaseCon(
          setup.standAlone && setup.startUp != StartUpType::Load &&
                  setup.startUp != StartUpType::LoadFile && setup.startUp != StartUpType::Replay
              ? ""
              : (setup.dataDir / dbName),
          setup.commonPragma,
          pragma,
          initSQL,
          journal)
{
}

DatabaseCon::DatabaseCon(
    Setup const& setup,
    std::string_view dbName,
    std::span<std::string const> pragma,
    std::span<std::string_view const> initSQL,
    CheckpointScheduler schedule,
    beast::Journal journal)
    : DatabaseCon(setup, dbName, pragma, initSQL, journal)
{
    setupCheckpointing(std::move(schedule));
}

void
DatabaseCon::setupCheckpointing(CheckpointScheduler schedule)
{
    if (!schedule)
        Throw<std::logic_error>("No checkpoint scheduler");

    checkpointer_ = makeCheckpointer(session_, std::move(schedule), j_);
}

}  // namespace xrpl
