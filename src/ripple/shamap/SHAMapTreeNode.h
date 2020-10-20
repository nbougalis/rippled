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

#ifndef RIPPLE_SHAMAP_SHAMAPTREENODE_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPTREENODE_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/TaggedCache.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/SHAMapNodeID.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace ripple {

// A SHAMapHash is the hash of a node in a SHAMap, and also the
// type of the hash of the entire SHAMap.
class SHAMapHash
{
    uint256 hash_;

public:
    SHAMapHash() = default;
    explicit SHAMapHash(uint256 const& hash) : hash_(hash)
    {
    }

    uint256 const&
    as_uint256() const
    {
        return hash_;
    }
    uint256&
    as_uint256()
    {
        return hash_;
    }
    bool
    isZero() const
    {
        return hash_.isZero();
    }
    bool
    isNonZero() const
    {
        return hash_.isNonZero();
    }
    int
    signum() const
    {
        return hash_.signum();
    }
    void
    zero()
    {
        hash_.zero();
    }

    friend bool
    operator==(SHAMapHash const& x, SHAMapHash const& y)
    {
        return x.hash_ == y.hash_;
    }

    friend bool
    operator<(SHAMapHash const& x, SHAMapHash const& y)
    {
        return x.hash_ < y.hash_;
    }

    friend std::ostream&
    operator<<(std::ostream& os, SHAMapHash const& x)
    {
        return os << x.hash_;
    }

    friend std::string
    to_string(SHAMapHash const& x)
    {
        return to_string(x.hash_);
    }

    template <class H>
    friend void
    hash_append(H& h, SHAMapHash const& x)
    {
        hash_append(h, x.hash_);
    }
};

inline bool
operator!=(SHAMapHash const& x, SHAMapHash const& y)
{
    return !(x == y);
}

class SHAMapAbstractNode
{
public:
    enum TNType : std::uint8_t {
        tnINNER = 1,
        tnTRANSACTION_NM = 2,  // transaction, no metadata
        tnTRANSACTION_MD = 3,  // transaction, with metadata
        tnACCOUNT_STATE = 4
    };

protected:
    SHAMapHash mHash;
    std::uint32_t owner_;

protected:
    virtual ~SHAMapAbstractNode() = 0;
    SHAMapAbstractNode(SHAMapAbstractNode const&) = delete;
    SHAMapAbstractNode&
    operator=(SHAMapAbstractNode const&) = delete;

    explicit SHAMapAbstractNode(std::uint32_t seq)
        : owner_(seq)
    {
    }

    explicit SHAMapAbstractNode(std::uint32_t seq, SHAMapHash const& hash)
        : mHash(hash), owner_(seq)
    {
    }

public:
    /** If the node is shared, return the ID of the SHAMap that "owns" it.

        A single node can be present in multiple SHAMap instances and we need
        a way of distinguishing whether a node is shared and, if it is shared,
        which map has the "definitive" copy.

        @return 0 if the node is not shared; a sharing identifer otherwise.
     */
    std::uint32_t
    owner() const;

    /** Mark this node as no longer shared between multiple SHAMap instances.    */
    void
    unshare();

    /** Returns whether the node in question is shared */
    bool
    isShared() const
    {
        return owner_ == 0;
    }

    SHAMapHash const&
    getNodeHash() const;

    virtual TNType
    getType() const = 0;

    virtual bool
    isLeaf() const = 0;

    virtual bool
    isInner() const = 0;

    virtual bool
    isInBounds(SHAMapNodeID const& id) const = 0;

    virtual bool
    updateHash() = 0;

    /** Serialize the node in a format appropriate for sending over the wire */
    virtual void
    serializeForWire(Serializer&) const = 0;

    /** Serialize the node in a format appropriate for hashing */
    virtual void
    serializeWithPrefix(Serializer&) const = 0;

    virtual std::string
    getString(SHAMapNodeID const&) const;
    virtual std::shared_ptr<SHAMapAbstractNode>
    clone(std::uint32_t seq) const = 0;
    virtual uint256 const&
    key() const = 0;
    virtual void
    invariants(bool is_root = false) const = 0;

    static std::shared_ptr<SHAMapAbstractNode>
    makeFromPrefix(Slice rawNode, SHAMapHash const& hash);

    static std::shared_ptr<SHAMapAbstractNode>
    makeFromWire(Slice rawNode);

private:
    static std::shared_ptr<SHAMapAbstractNode>
    makeTransaction(
        Slice data,
        std::uint32_t seq,
        SHAMapHash const& hash,
        bool hashValid);

    static std::shared_ptr<SHAMapAbstractNode>
    makeAccountState(
        Slice data,
        std::uint32_t seq,
        SHAMapHash const& hash,
        bool hashValid);

    static std::shared_ptr<SHAMapAbstractNode>
    makeTransactionWithMeta(
        Slice data,
        std::uint32_t seq,
        SHAMapHash const& hash,
        bool hashValid);
};

