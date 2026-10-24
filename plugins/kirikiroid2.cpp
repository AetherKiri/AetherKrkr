#include "TextStream.h"
#include "ncbind.hpp"
#include "kirikiroid2Compat.h"

#include <boost/locale/encoding.hpp>
#include <limits>
#include <string>
#include <utility>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("kirikiroid2.dll")

namespace {

tjs_error TJS_INTF_METHOD krkrStrOrd(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    if(param[0]->Type() != tvtString) {
        if(result)
            *result = *param[0];
        return TJS_S_OK;
    }

    const tjs_char *text = param[0]->GetString();
    if(result)
        *result = text && *text ? static_cast<tjs_int>(*text) : 0;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD krkrSetTextEncoding(tTJSVariant *, tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    TVPSetDefaultReadEncoding(ttstr(*param[0]));
    return TJS_S_OK;
}

std::string encodingName(const ttstr &value) {
    ttstr lower(value);
    lower.ToLowerCase();
    if(lower == TJS_W("sjis") || lower == TJS_W("shift_jis") ||
       lower == TJS_W("shift-jis") || lower == TJS_W("shiftjis"))
        return "CP932";
    if(lower == TJS_W("gb2312") || lower == TJS_W("cp936"))
        return "GBK";
    if(lower == TJS_W("utf16") || lower == TJS_W("utf-16"))
        return "UTF-16LE";
    if(lower == TJS_W("utf8"))
        return "UTF-8";
    return value.AsStdString();
}

tjs_error stringEncode(tTJSVariant *result, tjs_int count,
                        tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 1)
        return TJS_E_BADPARAMCOUNT;
    const ttstr text(*args[0]);
    const auto encoding = count > 1 && args[1]->Type() != tvtVoid
        ? encodingName(ttstr(*args[1])) : std::string("UTF-8");
    try {
        const auto wide = boost::locale::conv::utf_to_utf<wchar_t>(
            text.c_str(), text.c_str() + text.GetLen(), boost::locale::conv::stop);
        const auto bytes = boost::locale::conv::from_utf(
            wide.data(), wide.data() + wide.size(), encoding,
            boost::locale::conv::stop);
        if(bytes.size() > std::numeric_limits<tjs_uint>::max())
            return TJS_E_INVALIDPARAM;
        if(result) {
            auto *octet = TJSAllocVariantOctet(
                reinterpret_cast<const tjs_uint8 *>(bytes.data()),
                static_cast<tjs_uint>(bytes.size()));
            *result = octet;
            if(octet) octet->Release();
        }
        return TJS_S_OK;
    } catch(const boost::locale::conv::conversion_error &) {
        return TJS_E_INVALIDPARAM;
    } catch(const boost::locale::conv::invalid_charset_error &) {
        return TJS_E_INVALIDPARAM;
    }
}

tjs_error stringDecode(tTJSVariant *result, tjs_int count,
                        tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 1)
        return TJS_E_BADPARAMCOUNT;
    auto *octet = args[0]->AsOctetNoAddRef();
    if(!octet || !octet->GetLength()) {
        if(result) *result = TJS_W("");
        return TJS_S_OK;
    }
    const auto *bytes = reinterpret_cast<const char *>(octet->GetData());
    const auto *end = bytes + octet->GetLength();
    std::string encoding = "UTF-8";
    if(count > 1 && args[1]->Type() != tvtVoid &&
       !ttstr(*args[1]).IsEmpty()) {
        encoding = encodingName(ttstr(*args[1]));
    } else if(end - bytes >= 2 &&
              static_cast<unsigned char>(bytes[0]) == 0xff &&
              static_cast<unsigned char>(bytes[1]) == 0xfe) {
        encoding = "UTF-16LE";
        bytes += 2;
    } else if(end - bytes >= 2 &&
              static_cast<unsigned char>(bytes[0]) == 0xfe &&
              static_cast<unsigned char>(bytes[1]) == 0xff) {
        encoding = "UTF-16BE";
        bytes += 2;
    } else if(end - bytes >= 3 &&
              static_cast<unsigned char>(bytes[0]) == 0xef &&
              static_cast<unsigned char>(bytes[1]) == 0xbb &&
              static_cast<unsigned char>(bytes[2]) == 0xbf) {
        bytes += 3;
    }
    try {
        const auto wide = boost::locale::conv::to_utf<wchar_t>(
            bytes, end, encoding, boost::locale::conv::stop);
        const auto text = boost::locale::conv::utf_to_utf<tjs_char>(
            wide, boost::locale::conv::stop);
        if(result) *result = ttstr(text);
        return TJS_S_OK;
    } catch(const boost::locale::conv::conversion_error &) {
        return TJS_E_INVALIDPARAM;
    } catch(const boost::locale::conv::invalid_charset_error &) {
        return TJS_E_INVALIDPARAM;
    }
}

