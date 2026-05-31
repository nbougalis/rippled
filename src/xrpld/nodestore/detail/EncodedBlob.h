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

#ifndef RIPPLE_NODESTORE_ENCODEDBLOB_H_INCLUDED
#define RIPPLE_NODESTORE_ENCODEDBLOB_H_INCLUDED

#include <xrpld/nodestore/NodeObject.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <boost/align/align_up.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace ripple {
namespace NodeStore {

/** Convert a NodeObject from in-memory to database format.

    The (suboptimal) database format consists of:

    - 8 prefix bytes which will typically be 0, but don't assume that's the
      case; earlier versions of the code would use these bytes to store the
      ledger index either once or twice.
    - A single byte denoting the type of the object.
    - The payload.

    @note This class is typically instantiated on the stack, so the size of
          the object does not matter as much as it normally would since the
          allocation is, effectively, free.

          We leverage that fact to preallocate enough memory to handle most
          payloads as part of this object, eliminating the need for dynamic
          allocation. As of this writing ~98% of objects fit comfortably in
          600 bytes.

          This class is not intended to be copied or moved. Turning it into
          a function is a better approach.
 */

class EncodedBlob
{
    /** The 32-byte key of the serialized object. */
    std::array<std::uint8_t, 32> key_;

    /** The size of the serialized data. */
    std::uint32_t size_;

    /** A pre-allocated buffer for the serialized object. */
    std::array<std::uint8_t, 724> payload_;

    /** A pointer to the serialized data.

        This may point to the pre-allocated buffer (if it is sufficiently
        large) or to a dynamically allocated buffer.
     */
    std::uint8_t* const ptr_;

public:
    explicit EncodedBlob(boost::intrusive_ptr<NodeObject> const& obj)
        : size_([&obj]() {
            return checked_cast<decltype(size_)>(obj->size() + 9);
        }())
        , ptr_(
              (size_ <= payload_.size()) ? payload_.data()
                                         : new std::uint8_t[size_])
    {
        std::fill_n(ptr_, 8, std::uint8_t{0});
        ptr_[8] = static_cast<std::uint8_t>(obj->type());
        if (auto data = obj->data(); !data.empty())
            std::copy_n(data.data(), data.size(), ptr_ + 9);
        std::copy_n(obj->key().data(), obj->key().size(), key_.data());
    }

    ~EncodedBlob()
    {
        XRPL_ASSERT(
            ((ptr_ == payload_.data()) && (size_ <= payload_.size())) ||
                ((ptr_ != payload_.data()) && (size_ > payload_.size())),
            "ripple::NodeStore::EncodedBlob::~EncodedBlob : valid payload "
            "pointer");

        if (ptr_ != payload_.data())
            delete[] ptr_;
    }

    EncodedBlob(EncodedBlob const&) = delete;
    EncodedBlob&
    operator=(EncodedBlob const&) = delete;

    EncodedBlob(EncodedBlob&&) = delete;
    EncodedBlob&
    operator=(EncodedBlob&&) = delete;

    [[nodiscard]] void const*
    getKey() const noexcept
    {
        return key_.data();
    }

    [[nodiscard]] std::size_t
    getSize() const noexcept
    {
        return size_;
    }

    [[nodiscard]] void const*
    getData() const noexcept
    {
        return ptr_;
    }
};

}  // namespace NodeStore
}  // namespace ripple

#endif
