#include <xrpl/protocol/STObject.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/detail/STVar.h>
#include <xrpl/protocol/digest.h>

#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

// Returns the fields of an object in the order in which they will be
// serialized: sorted by field code, not by address.
[[nodiscard]] auto
getSortedFields(STObject const& object, bool includeSigningFields)
{
    // Instrumentation suggests 98% of objects have at most 12 fields and the
    // largest value observed was 17. Given the size of the object, leaving a
    // little headroom makes sense, so we use 24 as in-place capacity.
    boost::container::small_vector<STBase const*, 24> sf;

    for (STBase const& base : object)
    {
        if (base.getSType() != STI_NOTPRESENT &&
            base.getFName().shouldInclude(includeSigningFields))
        {
            sf.push_back(&base);
        }
    }

    std::ranges::sort(sf, [](STBase const* lhs, STBase const* rhs) {
        return lhs->getFName().fieldCodeMem < rhs->getFName().fieldCodeMem;
    });

    return sf;
}

}  // namespace

void
STObject::throwFieldError(std::string_view text)
{
    Throw<FieldErr>(std::string{text});
}

void
STObject::throwFieldError(std::string_view head, SField const& field)
{
    Throw<FieldErr>(std::format("{}: '{}'", head, field.getName()));
}

void
STObject::throwWrongFieldType()
{
    Throw<std::runtime_error>("Wrong field type");
}

void
STObject::throwFieldNotFound(SField const& field)
{
    Throw<std::runtime_error>("Field not found: " + field.getName());
}

STObject::STObject(STObject&& other) noexcept
    : STBase(other.getFName()), v_(std::move(other.v_)), type_(std::exchange(other.type_, nullptr))
{
}

STObject::STObject(SField const& name) : STBase(name)
{
}

STObject::STObject(SOTemplate const& type, SField const& name) : STBase(name)
{
    set(type);
}

STObject::STObject(
    SOTemplate const& type,
    SerialIter& sit,
    SField const& name,
    bool requireCanonicalOrder)
    : STBase(name)
{
    v_.reserve(type.size());
    set(sit, 0, requireCanonicalOrder);
    applyTemplate(type);  // May throw
}

STObject::STObject(SerialIter& sit, SField const& name, int depth) : STBase(name)
{
    if (depth > 10)
        Throw<std::runtime_error>("Maximum nesting depth of STObject exceeded");
    set(sit, depth);
}

STObject
STObject::makeInnerObject(SField const& name)
{
    STObject obj{name};

    // The if is complicated because inner object templates were added in
    // two phases:
    //  1. If there are no available Rules, then always apply the template.
    //  2. fixInnerObjTemplate2 added templates to all remaining inner objects.
    std::optional<Rules> const& rules = getCurrentTransactionRules();
    bool const isAMMObj = name == sfAuctionSlot || name == sfVoteEntry;
    if (!rules || isAMMObj || rules->enabled(fixInnerObjTemplate2))
    {
        if (SOTemplate const* elements =
                InnerObjectFormats::getInstance().findSOTemplateBySField(name))
            obj.set(*elements);
    }
    return obj;
}

STBase*
STObject::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STObject::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

bool
STObject::isDefault() const
{
    return v_.empty();
}

void
STObject::add(Serializer& s) const
{
    add(s, WhichFields::WithAllFields);  // just inner elements
}

STObject&
STObject::operator=(STObject&& other) noexcept
{
    if (this != &other)
    {
        setFName(other.getFName());
        type_ = std::exchange(other.type_, nullptr);
        v_ = std::move(other.v_);
    }

    return *this;
}

void
STObject::set(SOTemplate const& type)
{
    v_.clear();
    v_.reserve(type.size());
    type_ = &type;

    for (auto const& elem : type)
    {
        if (elem.style() != SoeRequired)
        {
            v_.emplace_back(detail::kNonPresentObject, elem.sField());
        }
        else
        {
            v_.emplace_back(detail::kDefaultObject, elem.sField());
        }
    }
}

