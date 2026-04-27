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

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/detail/STVar.h>

#include <charconv>
#include <memory>

namespace ripple {

namespace STParsedJSONDetail {
template <typename U, typename S>
constexpr std::
    enable_if_t<std::is_unsigned<U>::value && std::is_signed<S>::value, U>
    to_unsigned(S value)
{
    if (value < 0 || std::numeric_limits<U>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U>(value);
}

template <typename U1, typename U2>
constexpr std::
    enable_if_t<std::is_unsigned<U1>::value && std::is_unsigned<U2>::value, U1>
    to_unsigned(U2 value)
{
    if (std::numeric_limits<U1>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U1>(value);
}

static std::string
make_name(std::string_view object, std::string_view field = {})
{
    std::string ret{object};

    if (!field.empty())
    {
        ret += ".";
        ret += field;
    }

    return ret;
}

static Json::Value
not_an_object(std::string_view object, std::string_view field = {})
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' is not a JSON object.");
}

static Json::Value
not_an_array(std::string_view object)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object) + "' is not a JSON array.");
}

static Json::Value
unknown_field(std::string_view object, std::string_view field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' is unknown.");
}

static Json::Value
out_of_range(std::string_view object, std::string_view field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' is out of range.");
}

static Json::Value
bad_type(std::string_view object, std::string_view field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' has bad type.");
}

static Json::Value
invalid_data(std::string_view object, std::string_view field = {})
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' has invalid data.");
}

static Json::Value
array_expected(std::string_view object, std::string_view field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' must be a JSON array.");
}

static Json::Value
string_expected(std::string_view object, std::string_view field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' must be a string.");
}

static Json::Value
too_deep(std::string_view object)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object) + "' exceeds nesting depth limit.");
}

static Json::Value
singleton_expected(std::string_view object, unsigned int index)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object) + "[" + std::to_string(index) +
            "]' must be an object with a single key/object value.");
}

static Json::Value
template_mismatch(SField const& f)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Object '" + to_string(f) +
            "' contents did not meet requirements for that type.");
}

static Json::Value
non_object_in_array(std::string_view item, Json::UInt index)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Item '" + make_name(item) + "' at index " + std::to_string(index) +
            " is not an object.  Arrays may only contain objects.");
}

template <typename ST>
static std::optional<detail::STVar>
parseBitString(
    Json::Value const& value,
    SField const& field,
    std::string_view json_name,
    std::string_view fieldName,
    Json::Value& error)
{
    XRPL_ASSERT(
        field.fieldType == ST::type_id,
        "ripple::parseBitString : field type matches template parameter");

    if (!value.isString())
    {
        error = bad_type(json_name, fieldName);
        return std::nullopt;
    }

    auto const s = value.asString();

    if (typename ST::value_type num; s.empty() || num.parseHex(s))
        return detail::make_stvar<ST>(field, num);

    error = invalid_data(json_name, fieldName);
    return std::nullopt;
}

