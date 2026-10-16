#include <xrpl/protocol/STTakesAsset.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/detail/STTypeList.h>

#include <concepts>

namespace xrpl {

void
associateAsset(SLE& sle, Asset const& asset)
{
    // Iterating by offset is the only way to get non-const references
    for (int i = 0; i < sle.getCount(); ++i)
    {
        STBase& entry = sle.getIndex(i);
        SField const& field = entry.getFName();

        if (!field.shouldMeta(SField::kSmdNeedsAsset) || entry.getSType() == STI_NOTPRESENT)
            continue;

        auto const style = sle.getStyle(field);

        XRPL_ASSERT(style != SoeInvalid, "xrpl::associateAsset : valid template element style");
        XRPL_ASSERT(
            style != SoeDefault || !entry.isDefault(), "xrpl::associateAsset : non-default value");

        // Dispatch to the leaf type, so that it can react to the association
        // (e.g. STNumber rounds its value to the precision of the asset).
        bool const handled = detail::visitSTLeafType(entry.getSType(), [&]<typename T>() {
            if constexpr (std::derived_from<T, detail::TakesAssetBase>)
            {
                entry.downcast<T>().associateAsset(asset);
            }
            else
            {
                // If the type doesn't take an asset, then the flag shouldn't
                // be on the SField.
                UNREACHABLE("xrpl::associateAsset : flagged field does not take an asset");
            }
        });

        XRPL_ASSERT(handled, "xrpl::associateAsset : flagged field has a leaf type");

        // associateAsset in the leaf may change the underlying value, but it
        // won't know anything about how the value relates to the SLE. If the
        // template element is soeDEFAULT, and the value changed to the
        // default value, remove the field.
        if (style == SoeDefault && entry.isDefault())
            sle.makeFieldAbsent(field);
    }
}

}  // namespace xrpl
