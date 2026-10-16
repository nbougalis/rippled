#include <xrpl/basics/FixedPool.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

namespace xrpl {

//------------------------------------------------------------------------------
// detail::FixedPoolHead
//
// The freelist's state word. These tests pin the packed layout: a fresh head
// is all-zero bits, and packing is a bijection with unpacking.

TEST(FixedPoolHeadTest, default_is_all_zero_bits)
{
    detail::FixedPoolHead const head;

    EXPECT_EQ(head.slot, 0U);
    EXPECT_EQ(head.used, 0U);
    EXPECT_EQ(head.tag, 0U);
    EXPECT_EQ(head.packed(), 0U);
}

TEST(FixedPoolHeadTest, packing_round_trips)
{
    constexpr std::array<std::uint64_t, 6> kWords = {
        0,
        1,
        0x0000'0000'0001'0000,
        0x0000'0001'0000'0000,
        0x0123'4567'89AB'CDEF,
        0xFFFF'FFFF'FFFF'FFFF,
    };

    for (auto const word : kWords)
    {
        detail::FixedPoolHead const head(word);

        EXPECT_EQ(head.slot, static_cast<std::uint16_t>(word));
        EXPECT_EQ(head.used, static_cast<std::uint16_t>(word >> 16));
        EXPECT_EQ(head.tag, static_cast<std::uint32_t>(word >> 32));
        EXPECT_EQ(head.packed(), word);
    }
}

TEST(FixedPoolHeadTest, fields_land_in_their_own_bits)
{
    detail::FixedPoolHead head;

    head.slot = 0xFFFF;
    EXPECT_EQ(head.packed(), 0x0000'0000'0000'FFFFU);

    head.slot = 0;
    head.used = 0xFFFF;
    EXPECT_EQ(head.packed(), 0x0000'0000'FFFF'0000);

    head.used = 0;
    head.tag = 0xFFFF'FFFF;
    EXPECT_EQ(head.packed(), 0xFFFF'FFFF'0000'0000);
}

TEST(FixedPoolHeadTest, max_capacity_leaves_one_slot_value_for_the_sentinel)
{
    // Slots are stored as index + 1 so that zero can mean "empty", so the
    // largest capacity is the largest slot value.
    EXPECT_EQ(detail::FixedPoolHead::kMaxCapacity, 65535U);
    static_assert(detail::FixedPoolHead::kMaxCapacity == 0xFFFF);
}

//------------------------------------------------------------------------------
// detail::FixedPoolFreelist
//
// The index-level machinery, which has no notion of chunks or types. It is
// tested over several capacities, including the edges.

template <std::size_t N>
using Capacity = std::integral_constant<std::size_t, N>;

using FreelistCapacities =
    ::testing::Types<Capacity<1>, Capacity<2>, Capacity<7>, Capacity<64>, Capacity<65535>>;

template <typename CapacityType>
class FixedPoolFreelistTest : public ::testing::Test
{
public:
    static constexpr std::size_t kCapacity = CapacityType::value;

    using Freelist = detail::FixedPoolFreelist<kCapacity>;

    /**
     * Drain the list, returning the indices in the order they came out.
     *
     * Asserts that no more than kCapacity indices come out, so that a
     * broken list cannot loop forever.
     */
    static std::vector<std::size_t>
    drain(Freelist& list)
    {
        std::vector<std::size_t> indices;

        while (auto const index = list.acquire())
        {
            indices.push_back(*index);

            if (indices.size() > kCapacity)
                break;
        }

        EXPECT_LE(indices.size(), kCapacity);

        return indices;
    }
};

TYPED_TEST_SUITE(FixedPoolFreelistTest, FreelistCapacities);

TYPED_TEST(FixedPoolFreelistTest, is_constant_initialized_and_trivially_destructible)
{
    using Freelist = typename TestFixture::Freelist;

    static_assert(std::is_trivially_destructible_v<Freelist>);
    static_assert(!std::is_copy_constructible_v<Freelist>);
    static_assert(!std::is_move_constructible_v<Freelist>);

    // A constinit object is constant-initialized by definition; this does
    // not compile otherwise.
    static constinit Freelist list;

    EXPECT_TRUE(list.acquire().has_value());
}

TYPED_TEST(FixedPoolFreelistTest, fresh_list_hands_out_every_index_in_order_then_nothing)
{
    typename TestFixture::Freelist list;

    auto const indices = TestFixture::drain(list);

    std::vector<std::size_t> expected(TestFixture::kCapacity);
    std::iota(expected.begin(), expected.end(), 0);

    EXPECT_EQ(indices, expected);
    EXPECT_FALSE(list.acquire().has_value());
}

TYPED_TEST(FixedPoolFreelistTest, released_indices_come_back_last_in_first_out)
{
    typename TestFixture::Freelist list;

    auto const first = TestFixture::drain(list);

    for (auto const index : first)
        list.release(index);

    auto const second = TestFixture::drain(list);

    std::vector<std::size_t> expected(first.rbegin(), first.rend());

    EXPECT_EQ(second, expected);
    EXPECT_FALSE(list.acquire().has_value());
}

TYPED_TEST(FixedPoolFreelistTest, released_indices_are_reused_before_fresh_ones)
{
    constexpr auto kCapacity = TestFixture::kCapacity;

    if constexpr (kCapacity >= 3)
    {
        typename TestFixture::Freelist list;

        // Take two, hand the first one back: it must be the next thing out,
        // ahead of index 2, which has never been handed out.
        auto const a = list.acquire();
        auto const b = list.acquire();
        ASSERT_TRUE(a && b);
        EXPECT_EQ(*a, 0U);
        EXPECT_EQ(*b, 1U);

        list.release(*a);

        EXPECT_EQ(list.acquire(), 0U);
        EXPECT_EQ(list.acquire(), 2U);
    }
}

TYPED_TEST(
    FixedPoolFreelistTest,
    the_whole_capacity_is_available_again_after_everything_is_released)
{
    typename TestFixture::Freelist list;

    // Churn the list a few times with a mixed release order, so that the
    // freelist and the high-water mark are both exercised, then check that
    // nothing has leaked: a full drain still yields every index once.
    for (int round = 0; round < 3; ++round)
    {
        auto indices = TestFixture::drain(list);
        ASSERT_EQ(indices.size(), TestFixture::kCapacity);

        if (round % 2 == 0)
            std::reverse(indices.begin(), indices.end());

        for (auto const index : indices)
            list.release(index);
    }

    auto indices = TestFixture::drain(list);
    ASSERT_EQ(indices.size(), TestFixture::kCapacity);

    std::sort(indices.begin(), indices.end());
    EXPECT_EQ(std::adjacent_find(indices.begin(), indices.end()), indices.end());
    EXPECT_EQ(indices.front(), 0U);
    EXPECT_EQ(indices.back(), TestFixture::kCapacity - 1);
}

TYPED_TEST(FixedPoolFreelistTest, the_highest_index_survives_a_round_trip)
{
    // For the largest capacity this stores the slot value 0xFFFF, the
    // largest the field holds; a truncation or an off-by-one in the +1
    // encoding would show up here.
    typename TestFixture::Freelist list;

    auto const indices = TestFixture::drain(list);
    ASSERT_EQ(indices.size(), TestFixture::kCapacity);

    auto const last = indices.back();
    EXPECT_EQ(last, TestFixture::kCapacity - 1);

    list.release(last);
    EXPECT_EQ(list.acquire(), last);
    EXPECT_FALSE(list.acquire().has_value());
}

TEST(FixedPoolFreelistConcurrencyTest, no_index_is_held_by_two_threads_at_once)
{
    // A small list and many threads, so that the CAS loops actually retry.
    constexpr std::size_t kCapacity = 16;
    constexpr std::size_t kIterations = 50'000;

    auto const threadCount = std::max(4U, std::thread::hardware_concurrency());

    detail::FixedPoolFreelist<kCapacity> list;

    // holders[i] is the number of threads that currently believe they hold
    // index i. It must never exceed one.
    std::array<std::atomic<int>, kCapacity> holders{};
    std::atomic<std::size_t> violations = 0;
    std::atomic<std::size_t> acquisitions = 0;

    {
        std::vector<std::jthread> threads;
        threads.reserve(threadCount);

        for (unsigned t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&] {
                for (std::size_t i = 0; i < kIterations; ++i)
                {
                    auto const index = list.acquire();

                    if (!index)
                        continue;

                    if (*index >= kCapacity || holders[*index].fetch_add(1) != 0)
                        ++violations;

                    ++acquisitions;

                    // Hold it across a little work, so that overlaps have a
                    // window to occur in.
                    for (int spin = 0; spin < 10; ++spin)
                        std::atomic_signal_fence(std::memory_order_seq_cst);

                    if (holders[*index].fetch_sub(1) != 1)
                        ++violations;

                    list.release(*index);
                }
            });
        }
    }

    EXPECT_EQ(violations, 0U);
    EXPECT_GT(acquisitions, 0U);

    for (auto const& holder : holders)
        EXPECT_EQ(holder, 0);

    // Everything was released, so the whole capacity is available again.
    std::vector<std::size_t> indices;

    while (auto const index = list.acquire())
    {
        indices.push_back(*index);

        if (indices.size() > kCapacity)
            break;
    }

    ASSERT_EQ(indices.size(), kCapacity);
    std::sort(indices.begin(), indices.end());
    EXPECT_EQ(std::adjacent_find(indices.begin(), indices.end()), indices.end());
}

//------------------------------------------------------------------------------
// FixedPool
//
// The typed wrapper: pointer arithmetic over the freelist, and the ownership
// test in deallocate().

namespace {

struct Blob
{
    std::byte bytes[48];
};

struct alignas(64) OverAligned
{
    std::byte bytes[8];
};

/**
 * A type that counts its live instances, to show that objects can really be
 * constructed into and destroyed in the chunks.
 */
struct Counted
{
    static inline int live = 0;
    int value;

    explicit Counted(int v) noexcept : value(v)
    {
        ++live;
    }

    ~Counted()
    {
        --live;
    }

    Counted(Counted const&) = delete;
    Counted&
    operator=(Counted const&) = delete;
};

}  // namespace

TEST(FixedPoolTest, is_constant_initialized_and_trivially_destructible)
{
    using Pool = FixedPool<Blob, 8>;

    static_assert(std::is_trivially_destructible_v<Pool>);
    static_assert(!std::is_copy_constructible_v<Pool>);
    static_assert(!std::is_move_constructible_v<Pool>);

    static constinit Pool pool;

    EXPECT_NE(pool.allocate(), nullptr);
}

TEST(FixedPoolTest, chunks_are_distinct_aligned_and_contiguous)
{
    constexpr std::size_t kCapacity = 8;
    FixedPool<Blob, kCapacity> pool;

    std::array<void*, kCapacity> chunks{};

    for (auto& chunk : chunks)
    {
        chunk = pool.allocate();
        ASSERT_NE(chunk, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(chunk) % alignof(Blob), 0U);
    }

    EXPECT_EQ(pool.allocate(), nullptr);

    // Handed out in order the first time around, sizeof(Blob) apart.
    for (std::size_t i = 1; i < kCapacity; ++i)
    {
        auto const distance =
            static_cast<std::byte*>(chunks[i]) - static_cast<std::byte*>(chunks[0]);
        EXPECT_EQ(distance, static_cast<std::ptrdiff_t>(i * sizeof(Blob)));
    }

    for (auto const chunk : chunks)
        EXPECT_TRUE(pool.deallocate(chunk));
}

TEST(FixedPoolTest, over_aligned_types_get_over_aligned_chunks)
{
    FixedPool<OverAligned, 4> pool;

    for (int i = 0; i < 4; ++i)
    {
        auto* const chunk = pool.allocate();
        ASSERT_NE(chunk, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(chunk) % alignof(OverAligned), 0U);
    }
}

TEST(FixedPoolTest, exhausted_pool_recovers_as_chunks_are_returned)
{
    FixedPool<Blob, 4> pool;

    std::array<void*, 4> chunks{};
    for (auto& chunk : chunks)
        chunk = pool.allocate();

    EXPECT_EQ(pool.allocate(), nullptr);

    // Return one; exactly one more allocation succeeds, and it is that one.
    ASSERT_TRUE(pool.deallocate(chunks[2]));
    EXPECT_EQ(pool.allocate(), chunks[2]);
    EXPECT_EQ(pool.allocate(), nullptr);

    // Return everything; the whole capacity is available again.
    for (auto const chunk : chunks)
        ASSERT_TRUE(pool.deallocate(chunk));

    for (int i = 0; i < 4; ++i)
        EXPECT_NE(pool.allocate(), nullptr);

    EXPECT_EQ(pool.allocate(), nullptr);
}

TEST(FixedPoolTest, deallocate_rejects_pointers_that_are_not_chunks)
{
    FixedPool<Blob, 4> pool;

    auto* const first = static_cast<std::byte*>(pool.allocate());
    ASSERT_NE(first, nullptr);

    // Not from the pool at all.
    EXPECT_FALSE(pool.deallocate(nullptr));

    Blob local{};
    EXPECT_FALSE(pool.deallocate(&local));

    auto const heap = std::make_unique<Blob>();
    EXPECT_FALSE(pool.deallocate(heap.get()));

    // Near the pool but not on a chunk boundary.
    EXPECT_FALSE(pool.deallocate(first - 1));
    EXPECT_FALSE(pool.deallocate(first + 1));
    EXPECT_FALSE(pool.deallocate(first + sizeof(Blob) - 1));
    EXPECT_FALSE(pool.deallocate(first + sizeof(Blob) + 1));

    // Below the first chunk and at or past the end of the last one.
    EXPECT_FALSE(pool.deallocate(first - sizeof(Blob)));
    EXPECT_FALSE(pool.deallocate(first + 4 * sizeof(Blob)));
    EXPECT_FALSE(pool.deallocate(first + 5 * sizeof(Blob)));

    // None of those touched the pool: the one chunk we hold is still held
    // and the other three are still available, no more and no fewer.
    for (int i = 0; i < 3; ++i)
        EXPECT_NE(pool.allocate(), nullptr);

    EXPECT_EQ(pool.allocate(), nullptr);
    EXPECT_TRUE(pool.deallocate(first));
}

TEST(FixedPoolTest, objects_can_be_constructed_in_and_destroyed_from_chunks)
{
    FixedPool<Counted, 3> pool;

    ASSERT_EQ(Counted::live, 0);

    std::vector<Counted*> objects;

    for (int i = 0; i < 3; ++i)
    {
        auto* const chunk = pool.allocate();
        ASSERT_NE(chunk, nullptr);
        objects.push_back(std::construct_at(static_cast<Counted*>(chunk), i));
    }

    EXPECT_EQ(Counted::live, 3);
    EXPECT_EQ(objects[0]->value, 0);
    EXPECT_EQ(objects[1]->value, 1);
    EXPECT_EQ(objects[2]->value, 2);

    for (auto* const object : objects)
    {
        std::destroy_at(object);
        EXPECT_TRUE(pool.deallocate(object));
    }

    EXPECT_EQ(Counted::live, 0);

    // The recycled chunks hold new objects just as well.
    auto* const again = std::construct_at(static_cast<Counted*>(pool.allocate()), 42);
    EXPECT_EQ(again->value, 42);
    EXPECT_EQ(Counted::live, 1);

    std::destroy_at(again);
    EXPECT_TRUE(pool.deallocate(again));
    EXPECT_EQ(Counted::live, 0);
}

TEST(FixedPoolTest, a_pool_of_one)
{
    FixedPool<int, 1> pool;

    auto* const only = pool.allocate();
    ASSERT_NE(only, nullptr);
    EXPECT_EQ(pool.allocate(), nullptr);

    EXPECT_TRUE(pool.deallocate(only));
    EXPECT_EQ(pool.allocate(), only);
    EXPECT_EQ(pool.allocate(), nullptr);
}

}  // namespace xrpl
