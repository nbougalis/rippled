#pragma once

// NOLINTBEGIN(readability-identifier-naming)

#include <xrpl/json/json.h>

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>

namespace xrpl {

/**
 * The machinery behind the transaction result codes.
 *
 * What the rest of the code base uses is declared in namespace xrpl, below:
 * the named codes (tesSUCCESS, tecCLAIM, ...), TER, NotTEC,
 * TransactionResult and the free functions.
 *
 * Every type is defined in here, even those that xrpl re-exports, so that
 * the comparison operators, which are also in here, are found for all of
 * them by argument-dependent lookup, and are considered for nothing else.
 */
namespace ter {

/**
 * The integer type that holds the numeric value of a transaction result.
 */
using Value = int;

/**
 * What is known about one transaction result code: one row of a table.
 *
 * Both views refer to string literals, which live for the life of the
 * program.
 *
 * An Info can only be created at compile time, where its strings are
 * checked.  At run time one can only be copied or referred to.
 */
struct Info
{
    /**
     * The numeric value, for example 100.
     */
    Value value;

    /**
     * The token, for example "tecCLAIM".
     */
    std::string_view token;

    /**
     * The human-readable description, for example "Fee claimed. Sequence
     * used. No action."
     */
    std::string_view human;

    /**
     * Creates a row, at compile time only.
     *
     * The build stops if either string is empty, or is not null-terminated.
     * A string literal is always null-terminated; a view of part of one is
     * not.
     *
     * @param v The numeric value.
     * @param t The token.
     * @param h The human-readable description.
     */
    consteval Info(Value v, std::string_view t, std::string_view h) : value(v), token(t), human(h)
    {
        if (token.empty())
            throw "A transaction result has no token";

        if (human.empty())
            throw "A transaction result has no description";

        // Both strings must be null-terminated.
        if (token.data()[token.size()] != '\0' || human.data()[human.size()] != '\0')
            throw "A transaction result string is not null-terminated";
    }

    /**
     * Returns the numeric value of a row.
     *
     * A named free function, for the reason given at Subset.
     *
     * @param v The row.
     * @returns The numeric value.
     */
    friend constexpr Value
    TERtoInt(Info const& v) noexcept
    {
        return v.value;
    }

    /**
     * Writes the numeric value of a row to a stream.
     *
     * @param os The stream.
     * @param v The row.
     * @returns The stream.
     */
    friend std::ostream&
    operator<<(std::ostream& os, Info const& v)
    {
        return os << v.value;
    }
};

//------------------------------------------------------------------------------

/**
 * Whether [Min, Max] can be the range of a category.
 *
 * A category has more than one value.  The exception is the one that holds
 * only zero: success.
 *
 * @tparam Min The lowest value of the range.
 * @tparam Max The highest value of the range.
 */
template <Value Min, Value Max>
constexpr bool kIsCategoryRange = Min < Max || (Min == 0 && Max == 0);

/**
 * One transaction result code: a reference to its row.
 *
 * The type records the range of the code's category, which is what lets
 * conversions to TER and friends be decided at compile time.
 *
 * The only way to obtain one is from the table of its category, so a code
 * that has no row cannot exist.
 *
 * @tparam Min The lowest value that a code of the category can have.
 * @tparam Max The highest value that a code of the category can have.
 */
template <Value Min, Value Max>
    requires kIsCategoryRange<Min, Max>
class Code
{
    // Only a category creates codes.  Every category is named here because
    // a friend declaration cannot single out those with this range.
    template <Value A, Value B, std::size_t N>
        requires kIsCategoryRange<A, B>
    friend class Category;

    /**
     * The row of this code. Never null.
     */
    Info const* info_;

    /**
     * Creates the code of a row.
     *
     * @param info The row, which must be one of the category's.
     */
    constexpr explicit Code(Info const& info) noexcept : info_(&info)
    {
    }

public:
    /**
     * Returns what is known about this code.
     *
     * @returns The row of this code, which lives for the life of the
     * program.
     */
    constexpr Info const&
    info() const noexcept
    {
        return *info_;
    }

    /**
     * Returns the numeric value of a code.
     *
     * A named free function, for the reason given at Subset.
     *
     * @param v The code.
     * @returns The numeric value.
     */
    friend constexpr Value
    TERtoInt(Code v) noexcept
    {
        return v.info_->value;
    }

    /**
     * Writes the numeric value of a code to a stream.
     *
     * @param os The stream.
     * @param v The code.
     * @returns The stream.
     */
    friend std::ostream&
    operator<<(std::ostream& os, Code v)
    {
        return os << v.info_->value;
    }
};

//------------------------------------------------------------------------------

/**
 * A category of transaction result codes: every code whose value lies in
 * [Min, Max].
 *
 * This is also the table of those codes: it owns their rows, in ascending
 * order of value.  There is one object of each category, built with
 * makeCategory().
 *
 * The rows are checked when the table is built, which is at compile time: a
 * row that is out of range, has the wrong prefix, is out of order or repeats
 * an earlier row stops the build at the throw that names the problem.
 *
 * @tparam Min The lowest value that a code of this category can have.
 * @tparam Max The highest value that a code of this category can have.
 * @tparam N The number of rows.
 */
template <Value Min, Value Max, std::size_t N>
    requires kIsCategoryRange<Min, Max>
class Category
{
public:
    /**
     * The type of a code of this category.
     */
    using Code = ter::Code<Min, Max>;

    /**
     * The lowest value that a code of this category can have.
     */
    static constexpr Value min = Min;

    /**
     * The highest value that a code of this category can have.
     */
    static constexpr Value max = Max;

    /**
     * Creates the table from its rows, and checks them.
     *
     * Each row must have a value in [Min, Max], and a token that starts with
     * the prefix and is longer than it.  The values must be in strictly
     * ascending order, and no token may appear twice.
     *
     * @param prefix What every token must start with, e.g. "tec".
     * @param rows The rows, in ascending order of value.
     * @throws char const* if a row fails a check.  Every category is a
     * constant, so this stops the build; it is not a run-time exception.
     */
    consteval Category(std::string_view prefix, Info const (&rows)[N]) : rows_(std::to_array(rows))
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            auto const& row = rows_[i];

            // The table is direct if row i holds the value Min + i.
            direct_ = direct_ && row.value == Min + static_cast<Value>(i);

            if (!contains(row.value))
                throw "A transaction result code is outside the range of its category";

            if (row.token.size() <= prefix.size() || !row.token.starts_with(prefix))
                throw "A transaction result token does not start with the prefix of its category";

            // Ascending order is what find() relies on.  It also means that
            // no value appears twice.
            if (i != 0 && rows_[i - 1].value >= row.value)
                throw "The rows of a transaction result category are not in ascending order";

            // Sizes are compared first, as in find().
            for (std::size_t j = 0; j < i; ++j)
            {
                if (rows_[j].token.size() == row.token.size() && rows_[j].token == row.token)
                    throw "Duplicate token in a transaction result category";
            }
        }
    }

    /**
     * Not copyable: a code refers to a row of one particular table.
     */
    Category(Category const&) = delete;

    /**
     * Not assignable, for the same reason.
     */
    Category&
    operator=(Category const&) = delete;

    /**
     * Tests whether a value lies in this category's range.
     *
     * A value in the range is not necessarily the value of a code: a range
     * can have gaps.
     *
     * @param value The value to test.
     * @returns true if Min <= value <= Max.
     */
    static constexpr bool
    contains(Value value) noexcept
    {
        return Min <= value && value <= Max;
    }

    /**
     * Returns the code with the given token, at compile time only.
     *
     * Asking for a token that has no row stops the build.
     *
     * @param token The token, e.g. "tecCLAIM".
     * @returns The code.
     */
    consteval Code
    lookup(std::string_view token) const
    {
        if (auto const code = find(token))
            return *code;

        throw "No such token in this transaction result category";
    }

    /**
     * Finds the code with the given value.
     *
     * @param value The numeric value.
     * @returns The code, or empty if no row has that value.
     */
    constexpr std::optional<Code>
    find(Value value) const noexcept
    {
        if (!contains(value))
            return std::nullopt;

        if (direct_)
        {
            // The value is at least Min, so the index cannot be negative.
            if (auto const index = static_cast<std::size_t>(value - min); index < N)
                return Code{rows_[index]};
        }
        else
        {
            if (auto const row = std::ranges::lower_bound(rows_, value, {}, &Info::value);
                row != rows_.end() && row->value == value)
                return Code{*row};
        }

        return std::nullopt;
    }

    /**
     * Finds the code with the given token.
     *
     * This is a linear search.  The comparison is exact.
     *
     * @param token The token, e.g. "tecCLAIM".
     * @returns The code, or empty if no row has that token.
     */
    constexpr std::optional<Code>
    find(std::string_view token) const noexcept
    {
        for (auto const& row : rows_)
        {
            // Sizes are compared first because that is cheap to evaluate at
            // compile time, and this runs there once for every named
            // constant, in every translation unit.
            if (row.token.size() == token.size() && row.token == token)
                return Code{row};
        }
        return std::nullopt;
    }

