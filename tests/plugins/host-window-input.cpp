#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "EngineLoop.h"
#include "EventIntf.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "SystemControl.h"
#include "WindowIntf.h"
#include "tvpgl.h"

extern tTJS *TVPScriptEngine;

namespace {
class WindowRuntime {
public:
    WindowRuntime() {
        TVPInitTVPGL();
        TVPSetCurrentDirectory(TJS_W("file://./"));
        TVPSetCommandLine(TJS_W("renderer"), TJS_W("software"));
        TVPProjectDir = TJS_W("file://./");
        system_control_ = std::make_unique<tTVPSystemControl>();
        TVPSystemControl = system_control_.get();
        TVPScriptEngine = new tTJS();
        auto *global = TVPScriptEngine->GetGlobalNoAddRef();
        auto *klass = TVPCreateNativeClass_Window();
        tTJSVariant value(klass);
        global->PropSet(TJS_MEMBERENSURE, TJS_W("Window"), nullptr,
                        &value, global);
        klass->Release();
        run(TJS_W(
            "class InputWindow extends Window {\n"
            "  var downs = 0, moves = 0, ups = 0, wheels = 0;\n"
            "  var keys = 0, releases = 0, presses = 0;\n"
            "  function InputWindow() { super.Window(); }\n"
            "  function onMouseDown(x, y, button, shift) { downs++; }\n"
            "  function onMouseMove(x, y, shift) { moves++; }\n"
            "  function onMouseUp(x, y, button, shift) { ups++; }\n"
            "  function onMouseWheel(shift, delta, x, y) { wheels++; }\n"
            "  function onKeyDown(key, shift) { keys++; }\n"
            "  function onKeyUp(key, shift) { releases++; }\n"
            "  function onKeyPress(key) { presses++; }\n"
            "}\n"
            "var main = new InputWindow();\n"
            "var setup = new InputWindow();\n"));
        loop_ = EngineLoop::CreateInstance();
        loop_->ResetPointerState();
    }

    ~WindowRuntime() {
        run(TJS_W("invalidate setup; invalidate main;"));
        loop_->ResetPointerState();
        TVPScriptEngine->Release();
        TVPScriptEngine = nullptr;
        TVPSystemControl = nullptr;
        TVPSystemControlAlive = false;
    }

    void run(const tjs_char *script) { TVPScriptEngine->ExecScript(script); }
    tjs_int value(const tjs_char *expression) {
        tTJSVariant result;
        TVPScriptEngine->EvalExpression(expression, &result);
        return result.AsInteger();
    }
    bool send(uint32_t type) {
        EngineInputEvent event;
        event.type = type;
        event.x = 200;
        event.y = 100;
        event.delta_y = 1;
        event.key_code = 65;
        event.unicode_codepoint = 0x4e2d;
        const bool delivered = loop_->HandleInputEvent(event);
        TVPDeliverAllEvents();
        loop_->CompleteInputFrame();
        return delivered;
    }

private:
    std::unique_ptr<tTVPSystemControl> system_control_;
    EngineLoop *loop_ = nullptr;
};
} // namespace

TEST_CASE("host input reaches the visible startup window", "[input][window]") {
    WindowRuntime runtime;
    runtime.run(TJS_W("main.visible = false; setup.visible = true;"));
    for(uint32_t type = kEngineInputPointerDown; type <= kEngineInputBack; ++type)
        REQUIRE(runtime.send(type));
    CHECK(runtime.value(TJS_W("setup.downs")) == 1);
    CHECK(runtime.value(TJS_W("setup.moves")) == 1);
    CHECK(runtime.value(TJS_W("setup.ups")) == 1);
    CHECK(runtime.value(TJS_W("setup.wheels")) == 1);
    CHECK(runtime.value(TJS_W("setup.keys")) == 2);
    CHECK(runtime.value(TJS_W("setup.releases")) == 1);
    CHECK(runtime.value(TJS_W("setup.presses")) == 1);
    CHECK(runtime.value(TJS_W("main.downs + main.keys + main.presses")) == 0);
}

TEST_CASE("host input follows window visibility and foreground changes",
          "[input][window]") {
    WindowRuntime runtime;
    SECTION("a hidden secondary window yields to the main window") {
        runtime.run(TJS_W("main.visible = true; setup.visible = false;"));
    }
    SECTION("a closed startup window yields to the main window") {
        runtime.run(TJS_W("invalidate setup; main.visible = true;"));
    }
    SECTION("bringing the main window forward changes the input target") {
        runtime.run(TJS_W(
            "main.visible = setup.visible = true; main.bringToFront();"));
    }
    REQUIRE(runtime.send(kEngineInputKeyDown));
    CHECK(runtime.value(TJS_W("main.keys")) == 1);
    if(runtime.value(TJS_W("isvalid setup")))
        CHECK(runtime.value(TJS_W("setup.keys")) == 0);
}

TEST_CASE("host input is unavailable when every window is hidden",
          "[input][window]") {
    WindowRuntime runtime;
    runtime.run(TJS_W("main.visible = setup.visible = false;"));
    CHECK_FALSE(runtime.send(kEngineInputKeyDown));
    CHECK(runtime.value(TJS_W("main.keys + setup.keys")) == 0);
    runtime.run(TJS_W("main.visible = true; setup.visible = true;"));
    REQUIRE(runtime.send(kEngineInputKeyDown));
    CHECK(runtime.value(TJS_W("setup.keys")) == 1);
}
