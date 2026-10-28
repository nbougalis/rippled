#include <xrpl/rdb/SociDB.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/rdb/detail/SqliteConnection.h>

#include <soci/sqlite3/soci-sqlite3.h>  // IWYU pragma: keep
#include <soci/version.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

// Tripwire for the soci version bump. The convert() templates in SociDB.h
// carry a legacy branch for soci < 4.1, where the sqlite3 backend does not
// implement read_from_start()/write_from_start(). On >= 4.1 that branch is
// dead code behind an #if, so no deprecation warning will ever point at it.
//
// To fix: delete the #else branches in both convert() overloads, then delete
// this block. Do not raise the threshold.
#if SOCI_VERSION >= 400100
#error "soci >= 4.1: remove the legacy blob read/write branches from convert() in SociDB.h"
#endif

namespace xrpl {

std::string
setupSession(BasicConfig const& config, std::string_view name)
{
    auto const& section = config.section(Sections::kSqdb);
    auto const backendName = get(section, Keys::kBackend, "sqlite");

    if (backendName != "sqlite")
        Throw<std::runtime_error>(std::format("Unsupported soci backend: {}", backendName));

    auto const dir = config.legacy(Sections::kDatabasePath);

    // CHECKME: the message claims both are required, but only the name is
    // checked, and it is interpolated while known to be empty. Every caller
    // passes a literal, so this is a programming error, not a config error.
    if (name.empty())
    {
        Throw<std::runtime_error>(std::format(
            "Sqlite databases must specify a dir and a name. Name: {} Dir: {}", name, dir));
    }

    // CHECKME: when the directory does not exist the name and extension are
    // dropped and the result is whatever `dir` was. For an empty dir -- what
    // standalone produces, since Config clears dataDir and never sets
    // database_path -- that yields an empty connection string, i.e. a
    // temporary database, and that is load bearing. The same branch also
    // swallows a dir that is non-empty but is not a directory (a typo, or a
    // path naming a file): the server then starts, persists nothing, and
    // says nothing.
    std::filesystem::path file(dir);
    if (std::filesystem::is_directory(file))
    {
        file /= name;
        file += name == "validators" || name == "peerfinder" ? ".sqlite" : ".db";
    }

    return file.string();
}

void
openSession(soci::session& s, std::string const& setup)
{
    s.open(soci::sqlite3, setup);
}

namespace {

/**
 * Converts a byte count to kilobytes, saturating rather than wrapping.
 *
 * The sqlite counters are signed and narrower than the result, so neither a
 * negative value nor one past the top of the range can be represented. The
 * clamp is what makes the cast below value-preserving.
 */
template <std::signed_integral T>
constexpr std::uint32_t
toKilobytes(T bytes) noexcept
{
    return checkedCast<std::uint32_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(bytes) / kilobytes(std::int64_t{1}),
        0,
        std::numeric_limits<std::uint32_t>::max()));
}

constexpr int
sqliteStatusOp(DbMemoryUse use) noexcept
{
    switch (use)
    {
        case DbMemoryUse::Cache:
            return SQLITE_DBSTATUS_CACHE_USED;
        case DbMemoryUse::SharedCache:
            return SQLITE_DBSTATUS_CACHE_USED_SHARED;
    }

    UNREACHABLE("xrpl::sqliteStatusOp : invalid DbMemoryUse");
    return SQLITE_DBSTATUS_CACHE_USED;
}

}  // namespace

std::uint32_t
getKBUsedAll()
{
    return toKilobytes(sqlite_api::sqlite3_memory_used());
}

std::uint32_t
getKBUsedDB(soci::session& s, DbMemoryUse use)
{
    // This function will have to be customized when other backends are added
    if (auto* const conn = detail::getConnection(s))
    {
        int current = 0, highWater = 0;
        sqlite_api::sqlite3_db_status(conn, sqliteStatusOp(use), &current, &highWater, 0);
        return toKilobytes(current);
    }

    Throw<std::logic_error>("No connection found.");
}

}  // namespace xrpl
