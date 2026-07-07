#ifndef RIPPLE_BASICS_KEYCACHE_H
#define RIPPLE_BASICS_KEYCACHE_H

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/insight/Insight.h>

#include <boost/unordered/unordered_flat_map.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace ripple {

template <std::size_t Bits, class Tag = void, std::size_t N = 64>
    requires(N != 1 && (N < 256) && std::has_single_bit(N))
class TaggedKeyCache
{
public:
    using key_type = base_uint<Bits, Tag>;
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

private:
    using time_point = clock_type::time_point;

    // Intra-shard bucket hash: we take a window that will be disjoint
    // from the one we use for shard selection (except when Bits == 64
    // in which case we can't do anything) to avoid correlating shards
    // and buckets.
    struct key_hash
    {
        std::size_t
        operator()(key_type const& k) const noexcept
        {
            std::size_t ret;
            std::memcpy(&ret, k.data(), sizeof(ret));
            return ret;
        }
    };

    /** The number of entries below which we do not do a parallel sweep. */
    static constexpr std::size_t sweepParallelThreshold = 10000;

    struct Shard
    {
        std::mutex mutex;

        /** The items in this shart. */
        boost::unordered_flat_map<key_type, time_point, key_hash> map;
    };

    Shard&
    map_shard(key_type const& key) noexcept
    {
        auto const* data = key.data();
        return shards_[data[key_type::size() - 1] & (N - 1)];
    }

public:
    TaggedKeyCache(
        std::string const& name,
        std::size_t size,
        clock_type::duration expiration,
        clock_type& clock,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector =
            beast::insight::NullCollector::New())
        : journal_(journal)
        , clock_(clock)
        , stats_(
              name,
              [this] {
                  stats_.size.set(this->size());
                  auto const hits = stats_.hits.load(std::memory_order_relaxed);
                  auto const misses =
                      stats_.misses.load(std::memory_order_relaxed);
                  auto const total = hits + misses;
                  stats_.hit_rate.set(total ? (hits * 100) / total : 0);
              },
              collector)
        , name_(name)
        , age_(expiration)
    {
        size = (size + N - 1) / N;

        if (size)
        {
            for (auto& shard : shards_)
                shard.map.reserve(size);
        }
    }

    clock_type&
    clock()
    {
        return clock_;
    }

    std::size_t
    size() const
    {
        return size_.load(std::memory_order_relaxed);
    }

    bool
    touch_if_exists(key_type const& key)
    {
        auto& shard = map_shard(key);
        std::lock_guard lock(shard.mutex);

        auto it = shard.map.find(key);
        if (it == shard.map.end())
        {
            stats_.misses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        it->second = clock_.now();
        stats_.hits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void
    insert(key_type const& key)
    {
        auto const now = clock_.now();
        auto& shard = map_shard(key);
        std::lock_guard lock(shard.mutex);

        auto [it, inserted] = shard.map.try_emplace(key, now);
        if (inserted)
            size_.fetch_add(1, std::memory_order_relaxed);
        else
            it->second = now;
    }

    bool
    clear() noexcept
    {
        // Note the map's clear method is noexcept; this matters because
        // we will never leave the maps in a half-cleared state. We will
        // either succeed in cleaning them all, or exit without clearing
        // any of them.
        try
        {
            std::vector<std::unique_lock<std::mutex>> guards;
            guards.reserve(shards_.size());
            for (auto& s : shards_)
                guards.emplace_back(s.mutex);

            for (auto& s : shards_)
                s.map.clear();

            size_.store(0, std::memory_order_relaxed);
            return true;
        }
        catch (std::exception const& ex)
        {
            JLOG(journal_.error())
                << name_ << " key cache clear failed: " << ex.what();
            return false;
        }
    }

    void
    sweep()
    {
        auto const wc = [this]() -> std::size_t {
            if (size() < sweepParallelThreshold)
                return 1;
            return std::min<std::size_t>(4, shards_.size());
        }();

        auto const now = clock_.now();
        auto const expiration = now - age_;

        std::atomic<std::size_t> next{0};

        auto const sweepShards = [this, &next, now, expiration]() -> int {
            int removals = 0;
            for (;;)
            {
                auto const i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= shards_.size())
                    break;

                auto& shard = shards_[i];
                std::lock_guard lock(shard.mutex);
                removals += sweep_shard(shard, now, expiration);
            }
            return removals;
        };

        struct Worker
        {
            std::thread t;
            std::atomic<int> removals{0};
        };

        std::vector<Worker> workers(wc - 1);
        for (auto& w : workers)
            w.t = std::thread([this, &removals = w.removals, &sweepShards] {
                beast::setCurrentThreadName("sweep:" + name_);
                removals.store(sweepShards(), std::memory_order_relaxed);
            });

        int total = sweepShards();

        for (auto& w : workers)
        {
            w.t.join();
            total += w.removals.load(std::memory_order_relaxed);
        }

        if (total)
            size_.fetch_sub(total, std::memory_order_relaxed);

        JLOG(journal_.debug()) << name_ << " key cache sweep (workers: " << wc
                               << ", removed: " << total << ")";
    }

private:
    [[nodiscard]] int
    sweep_shard(Shard& shard, time_point now, time_point expiration)
    {
        int removals = 0;
        auto it = shard.map.begin();
        while (it != shard.map.end())
        {
            if (it->second > now)
            {
                it->second = now;
                ++it;
            }
            else if (it->second <= expiration)
            {
                it = shard.map.erase(it);
                ++removals;
            }
            else
            {
                ++it;
            }
        }
        return removals;
    }

    struct Stats
    {
        template <class Handler>
        Stats(
            std::string const& prefix,
            Handler const& handler,
            beast::insight::Collector::ptr const& collector)
            : hook(collector->make_hook(handler))
            , size(collector->make_gauge(prefix, "size"))
            , hit_rate(collector->make_gauge(prefix, "hit_rate"))
            , hits(0)
            , misses(0)
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge size;
        beast::insight::Gauge hit_rate;
        std::atomic<std::uint64_t> hits;
        std::atomic<std::uint64_t> misses;
    };

    beast::Journal journal_;
    clock_type& clock_;
    Stats stats_;
    std::string name_;

    clock_type::duration age_;

    std::atomic<std::size_t> size_{0};
    std::array<Shard, N> shards_;
};

}  // namespace ripple

#endif
