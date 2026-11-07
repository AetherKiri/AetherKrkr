#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "EngineLoop.h"
#include "EventIntf.h"
#include "GodotRenderManager.h"
#include "HostViewport.h"
#include "HostWindow.h"
#include "LayerImpl.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "SystemControl.h"
#include "WindowIntf.h"
#include "WindowImpl.h"
#include "tvpgl.h"

extern tTJS *TVPScriptEngine;
extern "C" void TVPHostSetSurfaceSize(uint32_t width, uint32_t height);
extern "C" void TVPHostSetPreferGpuFrame(bool enabled);

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
    bool send(uint32_t type, int x = 200, int y = 100) {
        EngineInputEvent event;
        event.type = type;
        event.x = x;
        event.y = y;
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

TEST_CASE("host presentation fits both wide and tall surfaces",
          "[render][window]") {
    CHECK(TVPGetHostViewport(1024, 768, 1920, 1080) ==
          tTVPRect(240, 0, 1680, 1080));
    CHECK(TVPGetHostViewport(1920, 1080, 1024, 768) ==
          tTVPRect(0, 96, 1024, 672));
    CHECK(TVPGetHostViewport(1280, 720, 1920, 1080) ==
          tTVPRect(0, 0, 1920, 1080));
    CHECK(TVPGetHostViewport(1024, 768, 0, 1080).is_empty());
}

TEST_CASE("host raw frames map pointer input through the fitted viewport",
          "[input][render][window]") {
    WindowRuntime runtime;
    auto *global = TVPScriptEngine->GetGlobalNoAddRef();
    auto *klass = TVPCreateNativeClass_Layer();
    tTJSVariant value(klass);
    global->PropSet(TJS_MEMBERENSURE, TJS_W("Layer"), nullptr, &value, global);
    klass->Release();
    runtime.run(TJS_W(
        "setup.visible = false; main.visible = true;\n"
        "var mouseX = -1, mouseY = -1;\n"
        "class Surface extends Layer {\n"
        "  function Surface(w) { super.Layer(w, null); }\n"
        "  function onMouseMove(x, y, shift) { mouseX = x; mouseY = y; }\n"
        "}\n"
        "var primary = new Surface(main);\n"
        "primary.setSize(1024, 768); primary.visible = true;\n"
        "primary.hitThreshold = 0;\n"));
    TVPHostSetPreferGpuFrame(false);
    TVPHostSetSurfaceSize(1920, 1080);
    GodotTexture2D texture(nullptr, 0, 1024, 768, TVPTextureFormat::RGBA);
    auto *window = TVPGetHostWindow();
    REQUIRE(window != nullptr);
    REQUIRE(window->GetForm() != nullptr);
    window->GetForm()->UpdateDrawBuffer(&texture);
    REQUIRE(runtime.send(kEngineInputPointerMove, 240, 0));
    CHECK(runtime.value(TJS_W("mouseX")) == 0);
    CHECK(runtime.value(TJS_W("mouseY")) == 0);
    REQUIRE(runtime.send(kEngineInputPointerMove, 1320, 810));
    CHECK(runtime.value(TJS_W("mouseX")) == 768);
    CHECK(runtime.value(TJS_W("mouseY")) == 576);
    // A live host resize changes the input rectangle with the next frame.
    TVPHostSetSurfaceSize(1024, 1024);
    window->GetForm()->UpdateDrawBuffer(&texture);
    REQUIRE(runtime.send(kEngineInputPointerMove, 256, 320));
    CHECK(runtime.value(TJS_W("mouseX")) == 256);
    CHECK(runtime.value(TJS_W("mouseY")) == 192);
    runtime.run(TJS_W("invalidate primary;"));
    TVPHostSetSurfaceSize(1920, 1080);
}
