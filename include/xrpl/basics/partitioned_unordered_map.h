//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_PARTITIONED_UNORDERED_MAP_H
#define RIPPLE_BASICS_PARTITIONED_UNORDERED_MAP_H

#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ripple {

template <typename Key>
static std::size_t
extract(Key const& key)
{
    return key;
}

template <>
inline std::size_t
extract(std::string const& key)
{
    return ::beast::uhash<>{}(key);
}

template <
    typename Key,
    typename Value,
    typename Hash,
    typename Pred = std::equal_to<Key>,
    typename Alloc = std::allocator<std::pair<const Key, Value>>>
class partitioned_unordered_map
{
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key const, mapped_type>;
    using size_type = std::size_t;
    using difference_type = std::size_t;
    using hasher = Hash;
    using key_equal = Pred;
    using allocator_type = Alloc;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using map_type = std::
        unordered_map<key_type, mapped_type, hasher, key_equal, allocator_type>;
    using partition_map_type = std::vector<map_type>;

    struct iterator
    {
        using iterator_category = std::forward_iterator_tag;
        partition_map_type* map_{nullptr};
        typename partition_map_type::iterator ait_;
        typename map_type::iterator mit_;

        iterator() = default;

        iterator(partition_map_type* map) : map_(map)
        {
        }

        reference
        operator*() const
        {
            return *mit_;
        }

        pointer
        operator->() const
        {
            return &(*mit_);
        }

        void
        inc()
        {
            ++mit_;
            while (mit_ == ait_->end())
            {
                ++ait_;
                if (ait_ == map_->end())
                    return;
                mit_ = ait_->begin();
            }
        }

        // ++it
        iterator&
        operator++()
        {
            inc();
            return *this;
        }

        // it++
        iterator
        operator++(int)
        {
            iterator tmp(*this);
            inc();
            return tmp;
        }

        friend bool
        operator==(iterator const& lhs, iterator const& rhs)
        {
            return lhs.map_ == rhs.map_ && lhs.ait_ == rhs.ait_ &&
                lhs.mit_ == rhs.mit_;
        }

        friend bool
        operator!=(iterator const& lhs, iterator const& rhs)
        {
            return !(lhs == rhs);
        }
    };

    struct const_iterator
    {
        using iterator_category = std::forward_iterator_tag;

        partition_map_type* map_{nullptr};
        typename partition_map_type::iterator ait_;
        typename map_type::iterator mit_;

        const_iterator() = default;

        const_iterator(partition_map_type* map) : map_(map)
        {
        }

        const_iterator(iterator const& orig)
        {
            map_ = orig.map_;
            ait_ = orig.ait_;
            mit_ = orig.mit_;
        }

        const_reference
        operator*() const
        {
            return *mit_;
        }

        const_pointer
        operator->() const
        {
            return &(*mit_);
        }

        void
        inc()
        {
            ++mit_;
            while (mit_ == ait_->end())
            {
                ++ait_;
                if (ait_ == map_->end())
                    return;
                mit_ = ait_->begin();
            }
        }

        // ++it
        const_iterator&
        operator++()
        {
            inc();
            return *this;
        }

        // it++
        const_iterator
        operator++(int)
        {
            const_iterator tmp(*this);
            inc();
            return tmp;
        }

        friend bool
        operator==(const_iterator const& lhs, const_iterator const& rhs)
        {
            return lhs.map_ == rhs.map_ && lhs.ait_ == rhs.ait_ &&
                lhs.mit_ == rhs.mit_;
        }

        friend bool
        operator!=(const_iterator const& lhs, const_iterator const& rhs)
        {
            return !(lhs == rhs);
        }
    };

private:
    template <class T>
    static void
    end(T& it)
    {
        it.ait_ = it.map_->end();
        it.mit_ = it.map_->back().end();
    }