void
STObject::applyTemplate(SOTemplate const& type)
{
    auto const throwFieldErr = [](std::string const& field, char const* description) {
        auto const text = std::format("Field '{}' {}", field, description);
        JLOG(debugLog().error()) << "STObject::applyTemplate failed: " << text;
        Throw<FieldErr>(text);
    };

    type_ = &type;

    ListType v;
    v.reserve(type.size());
    for (auto const& e : type)
    {
        auto const iter = std::ranges::find_if(
            v_, [&](detail::STVar const& b) { return b.get().getFName() == e.sField(); });
        if (iter != v_.end())
        {
            if ((e.style() == SoeDefault) && iter->get().isDefault())
            {
                throwFieldErr(e.sField().fieldName, "may not be explicitly set to default.");
            }
            v.emplace_back(std::move(*iter));
            v_.erase(iter);
        }
        else
        {
            if (e.style() == SoeRequired)
            {
                throwFieldErr(e.sField().fieldName, "is required but missing.");
            }
            v.emplace_back(detail::kNonPresentObject, e.sField());
        }
    }
    for (auto const& e : v_)
    {
        // Anything left over in the object must be discardable
        if (!e->getFName().isDiscardable())
        {
            throwFieldErr(e->getFName().getName(), "found in disallowed location.");
        }
    }
    // Swap the template matching data in for the old data,
    // freeing any leftover junk
    v_.swap(v);
}

void
STObject::applyTemplateFromSField(SField const& sField)
{
    SOTemplate const* elements = InnerObjectFormats::getInstance().findSOTemplateBySField(sField);
    if (elements != nullptr)
        applyTemplate(*elements);  // May throw
}

// return true = terminated with end-of-object
bool
STObject::set(SerialIter& sit, int depth, bool requireCanonicalOrder)
{
    bool reachedEndOfObject = false;

    v_.clear();

    std::optional<int> prevFieldCode;
    // Consume data in the pipe until we run out or reach the end
    while (!sit.empty())
    {
        int type = 0;
        int field = 0;

        // Get the metadata for the next field
        sit.getFieldID(type, field);

        // The object termination marker has been found and the termination
        // marker has been consumed. Done deserializing.
        if (type == STI_OBJECT && field == 1)
        {
            reachedEndOfObject = true;
            break;
        }

        if (type == STI_ARRAY && field == 1)
        {
            JLOG(debugLog().error()) << "Encountered object with embedded end-of-array marker";
            Throw<std::runtime_error>("Illegal end-of-array marker in object");
        }

        auto const& fn = SField::getField(type, field);
        if (fn.isInvalid())
        {
            JLOG(debugLog().error())
                << "Unknown field: field_type=" << type << ", field_name=" << field;
            Throw<std::runtime_error>("Unknown field");
        }

        if (requireCanonicalOrder && prevFieldCode.has_value() && fn.fieldCodeMem <= *prevFieldCode)
        {
            JLOG(debugLog().error()) << "Fields in object are not in canonical order";
            Throw<std::runtime_error>("Fields in object are not in canonical order");
        }
        prevFieldCode = fn.fieldCodeMem;

        // Unflatten the field
        v_.emplace_back(sit, fn, depth + 1);

        if (fn.fieldType == STI_OBJECT)
        {
            // If the object type has a known SOTemplate then set it.
            if (auto const obj = dynamic_cast<STObject*>(&v_.back().get()))
                obj->applyTemplateFromSField(fn);  // May throw
        }
    }

    // We want to ensure that the deserialized object does not contain any
    // duplicate fields. This is a key invariant:
    auto const sf = getSortedFields(*this, true);

    auto const dup = std::ranges::adjacent_find(sf, [](STBase const* lhs, STBase const* rhs) {
        return lhs->getFName() == rhs->getFName();
    });

    if (dup != sf.cend())
        Throw<std::runtime_error>("Duplicate field detected");

    return reachedEndOfObject;
}

bool
STObject::hasMatchingEntry(STBase const& t) const
{
    STBase const* o = peekAtPField(t.getFName());

    if (o == nullptr)
        return false;

    return t == *o;
}