    /**
     * Returns the rows of this category, in ascending order of value.
     *
     * @returns A view of the rows, which live for the life of the program.
     */
    constexpr std::span<Info const>
    rows() const noexcept
    {
        return rows_;
    }

private:
    /**
     * The rows of this category, in ascending order of value.
     */
    std::array<Info, N> rows_;

    /**
     * Whether row i holds the value Min + i, for every row.
     *
     * Worked out when the table is built.  It is true of a table whose
     * values start at Min and have no gaps, and lets find() use an index.
     */
    bool direct_ = true;
};

/**
 * Creates the table of a category, at compile time only.
 *
 * The number of rows is deduced, so that it is never written out.
 *
 * @tparam Min The lowest value that a code of the category can have.
 * @tparam Max The highest value that a code of the category can have.
 * @tparam N The number of rows.
 * @param prefix What every token must start with, e.g. "tec".
 * @param rows The rows, in ascending order of value.
 * @returns The table.
 */
template <Value Min, Value Max, std::size_t N>
consteval Category<Min, Max, N>
makeCategory(std::string_view prefix, Info const (&rows)[N])
{
    return Category<Min, Max, N>{prefix, rows};
}

}  // namespace ter

/**
 * Defines the named constant for one code.
 *
 * The name is also the token, so the two cannot differ, and a name with no
 * row does not build.  The reverse is not required: a row can have no named
 * constant.
 *
 * For use in this file only, which undefines it after the last table.
 *
 * @param category The table that has the row, e.g. ter::kTec.
 * @param name The name of the constant, which is also the token.
 */
#define XRPL_DEFINE_TER(category, name) inline constexpr auto name = (category).lookup(#name)

//------------------------------------------------------------------------------
namespace ter {

// clang-format off
/**
 * The tel codes: local errors.  -399 .. -300.
 *
 * For example: the transaction fee is inadequate, or a local limit is
 * exceeded.  Only valid during non-consensus processing.
 *
 * Implications:
 * - Not forwarded
 * - No fee check
 *
 * The range is stable.  The exact numbers are used in ripple-binary-codec:
 * https://github.com/XRPLF/xrpl.js/blob/main/packages/ripple-binary-codec/src/enums/definitions.json
 *
 * Use tokens.
 */
inline constexpr auto kTel = makeCategory<-399, -300>("tel", {
    {-399, "telLOCAL_ERROR",                       "Local failure."},
    {-398, "telBAD_DOMAIN",                        "Domain too long."},
    {-397, "telBAD_PATH_COUNT",                    "Malformed: Too many paths."},
    {-396, "telBAD_PUBLIC_KEY",                    "Public key is not valid."},
    {-395, "telFAILED_PROCESSING",                 "Failed to correctly process transaction."},
    {-394, "telINSUF_FEE_P",                       "Fee insufficient."},
    {-393, "telNO_DST_PARTIAL",                    "Partial payment to create account not allowed."},
    {-392, "telCAN_NOT_QUEUE",                     "Can not queue at this time."},
    {-391, "telCAN_NOT_QUEUE_BALANCE",             "Can not queue at this time: insufficient balance to pay all queued fees."},
    {-390, "telCAN_NOT_QUEUE_BLOCKS",              "Can not queue at this time: would block later queued transaction(s)."},
    {-389, "telCAN_NOT_QUEUE_BLOCKED",             "Can not queue at this time: blocking transaction in queue."},
    {-388, "telCAN_NOT_QUEUE_FEE",                 "Can not queue at this time: fee insufficient to replace queued transaction."},
    {-387, "telCAN_NOT_QUEUE_FULL",                "Can not queue at this time: queue is full."},
    {-386, "telWRONG_NETWORK",                     "Transaction specifies a Network ID that differs from that of the local node."},
    {-385, "telREQUIRES_NETWORK_ID",               "Transactions submitted to this node/network must include a correct NetworkID field."},
    {-384, "telNETWORK_ID_MAKES_TX_NON_CANONICAL", "Transactions submitted to this node/network must NOT include a NetworkID field."},
    {-383, "telENV_RPC_FAILED",                    "Unit test RPC failure."},
});
// clang-format on

/**
 * The category of the tel codes.
 */
using TELcategory = decltype(kTel);

}  // namespace ter

/**
 * The type of a tel code, for example of telLOCAL_ERROR.
 */
using TELcodes = decltype(ter::kTel)::Code;

XRPL_DEFINE_TER(ter::kTel, telLOCAL_ERROR);
XRPL_DEFINE_TER(ter::kTel, telBAD_DOMAIN);
XRPL_DEFINE_TER(ter::kTel, telBAD_PATH_COUNT);
XRPL_DEFINE_TER(ter::kTel, telBAD_PUBLIC_KEY);
XRPL_DEFINE_TER(ter::kTel, telFAILED_PROCESSING);
XRPL_DEFINE_TER(ter::kTel, telINSUF_FEE_P);
XRPL_DEFINE_TER(ter::kTel, telNO_DST_PARTIAL);
XRPL_DEFINE_TER(ter::kTel, telCAN_NOT_QUEUE);
XRPL_DEFINE_TER(ter::kTel, telCAN_NOT_QUEUE_BALANCE);
XRPL_DEFINE_TER(ter::kTel, telCAN_NOT_QUEUE_BLOCKS);
XRPL_DEFINE_TER(ter::kTel, telCAN_NOT_QUEUE_BLOCKED);
XRPL_DEFINE_TER(ter::kTel, telCAN_NOT_QUEUE_FEE);
XRPL_DEFINE_TER(ter::kTel, telCAN_NOT_QUEUE_FULL);
XRPL_DEFINE_TER(ter::kTel, telWRONG_NETWORK);
XRPL_DEFINE_TER(ter::kTel, telREQUIRES_NETWORK_ID);
XRPL_DEFINE_TER(ter::kTel, telNETWORK_ID_MAKES_TX_NON_CANONICAL);
XRPL_DEFINE_TER(ter::kTel, telENV_RPC_FAILED);