// This function is used by parseObject to parse any JSON type that doesn't
// recurse.  Everything represented here is a leaf-type.
static std::optional<detail::STVar>
parseLeaf(
    std::string_view json_name,
    std::string_view fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;

    auto const& field = SField::getField(fieldName);

    if (field == sfInvalid)
    {
        error = unknown_field(json_name, fieldName);
        return ret;
    }

    try
    {
        switch (field.fieldType)
        {
            case STI_UINT8: {
                if (auto val = to_integer<std::uint8_t>(value))
                {
                    ret = detail::make_stvar<STUInt8>(field, *val);
                    break;
                }

                // sfTransactionResult can be specified by name
                if (field == sfTransactionResult && value.isString())
                {
                    if (auto sv = value.asStringView(); !sv.empty())
                    {
                        auto ter = transCode(sv);

                        if (!ter ||
                            !std::in_range<std::uint8_t>(TERtoInt(*ter)))
                        {
                            error = out_of_range(json_name, fieldName);
                            return ret;
                        }

                        ret = detail::make_stvar<STUInt8>(
                            field, static_cast<std::uint8_t>(TERtoInt(*ter)));

                        break;
                    }
                }

                error = invalid_data(json_name, fieldName);
                return ret;
            }

            case STI_UINT16: {
                if (auto val = to_integer<std::uint16_t>(value))
                {
                    ret = detail::make_stvar<STUInt16>(field, *val);
                    break;
                }

                // For user convenience, we allow the sfTransactionType and
                // sfLedgerEntryType fields to be specified by the names of
                // their transaction or ledger entry types.
                if (value.isString())
                {
                    auto const str = value.asStringView();

                    if (field == sfTransactionType)
                    {
                        ret = detail::make_stvar<STUInt16>(
                            field,
                            static_cast<std::uint16_t>(
                                TxFormats::getInstance().findTypeByName(str)));

                        if (*name == sfGeneric)
                            name = &sfTransaction;

                        break;
                    }

                    if (field == sfLedgerEntryType)
                    {
                        ret = detail::make_stvar<STUInt16>(
                            field,
                            static_cast<std::uint16_t>(
                                LedgerFormats::getInstance().findTypeByName(
                                    str)));

                        if (*name == sfGeneric)
                            name = &sfLedgerEntry;

                        break;
                    }
                }

                error = invalid_data(json_name, fieldName);
                return ret;
            }

            case STI_UINT32: {
                if (auto val = to_integer<std::uint32_t>(value))
                {
                    ret = detail::make_stvar<STUInt32>(field, *val);
                    break;
                }

                error = invalid_data(json_name, fieldName);
                return ret;
            }

            case STI_UINT64: {
                if (value.isString())
                {
                    auto const str = value.asString();
                    std::uint64_t val;

                    auto [p, ec] = std::from_chars(
                        str.data(),
                        str.data() + str.size(),
                        val,
                        field.shouldMeta(SField::sMD_BaseTen) ? 10 : 16);

                    if (ec == std::errc() && p == str.data() + str.size())
                    {
                        ret = detail::make_stvar<STUInt64>(field, val);
                        break;
                    }
                }
                else if (value.isInt())
                {
                    if (auto const raw = value.asInt();
                        std::in_range<std::uint64_t>(raw))
                    {
                        ret = detail::make_stvar<STUInt64>(
                            field, static_cast<std::uint64_t>(raw));
                        break;
                    }
                }
                else if (value.isUInt())
                {
                    ret = detail::make_stvar<STUInt64>(
                        field, static_cast<std::uint64_t>(value.asUInt()));
                    break;
                }

                error = invalid_data(json_name, fieldName);
                return ret;
            }

            case STI_UINT96:
                ret = parseBitString<STUInt96>(
                    value, field, json_name, fieldName, error);
                break;

            case STI_UINT128:
                ret = parseBitString<STUInt128>(
                    value, field, json_name, fieldName, error);
                break;

            case STI_UINT160:
                ret = parseBitString<STUInt160>(
                    value, field, json_name, fieldName, error);
                break;

            case STI_UINT192:
                ret = parseBitString<STUInt192>(
                    value, field, json_name, fieldName, error);
                break;

            case STI_UINT256:
                ret = parseBitString<STUInt256>(
                    value, field, json_name, fieldName, error);
                break;

            case STI_UINT384:
                ret = parseBitString<STUInt384>(
                    value, field, json_name, fieldName, error);
                break;

            case STI_UINT512:
                ret = parseBitString<STUInt512>(
                    value, field, json_name, fieldName, error);
                break;

            case STI_VL: {
                if (!value.isString())
                {
                    error = bad_type(json_name, fieldName);
                    return ret;
                }

                auto vBlob = strUnHex(value.asString());
                if (!vBlob)
                    Throw<std::invalid_argument>("invalid data");

                ret = detail::make_stvar<STBlob>(
                    field, vBlob->data(), vBlob->size());
                break;
            }

            case STI_AMOUNT:
                ret =
                    detail::make_stvar<STAmount>(amountFromJson(field, value));
                break;

            case STI_VECTOR256: {
                if (!value.isArrayOrNull())
                {
                    error = array_expected(json_name, fieldName);
                    return ret;
                }

                STVector256 tail(field);
                for (auto const& v : value)
                {
                    uint256 s;
                    if (!s.parseHex(v.asString()))
                        Throw<std::invalid_argument>("invalid data");
                    tail.push_back(s);
                }
                ret = detail::make_stvar<STVector256>(std::move(tail));
                break;
            }

            case STI_PATHSET: {
                if (!value.isArrayOrNull())
                {
                    error = array_expected(json_name, fieldName);
                    return ret;
                }

                STPathSet tail(field);

                for (auto const& path : value)
                {
                    if (!path.isArrayOrNull())
                    {
                        error = array_expected(
                            json_name,
                            std::string(fieldName) + "[" +
                                std::to_string(tail.size()) + "]");
                        return ret;
                    }

                    STPath p;

                    for (auto const& step : path)
                    {
                        std::string const element_name =
                            std::string(json_name) + "." +
                            std::string(fieldName) + "[" +
                            std::to_string(tail.size()) + "][" +
                            std::to_string(p.size()) + "]";

                        if (!step.isObject())
                        {
                            error = not_an_object(element_name);
                            return ret;
                        }

                        Json::Value const& account = step["account"];
                        Json::Value const& currency = step["currency"];
                        Json::Value const& issuer = step["issuer"];
                        bool hasCurrency = false;
                        AccountID uAccount, uIssuer;
                        Currency uCurrency;

                        if (account)
                        {
                            if (!account.isString())
                            {
                                error =
                                    string_expected(element_name, "account");
                                return ret;
                            }

                            if (!uAccount.parseHex(account.asString()))
                            {
                                auto const a =
                                    parseBase58<AccountID>(account.asString());
                                if (!a)
                                {
                                    error =
                                        invalid_data(element_name, "account");
                                    return ret;
                                }
                                uAccount = *a;
                            }
                        }

                        if (currency)
                        {
                            if (!currency.isString())
                            {
                                error =
                                    string_expected(element_name, "currency");
                                return ret;
                            }

                            hasCurrency = true;

                            if (!uCurrency.parseHex(currency.asString()))
                            {
                                if (!to_currency(
                                        uCurrency, currency.asString()))
                                {
                                    error =
                                        invalid_data(element_name, "currency");
                                    return ret;
                                }
                            }
                        }

                        if (issuer)
                        {
                            if (!issuer.isString())
                            {
                                error = string_expected(element_name, "issuer");
                                return ret;
                            }

                            if (!uIssuer.parseHex(issuer.asString()))
                            {
                                auto const a =
                                    parseBase58<AccountID>(issuer.asString());
                                if (!a)
                                {
                                    error =
                                        invalid_data(element_name, "issuer");
                                    return ret;
                                }
                                uIssuer = *a;
                            }
                        }

                        p.emplace_back(
                            uAccount, uCurrency, uIssuer, hasCurrency);
                    }

                    tail.push_back(p);
                }
                ret = detail::make_stvar<STPathSet>(std::move(tail));
                break;
            }

            case STI_ACCOUNT: {
                if (!value.isString())
                {
                    error = bad_type(json_name, fieldName);
                    return ret;
                }

                std::string const strValue = value.asString();

                if (AccountID account; account.parseHex(strValue))
                    return detail::make_stvar<STAccount>(field, account);

                if (auto result = parseBase58<AccountID>(strValue))
                    return detail::make_stvar<STAccount>(field, *result);

                error = invalid_data(json_name, fieldName);
                return ret;
            }

            case STI_ISSUE:
                try
                {
                    ret = detail::make_stvar<STIssue>(
                        issueFromJson(field, value));
                }
                catch (std::exception const&)
                {
                    error = invalid_data(json_name, fieldName);
                    return ret;
                }
                break;

            case STI_XCHAIN_BRIDGE:
                try
                {
                    ret = detail::make_stvar<STXChainBridge>(
                        STXChainBridge(field, value));
                }
                catch (std::exception const&)
                {
                    error = invalid_data(json_name, fieldName);
                    return ret;
                }
                break;

            case STI_CURRENCY:
                try
                {
                    ret = detail::make_stvar<STCurrency>(
                        currencyFromJson(field, value));
                }
                catch (std::exception const&)
                {
                    error = invalid_data(json_name, fieldName);
                    return ret;
                }
                break;

            default:
                error = bad_type(json_name, fieldName);
                return ret;
        }
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return ret;
    }

    return ret;
}