bool
STObject::isEquivalent(STBase const& t) const
{
    auto const* v = dynamic_cast<STObject const*>(&t);

    if (v == nullptr)
        return false;

    if (type_ != nullptr && v->type_ == type_)
    {
        return std::ranges::equal(*this, *v, [](STBase const& st1, STBase const& st2) {
            return (st1.getSType() == st2.getSType()) && st1.isEquivalent(st2);
        });
    }

    auto const sf1 = getSortedFields(*this, true);
    auto const sf2 = getSortedFields(*v, true);

    return std::ranges::equal(sf1, sf2, [](STBase const* st1, STBase const* st2) {
        return (st1->getSType() == st2->getSType()) && st1->isEquivalent(*st2);
    });
}

UInt256
STObject::getHash(HashPrefix prefix) const
{
    Serializer s;
    s.add32(prefix);
    add(s, WhichFields::WithAllFields);
    return sha512Half(s);
}

UInt256
STObject::getSigningHash(HashPrefix prefix) const
{
    Serializer s;
    s.add32(prefix);
    add(s, WhichFields::OmitSigningFields);
    return sha512Half(s);
}

int
STObject::getFieldIndex(SField const& field) const
{
    if (type_ != nullptr)
        return type_->getIndex(field);

    int i = 0;
    for (auto const& elem : v_)
    {
        if (elem->getFName() == field)
            return i;
        ++i;
    }
    return -1;
}

STBase const&
STObject::peekAtField(SField const& field) const
{
    int const index = getFieldIndex(field);

    if (index == -1)
        throwFieldNotFound(field);

    return peekAtIndex(index);
}

STBase&
STObject::getField(SField const& field)
{
    int const index = getFieldIndex(field);

    if (index == -1)
        throwFieldNotFound(field);

    return getIndex(index);
}

STBase const*
STObject::peekAtPField(SField const& field) const
{
    int const index = getFieldIndex(field);

    if (index == -1)
        return nullptr;

    return peekAtPIndex(index);
}

STBase*
STObject::getPField(SField const& field, bool createOkay)
{
    int const index = getFieldIndex(field);

    if (index == -1)
    {
        if (createOkay && isFree())
            return getPIndex(emplaceBack(detail::kDefaultObject, field));

        return nullptr;
    }

    return getPIndex(index);
}

bool
STObject::isFieldPresent(SField const& field) const
{
    int const index = getFieldIndex(field);

    if (index == -1)
        return false;

    return peekAtIndex(index).getSType() != STI_NOTPRESENT;
}

STObject&
STObject::peekFieldObject(SField const& field)
{
    return peekField<STObject>(field);
}

STArray&
STObject::peekFieldArray(SField const& field)
{
    return peekField<STArray>(field);
}

bool
STObject::setFlag(std::uint32_t f)
{
    if (auto* t = dynamic_cast<STUInt32*>(getPField(sfFlags, true)))
    {
        t->setValue(t->value() | f);
        return true;
    }

    return false;
}

bool
STObject::clearFlag(std::uint32_t f)
{
    if (auto* t = dynamic_cast<STUInt32*>(getPField(sfFlags)))
    {
        t->setValue(t->value() & ~f);
        return true;
    }

    return false;
}

bool
STObject::isFlag(std::uint32_t f) const
{
    return (getFlags() & f) == f;
}

std::uint32_t
STObject::getFlags() const
{
    if (auto const* t = dynamic_cast<STUInt32 const*>(peekAtPField(sfFlags)))
        return t->value();

    return 0;
}

STBase*
STObject::makeFieldPresent(SField const& field)
{
    int const index = getFieldIndex(field);

    if (index == -1)
    {
        if (!isFree())
            throwFieldNotFound(field);

        return getPIndex(emplaceBack(detail::kNonPresentObject, field));
    }

    STBase* f = getPIndex(index);  // NOLINT(misc-const-correctness)

    if (f->getSType() != STI_NOTPRESENT)
        return f;

    v_[index] = detail::STVar(detail::kDefaultObject, f->getFName());
    return getPIndex(index);
}

void
STObject::makeFieldAbsent(SField const& field)
{
    int const index = getFieldIndex(field);

    if (index == -1)
        throwFieldNotFound(field);

    if (STBase const& f = peekAtIndex(index); f.getSType() != STI_NOTPRESENT)
        v_[index] = detail::STVar(detail::kNonPresentObject, f.getFName());
}

bool
STObject::delField(SField const& field)
{
    if (int const index = getFieldIndex(field); index != -1)
    {
        delField(index);
        return true;
    }

    return false;
}