//------------------------------------------------------------------------------
namespace ter {

// clang-format off
/**
 * The tem codes: malformed transactions.  -299 .. -200.
 *
 * For example: a bad signature.
 *
 * Causes:
 * - Transaction corrupt.
 *
 * Implications:
 * - Not applied
 * - Not forwarded
 * - Reject
 * - Cannot succeed in any imagined ledger.
 *
 * The range is stable.  Use tokens.
 */
inline constexpr auto kTem = makeCategory<-299, -200>("tem", {
    {-299, "temMALFORMED",                                   "Malformed transaction."},
    {-298, "temBAD_AMOUNT",                                  "Malformed: Bad amount."},
    {-297, "temBAD_CURRENCY",                                "Malformed: Bad currency."},
    {-296, "temBAD_EXPIRATION",                              "Malformed: Bad expiration."},
    {-295, "temBAD_FEE",                                     "Invalid fee, negative or not XRP."},
    {-294, "temBAD_ISSUER",                                  "Malformed: Bad issuer."},
    {-293, "temBAD_LIMIT",                                   "Limits must be non-negative."},
    {-292, "temBAD_OFFER",                                   "Malformed: Bad offer."},
    {-291, "temBAD_PATH",                                    "Malformed: Bad path."},
    {-290, "temBAD_PATH_LOOP",                               "Malformed: Loop in path."},
    {-289, "temBAD_REGKEY",                                  "Malformed: Regular key cannot be same as master key."},
    {-288, "temBAD_SEND_XRP_LIMIT",                          "Malformed: Limit quality is not allowed for XRP to XRP."},
    {-287, "temBAD_SEND_XRP_MAX",                            "Malformed: Send max is not allowed for XRP to XRP."},
    {-286, "temBAD_SEND_XRP_NO_DIRECT",                      "Malformed: No Ripple direct is not allowed for XRP to XRP."},
    {-285, "temBAD_SEND_XRP_PARTIAL",                        "Malformed: Partial payment is not allowed for XRP to XRP."},
    {-284, "temBAD_SEND_XRP_PATHS",                          "Malformed: Paths are not allowed for XRP to XRP."},
    {-283, "temBAD_SEQUENCE",                                "Malformed: Sequence is not in the past."},
    {-282, "temBAD_SIGNATURE",                               "Malformed: Bad signature."},
    {-281, "temBAD_SRC_ACCOUNT",                             "Malformed: Bad source account."},
    {-280, "temBAD_TRANSFER_RATE",                           "Malformed: Transfer rate must be >= 1.0 and <= 2.0"},
    {-279, "temDST_IS_SRC",                                  "Destination may not be source."},
    {-278, "temDST_NEEDED",                                  "Destination not specified."},
    {-277, "temINVALID",                                     "The transaction is ill-formed."},
    {-276, "temINVALID_FLAG",                                "The transaction has an invalid flag."},
    {-275, "temREDUNDANT",                                   "The transaction is redundant."},
    {-274, "temRIPPLE_EMPTY",                                "PathSet with no paths."},
    {-273, "temDISABLED",                                    "The transaction requires logic that is currently disabled."},
    {-272, "temBAD_SIGNER",                                  "Malformed: No signer may duplicate account or other signers."},
    {-271, "temBAD_QUORUM",                                  "Malformed: Quorum is unreachable."},
    {-270, "temBAD_WEIGHT",                                  "Malformed: Weight must be a positive value."},
    {-269, "temBAD_TICK_SIZE",                               "Malformed: Tick size out of range."},
    {-268, "temINVALID_ACCOUNT_ID",                          "Malformed: A field contains an invalid account ID."},
    {-267, "temCANNOT_PREAUTH_SELF",                         "Malformed: An account may not preauthorize itself."},
    {-266, "temINVALID_COUNT",                               "Malformed: Count field outside valid range."},
    // An internal intermediate result; should never be returned.
    {-265, "temUNCERTAIN",                                   "In process of determining result. Never returned."},
    // An internal intermediate result; should never be returned.
    {-264, "temUNKNOWN",                                     "The transaction requires logic that is not implemented yet."},
    {-263, "temSEQ_AND_TICKET",                              "Transaction contains a TicketSequence and a non-zero Sequence."},
    {-262, "temBAD_NFTOKEN_TRANSFER_FEE",                    "Malformed: The NFToken transfer fee must be between 1 and 5000, inclusive."},
    {-261, "temBAD_AMM_TOKENS",                              "Malformed: Invalid LPTokens."},
    {-260, "temXCHAIN_EQUAL_DOOR_ACCOUNTS",                  "Malformed: Bridge must have unique door accounts."},
    {-259, "temXCHAIN_BAD_PROOF",                            "Malformed: Bad cross-chain claim proof."},
    {-258, "temXCHAIN_BRIDGE_BAD_ISSUES",                    "Malformed: Bad bridge issues."},
    {-257, "temXCHAIN_BRIDGE_NONDOOR_OWNER",                 "Malformed: Bridge owner must be one of the door accounts."},
    {-256, "temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT", "Malformed: Bad min account create amount."},
    {-255, "temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT",             "Malformed: Bad reward amount."},
    {-254, "temEMPTY_DID",                                   "Malformed: No DID data provided."},
    {-253, "temARRAY_EMPTY",                                 "Malformed: Array is empty."},
    {-252, "temARRAY_TOO_LARGE",                             "Malformed: Array is too large."},
    {-251, "temBAD_TRANSFER_FEE",                            "Malformed: Transfer fee is outside valid range."},
    {-250, "temINVALID_INNER_BATCH",                         "Malformed: Invalid inner batch transaction."},
    {-249, "temBAD_MPT",                                     "Malformed: Bad MPT."},
    {-248, "temBAD_CIPHERTEXT",                              "Malformed: Invalid ciphertext."},
    {-247, "temINVALID_BYTECODE",                            "Malformed: Provided byte code is invalid."},
});
// clang-format on

/**
 * The category of the tem codes.
 */
using TEMcategory = decltype(kTem);

}  // namespace ter

/**
 * The type of a tem code, for example of temMALFORMED.
 */
using TEMcodes = decltype(ter::kTem)::Code;

XRPL_DEFINE_TER(ter::kTem, temMALFORMED);
XRPL_DEFINE_TER(ter::kTem, temBAD_AMOUNT);
XRPL_DEFINE_TER(ter::kTem, temBAD_CURRENCY);
XRPL_DEFINE_TER(ter::kTem, temBAD_EXPIRATION);
XRPL_DEFINE_TER(ter::kTem, temBAD_FEE);
XRPL_DEFINE_TER(ter::kTem, temBAD_ISSUER);
XRPL_DEFINE_TER(ter::kTem, temBAD_LIMIT);
XRPL_DEFINE_TER(ter::kTem, temBAD_OFFER);
XRPL_DEFINE_TER(ter::kTem, temBAD_PATH);
XRPL_DEFINE_TER(ter::kTem, temBAD_PATH_LOOP);
XRPL_DEFINE_TER(ter::kTem, temBAD_REGKEY);
XRPL_DEFINE_TER(ter::kTem, temBAD_SEND_XRP_LIMIT);
XRPL_DEFINE_TER(ter::kTem, temBAD_SEND_XRP_MAX);
XRPL_DEFINE_TER(ter::kTem, temBAD_SEND_XRP_NO_DIRECT);
XRPL_DEFINE_TER(ter::kTem, temBAD_SEND_XRP_PARTIAL);
XRPL_DEFINE_TER(ter::kTem, temBAD_SEND_XRP_PATHS);
XRPL_DEFINE_TER(ter::kTem, temBAD_SEQUENCE);
XRPL_DEFINE_TER(ter::kTem, temBAD_SIGNATURE);
XRPL_DEFINE_TER(ter::kTem, temBAD_SRC_ACCOUNT);
XRPL_DEFINE_TER(ter::kTem, temBAD_TRANSFER_RATE);
XRPL_DEFINE_TER(ter::kTem, temDST_IS_SRC);
XRPL_DEFINE_TER(ter::kTem, temDST_NEEDED);
XRPL_DEFINE_TER(ter::kTem, temINVALID);
XRPL_DEFINE_TER(ter::kTem, temINVALID_FLAG);
XRPL_DEFINE_TER(ter::kTem, temREDUNDANT);
XRPL_DEFINE_TER(ter::kTem, temRIPPLE_EMPTY);
XRPL_DEFINE_TER(ter::kTem, temDISABLED);
XRPL_DEFINE_TER(ter::kTem, temBAD_SIGNER);
XRPL_DEFINE_TER(ter::kTem, temBAD_QUORUM);
XRPL_DEFINE_TER(ter::kTem, temBAD_WEIGHT);
XRPL_DEFINE_TER(ter::kTem, temBAD_TICK_SIZE);
XRPL_DEFINE_TER(ter::kTem, temINVALID_ACCOUNT_ID);
XRPL_DEFINE_TER(ter::kTem, temCANNOT_PREAUTH_SELF);
XRPL_DEFINE_TER(ter::kTem, temINVALID_COUNT);
XRPL_DEFINE_TER(ter::kTem, temUNCERTAIN);
XRPL_DEFINE_TER(ter::kTem, temUNKNOWN);
XRPL_DEFINE_TER(ter::kTem, temSEQ_AND_TICKET);
XRPL_DEFINE_TER(ter::kTem, temBAD_NFTOKEN_TRANSFER_FEE);
XRPL_DEFINE_TER(ter::kTem, temBAD_AMM_TOKENS);
XRPL_DEFINE_TER(ter::kTem, temXCHAIN_EQUAL_DOOR_ACCOUNTS);
XRPL_DEFINE_TER(ter::kTem, temXCHAIN_BAD_PROOF);
XRPL_DEFINE_TER(ter::kTem, temXCHAIN_BRIDGE_BAD_ISSUES);
XRPL_DEFINE_TER(ter::kTem, temXCHAIN_BRIDGE_NONDOOR_OWNER);
XRPL_DEFINE_TER(ter::kTem, temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT);
XRPL_DEFINE_TER(ter::kTem, temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT);
XRPL_DEFINE_TER(ter::kTem, temEMPTY_DID);
XRPL_DEFINE_TER(ter::kTem, temARRAY_EMPTY);
XRPL_DEFINE_TER(ter::kTem, temARRAY_TOO_LARGE);
XRPL_DEFINE_TER(ter::kTem, temBAD_TRANSFER_FEE);
XRPL_DEFINE_TER(ter::kTem, temINVALID_INNER_BATCH);
XRPL_DEFINE_TER(ter::kTem, temBAD_MPT);
XRPL_DEFINE_TER(ter::kTem, temBAD_CIPHERTEXT);
XRPL_DEFINE_TER(ter::kTem, temINVALID_BYTECODE);

