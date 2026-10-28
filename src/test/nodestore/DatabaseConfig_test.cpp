#include <test/jtx/CheckMessageLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/core/Config.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/rdb/DBInit.h>
#include <xrpl/rdb/DatabaseCon.h>

#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::node_store {

using namespace xrpl::test;
using namespace xrpl::test::jtx;

class DatabaseConfig_test : public beast::unit_test::Suite
{
    using Setting = std::pair<std::string_view, std::string_view>;
    using Settings = std::initializer_list<Setting>;

    /**
     * Returns a config carrying the given [sqlite] settings.
     *
     * The common pragmas are derived once per process and cached, so every
     * case has to drop the cache before building its config or it would be
     * testing whichever case ran first.
     */
    static std::unique_ptr<Config>
    freshConfig(Settings settings, std::uint32_t ledgerHistory = 0)
    {
        DatabaseCon::Setup::globalPragma.reset();

        auto p = envconfig();
        auto& sqlite = p->section("sqlite");

        for (auto const& [key, value] : settings)
            sqlite.set(std::string(key), std::string(value));

        if (ledgerHistory != 0)
            p->ledgerHistory = ledgerHistory;

        return p;
    }

    /**
     * Builds an Env that watches the log for @p watch.
     */
    Env
    watchingEnv(Settings settings, std::uint32_t ledgerHistory, std::string_view watch, bool& found)
    {
        return Env(
            *this,
            freshConfig(settings, ledgerHistory),
            std::make_unique<CheckMessageLogs>(watch, &found),
            beast::Severity::Warning);
    }

    void
    expectCommonPragma(
        Config const& config,
        std::string_view journalMode,
        std::string_view synchronous,
        std::string_view tempStore)
    {
        auto const& pragma = setupDatabaseCon(config).commonPragma;

        if (!BEAST_EXPECT(pragma.size() == 3))
            return;

        BEAST_EXPECT(pragma[0] == std::format("PRAGMA journal_mode={};", journalMode));
        BEAST_EXPECT(pragma[1] == std::format("PRAGMA synchronous={};", synchronous));
        BEAST_EXPECT(pragma[2] == std::format("PRAGMA temp_store={};", tempStore));
    }

    void
    expectTxPragma(
        Config const& config,
        std::string_view pageSize,
        std::string_view journalSizeLimit)
    {
        auto const& pragma = setupDatabaseCon(config).txPragma;

        if (!BEAST_EXPECT(pragma.size() == 4))
            return;

        BEAST_EXPECT(pragma[0] == std::format("PRAGMA page_size={};", pageSize));
        BEAST_EXPECT(pragma[1] == std::format("PRAGMA journal_size_limit={};", journalSizeLimit));
        BEAST_EXPECT(pragma[2] == "PRAGMA max_page_count=4294967294;");
        BEAST_EXPECT(pragma[3] == "PRAGMA mmap_size=17179869184;");
    }

    /**
     * Expects startup to throw, having first logged @p expected.
     */
    void
    expectStartupFailure(Settings settings, char const* expected)
    {
        bool found = false;

        try
        {
            Env const env(
                *this,
                freshConfig(settings),
                std::make_unique<CheckMessageLogs>(expected, &found),
                beast::Severity::Warning);
            fail();
        }
        catch (...)
        {
            BEAST_EXPECT(found);
        }
    }