    template <class T>
    static void
    begin(T& it)
    {
        for (it.ait_ = it.map_->begin(); it.ait_ != it.map_->end(); ++it.ait_)
        {
            if (it.ait_->begin() == it.ait_->end())
                continue;
            it.mit_ = it.ait_->begin();
            return;
        }
        end(it);
    }

public:
    partitioned_unordered_map(
        std::optional<std::size_t> partitions = std::nullopt)
    {
        // Set partitions to the twice the number of hardware threads if
        // the parameter is either empty or set to 0.
        if (partitions.value_or(0) == 0)
            partitions = std::clamp<std::size_t>(
                std::thread::hardware_concurrency(), 4, 64);

        // At this point, partitions has a value. If it was automatically
        // selected, it's in the range of [4, 64]. But if it was manually
        // specified, it can have any value, so we need to sanitize again
        // to make sure the values make sense. The bounds differ here and
        // do so on purpose: to afford the programmer flexibility.
        map_.resize(std::clamp<std::size_t>(*partitions, 1, 128));
    }

    std::size_t
    partitions() const noexcept
    {
        return map_.size();
    }

    map_type&
    partition(std::size_t index)
    {
        return map_.at(index);
    }

    iterator
    begin()
    {
        iterator it(&map_);
        begin(it);
        return it;
    }

    const_iterator
    cbegin() const
    {
        const_iterator it(&map_);
        begin(it);
        return it;
    }

    const_iterator
    begin() const
    {
        return cbegin();
    }

    iterator
    end()
    {
        iterator it(&map_);
        end(it);
        return it;
    }

    const_iterator
    cend() const
    {
        const_iterator it(&map_);
        end(it);
        return it;
    }

    const_iterator
    end() const
    {
        return cend();
    }

private:
    template <class T>
    void
    find(key_type const& key, T& it) const
    {
        it.ait_ = it.map_->begin() + partition_index(key);
        it.mit_ = it.ait_->find(key);
        if (it.mit_ == it.ait_->end())
            end(it);
    }

public:
    iterator
    find(key_type const& key)
    {
        iterator it(&map_);
        find(key, it);
        return it;
    }

    const_iterator
    find(key_type const& key) const
    {
        const_iterator it(&map_);
        find(key, it);
        return it;
    }

    template <class T, class U>
    std::pair<iterator, bool>
    emplace(std::piecewise_construct_t const&, T&& keyTuple, U&& valueTuple)
    {
        auto const& key = std::get<0>(keyTuple);
        iterator it(&map_);
        it.ait_ = it.map_->begin() + partition_index(key);
        auto [eit, inserted] = it.ait_->emplace(
            std::piecewise_construct,
            std::forward<T>(keyTuple),
            std::forward<U>(valueTuple));
        it.mit_ = eit;
        if (inserted)
            size_.fetch_add(1, std::memory_order_relaxed);
        return {it, inserted};
    }

    template <class T, class U>
    std::pair<iterator, bool>
    emplace(T&& key, U&& val)
    {
        iterator it(&map_);
        it.ait_ = it.map_->begin() + partition_index(key);
        auto [eit, inserted] =
            it.ait_->emplace(std::forward<T>(key), std::forward<U>(val));
        it.mit_ = eit;
        if (inserted)
            size_.fetch_add(1, std::memory_order_relaxed);
        return {it, inserted};
    }

    void
    clear()
    {
        for (auto& p : map_)
            p.clear();

        size_.store(0, std::memory_order_relaxed);
    }

    iterator
    erase(const_iterator position)
    {
        iterator it(&map_);
        it.ait_ = position.ait_;
        it.mit_ = position.ait_->erase(position.mit_);

        size_.fetch_sub(1, std::memory_order_relaxed);

        while (it.mit_ == it.ait_->end())
        {
            ++it.ait_;
            if (it.ait_ == it.map_->end())
                break;
            it.mit_ = it.ait_->begin();
        }

        return it;
    }

    std::size_t
    size() const noexcept
    {
        return size_.load(std::memory_order_relaxed);
    }

    Value&
    operator[](Key const& key)
    {
        return map_[partition_index(key)][key];
    }

    std::size_t
    partition_index(Key const& key) const noexcept
    {
        if (map_.size() == 1)
            return 0;

        return extract(key) % map_.size();
    }

private:
    // The number of items across all partitions
    std::atomic<std::size_t> size_{0};

    // The partitions
    mutable partition_map_type map_{};
};

}  // namespace ripple

#endif  // RIPPLE_BASICS_PARTITIONED_UNORDERED_MAP_H