void returnPair(tTJSVariant *result, tjs_error status,
                const tTJSVariant &value) {
    if(!result)
        return;
    iTJSDispatch2 *array = TJSCreateArrayObject();
    const tTJSVariant code(static_cast<tjs_int>(status));
    array->PropSetByNum(TJS_MEMBERENSURE, 0, &code, array);
    array->PropSetByNum(TJS_MEMBERENSURE, 1, &value, array);
    *result = tTJSVariant(array, array);
    array->Release();
}

// Kirikiroid's Object helpers expose the dispatch ABI, including its status
// values and numeric member dispatch. Scripts.propGet/propSet use a different
// calling convention and cannot serve as aliases for these entry points.
struct Member {
    ttstr name;
    tjs_int index = 0;
    bool numeric = false;
    bool unnamed = false;

    explicit Member(const tTJSVariant &value) {
        numeric = value.Type() == tvtInteger || value.Type() == tvtReal;
        unnamed = value.Type() == tvtVoid;
        if(value.Type() == tvtOctet)
            TJSThrowFrom_tjs_error(TJS_E_INVALIDPARAM, nullptr);
        if(numeric) index = static_cast<tjs_int>(value.AsInteger());
        else if(!unnamed) name = ttstr(value);
    }
    const tjs_char *text() const { return unnamed ? nullptr : name.c_str(); }
};

tjs_error objectContext(tTJSVariant *result, tjs_int count,
                         tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 1) return TJS_E_BADPARAMCOUNT;
    const auto object = args[0]->AsObjectClosureNoAddRef();
    if(result) *result = tTJSVariant(object.ObjThis, object.ObjThis);
    return TJS_S_OK;
}

tjs_error objectHasMember(tTJSVariant *result, tjs_int count,
                           tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 2) return TJS_E_BADPARAMCOUNT;
    const auto object = args[0]->AsObjectClosureNoAddRef();
    const bool numeric = args[1]->Type() == tvtInteger;
    const ttstr name = numeric ? ttstr() : ttstr(*args[1]);
    const tjs_uint32 flags = count > 2
        ? static_cast<tjs_uint32>(args[2]->AsInteger())
        : TJS_MEMBERMUSTEXIST;
    tTJSVariant value;
    const auto status = numeric
        ? object.PropGetByNum(flags, static_cast<tjs_int>(args[1]->AsInteger()),
                               &value, nullptr)
        : object.PropGet(flags, name.c_str(), nullptr, &value, nullptr);
    if(status != TJS_E_MEMBERNOTFOUND && TJS_FAILED(status))
        return status;
    if(result) *result = status != TJS_E_MEMBERNOTFOUND;
    return TJS_S_OK;
}

enum class DispatchOperation { Call, Get, Set, Count, Delete, Invalidate,
                                Valid, InstanceOf };

template<DispatchOperation operation>
tjs_error objectDispatch(tTJSVariant *result, tjs_int count,
                          tTJSVariant **args, iTJSDispatch2 *) {
    constexpr bool needsValue = operation == DispatchOperation::Set ||
                                 operation == DispatchOperation::InstanceOf;
    if(count < (needsValue ? 4 : 3)) return TJS_E_BADPARAMCOUNT;
    const auto object = args[0]->AsObjectClosureNoAddRef();
    if(!object.Object) return TJS_E_INVALIDOBJECT;
    const auto flags = static_cast<tjs_uint32>(args[1]->AsInteger());
    const Member member(*args[2]);
    tTJSVariant value;
    tjs_error status;
    if constexpr(operation == DispatchOperation::Call) {
        status = member.numeric
            ? object.FuncCallByNum(flags, member.index, &value,
                                    count - 3, args + 3, nullptr)
            : object.FuncCall(flags, member.text(), nullptr, &value,
                              count - 3, args + 3, nullptr);
        returnPair(result, status, value);
    } else if constexpr(operation == DispatchOperation::Get) {
        status = member.numeric
            ? object.PropGetByNum(flags, member.index, &value, nullptr)
            : object.PropGet(flags, member.text(), nullptr, &value, nullptr);
        returnPair(result, status, value);
    } else if constexpr(operation == DispatchOperation::Count) {
        tjs_int size = -1;
        status = member.numeric
            ? object.GetCountByNum(&size, member.index, nullptr)
            : object.GetCount(&size, member.text(), nullptr, nullptr);
        returnPair(result, status, tTJSVariant(size));
    } else {
        if constexpr(operation == DispatchOperation::Set) {
            status = member.numeric
                ? object.PropSetByNum(flags, member.index, args[3], nullptr)
                : object.PropSet(flags, member.text(), nullptr, args[3], nullptr);
        } else if constexpr(operation == DispatchOperation::Delete) {
            status = member.numeric
                ? object.DeleteMemberByNum(flags, member.index, nullptr)
                : object.DeleteMember(flags, member.text(), nullptr, nullptr);
        } else if constexpr(operation == DispatchOperation::Invalidate) {
            status = member.numeric
                ? object.InvalidateByNum(flags, member.index, nullptr)
                : object.Invalidate(flags, member.text(), nullptr, nullptr);
        } else if constexpr(operation == DispatchOperation::Valid) {
            status = member.numeric
                ? object.IsValidByNum(flags, member.index, nullptr)
                : object.IsValid(flags, member.text(), nullptr, nullptr);
        } else {
            ttstr className(*args[3]);
            status = member.numeric
                ? object.IsInstanceOf(flags, member.index,
                    className.Independ(), nullptr)
                : object.IsInstanceOf(flags, member.text(), nullptr,
                    className.c_str(), nullptr);
        }
        if(result) *result = static_cast<tjs_int>(status);
    }
    return TJS_S_OK;
}

