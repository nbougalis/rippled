#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

using namespace xrpl;

namespace {

// From converts to To in every way: implicitly, explicitly and by assignment.
template <typename From, typename To>
constexpr bool kConverts = std::is_convertible_v<From, To> && std::is_constructible_v<To, From> &&
    std::is_assignable_v<To&, From const&>;

// From converts to To in no way at all.
template <typename From, typename To>
constexpr bool kNeverConverts = !std::is_convertible_v<From, To> &&
    !std::is_constructible_v<To, From> && !std::is_assignable_v<To&, From const&>;

// Every one of From converts to To.
template <typename To, typename... From>
constexpr bool kConvertsFromAll = (kConverts<From, To> && ...);

// None of From converts to To.
template <typename To, typename... From>
constexpr bool kConvertsFromNone = (kNeverConverts<From, To> && ...);

// Of From, only To itself converts to To.
template <typename To, typename... From>
constexpr bool kConvertsOnlyFromItself =
    ((std::same_as<From, To> ? kConverts<From, To> : kNeverConverts<From, To>) && ...);

// No one of T converts to any other.
template <typename... T>
constexpr bool kMutuallyUnconvertible = (kConvertsOnlyFromItself<T, T...> && ...);

// T can be stored in a ledger: it is, or converts to, a TransactionResult.
template <typename T>
concept Storable = std::convertible_to<T, TransactionResult>;

// T always has a token and a description: neither lookup takes a default.
template <typename T>
concept AlwaysHasText = requires(T t) {
    transToken(t);
    transHuman(t);
};

// T may have no token or description: both lookups take a default.
template <typename T>
concept MayHaveText = requires(T t, std::string_view def) {
    transToken(t, def);
    transHuman(t, def);
};

// Checks every comparison operator on one pair of values.
template <typename L, typename R>
void
expectComparable(L const& lhs, R const& rhs)
{
    static_assert(std::same_as<decltype(lhs == rhs), bool>);
    static_assert(std::same_as<decltype(lhs != rhs), bool>);
    static_assert(std::same_as<decltype(lhs < rhs), bool>);
    static_assert(std::same_as<decltype(lhs <= rhs), bool>);
    static_assert(std::same_as<decltype(lhs > rhs), bool>);
    static_assert(std::same_as<decltype(lhs >= rhs), bool>);

    EXPECT_EQ(lhs == rhs, TERtoInt(lhs) == TERtoInt(rhs));
    EXPECT_EQ(lhs != rhs, TERtoInt(lhs) != TERtoInt(rhs));
    EXPECT_EQ(lhs < rhs, TERtoInt(lhs) < TERtoInt(rhs));
    EXPECT_EQ(lhs <= rhs, TERtoInt(lhs) <= TERtoInt(rhs));
    EXPECT_EQ(lhs > rhs, TERtoInt(lhs) > TERtoInt(rhs));
    EXPECT_EQ(lhs >= rhs, TERtoInt(lhs) >= TERtoInt(rhs));
}

}  // namespace

// knownResult, transToken, transHuman and storedResult agree with one
// another, and a number is a result exactly when a code has that value.
TEST(TER, lookup)
{
    for (auto i = -400; i < 400; ++i)
    {
        SCOPED_TRACE(i);

        auto const known = knownResult(i);
        auto const* const info = ter::findInfo(i);

        EXPECT_EQ(known.has_value(), info != nullptr);

        if (!known || info == nullptr)
            continue;

        TER const t = *known;

        EXPECT_EQ(TERtoInt(t), i);
        EXPECT_EQ(*info, t);
        EXPECT_FALSE(info->token.empty());
        EXPECT_FALSE(info->human.empty());
        EXPECT_EQ(transToken(t), info->token);
        EXPECT_EQ(transHuman(t), info->human);

        // A result is in exactly one category.
        auto const categories = (isTelLocal(t) ? 1 : 0) + (isTemMalformed(t) ? 1 : 0) +
            (isTefFailure(t) ? 1 : 0) + (isTerRetry(t) ? 1 : 0) + (isTesSuccess(t) ? 1 : 0) +
            (isTecClaim(t) ? 1 : 0);
        EXPECT_EQ(categories, 1);

        // Only tesSUCCESS and the tec codes can be found by token.
        auto const stored = storedResult(transToken(t));
        auto const storable = isTesSuccess(t) || isTecClaim(t);
        EXPECT_EQ(stored.has_value(), storable);
        if (stored)
        {
            EXPECT_EQ(*stored, t);
            EXPECT_EQ(stored->value(), i);
        }

        // Going by the result gives the same answer as going by its token.
        EXPECT_EQ(storedResult(t), stored);
    }

    EXPECT_FALSE(storedResult(""));
    EXPECT_FALSE(storedResult("tec"));
    EXPECT_FALSE(storedResult("notAToken"));
}