void
STObject::delField(int index)
{
    v_.erase(v_.begin() + index);
}

SOEStyle
STObject::getStyle(SField const& field) const
{
    return (type_ != nullptr) ? type_->style(field) : SoeInvalid;
}

unsigned char
STObject::getFieldU8(SField const& field) const
{
    return getFieldByValue<STUInt8>(field);
}

std::uint16_t
STObject::getFieldU16(SField const& field) const
{
    return getFieldByValue<STUInt16>(field);
}

std::uint32_t
STObject::getFieldU32(SField const& field) const
{
    return getFieldByValue<STUInt32>(field);
}

std::uint64_t
STObject::getFieldU64(SField const& field) const
{
    return getFieldByValue<STUInt64>(field);
}

UInt128
STObject::getFieldH128(SField const& field) const
{
    return getFieldByValue<STUInt128>(field);
}

UInt160
STObject::getFieldH160(SField const& field) const
{
    return getFieldByValue<STUInt160>(field);
}

UInt192
STObject::getFieldH192(SField const& field) const
{
    return getFieldByValue<STUInt192>(field);
}

UInt256
STObject::getFieldH256(SField const& field) const
{
    return getFieldByValue<STUInt256>(field);
}

std::int32_t
STObject::getFieldI32(SField const& field) const
{
    return getFieldByValue<STInt32>(field);
}

AccountID
STObject::getAccountID(SField const& field) const
{
    return getFieldByValue<STAccount>(field);
}

Slice
STObject::getFieldVL(SField const& field) const
{
    return getFieldByValue<STBlob>(field);
}

STAmount const&
STObject::getFieldAmount(SField const& field) const
{
    static STAmount const kEmpty{};
    return getFieldByConstRef<STAmount>(field, kEmpty);
}

STPathSet const&
STObject::getFieldPathSet(SField const& field) const
{
    static STPathSet const kEmpty{};
    return getFieldByConstRef<STPathSet>(field, kEmpty);
}

STVector256 const&
STObject::getFieldV256(SField const& field) const
{
    static STVector256 const kEmpty{};
    return getFieldByConstRef<STVector256>(field, kEmpty);
}

STObject
STObject::getFieldObject(SField const& field) const
{
    if (auto const* obj = peekAtTypedField<STObject>(field))
    {
        STObject ret{*obj};

        if (ret.isFree() && !ret.empty())
            ret.applyTemplateFromSField(field);

        return ret;
    }

    return STObject{field};
}

STArray const&
STObject::getFieldArray(SField const& field) const
{
    static STArray const kEmpty{};
    return getFieldByConstRef<STArray>(field, kEmpty);
}

STCurrency const&
STObject::getFieldCurrency(SField const& field) const
{
    static STCurrency const kEmpty{};
    return getFieldByConstRef<STCurrency>(field, kEmpty);
}

STNumber const&
STObject::getFieldNumber(SField const& field) const
{
    static STNumber const kEmpty{};
    return getFieldByConstRef<STNumber>(field, kEmpty);
}

void
STObject::set(std::unique_ptr<STBase> v)
{
    set(std::move(*v));
}

void
STObject::set(STBase&& v)
{
    auto const i = getFieldIndex(v.getFName());
    if (i != -1)
    {
        v_[i] = std::move(v);
    }
    else
    {
        if (!isFree())
            Throw<std::runtime_error>("missing field in templated STObject");
        v_.emplace_back(std::move(v));
    }
}

void
STObject::setFieldU8(SField const& field, unsigned char v)
{
    setFieldUsingSetValue<STUInt8>(field, v);
}

void
STObject::setFieldU16(SField const& field, std::uint16_t v)
{
    setFieldUsingSetValue<STUInt16>(field, v);
}

void
STObject::setFieldU32(SField const& field, std::uint32_t v)
{
    setFieldUsingSetValue<STUInt32>(field, v);
}

void
STObject::setFieldU64(SField const& field, std::uint64_t v)
{
    setFieldUsingSetValue<STUInt64>(field, v);
}

void
STObject::setFieldH128(SField const& field, UInt128 const& v)
{
    setFieldUsingSetValue<STUInt128>(field, v);
}

