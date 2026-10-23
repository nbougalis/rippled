#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <random>

namespace beast {

/**
 * xoroshiro128++ generator.
 *
 * Models std::uniform_random_bit_generator. Period 2^128 - 1. All 64 output
 * bits are of equal quality, so the output can be used directly or filtered
 * through a distribution; unlike xorshift128+, the low bits are not an LFSR.
 *
 * @warning This PRNG is NOT cryptographically secure! It should not be used
 *          for the generation of keys, IVs, cookies, padding.
 *
 * Reference: https://prng.di.unimi.it/xoroshiro128plusplus.c
 */
class XorShiftEngine
{
public:
    /**
     * The unsigned integer type produced by the generator.
     */
    using result_type = std::uint64_t;  // NOLINT(readability-identifier-naming)

private:
    /**
     * Advances the state by one step.
     */
    constexpr void
    step() noexcept
    {
        auto const s0 = s_[0];
        auto const s1 = s_[1] ^ s0;
        s_[0] = std::rotl(s0, 49) ^ s1 ^ (s1 << 21);
        s_[1] = std::rotl(s1, 28);
    }

    /**
     * The 128-bit generator state. Never all-zero after construction.
     */
    std::array<result_type, 2> s_{};

public:
    /**
     * Constructs a XorShiftEngine with the given seeds.
     *
     * @param s1 The first seed value.
     * @param s2 The second seed value.
     *
     * @note Each seed is passed through splitmix64 before use, so any pair of
     *       values is acceptable, including both zero.
     */
    explicit constexpr XorShiftEngine(result_type s1, result_type s2) noexcept
    {
        // The splitmix64 sequence: (s + i * gamma).
        auto splitmix64 = [](result_type z, result_type i) noexcept {
            z += (i * 0x9e3779b97f4a7c15);

            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
            z = (z ^ (z >> 27)) * 0x94d049bb133111eb;

            return z ^ (z >> 31);
        };

        result_type cnt = 0;

        // Each word is a bijection of its seed, so to get the zero state
        // we need both seeds to hit their single preimage of zero at the
        // same time. If we hit that case, we remap it.
        do
        {
            s_[0] = splitmix64(s1, ++cnt);
            s_[1] = splitmix64(s2, ++cnt);
        } while (s_[0] == 0 && s_[1] == 0);
    }

    /**
     * Constructs a XorShiftEngine from a single 64-bit seed.
     *
     * @param s The seed value. Any value is acceptable, including zero.
     *
     * @note The seed is expanded into a 128-bit state using the procedure
     *       recommended by the reference implementation.
     */
    explicit constexpr XorShiftEngine(result_type s) noexcept : XorShiftEngine(s, s)
    {
    }

    /**
     * Default constructor, which uses a fixed seed.
     */
    constexpr XorShiftEngine() noexcept : XorShiftEngine(0x056b3a53bf965414, 0x54f7fcdd52aa7487)
    {
    }

    /**
     * Advances the generator and returns the next value.
     *
     * Each call returns a value uniformly distributed over [min(), max()] and
     * updates the internal state.
     *
     * @return The next value in the sequence.
     */
    [[nodiscard]] constexpr result_type
    operator()() noexcept
    {
        auto const result = std::rotl(s_[0] + s_[1], 17) + s_[0];
        step();
        return result;
    }

    /**
     * Advances the generator by @p n steps, discarding the output.
     *
     * Equivalent to calling operator() @p n times.
     */
    constexpr void
    discard(result_type n) noexcept
    {
        while (n--)
            step();
    }

    [[nodiscard]] static constexpr result_type
    min() noexcept
    {
        return std::numeric_limits<result_type>::min();
    }

    [[nodiscard]] static constexpr result_type
    max() noexcept
    {
        return std::numeric_limits<result_type>::max();
    }
};

static_assert(std::uniform_random_bit_generator<XorShiftEngine>);

}  // namespace beast