// A TransactionResult holds any value of the field, and has a token exactly
// when a code has that value.
TEST(TER, transactionResult)
{
    for (auto i = 0; i <= 255; ++i)
    {
        SCOPED_TRACE(i);

        TransactionResult const r{static_cast<std::uint8_t>(i)};

        // Every value is kept as it is.
        EXPECT_EQ(TERtoInt(r), i);
        EXPECT_EQ(r.value(), i);

        if (auto const known = knownResult(TERtoInt(r)))
        {
            EXPECT_EQ(r, *known);
            EXPECT_EQ(transToken(r, "?"), transToken(*known));
            EXPECT_EQ(transHuman(r, "?"), transHuman(*known));

            // The token leads back to the same result.
            auto const stored = storedResult(transToken(*known));
            EXPECT_TRUE(stored.has_value());
            if (stored)
            {
                EXPECT_EQ(*stored, r);
            }
        }
        else
        {
            // A value with no code is given the default.
            EXPECT_EQ(transToken(r, "?"), "?");
            EXPECT_EQ(transHuman(r, "?"), "?");
        }

        // These go by value alone, whether or not it is that of a code.
        EXPECT_EQ(isTesSuccess(r), i == 0);
        EXPECT_EQ(isTecClaim(r), i >= TERtoInt(tecCLAIM));
    }

    // The two values of the tec range that have no row on purpose, and one
    // below the range.
    EXPECT_FALSE(knownResult(153));
    EXPECT_FALSE(knownResult(198));
    EXPECT_FALSE(knownResult(50));
}

// forEachTransResult visits every known code once, in ascending order.
TEST(TER, forEachTransResult)
{
    std::size_t visited = 0;
    ter::Info const* previous = nullptr;

    forEachTransResult([&](ter::Info const& info) {
        SCOPED_TRACE(info.token);

        // Each row is the one that is found for its value.
        EXPECT_EQ(ter::findInfo(info.value), &info);

        if (previous != nullptr)
        {
            EXPECT_LT(previous->value, info.value);
        }

        previous = &info;
        ++visited;
    });

    // Every known code lies within [-400, 400), and knownResult knows no
    // more of them than were visited.
    std::size_t known = 0;
    for (auto i = -400; i < 400; ++i)
        known += knownResult(i).has_value() ? 1 : 0;
    EXPECT_EQ(visited, known);
}

