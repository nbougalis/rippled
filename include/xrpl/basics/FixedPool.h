#pragma once

#include <boost/align/align_up.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace xrpl {

/**
 * A fixed-capacity pool of uniformly sized chunks, guarded by a mutex.
 *
 * The pool owns a single, suitably aligned buffer of @p Capacity chunks,
 * each large enough for one @p Type or a freelist node, whichever is
 * larger. Chunks are handed out in order the first time around and, once
 * returned, recycled through a freelist threaded through the chunks
 * themselves.
 *
 * The pool hands out raw storage: callers construct into it (for example
 * with std::construct_at) and must destroy the object before returning
 * the chunk. When the pool is exhausted, allocate() returns nullptr and
 * the caller must obtain the memory elsewhere; deallocate() reports
 * whether it owned a chunk, so that memory from such a source can be
 * routed back to it.
 *
 * Every operation takes the lock, so the pool is at best on par with a
 * thread-local heap allocator. It is meant for small populations of
 * short-lived objects where the point is bounded, contiguous storage.
 *
 * The pool is constructible in a constant expression and holds no
 * resources, so it can be a `constinit` global that is never torn down.
 * The freelist cannot be built in a constant expression, which is why
 * chunks are handed out by a high-water mark and only enter the list when
 * they are returned. The pool is neither copyable nor movable, since
 * chunks handed out refer into it.
 *
 * @tparam Type The type the chunks are sized and aligned for.
 * @tparam Capacity The number of chunks in the pool; must be non-zero.
 */
template <typename Type, std::size_t Capacity>
    requires(Capacity != 0)
class FixedPool
{
    /**
     * A node in the intrusive freelist, constructed in a returned chunk.
     */
    struct Node
    {
        Node* next;
    };

    // Chunks are handed out as raw storage, regardless of which object last
    // lived there, which is only sound because a node needs no destruction.
    static_assert(std::is_trivially_destructible_v<Node>);

    /**
     * The alignment of every chunk: enough for a Type and a Node.
     */
    static constexpr std::size_t kAlign = std::max(alignof(Type), alignof(Node));

    /**
     * The distance between chunks: enough for a Type and a Node, rounded
     * up so that every chunk is aligned.
     */
    static constexpr std::size_t kChunkSize =
        boost::alignment::align_up(std::max(sizeof(Type), sizeof(Node)), kAlign);

    /**
     * The chunks. Zero-initialized, so that a constinit pool lives in .bss.
     */
    alignas(kAlign) std::byte storage_[kChunkSize * Capacity] = {};

    /**
     * Head of the freelist of chunks that have been returned.
     */
    Node* head_ = nullptr;

    /**
     * Chunks at or beyond this index have never been handed out.
     */
    std::size_t used_ = 0;

    /**
     * Guards head_ and used_.
     */
    std::mutex mutex_;

public:
    constexpr FixedPool() noexcept = default;
    ~FixedPool() = default;

    FixedPool(FixedPool const&) = delete;
    FixedPool&
    operator=(FixedPool const&) = delete;
    FixedPool(FixedPool&&) = delete;
    FixedPool&
    operator=(FixedPool&&) = delete;

    /**
     * Obtain a chunk from the pool.
     *
     * @return Pointer to uninitialized storage for one Type, or nullptr if
     *         every chunk is in use.
     */
    [[nodiscard]] void*
    allocate() noexcept
    {
        std::scoped_lock const lock(mutex_);

        if (head_ != nullptr)
        {
            auto* const node = std::exchange(head_, head_->next);

            if constexpr (!std::is_trivially_destructible_v<decltype(node)>)
                std::destroy_at(node);

            return node;
        }

        if (used_ < Capacity)
            return &storage_[kChunkSize * used_++];

        return nullptr;
    }

    /**
     * Return a chunk to the pool.
     *
     * Any object in the chunk must already have been destroyed.
     *
     * @param ptr A pointer previously returned by allocate(), or a pointer
     *            the pool does not own, which is left untouched.
     * @return true if the pool owned the chunk and reclaimed it, false if
     *         the pointer did not come from this pool.
     */
    [[nodiscard]] bool
    deallocate(void* ptr) noexcept
    {
        if (!owns(ptr))
            return false;

        std::scoped_lock const lock(mutex_);

        head_ = std::construct_at(static_cast<Node*>(ptr), head_);

        return true;
    }

    /**
     * Determine whether a pointer refers to a chunk of this pool.
     *
     * @note A pointer into the pool that was not obtained from allocate(),
     *       or has already been returned, is also reported as owned.
     */
    [[nodiscard]] bool
    owns(void const* ptr) const noexcept
    {
        auto const b = reinterpret_cast<std::uintptr_t>(storage_);
        auto const p = reinterpret_cast<std::uintptr_t>(ptr);

        return (p >= b) && (p < b + sizeof(storage_)) && ((p - b) % kChunkSize == 0);
    }
};

}  // namespace xrpl
