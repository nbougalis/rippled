#pragma once

#include <xrpl/basics/FixedPool.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>

#include <boost/align/aligned_alloc.hpp>

#include <memory>
#include <new>
#include <utility>

namespace xrpl {

namespace detail {
/**
 * Holds the Asset that a serialized type may be associated with.
 *
 * The asset is only ever used at runtime; it is never serialized. It is
 * held through a pointer so that the (far more common) unassociated state
 * costs a single word, and copies of an associated object carry a copy of
 * the asset.
 *
 * This class is not polymorphic, and it is not a serialized type: it is the
 * part of @ref STTakesAsset that does not depend on the template parameters,
 * and serves to identify asset-taking types (see @ref associateAsset).
 */
class TakesAssetBase
{
    // The private constructor obtains raw storage and, if the allocation is
    // successful, constructs an Asset into it. If Asset's constructor threw,
    // the allocated memory would leak. Rather than guard that window with a
    // try block, we simply require that it cannot throw at all.
    static_assert(std::is_nothrow_copy_constructible_v<Asset>);

    /**
     * A small pool, to speed up allocation of assets.
     */
    static constinit inline FixedPool<Asset, 1024> pool;

    /**
     * An intentionally stateless deleter, which destroys an asset
     * and releases its storage.
     */
    struct Deleter
    {
        void
        operator()(Asset* asset) const noexcept
        {
            std::destroy_at(asset);

            if (!pool.deallocate(asset))
                boost::alignment::aligned_free(asset);
        }
    };

    /**
     * The asset we hold, or nullptr if none has been associated.
     *
     * @note The use of `std::unique_ptr` with a stateless deleter is
     *       intentional: we want to minimize the size of this helper
     *       class.
     */
    std::unique_ptr<Asset, Deleter> asset_;

    explicit TakesAssetBase(Asset const* asset)
    {
        if (asset != nullptr)
        {
            auto* ptr = pool.allocate();

            if (ptr == nullptr)
            {
                ptr = boost::alignment::aligned_alloc(alignof(Asset), sizeof(Asset));

                if (ptr == nullptr) [[unlikely]]
                    Throw<std::bad_alloc>();
            }

            asset_.reset(std::construct_at(static_cast<Asset*>(ptr), *asset));
        }
    }

protected:
    TakesAssetBase() = default;

    TakesAssetBase(TakesAssetBase const& other) : TakesAssetBase(other.asset_.get())
    {
    }

    TakesAssetBase&
    operator=(TakesAssetBase const& other)
    {
        // The copy is made before anything is released, so the held asset
        // survives a failed allocation.
        if (this != &other)
            *this = TakesAssetBase(other);

        return *this;
    }

    TakesAssetBase(TakesAssetBase&&) noexcept = default;

    TakesAssetBase&
    operator=(TakesAssetBase&&) noexcept = default;

    ~TakesAssetBase() = default;

    /**
     * The associated asset, or nullptr if none has been associated.
     */
    [[nodiscard]] Asset const*
    asset() const noexcept
    {
        return asset_.get();
    }

    /**
     * Associate an asset with this object.
     *
     * The asset is a property of the field and is fixed once associated.
     */
    void
    assign(Asset const& a)
    {
        XRPL_ASSERT(!asset_, "xrpl::TakesAssetBase::assign : asset is fixed once associated");
        *this = TakesAssetBase(&a);
    }
};

}  // namespace detail

/**
 * Base class for serialized types that can be associated with an Asset.
 *
 * This sits between @ref STTypedBase and the leaf type, so that the leaf
 * keeps its compile-time type identifier. The asset is stored and used only
 * at runtime; it is never serialized to the ledger.
 *
 * The leaf decides what to do with the asset, and when. It will not
 * necessarily be set at any given time. As of this writing, only
 * @ref STNumber uses it, to round the stored Number to the precision of
 * the asset both when associated and when serializing.
 *
 * @tparam ID      The @ref SerializedTypeID of the leaf type.
 * @tparam Derived The leaf type (CRTP); it must be `final`.
 *
 * @see STTypedBase, TakesAssetBase, associateAsset
 */
template <SerializedTypeID ID, typename Derived>
class STTakesAsset : public STTypedBase<ID, Derived, STTakesAsset<ID, Derived>>,
                     public detail::TakesAssetBase
{
    // As with STTypedBase, only the leaf may construct this "shim" class.
    friend Derived;

    STTakesAsset() = default;

    STTakesAsset(STTakesAsset const&) = default;
    STTakesAsset(STTakesAsset&&) noexcept = default;

    STTakesAsset&
    operator=(STTakesAsset const&) = default;
    STTakesAsset&
    operator=(STTakesAsset&&) noexcept = default;

    explicit STTakesAsset(SField const& n) : STTypedBase<ID, Derived, STTakesAsset>(n)
    {
    }

    virtual void
    associateAsset(Asset const& a)
    {
        XRPL_ASSERT(asset() == nullptr, "xrpl::STTakesAsset::associateAsset : already associated");
        assign(a);
    }
};

class STLedgerEntry;

/**
 * Associate an Asset with all kSmdNeedsAsset fields in a ledger entry.
 *
 * This function iterates over all fields in the given ledger entry. For each
 * field that is set and has the SField::kSmdNeedsAsset metadata flag, it calls
 * `associateAsset` on that field with the given Asset. Such a field must be of
 * a type derived from STTakesAsset; a field which is not is a programmer error.
 *
 * Typically, associateAsset should be called near the end of doApply() of any
 * Transactor classes on the SLEs of any new or modified ledger entries
 * containing STNumber fields, after doing all of the modifications to the SLEs.
 *
 * @param sle The ledger entry whose fields will be updated.
 * @param asset The Asset to associate with the relevant fields.
 */
void
associateAsset(STLedgerEntry& sle, Asset const& asset);

}  // namespace xrpl