// Valid conversions are valid, and invalid conversions do not compile.
TEST(TER, conversion)
{
    // A code converts to no other kind of code, and nothing converts from
    // an integer.
    static_assert(
        kMutuallyUnconvertible<TELcodes, TEMcodes, TEFcodes, TERcodes, TEScodes, TECcodes>);
    static_assert(kNeverConverts<int, TELcodes> && kNeverConverts<int, TEMcodes>);
    static_assert(kNeverConverts<int, TEFcodes> && kNeverConverts<int, TERcodes>);
    static_assert(kNeverConverts<int, TEScodes> && kNeverConverts<int, TECcodes>);

    // A code does not convert to an integer either.
    static_assert(!std::is_convertible_v<TECcodes, int>);
    static_assert(!std::is_convertible_v<TER, int>);

    // NotTEC: everything except tec.
    static_assert(kConvertsFromAll<NotTEC, TELcodes, TEMcodes, TEFcodes, TERcodes, TEScodes>);
    static_assert(kConvertsFromAll<NotTEC, NotTEC>);
    static_assert(kConvertsFromNone<NotTEC, TECcodes, TER, TransactionResult, int>);

    // TER: every code, and NotTEC.  Never a number, and never a result that
    // was read from a ledger.
    static_assert(
        kConvertsFromAll<TER, TELcodes, TEMcodes, TEFcodes, TERcodes, TEScodes, TECcodes>);
    static_assert(kConvertsFromAll<TER, NotTEC, TER>);
    static_assert(kConvertsFromNone<TER, TransactionResult, int, std::uint8_t>);

    // TransactionResult: only tesSUCCESS and tec, and nothing wider.
    static_assert(kConvertsFromAll<TransactionResult, TEScodes, TECcodes, TransactionResult>);
    static_assert(kConvertsFromNone<TransactionResult, TELcodes, TEMcodes, TEFcodes, TERcodes>);
    static_assert(kConvertsFromNone<TransactionResult, NotTEC, TER>);

    // A TransactionResult is made from the value of the field, but only
    // explicitly.
    static_assert(std::is_constructible_v<TransactionResult, std::uint8_t>);
    static_assert(!std::is_convertible_v<std::uint8_t, TransactionResult>);
    static_assert(!std::is_assignable_v<TransactionResult&, std::uint8_t>);

    // Only what can be stored in the ledger has a one-byte value.
    static_assert(Storable<TEScodes> && Storable<TECcodes>);
    static_assert(Storable<TransactionResult>);
    static_assert(!Storable<TELcodes> && !Storable<TEMcodes>);
    static_assert(!Storable<TEFcodes> && !Storable<TERcodes>);
    static_assert(!Storable<NotTEC> && !Storable<TER>);

    // What this program produced always has a token and a description.  What
    // was read from a ledger may not, so a default has to be given.
    static_assert(AlwaysHasText<TER> && AlwaysHasText<NotTEC>);
    static_assert(!MayHaveText<TER> && !MayHaveText<NotTEC>);
    static_assert(MayHaveText<TransactionResult> && !AlwaysHasText<TransactionResult>);

    // A default-constructed result is tesSUCCESS.
    EXPECT_EQ(TER{}, tesSUCCESS);
    EXPECT_EQ(NotTEC{}, tesSUCCESS);
    EXPECT_EQ(TransactionResult{}, tesSUCCESS);

    // Conversion keeps the value.
    EXPECT_EQ(TERtoInt(TER{tecCLAIM}), TERtoInt(tecCLAIM));
    EXPECT_EQ(TERtoInt(TER{NotTEC{temMALFORMED}}), TERtoInt(temMALFORMED));
    EXPECT_EQ(TERtoInt(TransactionResult{tecCLAIM}), TERtoInt(tecCLAIM));
    EXPECT_EQ(TransactionResult{tecCLAIM}.value(), TERtoInt(tecCLAIM));
}

// All of the TER-related types compare with one another, by value.
TEST(TER, comparison)
{
    // An example of each kind of code, and of each kind of result.
    auto const values = std::make_tuple(
        telLOCAL_ERROR,
        temMALFORMED,
        tefFAILURE,
        terRETRY,
        tesSUCCESS,
        tecCLAIM,
        NotTEC{telLOCAL_ERROR},
        TransactionResult{tecCLAIM},
        TER{tecCLAIM});

    // Every one of them against every one of them.
    std::apply(
        [&](auto const&... lhs) {
            auto const againstAll = [&](auto const& l) {
                std::apply([&](auto const&... rhs) { (expectComparable(l, rhs), ...); }, values);
            };
            (againstAll(lhs), ...);
        },
        values);

    // The categories are ordered as their ranges are.
    EXPECT_LT(telLOCAL_ERROR, temMALFORMED);
    EXPECT_LT(temMALFORMED, tefFAILURE);
    EXPECT_LT(tefFAILURE, terRETRY);
    EXPECT_LT(terRETRY, tesSUCCESS);
    EXPECT_LT(tesSUCCESS, tecCLAIM);
}
