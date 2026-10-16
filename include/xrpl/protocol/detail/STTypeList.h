#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>

#include <tuple>
#include <type_traits>
#include <utility>

namespace xrpl::detail {

/**
 * The leaf serialized types: every type with a compile-time type identifier.
 *
 * STObject and STArray (and the high-level types derived from STObject) are
 * not leaves: their type identifier is only known at runtime.
 *
 * @note This list must be kept in sync with STVar::constructST.
 */
using STLeafTypes = std::tuple<
    STUInt8,
    STUInt16,
    STUInt32,
    STUInt64,
    STInt32,
    STUInt128,
    STUInt160,
    STUInt192,
    STUInt256,
    STAmount,
    STNumber,
    STVector256,
    STBlob,
    STAccount,
    STPathSet,
    STIssue,
    STCurrency>;

/**
 * Invoke @p f with the leaf type whose type identifier is @p id.
 *
 * The callable is invoked as `f.template operator()<T>()`, where T is the
 * leaf type with `T::kTypeId == id`.
 *
 * @return true if a leaf type with that identifier exists and @p f was
 *         invoked; false otherwise (for example for STI_OBJECT, STI_ARRAY
 *         or STI_NOTPRESENT).
 */
template <typename F>
bool
visitSTLeafType(SerializedTypeID id, F&& f)
{
    return [&]<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
        static_assert((STTyped<Ts> && ...));
        return ((id == Ts::kTypeId && (f.template operator()<Ts>(), true)) || ...);
    }(std::type_identity<STLeafTypes>{});
}

}  // namespace xrpl::detail
