#include <xrpl/protocol/STInteger.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <string>

namespace xrpl::detail {

// The helpers below take the value widened to 32 bits, but the fields they
// special-case each have a fixed width: the value of a matching field always
// originates from an integer of that width, so narrowing it back is safe.

std::string
specializedText(SField const& field, std::uint32_t value, std::string def)
{
    if (field == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo(TER::fromInt(checkedCast<int>(value)), token, human))
            return human;

        // LCOV_EXCL_START
        JLOG(debugLog().error()) << "Unknown result code in metadata: " << value;
        // LCOV_EXCL_STOP
    }
    else if (field == sfLedgerEntryType)
    {
        if (auto const* item =
                LedgerFormats::getInstance().findByType(checkedCast<LedgerEntryType>(value));
            item != nullptr)
            return item->getName();
    }
    else if (field == sfTransactionType)
    {
        if (auto const* item = TxFormats::getInstance().findByType(checkedCast<TxType>(value));
            item != nullptr)
            return item->getName();
    }
    else if (field == sfPermissionValue)
    {
        if (auto const name = Permission::getInstance().getPermissionName(value))
            return *name;
    }

    return def;
}

json::Value
specializedJson(SField const& field, std::uint32_t value, json::Value def)
{
    if (field == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo(TER::fromInt(checkedCast<int>(value)), token, human))
            return token;

        // LCOV_EXCL_START
        JLOG(debugLog().error()) << "Unknown result code in metadata: " << value;
        // LCOV_EXCL_STOP
    }
    else if (field == sfLedgerEntryType)
    {
        if (auto const* item =
                LedgerFormats::getInstance().findByType(checkedCast<LedgerEntryType>(value));
            item != nullptr)
            return item->getName();
    }
    else if (field == sfTransactionType)
    {
        if (auto const* item = TxFormats::getInstance().findByType(checkedCast<TxType>(value));
            item != nullptr)
            return item->getName();
    }
    else if (field == sfPermissionValue)
    {
        if (auto const name = Permission::getInstance().getPermissionName(value))
            return *name;
    }

    return def;
}

}  // namespace xrpl::detail