//------------------------------------------------------------------------------
namespace ter {

// clang-format off
/**
 * The tef codes: failures.  -199 .. -100.
 *
 * For example: the sequence number was previously used.
 *
 * Causes:
 * - Transaction cannot succeed because of ledger state.
 * - Unexpected ledger state.
 * - C++ exception.
 *
 * Implications:
 * - Not applied
 * - Not forwarded
 * - Could succeed in an imagined ledger.
 *
 * The range is stable.  Use tokens.
 */
inline constexpr auto kTef = makeCategory<-199, -100>("tef", {
    {-199, "tefFAILURE",                     "Failed to apply."},
    {-198, "tefALREADY",                     "The exact transaction was already in this ledger."},
    {-197, "tefBAD_ADD_AUTH",                "Not authorized to add account."},
    {-196, "tefBAD_AUTH",                    "Transaction's public key is not authorized."},
    {-195, "tefBAD_LEDGER",                  "Ledger in unexpected state."},
    {-194, "tefCREATED",                     "Can't add an already created account."},
    {-193, "tefEXCEPTION",                   "Unexpected program state."},
    {-192, "tefINTERNAL",                    "Internal error."},
    // Can't set auth if auth is not required.
    {-191, "tefNO_AUTH_REQUIRED",            "Auth is not required."},
    {-190, "tefPAST_SEQ",                    "This sequence number has already passed."},
    {-189, "tefWRONG_PRIOR",                 "This previous transaction does not match."},
    {-188, "tefMASTER_DISABLED",             "Master key is disabled."},
    {-187, "tefMAX_LEDGER",                  "Ledger sequence too high."},
    {-186, "tefBAD_SIGNATURE",               "A signature is provided for a non-signer."},
    {-185, "tefBAD_QUORUM",                  "Signatures provided do not meet the quorum."},
    {-184, "tefNOT_MULTI_SIGNING",           "Account has no appropriate list of multi-signers."},
    {-183, "tefBAD_AUTH_MASTER",             "Auth for unclaimed account needs correct master key."},
    {-182, "tefINVARIANT_FAILED",            "Fee claim violated invariants for the transaction."},
    {-181, "tefTOO_BIG",                     "Transaction affects too many items."},
    {-180, "tefNO_TICKET",                   "Ticket is not in ledger."},
    {-179, "tefNFTOKEN_IS_NOT_TRANSFERABLE", "The specified NFToken is not transferable."},
    {-178, "tefINVALID_LEDGER_FIX_TYPE",     "The LedgerFixType field has an invalid value."},
    {-177, "tefNO_DST_PARTIAL",              "Partial payment to create account not allowed."},
    {-176, "tefBAD_PATH_COUNT",              "Malformed: Too many paths."},
    {-175, "tefNO_BYTECODE",                 "There is no WASM code to run, but a WASM-specific field was included."},
    {-174, "tefBYTECODE_NOT_INCLUDED",       "WASM code requires a field that was not included."},
    {-173, "tefTEMP_DISABLED",               "The transaction requires logic that is currently temporarily disabled."},
    {-172, "tefEXCEEDS_LIMIT",               "The transaction exceeds a limit set by fee voting."},
});
// clang-format on

/**
 * The category of the tef codes.
 */
using TEFcategory = decltype(kTef);

}  // namespace ter

/**
 * The type of a tef code, for example of tefFAILURE.
 */
using TEFcodes = decltype(ter::kTef)::Code;

XRPL_DEFINE_TER(ter::kTef, tefFAILURE);
XRPL_DEFINE_TER(ter::kTef, tefALREADY);
XRPL_DEFINE_TER(ter::kTef, tefBAD_ADD_AUTH);
XRPL_DEFINE_TER(ter::kTef, tefBAD_AUTH);
XRPL_DEFINE_TER(ter::kTef, tefBAD_LEDGER);
XRPL_DEFINE_TER(ter::kTef, tefCREATED);
XRPL_DEFINE_TER(ter::kTef, tefEXCEPTION);
XRPL_DEFINE_TER(ter::kTef, tefINTERNAL);
XRPL_DEFINE_TER(ter::kTef, tefNO_AUTH_REQUIRED);
XRPL_DEFINE_TER(ter::kTef, tefPAST_SEQ);
XRPL_DEFINE_TER(ter::kTef, tefWRONG_PRIOR);
XRPL_DEFINE_TER(ter::kTef, tefMASTER_DISABLED);
XRPL_DEFINE_TER(ter::kTef, tefMAX_LEDGER);
XRPL_DEFINE_TER(ter::kTef, tefBAD_SIGNATURE);
XRPL_DEFINE_TER(ter::kTef, tefBAD_QUORUM);
XRPL_DEFINE_TER(ter::kTef, tefNOT_MULTI_SIGNING);
XRPL_DEFINE_TER(ter::kTef, tefBAD_AUTH_MASTER);
XRPL_DEFINE_TER(ter::kTef, tefINVARIANT_FAILED);
XRPL_DEFINE_TER(ter::kTef, tefTOO_BIG);
XRPL_DEFINE_TER(ter::kTef, tefNO_TICKET);
XRPL_DEFINE_TER(ter::kTef, tefNFTOKEN_IS_NOT_TRANSFERABLE);
XRPL_DEFINE_TER(ter::kTef, tefINVALID_LEDGER_FIX_TYPE);
XRPL_DEFINE_TER(ter::kTef, tefNO_DST_PARTIAL);
XRPL_DEFINE_TER(ter::kTef, tefBAD_PATH_COUNT);
XRPL_DEFINE_TER(ter::kTef, tefNO_BYTECODE);
XRPL_DEFINE_TER(ter::kTef, tefBYTECODE_NOT_INCLUDED);
XRPL_DEFINE_TER(ter::kTef, tefTEMP_DISABLED);
XRPL_DEFINE_TER(ter::kTef, tefEXCEEDS_LIMIT);

//------------------------------------------------------------------------------
namespace ter {

// clang-format off
/**
 * The ter codes: retries.  -99 .. -1.
 *
 * For example: the sequence is too high, there are no funds for the
 * transaction fee, or the originating account does not exist.
 *
 * Cause:
 * - Prior application of another, possibly non-existent, transaction could
 *   allow this transaction to succeed.
 *
 * Implications:
 * - Not applied
 * - May be forwarded
 *   - Results indicating the txn was forwarded: terQUEUED
 *   - All others are not forwarded.
 * - Might succeed later
 * - Hold
 * - Makes hole in sequence which jams transactions.
 *
 * The range is stable.  Use tokens.
 */
inline constexpr auto kTer = makeCategory<-99, -1>("ter", {
    { -99, "terRETRY",                  "Retry transaction."},
    { -98, "terFUNDS_SPENT",            "DEPRECATED."},
    // Can't pay fee, therefore don't burden network.
    { -97, "terINSUF_FEE_B",            "Account balance can't pay fee."},
    // Can't pay fee, therefore don't burden network.
    { -96, "terNO_ACCOUNT",             "The source account does not exist."},
    { -95, "terNO_AUTH",                "Not authorized to hold IOUs."},
    // Internal flag.
    { -94, "terNO_LINE",                "No such line."},
    // Can't succeed with non-zero owner count.
    { -93, "terOWNERS",                 "Non-zero owner count."},
    // Can't pay fee, no point in forwarding, so don't burden network.
    { -92, "terPRE_SEQ",                "Missing/inapplicable prior transaction."},
    { -91, "terLAST",                   "DEPRECATED."},
    { -90, "terNO_RIPPLE",              "Path does not permit rippling."},
    // Transaction is being held in TxQ until fee drops.
    { -89, "terQUEUED",                 "Held until escalated fee drops."},
    // Ticket is not yet in ledger but might be on its way.
    { -88, "terPRE_TICKET",             "Ticket is not yet in ledger."},
    { -87, "terNO_AMM",                 "AMM doesn't exist for the asset pair."},
    // Failed to allocate AccountID when trying to create a pseudo-account.
    { -86, "terADDRESS_COLLISION",      "Failed to allocate an unique account address."},
    { -85, "terNO_DELEGATE_PERMISSION", "Delegated account lacks permission to perform this transaction."},
    // MPT is locked.
    { -84, "terLOCKED",                 "Fund is locked."},
    // No permission but retry.
    { -83, "terNO_PERMISSION",          "No permission to perform requested operation."},
    // Cannot be consumed until the reservation is released.
    { -82, "terTICKET_RESERVED",        "Ticket is reserved."},
});
// clang-format on

/**
 * The category of the ter codes.
 */
using TERcategory = decltype(kTer);

}  // namespace ter

/**
 * The type of a ter code, for example of terRETRY.
 */
using TERcodes = decltype(ter::kTer)::Code;

XRPL_DEFINE_TER(ter::kTer, terRETRY);
XRPL_DEFINE_TER(ter::kTer, terFUNDS_SPENT);
XRPL_DEFINE_TER(ter::kTer, terINSUF_FEE_B);
XRPL_DEFINE_TER(ter::kTer, terNO_ACCOUNT);
XRPL_DEFINE_TER(ter::kTer, terNO_AUTH);
XRPL_DEFINE_TER(ter::kTer, terNO_LINE);
XRPL_DEFINE_TER(ter::kTer, terOWNERS);
XRPL_DEFINE_TER(ter::kTer, terPRE_SEQ);
XRPL_DEFINE_TER(ter::kTer, terLAST);
XRPL_DEFINE_TER(ter::kTer, terNO_RIPPLE);
XRPL_DEFINE_TER(ter::kTer, terQUEUED);
XRPL_DEFINE_TER(ter::kTer, terPRE_TICKET);
XRPL_DEFINE_TER(ter::kTer, terNO_AMM);
XRPL_DEFINE_TER(ter::kTer, terADDRESS_COLLISION);
XRPL_DEFINE_TER(ter::kTer, terNO_DELEGATE_PERMISSION);
XRPL_DEFINE_TER(ter::kTer, terLOCKED);
XRPL_DEFINE_TER(ter::kTer, terNO_PERMISSION);
XRPL_DEFINE_TER(ter::kTer, terTICKET_RESERVED);