void
STObject::setFieldH192(SField const& field, UInt192 const& v)
{
    setFieldUsingSetValue<STUInt192>(field, v);
}

void
STObject::setFieldH256(SField const& field, UInt256 const& v)
{
    setFieldUsingSetValue<STUInt256>(field, v);
}

void
STObject::setFieldI32(SField const& field, std::int32_t v)
{
    setFieldUsingSetValue<STInt32>(field, v);
}

void
STObject::setFieldV256(SField const& field, STVector256 const& v)
{
    setFieldUsingSetValue<STVector256>(field, v);
}

void
STObject::setAccountID(SField const& field, AccountID const& v)
{
    setFieldUsingSetValue<STAccount>(field, v);
}

void
STObject::setFieldVL(SField const& field, Blob const& v)
{
    setFieldUsingSetValue<STBlob>(field, Buffer(v.data(), v.size()));
}

void
STObject::setFieldVL(SField const& field, Slice s)
{
    setFieldUsingSetValue<STBlob>(field, Buffer(s.data(), s.size()));
}

void
STObject::setFieldAmount(SField const& field, STAmount const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldCurrency(SField const& field, STCurrency const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldIssue(SField const& field, STIssue const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldNumber(SField const& field, STNumber const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldPathSet(SField const& field, STPathSet const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldArray(SField const& field, STArray const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldObject(SField const& field, STObject const& v)
{
    setFieldUsingAssignment(field, v);
}

json::Value
STObject::getJson(JsonOptions options) const
{
    json::Value ret(json::ValueType::Object);

    for (auto const& elem : v_)
    {
        if (elem->getSType() != STI_NOTPRESENT)
            ret[elem->getFName().getJsonName()] = elem->getJson(options);
    }
    return ret;
}

bool
STObject::operator==(STObject const& obj) const
{
    // This is not particularly efficient, and only compares data elements
    // with binary representations
    int matches = 0;
    for (auto const& t1 : v_)
    {
        if ((t1->getSType() != STI_NOTPRESENT) && t1->getFName().isBinary())
        {
            // each present field must have a matching field
            bool match = false;
            for (auto const& t2 : obj.v_)
            {
                if (t1->getFName() == t2->getFName())
                {
                    if (t2 != t1)
                        return false;

                    match = true;
                    ++matches;
                    break;
                }
            }

            if (!match)
                return false;
        }
    }

    int fields = 0;
    for (auto const& t2 : obj.v_)
    {
        if ((t2->getSType() != STI_NOTPRESENT) && t2->getFName().isBinary())
            ++fields;
    }

    return fields == matches;
}

void
STObject::add(Serializer& s, WhichFields whichFields) const
{
    // Depending on whichFields, signing fields are either serialized or
    // not.  Then fields are added to the Serializer sorted by fieldCode.
    for (STBase const* const field :
         getSortedFields(*this, whichFields == WhichFields::WithAllFields))
    {
        // When we serialize an object inside another object,
        // the type associated by rule with this field name
        // must be OBJECT, or the object cannot be deserialized
        SerializedTypeID const sType{field->getSType()};
        XRPL_ASSERT(
            (sType != STI_OBJECT) || (field->getFName().fieldType == STI_OBJECT),
            "xrpl::STObject::add : valid field type");
        XRPL_ASSERT(
            getStyle(field->getFName()) != SoeDefault || !field->isDefault(),
            "xrpl::STObject::add : non-default value");

        field->addFieldID(s);
        field->add(s);
        if (sType == STI_ARRAY || sType == STI_OBJECT)
            s.addFieldID(sType, 1);
    }
}

[[nodiscard]] std::string
STObject::getFullText() const
{
    std::string ret;

    if (getFName().hasName())
    {
        ret = getFName().getName();
        ret += " = {";
    }
    else
    {
        ret = "{";
    }

    ret += rangeToString(v_, [](detail::STVar const& object) { return object->getFullText(); });

    return ret + "}";
}

[[nodiscard]] std::string
STObject::getText() const
{
    return "{" + rangeToString(v_, [](detail::STVar const& object) { return object->getText(); }) +
        "}";
}

}  // namespace xrpl