static const int maxDepth = 64;

// Forward declaration since parseObject() and parseArray() call each other.
static std::optional<detail::STVar>
parseArray(
    std::string_view json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error);

static std::optional<STObject>
parseObject(
    std::string_view json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (!json.isObjectOrNull())
    {
        error = not_an_object(json_name);
        return std::nullopt;
    }

    if (depth > maxDepth)
    {
        error = too_deep(json_name);
        return std::nullopt;
    }

    try
    {
        STObject data(inName);

        for (auto const& fieldName : json.getMemberNames())
        {
            Json::Value const& value = json[fieldName];

            auto const& field = SField::getField(fieldName);

            if (field == sfInvalid)
            {
                error = unknown_field(json_name, fieldName);
                return std::nullopt;
            }

            switch (field.fieldType)
            {
                // Object-style containers (which recurse).
                case STI_OBJECT:
                case STI_TRANSACTION:
                case STI_LEDGERENTRY:
                case STI_VALIDATION:
                    if (!value.isObject())
                    {
                        error = not_an_object(json_name, fieldName);
                        return std::nullopt;
                    }

                    try
                    {
                        auto ret = parseObject(
                            make_name(json_name, fieldName),
                            value,
                            field,
                            depth + 1,
                            error);
                        if (!ret)
                            return std::nullopt;
                        data.emplace_back(std::move(*ret));
                    }
                    catch (std::exception const&)
                    {
                        error = invalid_data(json_name, fieldName);
                        return std::nullopt;
                    }

                    break;

                // Array-style containers (which recurse).
                case STI_ARRAY:
                    try
                    {
                        auto array = parseArray(
                            make_name(json_name, fieldName),
                            value,
                            field,
                            depth + 1,
                            error);
                        if (!array.has_value())
                            return std::nullopt;
                        data.emplace_back(std::move(*array));
                    }
                    catch (std::exception const&)
                    {
                        error = invalid_data(json_name, fieldName);
                        return std::nullopt;
                    }

                    break;

                // Everything else (types that don't recurse).
                default: {
                    auto leaf =
                        parseLeaf(json_name, fieldName, &inName, value, error);

                    if (!leaf)
                        return std::nullopt;

                    data.emplace_back(std::move(*leaf));
                }

                break;
            }
        }

        // Some inner object types have templates.  Attempt to apply that.
        data.applyTemplateFromSField(inName);  // May throw

        return data;
    }
    catch (STObject::FieldErr const& e)
    {
        error = template_mismatch(inName);
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name);
    }
    return std::nullopt;
}