//------------------------------------------------------------------------------
namespace ter {

// clang-format off
/**
 * The tes code: success.  0.
 *
 * Causes:
 * - Success.
 *
 * Implications:
 * - Applied
 * - Forwarded
 *
 * The exact number must stay stable.  This code is stored by value in
 * metadata for historic transactions.
 */
inline constexpr auto kTes = makeCategory<0, 0>("tes", {
    {   0, "tesSUCCESS", "The transaction was applied. Only final in a validated ledger."},
});
// clang-format on

/**
 * The category of the tes codes.
 */
using TEScategory = decltype(kTes);

}  // namespace ter

/**
 * The type of a tes code, for example of tesSUCCESS.
 */
using TEScodes = decltype(ter::kTes)::Code;

XRPL_DEFINE_TER(ter::kTes, tesSUCCESS);

//------------------------------------------------------------------------------
namespace ter {

// clang-format off
/**
 * The tec codes: claim fee only.  100 .. 255.
 *
 * For example: a payment with no good paths, or a payment to a non-existent
 * account.
 *
 * Causes:
 * - Success, but does not achieve optimal result.
 * - Invalid transaction or no effect, but claim fee to use the sequence
 *   number.
 *
 * Implications:
 * - Applied
 * - Forwarded
 *
 * Only allowed as a return code of appliedTransaction when `!tapRETRY`.
 * Otherwise, treated as terRETRY.
 *
 * DO NOT CHANGE THESE NUMBERS: they appear in ledger meta data, in
 * sfTransactionResult, which is an STUInt8.  That is why the range ends at
 * 255.
 *
 * Two values have no row on purpose:
 * - 153 was tecHOOK_REJECTED.  It is unused.
 * - 198 was tecNO_DELEGATE_PERMISSION.  It is reserved for backward
 *   compatibility with historical data on non-prod networks, and can be
 *   reclaimed after those networks reset.
 *
 * tecNO_ENTRY is often used interchangeably with tecOBJECT_NOT_FOUND.  While
 * there does not seem to be a clear rule which to use when, the following
 * guidance will help to keep errors consistent with the majority of (but not
 * all) transaction types:
 * - tecNO_ENTRY: cannot find the primary ledger object on which the
 *   transaction is being attempted
 * - tecOBJECT_NOT_FOUND: cannot find the additional object(s) needed to
 *   complete the transaction
 */
inline constexpr auto kTec = makeCategory<100, 255>("tec", {
    { 100, "tecCLAIM",                              "Fee claimed. Sequence used. No action."},
    { 101, "tecPATH_PARTIAL",                       "Path could not send full amount."},
    // Unused legacy code.
    { 102, "tecUNFUNDED_ADD",                       "DEPRECATED."},
    { 103, "tecUNFUNDED_OFFER",                     "Insufficient balance to fund created offer."},
    { 104, "tecUNFUNDED_PAYMENT",                   "Insufficient XRP balance to send."},
    { 105, "tecFAILED_PROCESSING",                  "Failed to correctly process transaction."},
    { 121, "tecDIR_FULL",                           "Can not add entry to full directory."},
    { 122, "tecINSUF_RESERVE_LINE",                 "Insufficient reserve to add trust line."},
    { 123, "tecINSUF_RESERVE_OFFER",                "Insufficient reserve to create offer."},
    { 124, "tecNO_DST",                             "Destination does not exist. Send XRP to create it."},
    { 125, "tecNO_DST_INSUF_XRP",                   "Destination does not exist. Too little XRP sent to create it."},
    { 126, "tecNO_LINE_INSUF_RESERVE",              "No such line. Too little reserve to create it."},
    { 127, "tecNO_LINE_REDUNDANT",                  "Can't set non-existent line to default."},
    { 128, "tecPATH_DRY",                           "Path could not send partial amount."},
    { 129, "tecUNFUNDED",                           "Not enough XRP to satisfy the reserve requirement."},
    { 130, "tecNO_ALTERNATIVE_KEY",                 "The operation would remove the ability to sign transactions with the account."},
    { 131, "tecNO_REGULAR_KEY",                     "Regular key is not set."},
    { 132, "tecOWNERS",                             "Non-zero owner count."},
    { 133, "tecNO_ISSUER",                          "Issuer account does not exist."},
    { 134, "tecNO_AUTH",                            "Not authorized to hold asset."},
    { 135, "tecNO_LINE",                            "No such line."},
    { 136, "tecINSUFF_FEE",                         "Insufficient balance to pay fee."},
    { 137, "tecFROZEN",                             "Asset is frozen."},
    { 138, "tecNO_TARGET",                          "Target account does not exist."},
    { 139, "tecNO_PERMISSION",                      "No permission to perform requested operation."},
    { 140, "tecNO_ENTRY",                           "No matching entry found."},
    { 141, "tecINSUFFICIENT_RESERVE",               "Insufficient reserve to complete requested operation."},
    { 142, "tecNEED_MASTER_KEY",                    "The operation requires the use of the Master Key."},
    { 143, "tecDST_TAG_NEEDED",                     "A destination tag is required."},
    { 144, "tecINTERNAL",                           "An internal error has occurred during processing."},
    { 145, "tecOVERSIZE",                           "Object exceeded serialization limits."},
    { 146, "tecCRYPTOCONDITION_ERROR",              "Malformed, invalid, or mismatched conditional or fulfillment."},
    { 147, "tecINVARIANT_FAILED",                   "One or more invariants for the transaction were not satisfied."},
    { 148, "tecEXPIRED",                            "Expiration time is passed."},
    { 149, "tecDUPLICATE",                          "Ledger object already exists."},
    { 150, "tecKILLED",                             "No funds transferred and no offer created."},
    { 151, "tecHAS_OBLIGATIONS",                    "The account cannot be deleted since it has obligations."},
    { 152, "tecTOO_SOON",                           "It is too early to attempt the requested operation. Please wait."},
    // 153 has no row: unused.  See the comment at the top of this table.
    { 154, "tecMAX_SEQUENCE_REACHED",               "The maximum sequence number was reached."},
    { 155, "tecNO_SUITABLE_NFTOKEN_PAGE",           "A suitable NFToken page could not be located."},
    { 156, "tecNFTOKEN_BUY_SELL_MISMATCH",          "The 'Buy' and 'Sell' NFToken offers are mismatched."},
    { 157, "tecNFTOKEN_OFFER_TYPE_MISMATCH",        "The type of NFToken offer is incorrect."},
    { 158, "tecCANT_ACCEPT_OWN_NFTOKEN_OFFER",      "An NFToken offer cannot be claimed by its owner."},
    { 159, "tecINSUFFICIENT_FUNDS",                 "Not enough funds available to complete requested transaction."},
    { 160, "tecOBJECT_NOT_FOUND",                   "A requested object could not be located."},
    { 161, "tecINSUFFICIENT_PAYMENT",               "The payment is not sufficient."},
    { 162, "tecUNFUNDED_AMM",                       "Insufficient balance to fund AMM."},
    { 163, "tecAMM_BALANCE",                        "AMM has invalid balance."},
    { 164, "tecAMM_FAILED",                         "AMM transaction failed."},
    { 165, "tecAMM_INVALID_TOKENS",                 "AMM invalid LP tokens."},
    { 166, "tecAMM_EMPTY",                          "AMM is in empty state."},
    { 167, "tecAMM_NOT_EMPTY",                      "AMM is not in empty state."},
    { 168, "tecAMM_ACCOUNT",                        "This operation is not allowed on an AMM Account."},
    { 169, "tecINCOMPLETE",                         "Some work was completed, but more submissions required to finish."},
    { 170, "tecXCHAIN_BAD_TRANSFER_ISSUE",          "Bad xchain transfer issue."},
    { 171, "tecXCHAIN_NO_CLAIM_ID",                 "No such xchain claim id."},
    { 172, "tecXCHAIN_BAD_CLAIM_ID",                "Bad xchain claim id."},
    { 173, "tecXCHAIN_CLAIM_NO_QUORUM",             "Quorum was not reached on the xchain claim."},
    { 174, "tecXCHAIN_PROOF_UNKNOWN_KEY",           "Unknown key for the xchain proof."},
    { 175, "tecXCHAIN_CREATE_ACCOUNT_NONXRP_ISSUE", "Only XRP may be used for xchain create account."},
    { 176, "tecXCHAIN_WRONG_CHAIN",                 "XChain Transaction was submitted to the wrong chain."},
    { 177, "tecXCHAIN_REWARD_MISMATCH",             "The reward amount must match the reward specified in the xchain bridge."},
    { 178, "tecXCHAIN_NO_SIGNERS_LIST",             "The account did not have a signers list."},
    { 179, "tecXCHAIN_SENDING_ACCOUNT_MISMATCH",    "The sending account did not match the expected sending account."},
    { 180, "tecXCHAIN_INSUFF_CREATE_AMOUNT",        "Insufficient amount to create an account."},
    { 181, "tecXCHAIN_ACCOUNT_CREATE_PAST",         "The account create count has already passed."},
    { 182, "tecXCHAIN_ACCOUNT_CREATE_TOO_MANY",     "There are too many pending account create transactions to submit a new one."},
    { 183, "tecXCHAIN_PAYMENT_FAILED",              "Failed to transfer funds in a xchain transaction."},
    { 184, "tecXCHAIN_SELF_COMMIT",                 "Account cannot commit funds to itself."},
    { 185, "tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR", "Bad public key account pair in an xchain transaction."},
    { 186, "tecXCHAIN_CREATE_ACCOUNT_DISABLED",     "This bridge does not support account creation."},
    { 187, "tecEMPTY_DID",                          "The DID object did not have a URI or DIDDocument field."},
    { 188, "tecINVALID_UPDATE_TIME",                "The Oracle object has invalid LastUpdateTime field."},
    { 189, "tecTOKEN_PAIR_NOT_FOUND",               "Token pair is not found in Oracle object."},
    { 190, "tecARRAY_EMPTY",                        "Array is empty."},
    { 191, "tecARRAY_TOO_LARGE",                    "Array is too large."},
    { 192, "tecLOCKED",                             "Fund is locked."},
    { 193, "tecBAD_CREDENTIALS",                    "Bad credentials."},
    { 194, "tecWRONG_ASSET",                        "Wrong asset given."},
    { 195, "tecLIMIT_EXCEEDED",                     "Limit exceeded."},
    { 196, "tecPSEUDO_ACCOUNT",                     "This operation is not allowed against a pseudo-account."},
    { 197, "tecPRECISION_LOSS",                     "The amounts used by the transaction cannot interact."},
    // 198 has no row: reserved.  See the comment at the top of this table.
    { 199, "tecBAD_PROOF",                          "Proof cannot be verified"},
    { 200, "tecNO_SPONSOR_PERMISSION",              "Sponsor has not authorized this transaction."},
    { 201, "tecOUT_OF_GAS",                         "The WASM code ran out of gas during execution."},
    { 202, "tecBYTECODE_REJECTED",                  "The custom WASM code that was run rejected your transaction."},
});
// clang-format on

/**
 * The category of the tec codes.
 */
using TECcategory = decltype(kTec);

}  // namespace ter