tjs_error objectEnum(tTJSVariant *, tjs_int count,
                      tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 2) return TJS_E_BADPARAMCOUNT;
    const auto object = args[0]->AsObjectClosureNoAddRef();
    auto callback = args[1]->AsObjectClosureNoAddRef();
    const auto flags = count > 2
        ? static_cast<tjs_uint32>(args[2]->AsInteger()) : TJS_IGNOREPROP;
    return object.EnumMembers(flags, &callback, nullptr);
}

class ScriptProperty final : public tTJSDispatch {
    tTJSVariant getter_, setter_;
public:
    ScriptProperty(tTJSVariant getter, tTJSVariant setter) :
        getter_(std::move(getter)), setter_(std::move(setter)) {}

    tjs_error PropGet(tjs_uint32 flag, const tjs_char *name, tjs_uint32 *hint,
                       tTJSVariant *result, iTJSDispatch2 *thisObject) override {
        if(name) return tTJSDispatch::PropGet(flag, name, hint, result, thisObject);
        if(getter_.Type() != tvtObject) return TJS_E_ACCESSDENYED;
        return getter_.AsObjectClosureNoAddRef().FuncCall(
            0, nullptr, nullptr, result, 0, nullptr, thisObject);
    }
    tjs_error PropSet(tjs_uint32 flag, const tjs_char *name, tjs_uint32 *hint,
                       const tTJSVariant *value, iTJSDispatch2 *thisObject) override {
        if(name) return tTJSDispatch::PropSet(flag, name, hint, value, thisObject);
        if(setter_.Type() != tvtObject) return TJS_E_ACCESSDENYED;
        auto *argument = const_cast<tTJSVariant *>(value);
        return setter_.AsObjectClosureNoAddRef().FuncCall(
            0, nullptr, nullptr, nullptr, 1, &argument, thisObject);
    }
    tjs_error IsInstanceOf(tjs_uint32 flag, const tjs_char *name,
                            tjs_uint32 *hint, const tjs_char *className,
                            iTJSDispatch2 *thisObject) override {
        if(!name && !TJS_strcmp(className, TJS_W("Property"))) return TJS_S_TRUE;
        return tTJSDispatch::IsInstanceOf(flag, name, hint, className, thisObject);
    }
};

tjs_error objectProperty(tTJSVariant *, tjs_int count,
                          tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 2) return TJS_E_BADPARAMCOUNT;
    const auto target = args[0]->AsObjectClosureNoAddRef();
    const auto descriptor = args[1]->AsObjectClosureNoAddRef();
    tTJSVariant name, getter, setter;
    if(TJS_FAILED(descriptor.PropGet(TJS_MEMBERMUSTEXIST, TJS_W("name"),
                                      nullptr, &name, nullptr)))
        return TJS_E_INVALIDPARAM;
    descriptor.PropGet(TJS_MEMBERMUSTEXIST, TJS_W("getter"), nullptr, &getter, nullptr);
    descriptor.PropGet(TJS_MEMBERMUSTEXIST, TJS_W("setter"), nullptr, &setter, nullptr);
    if(getter.Type() == tvtVoid && setter.Type() == tvtVoid)
        return TJS_E_INVALIDPARAM;
    auto *property = new ScriptProperty(getter, setter);
    tTJSVariant value(property, property);
    property->Release();
    const ttstr memberName(name);
    return target.PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                           memberName.c_str(), nullptr, &value, nullptr);
}