    void
    testCommonPragma()
    {
        testcase("Common pragma");

        {
            // defaults
            Env env(*this, freshConfig({}));
            expectCommonPragma(env.app().config(), "wal", "normal", "file");
        }
        {
            // High safety level
            bool found = false;
            Env env =
                watchingEnv({{"safety_level", "high"}}, 100'000'000, kSqliteTuningWarning, found);

            BEAST_EXPECT(!found);
            expectCommonPragma(env.app().config(), "wal", "normal", "file");
        }
        {
            // Low safety level
            bool found = false;
            Env env =
                watchingEnv({{"safety_level", "low"}}, 100'000'000, kSqliteTuningWarning, found);

            BEAST_EXPECT(found);
            expectCommonPragma(env.app().config(), "memory", "off", "memory");
        }
        {
            // Override individual settings. No warning, even though higher
            // risk settings were used, because ledgerHistory is small.
            bool found = false;
            Env env = watchingEnv(
                {{"journal_mode", "off"}, {"synchronous", "extra"}, {"temp_store", "default"}},
                0,
                kSqliteTuningWarning,
                found);

            BEAST_EXPECT(!found);
            expectCommonPragma(env.app().config(), "off", "extra", "default");
        }
        {
            // The same settings with enough history to warrant the warning
            bool found = false;
            Env env = watchingEnv(
                {{"journal_mode", "off"}, {"synchronous", "extra"}, {"temp_store", "default"}},
                50'000'000,
                kSqliteTuningWarning,
                found);

            BEAST_EXPECT(found);
            expectCommonPragma(env.app().config(), "off", "extra", "default");
        }
    }

    void
    testConflictingSettings()
    {
        testcase("Conflicting settings");

        // safety_level implies all three, so naming any of them alongside it
        // is an error -- including when only one is named.
        expectStartupFailure(
            {{"safety_level", "low"},
             {"journal_mode", "off"},
             {"synchronous", "extra"},
             {"temp_store", "default"}},
            "Failed to initialize SQL databases: Configuration "
            "file may not define both "
            "\"safety_level\" and \"journal_mode\"");

        expectStartupFailure(
            {{"safety_level", "high"}, {"journal_mode", "off"}},
            "Failed to initialize SQL databases: Configuration "
            "file may not define both "
            "\"safety_level\" and \"journal_mode\"");

        expectStartupFailure(
            {{"safety_level", "low"}, {"synchronous", "extra"}},
            "Failed to initialize SQL databases: Configuration "
            "file may not define both "
            "\"safety_level\" and \"synchronous\"");

        expectStartupFailure(
            {{"safety_level", "high"}, {"temp_store", "default"}},
            "Failed to initialize SQL databases: Configuration "
            "file may not define both "
            "\"safety_level\" and \"temp_store\"");
    }

    void
    testInvalidSettings()
    {
        testcase("Invalid settings");

        expectStartupFailure(
            {{"safety_level", "slow"}},
            "Failed to initialize SQL databases: Invalid safety_level value: slow");

        expectStartupFailure(
            {{"journal_mode", "fast"}},
            "Failed to initialize SQL databases: Invalid journal_mode value: fast");

        expectStartupFailure(
            {{"synchronous", "instant"}},
            "Failed to initialize SQL databases: Invalid "
            "synchronous value: instant");

        expectStartupFailure(
            {{"temp_store", "network"}},
            "Failed to initialize SQL databases: Invalid "
            "temp_store value: network");
    }

    void
    testTxPragma()
    {
        testcase("Transaction pragma");

        {
            // defaults
            Env env(*this, freshConfig({}));
            expectTxPragma(env.app().config(), "4096", "1582080");
        }
        {
            // valid overrides
            Env env(*this, freshConfig({{"page_size", "512"}, {"journal_size_limit", "2582080"}}));
            expectTxPragma(env.app().config(), "512", "2582080");
        }

        expectStartupFailure(
            {{"page_size", "256"}}, "Invalid page_size. Must be between 512 and 65536.");

        expectStartupFailure(
            {{"page_size", "131072"}}, "Invalid page_size. Must be between 512 and 65536.");

        expectStartupFailure({{"page_size", "513"}}, "Invalid page_size. Must be a power of 2.");
    }

    void
    run() override
    {
        testCommonPragma();
        testConflictingSettings();
        testInvalidSettings();
        testTxPragma();
    }
};

BEAST_DEFINE_TESTSUITE(DatabaseConfig, nodestore, xrpl);

}  // namespace xrpl::node_store
