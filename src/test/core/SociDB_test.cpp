#include <test/jtx/TestSuite.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/rdb/SociDB.h>

#include <boost/optional/optional.hpp>  // IWYU pragma: keep

#include <soci/boost-optional.h>  // IWYU pragma: keep
#include <soci/into.h>
#include <soci/session.h>
#include <soci/use.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl {
class SociDB_test final : public TestSuite
{
private:
    static void
    setupSQLiteConfig(BasicConfig& config, std::filesystem::path const& dbPath)
    {
        config.overwrite(Sections::kSqdb, Keys::kBackend, "sqlite");
        auto const value = dbPath.string();
        if (!value.empty())
            config.legacy(Sections::kDatabasePath, value);
    }

    static void
    cleanupDatabaseDir(std::filesystem::path const& dbPath)
    {
        using namespace std::filesystem;
        if (!exists(dbPath) || !is_directory(dbPath) || !is_empty(dbPath))
            return;
        remove(dbPath);
    }

    static void
    setupDatabaseDir(std::filesystem::path const& dbPath)
    {
        using namespace std::filesystem;
        if (!exists(dbPath))
        {
            create_directory(dbPath);
            return;
        }

        if (!is_directory(dbPath))
        {
            // someone created a file where we want to put out directory
            Throw<std::runtime_error>("Cannot create directory: " + dbPath.string());
        }
    }
    static std::filesystem::path
    getDatabasePath()
    {
        return std::filesystem::current_path() / "socidb_test_databases";
    }

public:
    SociDB_test()
    {
        try
        {
            setupDatabaseDir(getDatabasePath());
        }
        catch (std::exception const&)  // NOLINT(bugprone-empty-catch)
        {
        }
    }
    ~SociDB_test() override
    {
        try
        {
            cleanupDatabaseDir(getDatabasePath());
        }
        catch (std::exception const&)  // NOLINT(bugprone-empty-catch)
        {
        }
    }
    void
    testSQLiteFileNames()
    {
        // confirm that files are given the correct extensions
        testcase("sqliteFileNames");
        BasicConfig c;
        setupSQLiteConfig(c, getDatabasePath());
        constexpr std::pair<std::string_view, std::string_view> d[] = {
            {"peerfinder", ".sqlite"},
            {"state", ".db"},
            {"random", ".db"},
            {"validators", ".sqlite"},
        };

        for (auto const& [name, ext] : d)
            BEAST_EXPECT(setupSession(c, name).ends_with(std::format("{}{}", name, ext)));
    }
    void
    testSQLiteSession()
    {
        testcase("open");
        BasicConfig c;
        setupSQLiteConfig(c, getDatabasePath());
        auto const setup = setupSession(c, "SociTestDB");
        std::vector<std::string> const stringData({"String1", "String2", "String3"});
        std::vector<int> const intData({1, 2, 3});
        auto const checkValues = [this, &stringData, &intData](soci::session& s) {
            // Check values in db
            std::vector<std::string> stringResult(20 * stringData.size());
            std::vector<int> intResult(20 * intData.size());
            s << "SELECT StringData, IntData FROM SociTestTable;", soci::into(stringResult),
                soci::into(intResult);
            BEAST_EXPECT(
                stringResult.size() == stringData.size() && intResult.size() == intData.size());

            for (auto const& [str, num] : std::views::zip(stringResult, intResult))
            {
                auto const si = std::ranges::find(stringData, str);
                auto const ii = std::ranges::find(intData, num);

                BEAST_EXPECT(si != stringData.end());
                BEAST_EXPECT(si - stringData.begin() == ii - intData.begin());
            }
        };

        {
            soci::session s;
            openSession(s, setup);
            s << "CREATE TABLE IF NOT EXISTS SociTestTable ("
                 "  Key                    INTEGER PRIMARY KEY,"
                 "  StringData             TEXT,"
                 "  IntData                INTEGER"
                 ");";

            s << "INSERT INTO SociTestTable (StringData, IntData) VALUES "
                 "(:stringData, :intData);",
                soci::use(stringData), soci::use(intData);
            checkValues(s);
        }
        {
            // Check values in db after session was closed
            soci::session s;
            openSession(s, setup);
            checkValues(s);
        }
        {
            namespace bfs = std::filesystem;
            // Remove the database
            bfs::path const dbPath(setup);
            if (bfs::is_regular_file(dbPath))
                bfs::remove(dbPath);
        }
    }

