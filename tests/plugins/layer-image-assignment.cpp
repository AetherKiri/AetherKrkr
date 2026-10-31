#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>

#include "LayerImpl.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "SystemControl.h"
#include "WindowIntf.h"
#include "tvpgl.h"
#include "motionplayer/D3DAdaptor.h"

extern tTJS *TVPScriptEngine;

namespace {
class ImageRuntime {
public:
    ImageRuntime() {
        TVPInitTVPGL();
        TVPSetCurrentDirectory(TJS_W("file://./"));
        TVPSetCommandLine(TJS_W("renderer"), TJS_W("software"));
        TVPProjectDir = TJS_W("file://./");
        control = std::make_unique<tTVPSystemControl>();
        TVPSystemControl = control.get();
        TVPScriptEngine = new tTJS();
        auto *global = TVPScriptEngine->GetGlobalNoAddRef();
        for(const auto &entry : {
                std::make_pair(TJS_W("Window"), TVPCreateNativeClass_Window()),
                std::make_pair(TJS_W("Layer"), TVPCreateNativeClass_Layer())}) {
            tTJSVariant value(entry.second);
            global->PropSet(TJS_MEMBERENSURE, entry.first, nullptr, &value, global);
            entry.second->Release();
        }
        run(TJS_W(
            "var window = new Window(), primary = new Layer(window, null);\n"
            "var pool = new Layer(window, primary);\n"
            "pool.name = 'AffineSource情報プール用'; pool.visible = false;\n"
            "var source = new Layer(window, pool);\n"
            "source.visible = false; source.setSize(64, 32);\n"
            "source.setImageSize(64, 32); source.fillRect(0, 0, 64, 32, 0x123456);\n"
            "var target = new Layer(window, primary);\n"
            "target.name = 'picture'; target.visible = true; target.setSize(64, 32);\n"
            "target.setImageSize(64, 32); target.fillRect(0, 0, 64, 32, 0);\n"));
    }
    ~ImageRuntime() {
        run(TJS_W("invalidate target; invalidate source; invalidate pool;"
                  "invalidate primary; invalidate window;"));
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
    tTJSVariant object(const tjs_char *expression) {
        tTJSVariant result;
        TVPScriptEngine->EvalExpression(expression, &result);
        return result;
    }
private:
    std::unique_ptr<tTVPSystemControl> control;
};
} // namespace

TEST_CASE("image assignment preserves reusable hidden affine sources",
          "[layer][image][assignment]") {
    ImageRuntime runtime;
    runtime.run(TJS_W("target.assignImages(source);"));
    CHECK(runtime.value(TJS_W("target.getMainPixel(0, 0)")) == 0x123456);
    CHECK(runtime.value(TJS_W("source.getMainPixel(0, 0)")) == 0x123456);

    // A PSD face update changes a small region of the retained base image.
    runtime.run(TJS_W("source.fillRect(20, 8, 8, 8, 0xabcdef);"));
    CHECK(runtime.value(TJS_W("target.getMainPixel(20, 8)")) == 0x123456);
    runtime.run(TJS_W("target.assignImages(source);"));
    CHECK(runtime.value(TJS_W("target.getMainPixel(0, 0)")) == 0x123456);
    CHECK(runtime.value(TJS_W("target.getMainPixel(20, 8)")) == 0xabcdef);

    // Re-entering a menu can request the same source without repainting it.
    runtime.run(TJS_W("target.fillRect(0, 0, 64, 32, 0); target.assignImages(source);"));
    CHECK(runtime.value(TJS_W("target.getMainPixel(0, 0)")) == 0x123456);
    CHECK(runtime.value(TJS_W("target.getMainPixel(20, 8)")) == 0xabcdef);
}

TEST_CASE("a completed motion capture authorizes exactly one image hand-off",
          "[layer][image][assignment][motion]") {
    ImageRuntime runtime;
    motion::D3DAdaptor adaptor;
    auto source = runtime.object(TJS_W("source"));
    SECTION("capture the rendered layer") {
        adaptor.setRenderedLayer(source.AsObjectNoAddRef());
    }
    SECTION("capture a CPU motion buffer") {
        adaptor.setSize(64, 32);
        // The capture buffer stores RGBA bytes; script colours are 0xRRGGBB.
        std::fill_n(reinterpret_cast<tjs_uint32 *>(adaptor.getBuffer()),
                    64 * 32, 0xff563412);
    }
    tTJSVariant *params[] = {&source};
    REQUIRE(adaptor.captureCanvas(nullptr, 1, params, nullptr) == TJS_S_OK);
    runtime.run(TJS_W(
        "target.setSize(32, 32); target.setImageSize(32, 32);"
        "target.assignImages(source);"));
    CHECK(runtime.value(TJS_W("target.getMainPixel(0, 0)")) == 0x123456);
    CHECK(runtime.value(TJS_W("source.getMainPixel(0, 0)")) == 0);
    CHECK(runtime.value(TJS_W("target.width")) == 64);
    CHECK(runtime.value(TJS_W("target.height")) == 32);

    // The returned buffer is ordinary image storage until captured again.
    runtime.run(TJS_W(
        "source.fillRect(0, 0, 32, 32, 0xabcdef);"
        "target.assignImages(source);"));
    CHECK(runtime.value(TJS_W("target.getMainPixel(0, 0)")) == 0xabcdef);
    CHECK(runtime.value(TJS_W("source.getMainPixel(0, 0)")) == 0xabcdef);
}

TEST_CASE("an empty motion capture cannot authorize an image hand-off",
          "[layer][image][assignment][motion]") {
    ImageRuntime runtime;
    motion::D3DAdaptor adaptor;
    auto source = runtime.object(TJS_W("source"));
    tTJSVariant *params[] = {&source};
    REQUIRE(adaptor.captureCanvas(nullptr, 1, params, nullptr) == TJS_S_OK);
    runtime.run(TJS_W("target.assignImages(source);"));
    CHECK(runtime.value(TJS_W("target.getMainPixel(0, 0)")) == 0x123456);
    CHECK(runtime.value(TJS_W("source.getMainPixel(0, 0)")) == 0x123456);
}