/**
 * The type of a tec code, for example of tecCLAIM.
 */
using TECcodes = decltype(ter::kTec)::Code;

XRPL_DEFINE_TER(ter::kTec, tecCLAIM);
XRPL_DEFINE_TER(ter::kTec, tecPATH_PARTIAL);
XRPL_DEFINE_TER(ter::kTec, tecUNFUNDED_ADD);
XRPL_DEFINE_TER(ter::kTec, tecUNFUNDED_OFFER);
XRPL_DEFINE_TER(ter::kTec, tecUNFUNDED_PAYMENT);
XRPL_DEFINE_TER(ter::kTec, tecFAILED_PROCESSING);
XRPL_DEFINE_TER(ter::kTec, tecDIR_FULL);
XRPL_DEFINE_TER(ter::kTec, tecINSUF_RESERVE_LINE);
XRPL_DEFINE_TER(ter::kTec, tecINSUF_RESERVE_OFFER);
XRPL_DEFINE_TER(ter::kTec, tecNO_DST);
XRPL_DEFINE_TER(ter::kTec, tecNO_DST_INSUF_XRP);
XRPL_DEFINE_TER(ter::kTec, tecNO_LINE_INSUF_RESERVE);
XRPL_DEFINE_TER(ter::kTec, tecNO_LINE_REDUNDANT);
XRPL_DEFINE_TER(ter::kTec, tecPATH_DRY);
XRPL_DEFINE_TER(ter::kTec, tecUNFUNDED);
XRPL_DEFINE_TER(ter::kTec, tecNO_ALTERNATIVE_KEY);
XRPL_DEFINE_TER(ter::kTec, tecNO_REGULAR_KEY);
XRPL_DEFINE_TER(ter::kTec, tecOWNERS);
XRPL_DEFINE_TER(ter::kTec, tecNO_ISSUER);
XRPL_DEFINE_TER(ter::kTec, tecNO_AUTH);
XRPL_DEFINE_TER(ter::kTec, tecNO_LINE);
XRPL_DEFINE_TER(ter::kTec, tecINSUFF_FEE);
XRPL_DEFINE_TER(ter::kTec, tecFROZEN);
XRPL_DEFINE_TER(ter::kTec, tecNO_TARGET);
XRPL_DEFINE_TER(ter::kTec, tecNO_PERMISSION);
XRPL_DEFINE_TER(ter::kTec, tecNO_ENTRY);
XRPL_DEFINE_TER(ter::kTec, tecINSUFFICIENT_RESERVE);
XRPL_DEFINE_TER(ter::kTec, tecNEED_MASTER_KEY);
XRPL_DEFINE_TER(ter::kTec, tecDST_TAG_NEEDED);
XRPL_DEFINE_TER(ter::kTec, tecINTERNAL);
XRPL_DEFINE_TER(ter::kTec, tecOVERSIZE);
XRPL_DEFINE_TER(ter::kTec, tecCRYPTOCONDITION_ERROR);
XRPL_DEFINE_TER(ter::kTec, tecINVARIANT_FAILED);
XRPL_DEFINE_TER(ter::kTec, tecEXPIRED);
XRPL_DEFINE_TER(ter::kTec, tecDUPLICATE);
XRPL_DEFINE_TER(ter::kTec, tecKILLED);
XRPL_DEFINE_TER(ter::kTec, tecHAS_OBLIGATIONS);
XRPL_DEFINE_TER(ter::kTec, tecTOO_SOON);
XRPL_DEFINE_TER(ter::kTec, tecMAX_SEQUENCE_REACHED);
XRPL_DEFINE_TER(ter::kTec, tecNO_SUITABLE_NFTOKEN_PAGE);
XRPL_DEFINE_TER(ter::kTec, tecNFTOKEN_BUY_SELL_MISMATCH);
XRPL_DEFINE_TER(ter::kTec, tecNFTOKEN_OFFER_TYPE_MISMATCH);
XRPL_DEFINE_TER(ter::kTec, tecCANT_ACCEPT_OWN_NFTOKEN_OFFER);
XRPL_DEFINE_TER(ter::kTec, tecINSUFFICIENT_FUNDS);
XRPL_DEFINE_TER(ter::kTec, tecOBJECT_NOT_FOUND);
XRPL_DEFINE_TER(ter::kTec, tecINSUFFICIENT_PAYMENT);
XRPL_DEFINE_TER(ter::kTec, tecUNFUNDED_AMM);
XRPL_DEFINE_TER(ter::kTec, tecAMM_BALANCE);
XRPL_DEFINE_TER(ter::kTec, tecAMM_FAILED);
XRPL_DEFINE_TER(ter::kTec, tecAMM_INVALID_TOKENS);
XRPL_DEFINE_TER(ter::kTec, tecAMM_EMPTY);
XRPL_DEFINE_TER(ter::kTec, tecAMM_NOT_EMPTY);
XRPL_DEFINE_TER(ter::kTec, tecAMM_ACCOUNT);
XRPL_DEFINE_TER(ter::kTec, tecINCOMPLETE);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_BAD_TRANSFER_ISSUE);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_NO_CLAIM_ID);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_BAD_CLAIM_ID);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_CLAIM_NO_QUORUM);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_PROOF_UNKNOWN_KEY);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_CREATE_ACCOUNT_NONXRP_ISSUE);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_WRONG_CHAIN);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_REWARD_MISMATCH);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_NO_SIGNERS_LIST);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_SENDING_ACCOUNT_MISMATCH);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_INSUFF_CREATE_AMOUNT);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_ACCOUNT_CREATE_PAST);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_ACCOUNT_CREATE_TOO_MANY);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_PAYMENT_FAILED);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_SELF_COMMIT);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR);
XRPL_DEFINE_TER(ter::kTec, tecXCHAIN_CREATE_ACCOUNT_DISABLED);
XRPL_DEFINE_TER(ter::kTec, tecEMPTY_DID);
XRPL_DEFINE_TER(ter::kTec, tecINVALID_UPDATE_TIME);
XRPL_DEFINE_TER(ter::kTec, tecTOKEN_PAIR_NOT_FOUND);
XRPL_DEFINE_TER(ter::kTec, tecARRAY_EMPTY);
XRPL_DEFINE_TER(ter::kTec, tecARRAY_TOO_LARGE);
XRPL_DEFINE_TER(ter::kTec, tecLOCKED);
XRPL_DEFINE_TER(ter::kTec, tecBAD_CREDENTIALS);
XRPL_DEFINE_TER(ter::kTec, tecWRONG_ASSET);
XRPL_DEFINE_TER(ter::kTec, tecLIMIT_EXCEEDED);
XRPL_DEFINE_TER(ter::kTec, tecPSEUDO_ACCOUNT);
XRPL_DEFINE_TER(ter::kTec, tecPRECISION_LOSS);
XRPL_DEFINE_TER(ter::kTec, tecBAD_PROOF);
XRPL_DEFINE_TER(ter::kTec, tecNO_SPONSOR_PERMISSION);
XRPL_DEFINE_TER(ter::kTec, tecOUT_OF_GAS);
XRPL_DEFINE_TER(ter::kTec, tecBYTECODE_REJECTED);