void registerPatchHelpers() {
    auto *global = TVPGetScriptEngine()->GetGlobalNoAddRef();
    const auto stringClass = TVPGetKirikiroidNamespace(global, TJS_W("String"));
    const auto objectClass = TVPGetKirikiroidNamespace(global, TJS_W("Object"));
    auto *strings = stringClass.AsObjectNoAddRef();
    auto *objects = objectClass.AsObjectNoAddRef();
    TVPRegisterKirikiroidMethod(strings, TJS_W("encode"), stringEncode);
    TVPRegisterKirikiroidMethod(strings, TJS_W("decode"), stringDecode);
    TVPRegisterKirikiroidMethod(strings, TJS_W("ord"), krkrStrOrd);
    TVPRegisterKirikiroidMethod(objects, TJS_W("getThisObject"), objectContext);
    TVPRegisterKirikiroidMethod(objects, TJS_W("hasMember"), objectHasMember);
    TVPRegisterKirikiroidMethod(objects, TJS_W("regProperty"), objectProperty);
    TVPRegisterKirikiroidMethod(objects, TJS_W("funcCall"), objectDispatch<DispatchOperation::Call>);
    TVPRegisterKirikiroidMethod(objects, TJS_W("propGet"), objectDispatch<DispatchOperation::Get>);
    TVPRegisterKirikiroidMethod(objects, TJS_W("propSet"), objectDispatch<DispatchOperation::Set>);
    TVPRegisterKirikiroidMethod(objects, TJS_W("getCount"), objectDispatch<DispatchOperation::Count>);
    TVPRegisterKirikiroidMethod(objects, TJS_W("enumMembers"), objectEnum);
    TVPRegisterKirikiroidMethod(objects, TJS_W("deleteMember"), objectDispatch<DispatchOperation::Delete>);
    TVPRegisterKirikiroidMethod(objects, TJS_W("invalidate"), objectDispatch<DispatchOperation::Invalidate>);
    TVPRegisterKirikiroidMethod(objects, TJS_W("isValid"), objectDispatch<DispatchOperation::Valid>);
    TVPRegisterKirikiroidMethod(objects, TJS_W("isInstanceOf"), objectDispatch<DispatchOperation::InstanceOf>);
    const std::pair<const tjs_char *, tjs_int> flags[] = {
        {TJS_W("MEMBERENSURE"), TJS_MEMBERENSURE},
        {TJS_W("IGNOREPROP"), TJS_IGNOREPROP},
        {TJS_W("HIDDENMEMBER"), TJS_HIDDENMEMBER},
        {TJS_W("STATICMEMBER"), TJS_STATICMEMBER},
        {TJS_W("ENUM_NO_VALUE"), TJS_ENUM_NO_VALUE}};
    for(const auto &entry : flags) {
        const tTJSVariant value(entry.second);
        const auto status = objects->PropSet(
            TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
            entry.first, nullptr, &value, objects);
        if(TJS_FAILED(status)) TJSThrowFrom_tjs_error(status, entry.first);
    }
    TVPRegisterKirikiroidStreamHelpers(global);
}

} // namespace

tTJSVariant TVPGetKirikiroidNamespace(iTJSDispatch2 *parent, const tjs_char *name) {
    tTJSVariant result;
    const auto status = parent->PropGet(TJS_MEMBERMUSTEXIST | TJS_IGNOREPROP,
                                         name, nullptr, &result, parent);
    if(TJS_SUCCEEDED(status) && result.Type() == tvtObject &&
       result.AsObjectNoAddRef())
        return result;
    if(status != TJS_E_MEMBERNOTFOUND && TJS_FAILED(status))
        TJSThrowFrom_tjs_error(status, name);
    // A real TJS class preserves the reference's instanceof/static-member
    // behavior. Do not replace a namespace that the host already provided.
    const ttstr source = ttstr(TJS_W("class ")) + name + TJS_W(" {}");
    TVPGetScriptEngine()->ExecScript(source);
    const auto readStatus = parent->PropGet(0, name, nullptr, &result, parent);
    if(TJS_FAILED(readStatus)) TJSThrowFrom_tjs_error(readStatus, name);
    return result;
}

void TVPRegisterKirikiroidMethod(iTJSDispatch2 *object, const tjs_char *name,
                                 tTJSNativeClassMethodCallback callback) {
    auto *method = TJSCreateNativeClassMethod(callback);
    tTJSVariant value(method);
    method->Release();
    const auto status = object->PropSet(
        TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
        name, nullptr, &value, object);
    if(TJS_FAILED(status)) TJSThrowFrom_tjs_error(status, name);
}

NCB_REGISTER_FUNCTION(_str_ord, krkrStrOrd);
NCB_ATTACH_FUNCTION(setTextEncoding, Storages, krkrSetTextEncoding);
NCB_PRE_REGIST_CALLBACK(registerPatchHelpers);