    void
    testSQLiteSelect()
    {
        testcase("select");
        BasicConfig c;
        setupSQLiteConfig(c, getDatabasePath());
        auto const setup = setupSession(c, "SociTestDB");
        std::vector<std::uint64_t> const ubid({std::numeric_limits<std::int64_t>::max(), 20, 30});
        std::vector<std::int64_t> const bid({-10, -20, -30});
        std::vector<std::uint32_t> const uid({std::numeric_limits<std::uint32_t>::max(), 2, 3});
        std::vector<std::int32_t> const id({-1, -2, -3});

        {
            soci::session s;
            openSession(s, setup);

            s << "DROP TABLE IF EXISTS STT;";

            s << "CREATE TABLE STT ("
                 "  I              INTEGER,"
                 "  UI             INTEGER UNSIGNED,"
                 "  BI             BIGINT,"
                 "  UBI            BIGINT UNSIGNED"
                 ");";

            s << "INSERT INTO STT (I, UI, BI, UBI) VALUES "
                 "(:id, :idu, :bid, :bidu);",
                soci::use(id), soci::use(uid), soci::use(bid), soci::use(ubid);

            try
            {
                std::int32_t ig = 0;
                std::uint32_t uig = 0;
                std::int64_t big = 0;
                std::uint64_t ubig = 0;
                s << "SELECT I, UI, BI, UBI from STT;", soci::into(ig), soci::into(uig),
                    soci::into(big), soci::into(ubig);
                BEAST_EXPECT(ig == id[0] && uig == uid[0] && big == bid[0] && ubig == ubid[0]);
            }
            catch (std::exception&)
            {
                fail();
            }
            try
            {
                // SOCI requires boost::optional (not std::optional) as
                // parameters.
                boost::optional<std::int32_t> ig;
                // Known bug: https://github.com/SOCI/soci/issues/926
                // boost::optional<std::uint32_t> uig;
                uint32_t uig = 0;
                boost::optional<std::int64_t> big;
                boost::optional<std::uint64_t> ubig;
                s << "SELECT I, UI, BI, UBI from STT;", soci::into(ig), soci::into(uig),
                    soci::into(big), soci::into(ubig);
                BEAST_EXPECT(*ig == id[0] && uig == uid[0] && *big == bid[0] && *ubig == ubid[0]);
            }
            catch (std::exception&)
            {
                fail();
            }
            // There are too many issues when working with soci::row and
            // boost::tuple. DO NOT USE soci row!
        }
        {
            namespace bfs = std::filesystem;
            // Remove the database
            bfs::path const dbPath(setup);
            if (bfs::is_regular_file(dbPath))
                bfs::remove(dbPath);
        }
    }

    void
    testSQLiteDeleteWithSubselect()
    {
        testcase("deleteWithSubselect");
        BasicConfig c;
        setupSQLiteConfig(c, getDatabasePath());
        auto const setup = setupSession(c, "SociTestDB");
        {
            soci::session s;
            openSession(s, setup);

            std::string_view const dbInit[] = {
                "BEGIN TRANSACTION;",

                "CREATE TABLE Ledgers ("
                "  LedgerHash CHARACTER(64) PRIMARY KEY,"
                "  LedgerSeq  BIGINT UNSIGNED"
                ");",

                "CREATE INDEX SeqLedger ON Ledgers(LedgerSeq);",
            };
            for (auto const st : dbInit)
                s << st;
            char lh[65];
            memset(lh, 'a', 64);
            lh[64] = '\0';
            int toIncIndex = 63;
            int const numRows = 16;
            std::vector<std::string> ledgerHashes;
            std::vector<int> ledgerIndexes;
            ledgerHashes.reserve(numRows);
            ledgerIndexes.reserve(numRows);
            for (int i = 0; i < numRows; ++i)
            {
                ++lh[toIncIndex];
                if (lh[toIncIndex] == 'z')
                    --toIncIndex;
                ledgerHashes.emplace_back(lh);
                ledgerIndexes.emplace_back(i);
            }
            s << "INSERT INTO Ledgers (LedgerHash, LedgerSeq) VALUES "
                 "(:lh, :li);",
                soci::use(ledgerHashes), soci::use(ledgerIndexes);

            std::vector<int> ledgersLS(numRows * 2);
            s << "SELECT LedgerSeq FROM Ledgers;", soci::into(ledgersLS);
            BEAST_EXPECT(ledgersLS.size() == numRows);
        }
        namespace bfs = std::filesystem;
        // Remove the database
        bfs::path const dbPath(setup);
        if (bfs::is_regular_file(dbPath))
            bfs::remove(dbPath);
    }
    void
    run() override
    {
        testSQLiteFileNames();
        testSQLiteSession();
        testSQLiteSelect();
        testSQLiteDeleteWithSubselect();
    }
};

BEAST_DEFINE_TESTSUITE(SociDB, core, xrpl);

}  // namespace xrpl