class SHAMapInnerNode : public SHAMapAbstractNode,
                        public CountedObject<SHAMapInnerNode>
{
    std::array<SHAMapHash, 16> mHashes;
    std::shared_ptr<SHAMapAbstractNode> mChildren[16];
    int mIsBranch = 0;
    std::uint32_t mFullBelowGen = 0;

    static std::mutex childLock;

public:
    SHAMapInnerNode(std::uint32_t seq);
    std::shared_ptr<SHAMapAbstractNode>
    clone(std::uint32_t seq) const override;

    TNType
    getType() const override
    {
        return tnINNER;
    }

    bool
    isLeaf() const override
    {
        return false;
    }

    bool
    isInner() const override
    {
        return true;
    }

    bool
    isInBounds(SHAMapNodeID const& id) const override
    {
        // Inner nodes can't be at the level of leaves:
        return id.getDepth() < 64;
    }

    bool
    isEmpty() const;
    bool
    isEmptyBranch(int m) const;
    int
    getBranchCount() const;
    SHAMapHash const&
    getChildHash(int m) const;

    void
    setChild(int m, std::shared_ptr<SHAMapAbstractNode> const& child);
    void
    shareChild(int m, std::shared_ptr<SHAMapAbstractNode> const& child);
    SHAMapAbstractNode*
    getChildPointer(int branch);
    std::shared_ptr<SHAMapAbstractNode>
    getChild(int branch);
    virtual std::shared_ptr<SHAMapAbstractNode>
    canonicalizeChild(int branch, std::shared_ptr<SHAMapAbstractNode> node);

    // sync functions
    bool
    isFullBelow(std::uint32_t generation) const;
    void
    setFullBelowGen(std::uint32_t gen);

    bool
    updateHash() override;
    void
    updateHashDeep();

    void
    serializeForWire(Serializer&) const override;

    void
    serializeWithPrefix(Serializer&) const override;

    std::string
    getString(SHAMapNodeID const&) const override;
    uint256 const&
    key() const override;
    void
    invariants(bool is_root = false) const override;

    static std::shared_ptr<SHAMapAbstractNode>
    makeFullInner(
        Slice data,
        std::uint32_t seq,
        SHAMapHash const& hash,
        bool hashValid);

    static std::shared_ptr<SHAMapAbstractNode>
    makeCompressedInner(Slice data, std::uint32_t seq);
};

// SHAMapTreeNode represents a leaf, and may eventually be renamed to reflect
// that.
class SHAMapTreeNode : public SHAMapAbstractNode,
                       public CountedObject<SHAMapTreeNode>
{
protected:
    std::shared_ptr<SHAMapItem const> mItem;

public:
    SHAMapTreeNode(const SHAMapTreeNode&) = delete;
    SHAMapTreeNode&
    operator=(const SHAMapTreeNode&) = delete;

    SHAMapTreeNode(
        std::shared_ptr<SHAMapItem const> item,
        std::uint32_t seq);

    SHAMapTreeNode(
        std::shared_ptr<SHAMapItem const> item,
        std::uint32_t seq,
        SHAMapHash const& hash);

    bool
    isLeaf() const override
    {
        return true;
    }

    bool
    isInner() const override
    {
        return false;
    }

    bool
    isInBounds(SHAMapNodeID const& id) const override
    {
        return true;
    }

    void
    serializeForWire(Serializer&) const override;

    void
    serializeWithPrefix(Serializer&) const override;

    uint256 const&
    key() const override;
    void
    invariants(bool is_root = false) const override;

public:  // public only to SHAMap
    // item node function
    bool
    hasItem() const;
    std::shared_ptr<SHAMapItem const> const&
    peekItem() const;
    bool
    setItem(std::shared_ptr<SHAMapItem const> i);

    std::string
    getString(SHAMapNodeID const&) const override;
    bool
    updateHash() override;
};

class SHAMapTxLeafNode : public SHAMapTreeNode
{
public:
    // Use the base class constructors
    using SHAMapTreeNode::SHAMapTreeNode;

    std::shared_ptr<SHAMapAbstractNode>
    clone(std::uint32_t seq) const override
    {
        return std::make_shared<SHAMapTxLeafNode>(mItem, seq, mHash);
    }

    TNType
    getType() const override
    {
        return tnTRANSACTION_NM;
    }
};

class SHAMapTxPlusMetaLeafNode : public SHAMapTreeNode
{
public:
    // Use the base class constructors
    using SHAMapTreeNode::SHAMapTreeNode;

    std::shared_ptr<SHAMapAbstractNode>
    clone(std::uint32_t seq) const override
    {
        return std::make_shared<SHAMapTxPlusMetaLeafNode>(mItem, seq, mHash);
    }

    TNType
    getType() const override
    {
        return tnTRANSACTION_MD;
    }
};

class SHAMapAccountStateLeafNode : public SHAMapTreeNode
{
public:
    // Use the base class constructors
    using SHAMapTreeNode::SHAMapTreeNode;

    std::shared_ptr<SHAMapAbstractNode>
    clone(std::uint32_t seq) const override
    {
        return std::make_shared<SHAMapAccountStateLeafNode>(mItem, seq, mHash);
    }

    TNType
    getType() const override
    {
        return tnACCOUNT_STATE;
    }
};

// SHAMapAbstractNode
inline std::uint32_t
SHAMapAbstractNode::owner() const
{
    return owner_;
}

inline void
SHAMapAbstractNode::unshare()
{
    owner_ = 0;
}

inline SHAMapHash const&
SHAMapAbstractNode::getNodeHash() const
{
    return mHash;
}

// SHAMapInnerNode

inline SHAMapInnerNode::SHAMapInnerNode(std::uint32_t seq)
    : SHAMapAbstractNode(seq)
{
}

inline bool
SHAMapInnerNode::isEmptyBranch(int m) const
{
    return (mIsBranch & (1 << m)) == 0;
}

inline SHAMapHash const&
SHAMapInnerNode::getChildHash(int m) const
{
    assert(m >= 0 && m < 16);
    return mHashes[m];
}

inline bool
SHAMapInnerNode::isFullBelow(std::uint32_t generation) const
{
    return mFullBelowGen == generation;
}

inline void
SHAMapInnerNode::setFullBelowGen(std::uint32_t gen)
{
    mFullBelowGen = gen;
}

// SHAMapTreeNode

inline bool
SHAMapTreeNode::hasItem() const
{
    return bool(mItem);
}

inline std::shared_ptr<SHAMapItem const> const&
SHAMapTreeNode::peekItem() const
{
    return mItem;
}

}  // namespace ripple

#endif
