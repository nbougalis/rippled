//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_NODESTORE_BACKEND_H_INCLUDED
#define RIPPLE_NODESTORE_BACKEND_H_INCLUDED

#include <xrpld/nodestore/Types.h>
#include <cstdint>
#include <memory>
#include <optional>

namespace ripple {
namespace NodeStore {

/** A backend used for the NodeStore.

    The NodeStore uses a swappable backend so that other database systems
    can be tried. Different databases may offer various features such
    as improved performance, fault tolerant or distributed storage, or
    all in-memory operation.

    A given instance of a backend is fixed to a particular key size.
*/
class Backend
{
public:
    /** Destroy the backend.

        All open files are closed and flushed. If there are batched writes
        or other tasks scheduled, they will be completed before this call
        returns.
    */
    virtual ~Backend() = default;

    /** Get the human-readable name of this backend.
        This is used for diagnostic output.
    */
    virtual std::string
    getName() = 0;

    /** Open the backend.
        @param createIfMissing Create the database files if necessary.
        This allows the caller to catch exceptions.
    */
    virtual void
    open(bool createIfMissing = true) = 0;

    /** Returns true is the database is open.
     */
    virtual bool
    isOpen() = 0;

    /** Open the backend.
        @param createIfMissing Create the database files if necessary.
        @param appType Deterministic appType used to create a backend.
        @param uid Deterministic uid used to create a backend.
        @param salt Deterministic salt used to create a backend.
        @throws std::runtime_error is function is called not for NuDB backend.
    */
    virtual void
    open(bool createIfMissing, uint64_t appType, uint64_t uid, uint64_t salt)
    {
        Throw<std::runtime_error>(
            "Deterministic appType/uid/salt not supported by backend " +
            getName());
    }

    /** Close the backend.
        This allows the caller to catch exceptions.
    */
    virtual void
    close() = 0;

    /** Fetch a single object.
        If the object is not found or an error is encountered, the
        result will indicate the condition.
        @note This will be called concurrently.
        @param key A pointer to the key data.
        @param pObject [out] The created object if successful.
        @return The result of the operation.
    */
    virtual Status
    fetch(void const* key, boost::intrusive_ptr<NodeObject>* pObject) = 0;

    /** Store a single object.
        Depending on the implementation this may happen immediately
        or deferred using a scheduled task.
        @note This will be called concurrently.
        @param object The object to store.
    */
    virtual void
    store(boost::intrusive_ptr<NodeObject> const& object) = 0;

    /** Store a group of objects.
        @note This function will not be called concurrently with
              itself or @ref store.
    */
    virtual void
    storeBatch(Batch const& batch) = 0;

    virtual void
    sync() = 0;

    /** Visit every object in the database
        This is usually called during import.
        @note This routine will not be called concurrently with itself
              or other methods.
        @see import
    */
    virtual void
    for_each(std::function<void(boost::intrusive_ptr<NodeObject>)> f) = 0;

    /** Estimate the number of write operations pending. */
    virtual int
    getWriteLoad() = 0;

    /** Remove contents on disk upon destruction. */
    virtual void
    setDeletePath() = 0;

    /** Perform consistency checks on database.
     *
     * This method is implemented only by NuDBBackend. It is not yet called
     * anywhere, but it might be a good idea to one day call it at startup to
     * avert a crash.
     */
    virtual void
    verify()
    {
    }

    /** Returns the number of file descriptors the backend expects to need. */
    virtual int
    fdRequired() const = 0;

    /** Get the block size for backends that support it
     */
    virtual std::optional<std::size_t>
    getBlockSize() const
    {
        return std::nullopt;
    }
};

/** Growable buffer with SBO support satisfying the BufferFactory concept.

    @warning The existing contents of the buffer may be lost across memory
             allocation requests.

    @note This class is intended for use as a function-local. It cannot be
          copied or moved.
 */
class scratch_buffer
{
    /** The size of the inline storage buffer. */
    static constexpr std::size_t fixed_size = 4096;

    /** The heap buffer for this object, if any. */
    std::unique_ptr<std::uint8_t[]> heap_;

    /** The size that the caller has requested. */
    std::size_t size_ = 0;

    /** The size that we have available. Always >= size_. */
    std::size_t capacity_ = fixed_size;

    /** The internal buffer for this object. The alignas is important. */
    alignas(std::max_align_t) std::uint8_t local_[fixed_size];

public:
    scratch_buffer() noexcept = default;

    scratch_buffer(scratch_buffer const&) = delete;
    scratch_buffer&
    operator=(scratch_buffer const&) = delete;

    scratch_buffer(scratch_buffer&&) = delete;
    scratch_buffer&
    operator=(scratch_buffer&&) = delete;

    /** Returns the number of bytes that are available to the caller.

        @note This may be less than the capacity, but it is a contract
              violation for the caller to use any of the excess, since
              the capacity of the buffer is an implementation detail.
     */
    std::size_t
    size() const noexcept
    {
        return size_;
    }

    /** Returns the buffer associated with this object.

        @note The returned pointer will remain valid at least until the next
              call to operator().

        @returns A non-null pointer that can hold at least @ref size() bytes.
     */
    std::uint8_t*
    get() noexcept
    {
        if (auto ret = heap_.get())
            return ret;

        return local_;
    }

    /** Request the buffer to be at least the given size.

        The call may or may not resize the buffer, depending on whether the
        request can be satisfied without reallocations.

        @warning Any buffer pointers previously returned from this function
                 or by @ref get() should be considered invalidated and must
                 not be accessed.

        @returns A pointer that can hold at least @ref size() bytes.
     */
    void*
    operator()(std::size_t size)
    {
        if (size > capacity_)
        {
            heap_ = std::make_unique_for_overwrite<std::uint8_t[]>(size);
            capacity_ = size;
        }

        size_ = size;
        return get();
    }
};

}  // namespace NodeStore
}  // namespace ripple

#endif