static std::optional<detail::STVar>
parseArray(
    std::string_view json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (!json.isArrayOrNull())
    {
        error = not_an_array(json_name);
        return std::nullopt;
    }

    if (depth > maxDepth)
    {
        error = too_deep(json_name);
        return std::nullopt;
    }

    try
    {
        STArray tail(inName);

        for (auto const& elem : json)
        {
            if (!elem.isObject() || elem.size() != 1)
            {
                error = singleton_expected(json_name, tail.size());
                return std::nullopt;
            }

            auto it = elem.cbegin();
            XRPL_ASSERT(it != elem.cend(), "expected non-empty array");

            auto const& nameField = SField::getField(it.memberName());

            if (nameField == sfInvalid)
            {
                error = unknown_field(json_name, it.memberName());
                return std::nullopt;
            }

            std::string const name = std::string(json_name) + ".[" +
                std::to_string(tail.size()) + "]." + it.memberName();

            auto ret = parseObject(name, *it, nameField, depth + 1, error);
            if (!ret)
            {
                error["error_message"] = "Error at '" + name + "'. " +
                    error["error_message"].asString();
                return std::nullopt;
            }

            if (ret->getFName().fieldType != STI_OBJECT)
            {
                error = non_object_in_array(name, tail.size());
                return std::nullopt;
            }

            tail.push_back(std::move(*ret));
        }

        return detail::make_stvar<STArray>(std::move(tail));
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name);
        return std::nullopt;
    }
}

}  // namespace STParsedJSONDetail

//------------------------------------------------------------------------------

STParsedJSONObject::STParsedJSONObject(
    std::string const& name,
    Json::Value const& json)
{
    using namespace STParsedJSONDetail;
    object = parseObject(name, json, sfGeneric, 0, error);
}

//------------------------------------------------------------------------------

STParsedJSONArray::STParsedJSONArray(
    std::string const& name,
    Json::Value const& json)
{
    using namespace STParsedJSONDetail;
    auto arr = parseArray(name, json, sfGeneric, 0, error);
    if (!arr)
        array.reset();
    else
    {
        auto p = dynamic_cast<STArray*>(&arr->get());
        if (p == nullptr)
            array.reset();
        else
            array = std::move(*p);
    }
}

}  // namespace ripple
