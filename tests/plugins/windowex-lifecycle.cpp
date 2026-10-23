#include <catch2/catch_test_macros.hpp>

#include "ScriptMgnIntf.h"
#include "SystemIntf.h"
#include "WindowIntf.h"
#include "PluginImpl.h"
#include "XP3Archive.h"
#include "ncbind.hpp"

#include <string>

extern tTJS *TVPScriptEngine;
extern void TVPSetXP3FilterScript(ttstr content);

namespace {
class WindowExRuntime {
public:
    WindowExRuntime() : previousEngine_(TVPScriptEngine) {
        TVPScriptEngine = new tTJS();
        publish(TJS_W("Window"), TVPCreateNativeClass_Window());
        publish(TJS_W("System"), TVPCreateNativeClass_System());
        publish(TJS_W("Scripts"), TVPCreateNativeClass_Scripts());
        publish(TJS_W("Plugins"), TVPCreateNativeClass_Plugins());
        ncbAutoRegister::AllRegist();
    }
    ~WindowExRuntime() {
        ncbAutoRegister::UnloadModule(TJS_W("windowEx.dll"));
        TVPScriptEngine->Release();
        TVPScriptEngine = previousEngine_;
    }

    tTJSVariant evaluate(const tjs_char *expression) {
        tTJSVariant result;
        TVPScriptEngine->EvalExpression(expression, &result);
        return result;
    }

private:
    void publish(const tjs_char *name, iTJSDispatch2 *object) {
        tTJSVariant value(object);
        object->Release();
        auto *global = TVPScriptEngine->GetGlobalNoAddRef();
        global->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, global);
    }
    tTJS *previousEngine_;
};
} // namespace

TEST_CASE("unloading windowEx restores the engine expression evaluator",
          "[plugin][startup]") {
    WindowExRuntime runtime;
    const auto original = runtime.evaluate(TJS_W("Scripts.eval"));
    for(int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(ncbAutoRegister::LoadModule(TJS_W("windowEx.dll")));
        CHECK(runtime.evaluate(TJS_W("Scripts.eval")).AsObjectNoAddRef() !=
              original.AsObjectNoAddRef());
        CHECK(runtime.evaluate(TJS_W("Scripts.eval('21 * 2')")).AsInteger() == 42);
        runtime.evaluate(TJS_W("Scripts.setEvalErrorLog(false)"));
        CHECK(runtime.evaluate(TJS_W("Scripts.eval('6 * 7')")).AsInteger() == 42);
        REQUIRE(ncbAutoRegister::UnloadModule(TJS_W("windowEx.dll")));
        CHECK(runtime.evaluate(TJS_W("Scripts.eval")).AsObjectNoAddRef() ==
              original.AsObjectNoAddRef());
        CHECK(runtime.evaluate(TJS_W("Scripts.eval('40 + 2')")).AsInteger() == 42);
        CHECK(ttstr(runtime.evaluate(TJS_W("typeof Scripts.setEvalErrorLog"))) ==
              TJS_W("undefined"));
    }
}

TEST_CASE("script unlink keeps active archive decoding until session teardown",
          "[plugin][startup]") {
    WindowExRuntime runtime;
    struct FilterCleanup {
        ~FilterCleanup() {
            ncbAutoRegister::UnloadModule(TJS_W("xp3filter.dll"));
        }
    } cleanup;
    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll")));
    TVPSetXP3FilterScript(TJS_W(
        "Storages.setXP3ArchiveExtractionFilter(function(hash, offset, buffer, length) {"
        " for(var i = 0; i < length; ++i) buffer[i] ^= 0x5a;"
        "});"));
    const auto decoder = TVPXP3ArchiveExtractionFilter;
    REQUIRE(decoder != nullptr);
    CHECK(runtime.evaluate(TJS_W("Plugins.unlink('plugin/XP3FILTER.DLL')")).AsInteger() == 0);
    REQUIRE(TVPXP3ArchiveExtractionFilter == decoder);

    unsigned char buffer[] = {'T' ^ 0x5a, 'J' ^ 0x5a, 'S' ^ 0x5a};
    const ttstr name(TJS_W("title.ks"));
    tTVPXP3ExtractionFilterInfo info(0, buffer, sizeof(buffer), 123, name);
    tTJSVariant context;
    decoder(&info, &context);
    CHECK(std::string(reinterpret_cast<char *>(buffer), sizeof(buffer)) == "TJS");

    // The host can still release the filter between games, and an inactive
    // filter module remains available for ordinary script-driven unloading.
    TVPUnloadInternalPlugins();
    CHECK(TVPXP3ArchiveExtractionFilter == nullptr);
    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll")));
    CHECK(TVPXP3ArchiveExtractionFilter == nullptr);
    CHECK(runtime.evaluate(TJS_W("Plugins.unlink('xp3filter.dll')")).AsInteger() == 1);
}
