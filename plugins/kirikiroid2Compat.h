#pragma once

#include "tjsNative.h"

tTJSVariant TVPGetKirikiroidNamespace(iTJSDispatch2 *parent,
                                      const tjs_char *name);
void TVPRegisterKirikiroidMethod(iTJSDispatch2 *object, const tjs_char *name,
                                 tTJSNativeClassMethodCallback callback);
void TVPRegisterKirikiroidStreamHelpers(iTJSDispatch2 *global);