#undef XRPL_DEFINE_TER

//------------------------------------------------------------------------------

namespace ter {

//------------------------------------------------------------------------------

/**
 * Whether a result class with range [Lo, Hi] accepts every code of a
 * category with range [Min, Max].
 *
 * It does if the range of the category lies within that of the class.  The
 * tes category is accepted by every class.
 *
 * @tparam Lo The lowest value of the range of the class.
 * @tparam Hi The highest value of the range of the class.
 * @tparam Min The lowest value of the range of the category.
 * @tparam Max The highest value of the range of the category.
 */
template <Value Lo, Value Hi, Value Min, Value Max>
constexpr bool kAccepts =
    (Lo <= Min && Max <= Hi) || (Min == TEScategory::min && Max == TEScategory::max);

/**
 * A transaction result restricted to tesSUCCESS plus the codes in [Lo, Hi].
 *
 * A Subset always holds a known code.  It can only be made from:
 * - tesSUCCESS,
 * - any code whose category lies within [Lo, Hi], and
 * - any other Subset whose range lies within [Lo, Hi].
 *
 * Nothing else converts to it, and it cannot be made from a number; see
 * knownResult() for that.  This is what stops, for example, a function that
 * returns NotTEC from returning a tec code.
 *
 * The classes in use are TER and NotTEC.
 *
 * @tparam Lo The lowest value of the range.
 * @tparam Hi The highest value of the range.
 */
template <Value Lo, Value Hi>
    requires(Lo <= Hi)
class Subset
{
    /**
     * The numeric value.  That of tesSUCCESS unless set otherwise.
     */
    Value code_{TERtoInt(tesSUCCESS)};

public:
    /**
     * Creates a result that holds tesSUCCESS.
     */
    constexpr Subset() noexcept = default;

    /**
     * Converts from a code of an accepted category.
     *
     * @tparam Min The lowest value of the range of the code's category.
     * @tparam Max The highest value of the range of the code's category.
     * @param rhs The code.
     */
    template <Value Min, Value Max>
        requires(kAccepts<Lo, Hi, Min, Max>)
    constexpr Subset(Code<Min, Max> rhs) noexcept : code_(TERtoInt(rhs))
    {
    }

    /**
     * Converts from a narrower Subset.
     *
     * @tparam FromLo The lowest value of the range of the other class.
     * @tparam FromHi The highest value of the range of the other class.
     * @param rhs The result to convert.
     */
    template <Value FromLo, Value FromHi>
        requires((FromLo != Lo || FromHi != Hi) && Lo <= FromLo && FromHi <= Hi)
    constexpr Subset(Subset<FromLo, FromHi> rhs) noexcept : code_(TERtoInt(rhs))
    {
    }

    /**
     * Tests whether this holds a failure.
     *
     * This follows the convention of UNIX return codes and of
     * std::error_code: zero is success, so the result is true for any value
     * other than that of tesSUCCESS.
     *
     * The conversion is explicit, so it applies only where a condition is
     * expected:
     *
     * @code
     * if (auto const ter = doApply())
     *     return ter;  // doApply() failed
     * @endcode
     *
     * A tec code is a failure here, even though a transaction with that
     * result claims a fee and is included in a ledger.  See isTecClaim().
     *
     * @returns true if this does not hold tesSUCCESS.
     */
    constexpr explicit
    operator bool() const noexcept
    {
        return code_ != TERtoInt(tesSUCCESS);
    }

    /**
     * Converts to a json::Value that holds the numeric value.
     *
     * Implicit, which allows assignment to a json object without a cast.
     *
     * @returns The numeric value, as a json::Value.
     */
    operator json::Value() const
    {
        return json::Value{code_};
    }

    /**
     * Writes the numeric value of a result to a stream.
     *
     * @param os The stream.
     * @param rhs The result.
     * @returns The stream.
     */
    friend std::ostream&
    operator<<(std::ostream& os, Subset const& rhs)
    {
        return os << rhs.code_;
    }

    /**
     * Returns the numeric value of a result.
     *
     * Not a member, so that free functions of the same name can do the same
     * work for the codes, for Info and for TransactionResult.
     *
     * It's worth noting that an explicit conversion operator was considered
     * and rejected.  Consider this case, taken from Status.h:
     *
     * @code
     * class Status
     * {
     *     int code_;
     *
     * public:
     *     Status(TER ter) : code_(ter)
     *     {
     *     }
     * };
     * @endcode
     *
     * This code compiles with no errors or warnings if TER has an explicit
     * (unnamed) conversion to int.  To avoid silent conversions like these
     * we provide (only) a named conversion.
     *
     * @param v The result.
     * @returns The numeric value.
     */
    friend constexpr Value
    TERtoInt(Subset v) noexcept
    {
        return v.code_;
    }
};

//------------------------------------------------------------------------------

/**
 * The result of a transaction as a ledger records it: the value of
 * sfTransactionResult, which is an STUInt8.
 *
 * A TER is a result that this program produced, and is always a known code.
 * A TransactionResult is one that was read from a ledger, or is to be
 * written to one.  It can hold any value of the field, including one that
 * this program has no code for, such as a code that a later version added.
 *
 * It converts implicitly from tesSUCCESS and from any tec code.  It does not
 * convert to TER; see knownResult() for that.
 */
class TransactionResult
{
    /**
     * The value of the field.
     */
    std::uint8_t value_;

public:
    /**
     * Creates a result that holds tesSUCCESS.
     */
    constexpr TransactionResult() noexcept : TransactionResult(tesSUCCESS)
    {
    }

    /**
     * Creates a result from the value of the field.
     *
     * Every value is accepted, whether or not it is that of a known code.
     *
     * @param value The value of the field.
     */
    constexpr explicit TransactionResult(std::uint8_t value) noexcept : value_(value)
    {
    }

    /**
     * Converts from a code that can be stored: one whose category fits in
     * the field.  Those are tesSUCCESS and the tec codes.
     *
     * @tparam Min The lowest value of the range of the code's category.
     * @tparam Max The highest value of the range of the code's category.
     * @param rhs The code.
     */
    template <Value Min, Value Max>
        requires(0 <= Min && Max <= 255)
    constexpr TransactionResult(Code<Min, Max> rhs) noexcept
        : value_(static_cast<std::uint8_t>(TERtoInt(rhs)))
    {
    }

    /**
     * Returns the numeric value of a result.
     */
    [[nodiscard]] constexpr std::uint8_t
    value() const noexcept
    {
        return value_;
    }

    /**
     * Returns the numeric value of a result.
     *
     * A named free function, for the reason given at Subset.
     *
     * @param v The result.
     * @returns The numeric value.
     */
    friend constexpr Value
    TERtoInt(TransactionResult v) noexcept
    {
        return v.value_;
    }

