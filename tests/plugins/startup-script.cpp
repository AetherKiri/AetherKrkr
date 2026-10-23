#include <catch2/catch_test_macros.hpp>

#include "ScriptMgnIntf.h"
#include "BinaryStream.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
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
