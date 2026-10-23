#pragma once

#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/beast/xor_shift_engine.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <type_traits>

namespace xrpl {

static_assert(
    std::numeric_limits<beast::XorShiftEngine::result_type>::digits >= 64,
    "The XRPL default PRNG engine must return at least 64 bits.");

namespace detail {
/**
 * An integral type randInt can return, except bool and the character types.
 *
 * The signed and unsigned char types are allowed because std::uint8_t
 * and std::int8_t are aliases for them; plain char, the charN_t types
 * and wchar_t are excluded as a matter of policy: they are characters,
 * not numbers.
 */
template <class T>
concept SomeRandIntegral = std::integral<T> && !std::same_as<T, bool> && !std::same_as<T, char> &&
    !std::same_as<T, wchar_t> && !std::same_as<T, char8_t> && !std::same_as<T, char16_t> &&
    !std::same_as<T, char32_t>;

/**
 * A byte-like type that randByte can return.
 *
 * Plain char and signed char are excluded: the former denotes a character,
 * and a "byte" with a sign has no sensible meaning.
 */
template <class T>
concept SomeRandByte =
    std::same_as<T, std::byte> || std::same_as<T, std::uint8_t> || std::same_as<T, unsigned char>;
}  // namespace detail

/**
 * Return the default random engine.
 *
 * @warning This is NOT a cryptographically secure PRNG and MUST NOT
 *          be used to generate randomness used in the generation of
 *          keys, secure cookies, IVs, padding, etc.
 *
 * Each thread gets its own instance of the engine, which is seeded
 * randomly with 128 bits from std::random_device.
 */
inline beast::XorShiftEngine&
defaultPrng()
{
    thread_local beast::XorShiftEngine engine{[] {
        std::random_device rd;
        std::uniform_int_distribution<std::uint64_t> d{};
        auto const s = d(rd);
        auto const t = d(rd);
        return beast::XorShiftEngine{s, t};
    }()};

    return engine;
}

/**
 * Return a uniformly distributed random integer.
 *
 * @param min The smallest value to return. If not specified
 *            the value defaults to 0.
 * @param max The largest value to return. If not specified
 *            the value defaults to the largest value that
 *            can be represented.
 * @param engine The engine to draw randomness from. If not
 *            specified, the default engine is used.
 *
 * The result is cryptographically secure only when the engine
 * passed into the function is cryptographically secure.
 *
 * @note The range is always a closed interval, so calling
 *       randInt(-5, 15) can return any integer in the
 *       closed interval [-5, 15]; similarly, calling
 *       randInt(7) can return any integer in the closed
 *       interval [0, 7].
 */
/** @{ */
template <
    detail::SomeRandIntegral Integral,
    std::uniform_random_bit_generator Engine = beast::XorShiftEngine>
Integral
randInt(Integral min, Integral max, Engine& engine = defaultPrng())
{
    XRPL_ASSERT(max >= min, "xrpl::randInt : max not below min");

    // Instantiating std::uniform_int_distribution for byte or character types
    // is UB, so we use the widest type of the right signedness to instantiate
    // the distribution. The returned value lies in the requested range, so we
    // can safely narrow it.
    using WideIntegral =
        std::conditional_t<std::is_signed_v<Integral>, std::intmax_t, std::uintmax_t>;

    std::uniform_int_distribution<WideIntegral> d(
        safeCast<WideIntegral>(min), safeCast<WideIntegral>(max));

    return checkedCast<Integral>(d(engine));
}

template <
    detail::SomeRandIntegral Integral,
    std::uniform_random_bit_generator Engine = beast::XorShiftEngine>
Integral
randInt(Integral max, Engine& engine = defaultPrng())
{
    return randInt(Integral{0}, max, engine);
}

template <
    detail::SomeRandIntegral Integral = int,
    std::uniform_random_bit_generator Engine = beast::XorShiftEngine>
Integral
randInt(Engine& engine = defaultPrng())
{
    return randInt(std::numeric_limits<Integral>::max(), engine);
}
/** @} */

/**
 * Return a random byte.
 *
 * @param engine The engine to draw randomness from. If not
 *            specified, the default engine is used.
 */
template <
    detail::SomeRandByte Byte = std::uint8_t,
    std::uniform_random_bit_generator Engine = beast::XorShiftEngine>
Byte
randByte(Engine& engine = defaultPrng())
{
    return safeCast<Byte>(randInt<unsigned char>(engine));
}

/**
 * Return a random boolean value.
 *
 * @param engine The engine to draw randomness from. If not
 *            specified, the default engine is used.
 */
template <std::uniform_random_bit_generator Engine = beast::XorShiftEngine>
bool
randBool(Engine& engine = defaultPrng())
{
    return randInt(1, engine) == 1;
}

}  // namespace xrpl
