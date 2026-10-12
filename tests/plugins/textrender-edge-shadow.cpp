#include <catch2/catch_test_macros.hpp>

#include "ScriptMgnIntf.h"
#include "ncbind.hpp"
#include "tjsDictionary.h"

#include <array>
#include <vector>

extern tTJS *TVPScriptEngine;

namespace {

class DrawRecorder : public tTJSDispatch {
public:
    struct Call {
        std::array<tTJSVariant, 11> args;
        iTJSDispatch2 *receiver;
    };
    std::vector<Call> calls;
    tjs_error status = TJS_S_OK;

    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *, tjs_int count, tTJSVariant **params,
                       iTJSDispatch2 *receiver) override {
        REQUIRE(count == 11);
        Call call;
        for (int i = 0; i < count; ++i) call.args[i] = *params[i];
        call.receiver = receiver;
        calls.push_back(call);
        return status;
    }
};

tTJSVariant edgeShadowMethod() {
    if (!TVPScriptEngine) {
        TVPScriptEngine = new tTJS();
        TVPScriptEngine->ExecScript(TJS_W("class Layer {}"), nullptr);
        ncbAutoRegister::AllRegist();
        REQUIRE(ncbAutoRegister::LoadModule(TJS_W("textrender.dll")));
    }
    tTJSVariant method;
    TVPScriptEngine->EvalExpression(TJS_W("Layer.EdgeShadowDrawText"), &method);
    REQUIRE(method.Type() == tvtObject);
    return method;
}

struct EdgeShadowCall {
    tTJSVariant method = edgeShadowMethod();
    DrawRecorder *recorder = new DrawRecorder();
    tTJSVariant callback{recorder};
    tTJSVariant target;
    std::array<tTJSVariant, 17> args;

    EdgeShadowCall() {
        recorder->Release(); // callback owns the reference
        auto *dictionary = TJSCreateDictionaryObject();
        target = tTJSVariant(dictionary, dictionary);
        dictionary->Release();
        args = {callback, target, 8, 23, TJS_W("文字"), 0xff0000, 0, 1,
                0, 0x123456, 2, 2, -3, 1, 0xffffff, 4096, 3};
    }

    tjs_error run(tjs_int count = 17) {
        std::array<tTJSVariant *, 17> pointers;
        for (int i = 0; i < count; ++i) pointers[i] = &args[i];
        return method.AsObjectClosureNoAddRef().FuncCall(
            0, nullptr, nullptr, nullptr, count, pointers.data(),
            target.AsObjectNoAddRef());
    }
};

} // namespace

TEST_CASE("EdgeShadowDrawText preserves an outline-only fade pass",
          "[textrender][edge-shadow]") {
    EdgeShadowCall call;
    REQUIRE(TJS_SUCCEEDED(call.run()));
    REQUIRE(call.recorder->calls.size() == 1);
    const auto &draw = call.recorder->calls[0];
    CHECK(draw.receiver == call.target.AsObjectNoAddRef());
    CHECK(draw.args[0].AsInteger() == 8);
    CHECK(draw.args[1].AsInteger() == 23);
    CHECK(ttstr(draw.args[2]) == TJS_W("文字"));
    CHECK(draw.args[3].AsInteger() == 0xffffff);
    CHECK(draw.args[4].AsInteger() == 255);
    CHECK(draw.args[5].AsInteger() == 1);
    CHECK(draw.args[6].AsInteger() == 4096);
    CHECK(draw.args[7].AsInteger() == 0xffffff);
    CHECK(draw.args[8].AsInteger() == 3);
    CHECK(draw.args[9].AsInteger() == 0);
    CHECK(draw.args[10].AsInteger() == 0);
}

TEST_CASE("EdgeShadowDrawText honors independent shadow and edge switches",
          "[textrender][edge-shadow]") {
    EdgeShadowCall call;
    call.args[13] = 0;
    REQUIRE(TJS_SUCCEEDED(call.run()));
    CHECK(call.recorder->calls.empty());

    call.args[8] = 1;
    call.args[6] = 128;
    call.args[7] = 0;
    REQUIRE(TJS_SUCCEEDED(call.run()));
    REQUIRE(call.recorder->calls.size() == 2);
    const auto &shadow = call.recorder->calls[0].args;
    CHECK(shadow[0].AsInteger() == 10);
    CHECK(shadow[1].AsInteger() == 20);
    CHECK(shadow[3].AsInteger() == 0x123456);
    CHECK(shadow[6].AsInteger() == 510);
    CHECK(shadow[7].AsInteger() == 0x123456);
    CHECK(shadow[8].AsInteger() == 2);
    const auto &ink = call.recorder->calls[1].args;
    CHECK(ink[0].AsInteger() == 8);
    CHECK(ink[1].AsInteger() == 23);
    CHECK(ink[3].AsInteger() == 0xff0000);
    CHECK(ink[4].AsInteger() == 128);
    CHECK(ink[5].AsInteger() == 0);
    CHECK(ink[6].AsInteger() == 0);
}

TEST_CASE("EdgeShadowDrawText draws shadow then edge then ink",
          "[textrender][edge-shadow]") {
    EdgeShadowCall call;
    call.args[8] = 1;
    call.args[6] = 255;
    REQUIRE(TJS_SUCCEEDED(call.run()));
    REQUIRE(call.recorder->calls.size() == 3);
    CHECK(call.recorder->calls[0].args[3].AsInteger() == 0x123456);
    CHECK(call.recorder->calls[1].args[3].AsInteger() == 0xffffff);
    CHECK(call.recorder->calls[2].args[3].AsInteger() == 0xff0000);
}

TEST_CASE("EdgeShadowDrawText accepts the eight-argument ink pass",
          "[textrender][edge-shadow]") {
    EdgeShadowCall call;
    call.args[6] = 200;
    REQUIRE(TJS_SUCCEEDED(call.run(8)));
    REQUIRE(call.recorder->calls.size() == 1);
    const auto &ink = call.recorder->calls[0].args;
    CHECK(ink[3].AsInteger() == 0xff0000);
    CHECK(ink[4].AsInteger() == 200);
    CHECK(ink[6].AsInteger() == 0);
    CHECK(ink[8].AsInteger() == 0);
}

TEST_CASE("EdgeShadowDrawText propagates callback failures",
          "[textrender][edge-shadow]") {
    EdgeShadowCall call;
    call.args[8] = 1;
    call.args[6] = 255;
    call.recorder->status = TJS_E_FAIL;
    CHECK(call.run() == TJS_E_FAIL);
    REQUIRE(call.recorder->calls.size() == 1);
}

TEST_CASE("EdgeShadowDrawText preserves default antialiasing",
          "[textrender][edge-shadow]") {
    EdgeShadowCall call;
    call.args[7].Clear();
    REQUIRE(TJS_SUCCEEDED(call.run()));
    REQUIRE(call.recorder->calls.size() == 1);
    CHECK(call.recorder->calls[0].args[5].AsInteger() == 1);
}

TEST_CASE("EdgeShadowDrawText rejects a null callback or layer",
          "[textrender][edge-shadow]") {
    EdgeShadowCall call;
    SECTION("null callback") {
        call.args[0] = tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
    }
    SECTION("null layer") {
        call.args[1] = tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
    }
    CHECK(call.run() == TJS_E_INVALIDPARAM);
    CHECK(call.recorder->calls.empty());
}
