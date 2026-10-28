#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace xrpl {

/**
 * Warning logged when lower-safety [sqlite] tuning meets a large history.
 *
 * Reducing durability is a reasonable trade on a node that can refetch what
 * it loses; it is a poor one on a node holding more than @ref
 * kSqliteTuningCutoff ledgers, where rebuilding corrupted data is the
 * expensive part.
 *
 * Named rather than written inline at the point of use so that whatever
 * matches on it — tests, log scrapers — cannot drift from what is emitted.
 * A copy of the text would keep compiling, and a negative assertion over a
 * stale copy passes without testing anything.
 *
 * @see kSqliteTuningCutoff
 */
inline constexpr std::string_view kSqliteTuningWarning =
    "reducing the data integrity guarantees from the "
    "default [sqlite] behavior is not recommended for "
    "nodes storing large amounts of history, because of the "
    "difficulty inherent in rebuilding corrupted data.";

// These pragmas are built at startup and applied to all database
// connections, unless otherwise noted.
//
// They are exposed as functions rather than as format-string constants so
// that the un-substituted template can never reach sqlite: an unrecognized
// pragma value is silently ignored, so forgetting to interpolate would
// leave the setting at its default instead of failing loudly.
[[nodiscard]] inline std::string
commonDbPragmaJournal(std::string_view journalMode)
{
    return std::format("PRAGMA journal_mode={};", journalMode);
}

[[nodiscard]] inline std::string
commonDbPragmaSync(std::string_view synchronous)
{
    return std::format("PRAGMA synchronous={};", synchronous);
}

[[nodiscard]] inline std::string
commonDbPragmaTemp(std::string_view tempStore)
{
    return std::format("PRAGMA temp_store={};", tempStore);
}

// A warning will be logged if any lower-safety sqlite tuning settings
// are used and at least this much ledger history is configured. This
// includes full history nodes. This is because such a large amount of
// data will be more difficult to recover if a rare failure occurs,
// which are more likely with some of the other available tuning settings.
inline constexpr std::uint32_t kSqliteTuningCutoff = 10'000'000;

// Ledger database holds ledgers and ledger confirmations
inline constexpr std::string_view kLgrDbName{"ledger.db"};

inline constexpr std::string_view kLgrDbInit[]{
    "BEGIN TRANSACTION;",

    "CREATE TABLE IF NOT EXISTS Ledgers ("
    "  LedgerHash      CHARACTER(64) PRIMARY KEY,"
    "  LedgerSeq       BIGINT UNSIGNED,"
    "  PrevHash        CHARACTER(64),"
    "  TotalCoins      BIGINT UNSIGNED,"
    "  ClosingTime     BIGINT UNSIGNED,"
    "  PrevClosingTime BIGINT UNSIGNED,"
    "  CloseTimeRes    BIGINT UNSIGNED,"
    "  CloseFlags      BIGINT UNSIGNED,"
    "  AccountSetHash  CHARACTER(64),"
    "  TransSetHash    CHARACTER(64)"
    ");",

    "CREATE INDEX IF NOT EXISTS SeqLedger ON Ledgers(LedgerSeq);",

    // Old table and indexes no longer needed
    "DROP TABLE IF EXISTS Validations;",

    "END TRANSACTION;",
};

// Transaction database holds transactions and public keys
inline constexpr std::string_view kTxDbName{"transaction.db"};

inline constexpr std::string_view kTxDbInit[]{
    "BEGIN TRANSACTION;",

    "CREATE TABLE IF NOT EXISTS Transactions ("
    "  TransID     CHARACTER(64) PRIMARY KEY,"
    "  TransType   CHARACTER(24),"
    "  FromAcct    CHARACTER(35),"
    "  FromSeq     BIGINT UNSIGNED,"
    "  LedgerSeq   BIGINT UNSIGNED,"
    "  Status      CHARACTER(1),"
    "  RawTxn      BLOB,"
    "  TxnMeta     BLOB"
    ");",

    "CREATE INDEX IF NOT EXISTS TxLgrIndex ON Transactions(LedgerSeq);",

    "CREATE TABLE IF NOT EXISTS AccountTransactions ("
    "  TransID     CHARACTER(64),"
    "  Account     CHARACTER(64),"
    "  LedgerSeq   BIGINT UNSIGNED,"
    "  TxnSeq      INTEGER"
    ");",

    "CREATE INDEX IF NOT EXISTS AcctTxIDIndex ON AccountTransactions(TransID);",

    "CREATE INDEX IF NOT EXISTS AcctTxIndex ON "
    "AccountTransactions(Account, LedgerSeq, TxnSeq, TransID);",

    "CREATE INDEX IF NOT EXISTS AcctLgrIndex ON "
    "AccountTransactions(LedgerSeq, Account, TransID);",

    "END TRANSACTION;",
};

inline constexpr std::string_view kWalletDbName{"wallet.db"};

inline constexpr std::string_view kWalletDbInit[]{
    "BEGIN TRANSACTION;",

    // A node's identity must be persisted, including
    // for clustering purposes. This table holds one
    // entry: the server's unique identity, but the
    // value can be overriden by specifying a node
    // identity in the config file using a [node_seed]
    // entry.
    "CREATE TABLE IF NOT EXISTS NodeIdentity ("
    "  PublicKey       CHARACTER(53),"
    "  PrivateKey      CHARACTER(52)"
    ");",

    // Peer reservations
    "CREATE TABLE IF NOT EXISTS PeerReservations ("
    "  PublicKey       CHARACTER(53) UNIQUE NOT NULL,"
    "  Description     CHARACTER(64) NOT NULL"
    ");",

    // Validator Manifests
    "CREATE TABLE IF NOT EXISTS ValidatorManifests ("
    "  RawData          BLOB NOT NULL"
    ");",

    "CREATE TABLE IF NOT EXISTS PublisherManifests ("
    "  RawData          BLOB NOT NULL"
    ");",

    "END TRANSACTION;",
};

}  // namespace xrpl
