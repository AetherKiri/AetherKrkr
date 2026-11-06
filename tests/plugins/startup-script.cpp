#include <catch2/catch_test_macros.hpp>

#include "ScriptMgnIntf.h"
#include "BinaryStream.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "StartupPatchState.h"
#include "tjs.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

extern tTJS *TVPScriptEngine;
extern bool TVPStartupSuccess;

namespace {
class StartupFixture {
public:
    StartupFixture()
        : previousEngine_(TVPScriptEngine),
          previousStartup_(TVPStartupScriptName),
          previousSuccess_(TVPStartupSuccess),
          previousProject_(TVPProjectDir),
          previousDirectory_(TVPNormalizeStorageName(TJS_W("./"))) {
        path_ = std::filesystem::temp_directory_path() /
                ("aether-startup-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_ / "system");
        TVPSetCurrentDirectory(ttstr(path_.string() + "/"));
        TVPProjectDir = TVPNormalizeStorageName(TJS_W("./"));
        TVPClearStorageCaches();
        TVPScriptEngine = new tTJS();
        TVPStartupScriptName = TJS_W("startup.tjs");
        TVPScriptEngine->ExecScript(TJS_W("var startupRuns = 0, fallbackRuns = 0;"));
        write("system/Initialize.tjs",
              "global.fallbackRuns++; global.fallbackEntryMarker();");
    }

    ~StartupFixture() {
        TVPScriptEngine->Release();
        TVPScriptEngine = previousEngine_;
        TVPStartupScriptName = previousStartup_;
        TVPStartupSuccess = previousSuccess_;
        TVPProjectDir = previousProject_;
        TVPSetCurrentDirectory(previousDirectory_);
        TVPClearStorageCaches();
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    void write(const char *name, const char *script) {
        std::ofstream(path_ / name, std::ios::binary) << script;
        TVPClearStorageCaches();
    }

    tjs_int value(const tjs_char *expression) {
        tTJSVariant result;
        TVPScriptEngine->EvalExpression(expression, &result);
        return result.AsInteger();
    }

private:
    tTJS *previousEngine_;
    ttstr previousStartup_;
    bool previousSuccess_;
    ttstr previousProject_;
    ttstr previousDirectory_;
    std::filesystem::path path_;
};
} // namespace

TEST_CASE("startup errors preserve the original failure without restarting KAG",
          "[startup]") {
    StartupFixture fixture;
    fixture.write("startup.tjs",
                  "global.startupRuns = global.startupRuns + 1; throw %[message: 'primaryEntryMarker'];");
    SECTION("text startup") {}
    SECTION("compiled startup") {
        std::unique_ptr<tTJSBinaryStream> output(
            TVPCreateStream(TJS_W("startup.tjs"), TJS_BS_WRITE));
        TVPScriptEngine->CompileScript(
            TJS_W("global.startupRuns = global.startupRuns + 1; throw %[message: 'primaryEntryMarker'];"), output.get(),
            false, false, false, TJS_W("startup.tjs"));
        output.reset();
        TVPClearStorageCaches();
    }
    try {
        TVPExecuteStartupScript();
        FAIL("startup should propagate its script error");
    } catch(const TJS::eTJSScriptError &error) {
        CHECK(error.GetMessage().AsStdString().find("primaryEntryMarker") !=
              std::string::npos);
        CHECK(ttstr(error.GetBlockName()) == TJS_W("startup.tjs"));
    }
    CHECK(fixture.value(TJS_W("startupRuns")) == 1);
    CHECK(fixture.value(TJS_W("fallbackRuns")) == 0);
    CHECK_FALSE(TVPStartupSuccess);
}

TEST_CASE("a missing startup script still enters the framework directly",
          "[startup]") {
    StartupFixture fixture;
    try {
        TVPExecuteStartupScript();
        FAIL("the fallback marker should propagate");
    } catch(const TJS::eTJSScriptError &error) {
        CHECK(error.GetMessage().AsStdString().find("fallbackEntryMarker") !=
              std::string::npos);
    }
    CHECK(fixture.value(TJS_W("startupRuns")) == 0);
    CHECK(fixture.value(TJS_W("fallbackRuns")) == 1);
    CHECK_FALSE(TVPStartupSuccess);
}

TEST_CASE("restored KAG logging does not require a native Win32 debug pad",
          "[startup][log-pad]") {
    StartupFixture fixture;
    TVPScriptEngine->ExecScript(TJS_W(
        "var padRequests=0, storyReady=0;"
        "var savedSettings=%[logMode:1];"
        "class WIN32Dialog {}"
        "class Krkr2CompatUtils {}"
        "class LogWindowPad {}"
        "var LogWindow=LogWindowPad;"
        "property Pad { getter { global.padRequests++; return WIN32Dialog.Header; } }"
    ));
    const auto *framework = TJS_W(
        "class KAGWindow {"
        " var logMode=0, logPad=void;"
        " function KAGWindow() {"
        "  this.logMode=global.savedSettings.logMode;"
        "  if(this.logMode) this.initLogPad(0);"
        "  global.storyReady=global.storyReady+1;"
        " }"
        " function initLogPad(position=0, clear=0) {"
        "  if(typeof global.Pad=='undefined') return;"
        "  logPad=new LogWindow();"
        " }"
        "}"
    );
    fixture.write("system/mainwindow.tjs", ttstr(framework).AsStdString().c_str());
    SECTION("text framework") {}
    SECTION("compiled framework") {
        std::unique_ptr<tTJSBinaryStream> output(
            TVPCreateStream(TJS_W("system/mainwindow.tjs"), TJS_BS_WRITE));
        TVPScriptEngine->CompileScript(framework, output.get(), false, false,
                                       false, TJS_W("mainwindow.tjs"));
        output.reset();
        TVPClearStorageCaches();
    }
    REQUIRE_NOTHROW(TVPExecuteStorage(TJS_W("system/mainwindow.tjs")));
    REQUIRE_NOTHROW(TVPScriptEngine->ExecScript(TJS_W(
        "var kag=new KAGWindow(); kag.initLogPad(1,1);")));
    CHECK(fixture.value(TJS_W("storyReady")) == 1);
    CHECK(fixture.value(TJS_W("padRequests")) == 0);
    CHECK(fixture.value(TJS_W("kag.logPad===void")) == 1);
    CHECK(fixture.value(TJS_W("kag.logMode")) == 1);
    CHECK(fixture.value(TJS_W("savedSettings.logMode")) == 1);
}

TEST_CASE("an incomplete root patch cannot disable framework script loading",
          "[startup]") {
    StartupFixture fixture;
    auto *scripts = TVPCreateNativeClass_Scripts();
    tTJSVariant value(scripts);
    scripts->Release();
    auto *global = TVPScriptEngine->GetGlobalNoAddRef();
    REQUIRE(TJS_SUCCEEDED(global->PropSet(TJS_MEMBERENSURE,
        TJS_W("Scripts"), nullptr, &value, global)));
    TVPScriptEngine->ExecScript(TJS_W("var patchRuns=0, patchCompleted=0;"));
    const auto *patch = TJS_W(
        "global.patchRuns=global.patchRuns+1; global.earlySetting=42;"
        "Scripts.execStorage=function() {};"
        "dm(); global.patchCompleted=global.patchCompleted+1;"
    );
    fixture.write("patch.tjs", ttstr(patch).AsStdString().c_str());
    fixture.write("startup.tjs",
        "global.startupRuns=global.startupRuns+1; Scripts.execStorage('system/Initialize.tjs');");
    fixture.write("system/Initialize.tjs",
        "global.fallbackRuns++; global.settingAtInit=earlySetting;"
        "global.dm=function() {}; global.kag=%[];");
    SECTION("text scripts") {}
    SECTION("compiled scripts") {
        for(const auto *name : {TJS_W("patch.tjs"), TJS_W("startup.tjs")}) {
            std::unique_ptr<tTJSBinaryStream> output(
                TVPCreateStream(name, TJS_BS_WRITE));
            TVPScriptEngine->CompileScript(
                ttstr(name) == TJS_W("patch.tjs") ? patch : TJS_W(
                    "global.startupRuns=global.startupRuns+1; Scripts.execStorage('system/Initialize.tjs');"),
                output.get(), false, false, false, name);
        }
        TVPClearStorageCaches();
    }
    REQUIRE_NOTHROW(TVPExecuteStartupScript());
    CHECK(TVPStartupSuccess);
    CHECK(fixture.value(TJS_W("startupRuns")) == 1);
    CHECK(fixture.value(TJS_W("fallbackRuns")) == 1);
    CHECK(fixture.value(TJS_W("settingAtInit")) == 42);
    CHECK(fixture.value(TJS_W("patchRuns")) == 1);
    CHECK(fixture.value(TJS_W("patchCompleted")) == 1);
    // The completed late patch owns the loader again.
    TVPScriptEngine->ExecScript(TJS_W("Scripts.execStorage('system/Initialize.tjs');"));
    CHECK(fixture.value(TJS_W("fallbackRuns")) == 1);
}

TEST_CASE("deferred patches can reapply framework method hooks without recursion",
          "[startup]") {
    StartupFixture fixture;
    auto *scripts = TVPCreateNativeClass_Scripts();
    tTJSVariant scriptsValue(scripts);
    scripts->Release();
    auto *global = TVPScriptEngine->GetGlobalNoAddRef();
    REQUIRE(TJS_SUCCEEDED(global->PropSet(TJS_MEMBERENSURE,
        TJS_W("Scripts"), nullptr, &scriptsValue, global)));
    TVPScriptEngine->ExecScript(TJS_W("var hookCalls=0, baseCalls=0;"));
    const ttstr hook = TJS_W(
        "function installWorkerHook(cls) {"
        " function methods {"
        "  function clear(value) {"
        "   global.hookCalls=global.hookCalls+1;"
        "   if(global.hookCalls>3) throw %[message:'recursive hook'];"
        "   return savedClear(value)+1;"
        "  }"
        " }"
        " with(cls) { &.savedClear=&methods.clear; &.savedClear <-> &.clear; }"
        "} installWorkerHook(Worker); delete global.installWorkerHook;"
    );
    const ttstr patch = ttstr(TJS_W("Scripts.execStorage=function() {}; dm();")) + hook;
    const ttstr framework = ttstr(TJS_W(
        "global.dm=function() {}; global.kag=%[];"
        "class Worker {"
        " var seed=7;"
        " function clear(value) { global.baseCalls=global.baseCalls+1; return seed+value; }"
        "}"
        "global.originalClear=&Worker.clear;"
    )) + hook + TJS_W("global.existingWorker=new Worker();");
    fixture.write("patch.tjs", patch.AsStdString().c_str());
    fixture.write("startup.tjs", "Scripts.execStorage('system/Initialize.tjs');");
    fixture.write("system/Initialize.tjs", framework.AsStdString().c_str());
    bool compiledPatch = false, compiledFramework = false;
    SECTION("text patch and framework") {}
    SECTION("compiled patch and text framework") { compiledPatch = true; }
    SECTION("text patch and compiled framework") { compiledFramework = true; }
    SECTION("compiled patch and framework") { compiledPatch = compiledFramework = true; }
    auto compile = [&](const tjs_char *name, const ttstr &source) {
        std::unique_ptr<tTJSBinaryStream> output(TVPCreateStream(name, TJS_BS_WRITE));
        TVPScriptEngine->CompileScript(source.c_str(), output.get(),
            false, false, false, name);
    };
    if(compiledPatch) compile(TJS_W("patch.tjs"), patch);
    if(compiledFramework) compile(TJS_W("system/Initialize.tjs"), framework);
    TVPClearStorageCaches();

    REQUIRE_NOTHROW(TVPExecuteStartupScript());
    REQUIRE(fixture.value(TJS_W("&Worker.savedClear === originalClear")) == 1);
    CHECK(fixture.value(TJS_W("(new Worker()).clear(4)")) == 12);
    CHECK(fixture.value(TJS_W("existingWorker.clear(9)")) == 17);
    CHECK(fixture.value(TJS_W("baseCalls")) == 2);
    CHECK(fixture.value(TJS_W("hookCalls")) == 2);
}

TEST_CASE("startup hook reconciliation preserves distinct method replacements",
          "[startup]") {
    StartupFixture fixture;
    TVPScriptEngine->ExecScript(TJS_W(
        "class Worker { function clear() { return 7; } }"
        "Worker.savedClear=&Worker.clear;"
    ));
    ttstr first, second;
    SECTION("different wrapper constants") {
        first = TJS_W("Worker.clear=function() { return savedClear()+1; };");
        second = TJS_W("Worker.clear=function() { return savedClear()+2; };");
    }
    SECTION("an alias name used only as a literal") {
        first = second = TJS_W("Worker.clear=function() { return 'savedClear'; };");
    }
    SECTION("an explicit call on a different object") {
        first = second = TJS_W("Worker.clear=function() { return global.other.savedClear(); };");
    }
    TVPScriptEngine->ExecScript(first.c_str());
    TVPScriptEngine->ExecScript(TJS_W("global.previousClear=&Worker.clear;"));
    tTVPStartupPatchState before(TVPScriptEngine);
    TVPScriptEngine->ExecScript(TJS_W("Worker.savedClear=&Worker.clear;"));
    TVPScriptEngine->ExecScript(second.c_str());
    CHECK(before.RestoreReappliedMethodAliases() == 0);
    CHECK(fixture.value(TJS_W("&Worker.savedClear === previousClear")) == 1);
}
