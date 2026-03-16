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

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <string>
#include <string_view>
#include <utility>

namespace ripple {

// Storage for static const members.
SField::IsSigning const SField::notSigning;
int SField::num = 0;
std::map<int, SField const*> SField::knownCodeToField;

template <class T>
template <class... Args>
TypedField<T>::TypedField(Args&&... args) : SField(std::forward<Args>(args)...)
{
}

// Construct all compile-time SFields, and register them in the knownCodeToField
// database:

// Use macros for most SField construction to enforce naming conventions.
#pragma push_macro("UNTYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma push_macro("TYPED_SFIELD")
#undef TYPED_SFIELD

// This is a weird way to define these macros. Instead of passing in
// the text and using that to synthesize the name of the variable by
// using the token-pasting operator, and the field name by using the
// token pasting operator, we instantiate and manipulate string_view
// instances inside a macro.
#define UNTYPED_SFIELD(sfName, stiSuffix, fieldValue, ...)             \
    SField const sfName(                                               \
        Json::StaticString(true, std::string_view(#sfName).substr(2)), \
        STI_##stiSuffix,                                               \
        fieldValue,                                                    \
        ##__VA_ARGS__);

#define TYPED_SFIELD(sfName, stiSuffix, fieldValue, ...)               \
    SF_##stiSuffix const sfName(                                       \
        Json::StaticString(true, std::string_view(#sfName).substr(2)), \
        STI_##stiSuffix,                                               \
        fieldValue,                                                    \
        ##__VA_ARGS__);

// SFields which, for historical reasons, do not follow naming conventions.
SField const sfInvalid(-1);
SField const sfGeneric(0);

// The following two fields aren't used anywhere, but they break tests/have
// downstream effects.
SField const sfHash(Json::StaticString("hash"), STI_UINT256, 257);
SField const sfIndex(Json::StaticString("index"), STI_UINT256, 258);

#include <xrpl/protocol/detail/sfields.macro>

#undef TYPED_SFIELD
#pragma pop_macro("TYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma pop_macro("UNTYPED_SFIELD")

SField const&
SField::getField(int code)
{
    auto it = knownCodeToField.find(code);

    if (it != knownCodeToField.end())
    {
        return *(it->second);
    }
    return sfInvalid;
}

int
SField::compare(SField const& f1, SField const& f2)
{
    // -1 = f1 comes before f2, 0 = illegal combination, 1 = f1 comes after f2
    if ((f1.fieldCode <= 0) || (f2.fieldCode <= 0))
        return 0;

    if (f1.fieldCode < f2.fieldCode)
        return -1;

    if (f2.fieldCode < f1.fieldCode)
        return 1;

    return 0;
}

SField const&
SField::getField(std::string_view fieldName)
{
    for (auto const& [_, f] : knownCodeToField)
    {
        (void)_;
        if (f->fieldName == fieldName)
            return *f;
    }
    return sfInvalid;
}

}  // namespace ripple