    /**
     * Writes the numeric value of a result to a stream.
     *
     * @param os The stream.
     * @param v The result.
     * @returns The stream.
     */
    friend std::ostream&
    operator<<(std::ostream& os, TransactionResult v)
    {
        return os << TERtoInt(v);
    }
};

//------------------------------------------------------------------------------

/**
 * Satisfied by a type for which TERtoInt() returns a Value.
 *
 * That covers the codes, Info, every Subset and TransactionResult.  It is
 * what enables the comparison operators, below.
 *
 * @tparam T The type to test.
 */
template <typename T>
concept Comparable = requires(T const& t) {
    { TERtoInt(t) } -> std::same_as<Value>;
};

/**
 * Compares two transaction results for equality, by numeric value.
 *
 * The operands can be of any Comparable types, in any combination.
 * != is synthesized from this.
 *
 * @param lhs The left operand.
 * @param rhs The right operand.
 * @returns true if the two have the same numeric value.
 */
constexpr bool
operator==(Comparable auto const& lhs, Comparable auto const& rhs) noexcept
{
    return TERtoInt(lhs) == TERtoInt(rhs);
}

/**
 * Orders two transaction results, by numeric value.
 *
 * The operands can be of any Comparable types, in any combination.
 * <, <=, > and >= are synthesized from this.
 *
 * @param lhs The left operand.
 * @param rhs The right operand.
 * @returns How the numeric value of lhs compares to that of rhs.
 */
constexpr std::strong_ordering
operator<=>(Comparable auto const& lhs, Comparable auto const& rhs) noexcept
{
    return TERtoInt(lhs) <=> TERtoInt(rhs);
}

//------------------------------------------------------------------------------

/**
 * Finds what is known about the code with the given numeric value.
 *
 * @param value The numeric value.
 * @returns The row of the code, which lives for the life of the program, or
 * nullptr if no code has that value.
 */
[[nodiscard]] constexpr Info const*
findInfo(Value value) noexcept
{
    // The categories are tried most common first.
    if (auto const code = kTes.find(value))
        return &code->info();
    if (auto const code = kTec.find(value))
        return &code->info();
    if (auto const code = kTer.find(value))
        return &code->info();
    if (auto const code = kTef.find(value))
        return &code->info();
    if (auto const code = kTem.find(value))
        return &code->info();
    if (auto const code = kTel.find(value))
        return &code->info();

    return nullptr;
}

}  // namespace ter

//------------------------------------------------------------------------------

/**
 * A transaction result that is not a tec code: tesSUCCESS, or any tel, tem,
 * tef or ter code.
 *
 * NotTEC is useful for codes returned by preflight in transactors.
 * Preflight checks occur prior to signature checking.  If preflight returned
 * a tec code, then a malicious user could submit a transaction with a very
 * large fee and have that fee charged against an account without using that
 * account's valid signature.
 */
using NotTEC = ter::Subset<ter::TELcategory::min, ter::TERcategory::max>;

/**
 * Any transaction result that this program can produce.
 *
 * "TER" stands for "Transaction Engine Result", or "Transaction ERror".  The
 * codes are described at https://xrpl.org/transaction-results.html
 *
 * TER accepts every code, and so every other ter::Subset.
 */
using TER = ter::Subset<ter::TELcategory::min, ter::TECcategory::max>;

using ter::TransactionResult;

//------------------------------------------------------------------------------

/**
 * Tests whether a result is a tel code: a local error.
 *
 * @param x The result.
 * @returns true if the value of x lies in the tel range.
 */
[[nodiscard]] constexpr bool
isTelLocal(TER x) noexcept
{
    return ter::TELcategory::contains(TERtoInt(x));
}

/**
 * Tests whether a result is a tem code: a malformed transaction.
 *
 * @param x The result.
 * @returns true if the value of x lies in the tem range.
 */
[[nodiscard]] constexpr bool
isTemMalformed(TER x) noexcept
{
    return ter::TEMcategory::contains(TERtoInt(x));
}

/**
 * Tests whether a result is a tef code: a failure.
 *
 * @param x The result.
 * @returns true if the value of x lies in the tef range.
 */
[[nodiscard]] constexpr bool
isTefFailure(TER x) noexcept
{
    return ter::TEFcategory::contains(TERtoInt(x));
}

/**
 * Tests whether a result is a ter code: a retry.
 *
 * @param x The result.
 * @returns true if the value of x lies in the ter range.
 */
[[nodiscard]] constexpr bool
isTerRetry(TER x) noexcept
{
    return ter::TERcategory::contains(TERtoInt(x));
}

/**
 * Tests whether a result is tesSUCCESS.
 *
 * @param x The result: a TER, a NotTEC, a TransactionResult or a code.
 * @returns true if x is tesSUCCESS.
 */
[[nodiscard]] constexpr bool
isTesSuccess(ter::Comparable auto const& x) noexcept
{
    return x == tesSUCCESS;
}

/**
 * Tests whether a result is a tec code: the fee is claimed.
 *
 * Only the lower end of the tec range is tested.  For a TransactionResult
 * that means any value from that of tecCLAIM up, known or not.
 *
 * @param x The result: a TER, a NotTEC, a TransactionResult or a code.
 * @returns true if the value of x is that of tecCLAIM, or greater.
 */
[[nodiscard]] constexpr bool
isTecClaim(ter::Comparable auto const& x) noexcept
{
    return x >= tecCLAIM;
}

//------------------------------------------------------------------------------

/**
 * Finds the result with the given numeric value.
 *
 * This is the only way to go from a number to a TER.  The number can be the
 * value of a TransactionResult, by way of TERtoInt(), or one that arrived
 * from outside, such as the engine_result_code of an RPC reply.
 *
 * @param value The numeric value.
 * @returns The result, or empty if no code has that value.
 */
[[nodiscard]] constexpr std::optional<TER>
knownResult(ter::Value value) noexcept
{
    // The categories are tried most common first.
    if (auto const code = ter::kTes.find(value))
        return *code;
    if (auto const code = ter::kTec.find(value))
        return *code;
    if (auto const code = ter::kTer.find(value))
        return *code;
    if (auto const code = ter::kTef.find(value))
        return *code;
    if (auto const code = ter::kTem.find(value))
        return *code;
    if (auto const code = ter::kTel.find(value))
        return *code;

    return std::nullopt;
}

/**
 * Returns the token of a result, for example "tecCLAIM".
 *
 * @param code The result.
 * @returns The token, which is a string literal.
 */
[[nodiscard]] constexpr std::string_view
transToken(TER code) noexcept
{
    // Never null: a TER always holds a known code.
    return ter::findInfo(TERtoInt(code))->token;
}

/**
 * Returns the token of a result that was read from a ledger, if this program
 * has a code for it.
 *
 * The result refers either to a string literal or to def.  If def is a
 * temporary, do not keep the result beyond the full expression.
 *
 * @param result The result.
 * @param def What to return if no code has that value.
 * @returns The token, or def.
 */
[[nodiscard]] constexpr std::string_view
transToken(TransactionResult result, std::string_view def) noexcept
{
    if (auto const* info = ter::findInfo(TERtoInt(result)))
        return info->token;

    return def;
}

/**
 * Returns the human-readable description of a result.
 *
 * @param code The result.
 * @returns The description, which is a string literal.
 */
[[nodiscard]] constexpr std::string_view
transHuman(TER code) noexcept
{
    // Never null: a TER always holds a known code.
    return ter::findInfo(TERtoInt(code))->human;
}

/**
 * Returns the human-readable description of a result that was read from a
 * ledger, if this program has a code for it.
 *
 * The result refers either to a string literal or to def.  If def is a
 * temporary, do not keep the result beyond the full expression.
 *
 * @param result The result.
 * @param def What to return if no code has that value.
 * @returns The description, or def.
 */
[[nodiscard]] constexpr std::string_view
transHuman(TransactionResult result, std::string_view def) noexcept
{
    if (auto const* info = ter::findInfo(TERtoInt(result)))
        return info->human;

    return def;
}

/**
 * Finds the result with the given token, if it is one that a ledger can
 * record.
 *
 * Only tesSUCCESS and the tec codes can be stored in the ledger, so the
 * token of any other code, although known, gives an empty result.
 *
 * @param token The token, e.g. "tecCLAIM".
 * @returns The result, or empty if the token is not that of a storable code.
 */
[[nodiscard]] constexpr std::optional<TransactionResult>
storedResult(std::string_view token) noexcept
{
    if (auto const code = ter::kTes.find(token))
        return *code;

    if (auto const code = ter::kTec.find(token))
        return *code;

    return std::nullopt;
}

/**
 * Converts a result that this program produced to the form a ledger records,
 * if a ledger can record it.
 *
 * Only tesSUCCESS and the tec codes can be stored in the ledger.
 *
 * @param code The result.
 * @returns The result, or empty if code is neither tesSUCCESS nor a tec code.
 */
[[nodiscard]] constexpr std::optional<TransactionResult>
storedResult(TER code) noexcept
{
    if (isTesSuccess(code) || isTecClaim(code))
        return TransactionResult{static_cast<std::uint8_t>(TERtoInt(code))};

    return std::nullopt;
}

/**
 * Calls a function with what is known about each transaction result, in
 * ascending order of value.
 *
 * @param f The function to call.  It takes a ter::Info const&; what it
 * returns is ignored.
 */
constexpr void
forEachTransResult(std::invocable<ter::Info const&> auto f)
{
    for (auto const rows : {
             ter::kTel.rows(),
             ter::kTem.rows(),
             ter::kTef.rows(),
             ter::kTer.rows(),
             ter::kTes.rows(),
             ter::kTec.rows(),
         })
    {
        for (auto const& row : rows)
            f(row);
    }
}

}  // namespace xrpl

// NOLINTEND(readability-identifier-naming)
