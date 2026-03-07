//
// Created by LiDon on 2025/9/13.
// TODO: implement emoteplayer.dll plugin
//
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "tjsDictionary.h"
#include "ncbind.hpp"
#include "psbfile/PSBFile.h"

#include "ResourceManager.h"
#include "EmotePlayer.h"
#include "Player.h"
#include "SeparateLayerAdaptor.h"

using namespace motion;
using namespace TJS;

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")
#define LOGGER spdlog::get("plugin")

NCB_REGISTER_SUBCLASS_DELAY(SeparateLayerAdaptor) { NCB_CONSTRUCTOR(()); }

// 脚本意图: EmoteVariable.useD3D = (typeof Motion.Player.useD3D === "Object") ? Motion.Player.useD3D : Motion.enableD3D;
// 但 then 分支实际赋的是比较结果 (int)1 而非对象，导致 (int)1 to Object。故让 useD3D 返回整数，
// 使 typeof === "Integer" 走 else，赋 Motion.enableD3D（stub 对象），避免两处 int→Object 报错。
static tjs_error Player_getUseD3D(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    *r = tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error Player_setUseD3D(tTJSVariant *, tjs_int count, tTJSVariant **p, iTJSDispatch2 *) {
    if (count >= 1 && (*p)->Type() == tvtInteger)
        motion::Player::setUseD3D(static_cast<bool>(**p));
    return TJS_S_OK;
}
static tjs_error Player_getEnableD3D(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    iTJSDispatch2 *obj = TJSCreateDictionaryObject();
    if (obj) {
        *r = tTJSVariant(obj);
        obj->Release();
    } else {
        *r = tTJSVariant();
    }
    return TJS_S_OK;
}
static tjs_error Player_setEnableD3D(tTJSVariant *, tjs_int count, tTJSVariant **p, iTJSDispatch2 *) {
    if (count >= 1 && (*p)->Type() == tvtInteger)
        motion::Player::setEnableD3D(static_cast<bool>(**p));
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS_DELAY(Player) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RAW_CALLBACK(useD3D, Player_getUseD3D, Player_setUseD3D, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK(enableD3D, Player_getEnableD3D, Player_setEnableD3D, TJS_STATICMEMBER);
}

NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) {
    NCB_CONSTRUCTOR((ResourceManager));
    NCB_PROPERTY(useD3D, getUseD3D, setUseD3D);
}

NCB_REGISTER_SUBCLASS(ResourceManager) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *, tjs_int));
    NCB_METHOD(load);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptSeed,
                            &ResourceManager::setEmotePSBDecryptSeed,
                            TJS_STATICMEMBER);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptFunc,
                            &ResourceManager::setEmotePSBDecryptFunc,
                            TJS_STATICMEMBER);
}

class Motion {
public:
    static tjs_error setEnableD3D(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *) {
        if(count == 1 && (*p)->Type() == tvtInteger) {
            _enableD3D = static_cast<bool>(**p);
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

    static tjs_error getEnableD3D(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *) {
        iTJSDispatch2 *obj = TJSCreateDictionaryObject();
        if (obj) {
            *r = tTJSVariant(obj);
            obj->Release();
        } else {
            *r = tTJSVariant();
        }
        return TJS_S_OK;
    }

private:
    inline static bool _enableD3D;
};

NCB_REGISTER_CLASS(Motion) {
    NCB_PROPERTY_RAW_CALLBACK(enableD3D, Motion::getEnableD3D,
                              Motion::setEnableD3D, TJS_STATICMEMBER);
    NCB_SUBCLASS(ResourceManager, ResourceManager);
    NCB_SUBCLASS(Player, Player);
    NCB_SUBCLASS(EmotePlayer, EmotePlayer);
    NCB_SUBCLASS(SeparateLayerAdaptor, SeparateLayerAdaptor);
}

static void PreRegistCallback() {}

static void PostUnregistCallback() {}

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
