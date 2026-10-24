#include <catch2/catch_test_macros.hpp>

#include "ncbind.hpp"
#include "StorageIntf.h"
#include "TextStream.h"
#include "StartupPatchState.h"
#include "tjs.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <string>

extern tTJS *TVPScriptEngine;

namespace {

class PatchRuntime {
    bool mock_ = TJS::TVPIsMockEnabled();
public:
    PatchRuntime() {
        TJS::TVPSetMockEnabled(false);
        if(!TVPGetScriptEngine()) TVPScriptEngine = new tTJS();
        TJSCreateTextStreamForRead = TVPCreateTextStreamForRead;
        TJSCreateTextStreamForWrite = TVPCreateTextStreamForWrite;
        auto *global = TVPGetScriptEngine()->GetGlobalNoAddRef();
        tTJSVariant value;
        if(TJS_FAILED(global->PropGet(TJS_MEMBERMUSTEXIST,
                                       TJS_W("Storages"), nullptr, &value, global))) {
            auto *storages = TVPCreateNativeClass_Storages();
            value = tTJSVariant(storages);
            storages->Release();
            REQUIRE(TJS_SUCCEEDED(global->PropSet(TJS_MEMBERENSURE,
                TJS_W("Storages"), nullptr, &value, global)));
        }
        ncbAutoRegister::AllRegist();
        REQUIRE(ncbAutoRegister::LoadModule(TJS_W("kirikiroid2.dll")));
    }
    ~PatchRuntime() { TJS::TVPSetMockEnabled(mock_); }
    tTJSVariant eval(const tjs_char *source) {
        tTJSVariant result;
        TVPGetScriptEngine()->EvalExpression(source, &result);
        return result;
    }
    void execute(const tjs_char *source) {
        TVPGetScriptEngine()->ExecScript(source);
    }
    void set(const tjs_char *name, const tTJSVariant &value) {
        auto *global = TVPGetScriptEngine()->GetGlobalNoAddRef();
        REQUIRE(TJS_SUCCEEDED(global->PropSet(TJS_MEMBERENSURE,
            name, nullptr, &value, global)));
    }
};

class TemporaryFile {
public:
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("krkr-patch-" + std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count()) + ".bin");
    ~TemporaryFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

} // namespace

TEST_CASE("Kirikiroid strings round trip patch encodings", "[kirikiroid-patch]") {
    PatchRuntime runtime;
    CHECK(runtime.eval(TJS_W(
        "String.decode(String.encode(\"中文\", \"GBK\"), \"GBK\") == \"中文\""
    )).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W(
        "String.decode(String.encode(\"日本語😀\")) == \"日本語😀\""
    )).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W(
        "String.decode(<%ff fe 2d 4e 87 65%>) == \"中文\""
    )).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W(
        "String.decode(<%ef bb bf 61 62 63%>) == \"abc\""
    )).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W("String.decode(String.encode(\"\")) == \"\""
    )).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W("String.ord(\"中\")")).AsInteger() == 20013);
    CHECK(runtime.eval(TJS_W("String.ord(123)")).AsInteger() == 123);
    CHECK_THROWS(runtime.eval(TJS_W("String.encode(\"x\", \"missing-codec\")")));
    CHECK_THROWS(runtime.eval(TJS_W("String.decode(<%ff%>, \"UTF-8\")")));
}

TEST_CASE("Kirikiroid Object preserves dispatch statuses and contexts",
           "[kirikiroid-patch]") {
    PatchRuntime runtime;
    runtime.execute(TJS_W(
        "global.__patchObject=%[value:7, add:function(n) { return this.value+n; }];"
        "global.__patchCall=Object.funcCall(__patchObject,0,\"add\",5);"
        "global.__patchGet=Object.propGet(__patchObject,0,\"value\");"
    ));
    CHECK(runtime.eval(TJS_W("__patchCall[0]")).AsInteger() == TJS_S_OK);
    CHECK(runtime.eval(TJS_W("__patchCall[1]")).AsInteger() == 12);
    CHECK(runtime.eval(TJS_W("__patchGet[0]")).AsInteger() == TJS_S_OK);
    CHECK(runtime.eval(TJS_W("__patchGet[1]")).AsInteger() == 7);
    CHECK(runtime.eval(TJS_W("Object.propGet(__patchObject,0x400,\"missing\")[0]"
    )).AsInteger() == TJS_E_MEMBERNOTFOUND);
    CHECK(runtime.eval(TJS_W("Object.propSet(__patchObject,Object.MEMBERENSURE,\"next\",3)"
    )).AsInteger() == TJS_S_OK);
    CHECK(runtime.eval(TJS_W("__patchObject.next")).AsInteger() == 3);
    CHECK(runtime.eval(TJS_W("Object.hasMember(__patchObject,\"value\")"
    )).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W("Object.hasMember(__patchObject,\"missing\")"
    )).AsInteger() == 0);
    CHECK(runtime.eval(TJS_W("Object.getThisObject(__patchObject.add incontextof __patchObject) === __patchObject"
    )).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W(
        "(function() {"
        " var array = [10,20];"
        " var get = Object.propGet(array, 0, 1);"
        " var count = Object.getCount(%[a:1,b:2], 0, void);"
        " var status = Object.propSet(array, 0, 0, 30);"
        " return get[0] == 0 && get[1] == 20 && count[0] == 0 &&"
        "   count[1] == 2 && status == 0 && array[0] == 30;"
        "})()"
    )).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W(
        "(function() {"
        " var object = %[a:1,b:2]; global.__patchNames = [];"
        " Object.enumMembers(object, function(name, flags, value) { __patchNames.add(name); return true; });"
        " var removed = Object.deleteMember(object, 0, \"a\");"
        " return __patchNames.count == 2 && removed >= 0 && !Object.hasMember(object, \"a\");"
        "})()"
    )).AsInteger() == 1);
    runtime.execute(TJS_W("class PatchObject {}"));
    CHECK(runtime.eval(TJS_W(
        "(function() {"
        " var object = new PatchObject();"
        " var matches = Object.isInstanceOf(object, 0, void, \"PatchObject\");"
        " var valid = Object.isValid(object, 0, void);"
        " var invalidated = Object.invalidate(object, 0, void);"
        " return matches == 1 && valid == 1 && invalidated >= 0 &&"
        "   Object.isValid(object, 0, void) == 2;"
        "})()"
    )).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W(
        "(function(){ var object=%[]; object[\"1.5\"]=9;"
        " return Object.hasMember(object,1.5); })()"
    )).AsInteger() == 1);
    CHECK_THROWS(runtime.eval(TJS_W("Object.propGet(null,0,\"value\")")));
    CHECK_THROWS(runtime.eval(TJS_W("Object.isInstanceOf(null,0,0,\"Object\")")));
}

TEST_CASE("Kirikiroid properties keep authored getter and setter closures",
           "[kirikiroid-patch]") {
    PatchRuntime runtime;
    CHECK(runtime.eval(TJS_W(
        "(function() {"
        " global.__patchState = %[value:2]; var object = %[];"
        " var descriptor=%[name:\"twice\"];"
        " descriptor[\"getter\"] = function() { return this.value*2; } incontextof __patchState;"
        " descriptor[\"setter\"] = function(value) { this.value=value/2; } incontextof __patchState;"
        " Object.regProperty(object, descriptor);"
        " object.twice=18;"
        " return __patchState.value == 9 && object.twice == 18 &&"
        "   Object.hasMember(object, \"twice\", Object.IGNOREPROP);"
        "})()"
    )).AsInteger() == 1);
}

TEST_CASE("Kirikiroid text streams close and preserve Unicode", "[kirikiroid-patch]") {
    PatchRuntime runtime;
    TemporaryFile file;
    runtime.set(TJS_W("__patchStreamPath"), tTJSVariant(ttstr(file.path.string())));
    CHECK(runtime.eval(TJS_W(
        "(function() {"
        " var write = new TextWriteStream(__patchStreamPath, \"utf-8\");"
        " write.write(\"中文abc\"); write.close(); write.close();"
        " var read = new TextReadStream(__patchStreamPath);"
        " var first=read.read(2), rest=read.read(); read.close();"
        " return first == \"中文\" && rest == \"abc\";"
        "})()"
    )).AsInteger() == 1);
    CHECK_THROWS(runtime.eval(TJS_W(
        "(function() { var read = new TextReadStream(__patchStreamPath);"
        " read.close(); return read.read(); })()")));
}

TEST_CASE("Kirikiroid BinaryReader reads and builds little endian data",
           "[kirikiroid-patch]") {
    PatchRuntime runtime;
    CHECK(runtime.eval(TJS_W(
        "(function() {"
        " var data = new BinaryReader();"
        " data.addInt8(-2); data.addInt16(0x1234); data.addInt32(0x89abcdef);"
        " data.addInt64(0x123456789); data.addFloat(1.5); data.addDouble(2.25);"
        " data.addString(\"中文\", 8);"
        " var peek=data.peekUInt8(), start=data.position;"
        " var ok=peek==254 && start==0 && data.readInt8()==-2 &&"
        "  data.readUInt16()==0x1234 && data.readUInt32()==0x89abcdef &&"
        "  data.readInt64()==0x123456789 && data.readFloat()==1.5 &&"
        "  data.readDouble()==2.25 && data.readString(8)==\"中文\";"
        " var copy=new BinaryReader(data.getBytes());"
        " copy.skip(1); copy.seek(0);"
        " return ok && data.position==data.length && copy.readInt8()==-2 &&"
        "  copy.length==data.length && copy.readBytes(1000).length==copy.length-1;"
        "})()"
    )).AsInteger() == 1);
    CHECK_THROWS(runtime.eval(TJS_W(
        "(function(){ var data=new BinaryReader(<%01%>); return data.readInt32(); })()")));
    CHECK_THROWS(runtime.eval(TJS_W(
        "(function(){ var data=new BinaryReader(); data.seek(-1); })()")));
}

TEST_CASE("Kirikiroid buffer accessors retain octets and bound mutations",
           "[kirikiroid-patch]") {
    PatchRuntime runtime;
    runtime.execute(TJS_W(
        "global.__patchBuffer=_createBufferAccessor(<%61 62 63%>);"));
    TVPGetScriptEngine()->DoGarbageCollection(true);
    CHECK(runtime.eval(TJS_W(
        "(function(){ __patchBuffer[1]=0x64; __patchBuffer.xor(0,3,1);"
        " return __patchBuffer.count==3 && __patchBuffer[0]==0x60 &&"
        " __patchBuffer[1]==0x65 && __patchBuffer[2]==0x62; })()"
    )).AsInteger() == 1);
    CHECK_THROWS(runtime.eval(TJS_W("__patchBuffer.xor(2,100,1)")));
    CHECK_THROWS(runtime.eval(TJS_W("__patchBuffer.ptr=-1")));
    CHECK(runtime.eval(TJS_W(
        "(function(){ __patchBuffer.ptr=1; ++__patchBuffer[0];"
        " return __patchBuffer[0]==0x66; })()"
    )).AsInteger() == 1);
    runtime.execute(TJS_W("invalidate __patchBuffer;"));
    CHECK_THROWS(runtime.eval(TJS_W("__patchBuffer[0]")));
    CHECK(runtime.eval(TJS_W(
        "(function(){ var empty=_createBufferAccessor(<%%>);"
        " empty.xor(0,0,1); return empty.count==0; })()"
    )).AsInteger() == 1);
    CHECK_THROWS(runtime.eval(TJS_W("_oct_from_addr(0,1)")));
    std::array<unsigned char, 3> bytes{'a','b','c'};
    runtime.set(TJS_W("__patchAddress"), tTJSVariant(
        static_cast<tjs_int64>(reinterpret_cast<uintptr_t>(bytes.data()))));
    CHECK(runtime.eval(TJS_W(
        "String.decode(_oct_from_addr(__patchAddress,3)) == \"abc\""
    )).AsInteger() == 1);
}

TEST_CASE("Kirikiroid IO reads seeks and closes native files", "[kirikiroid-patch]") {
    PatchRuntime runtime;
    TemporaryFile file;
    runtime.set(TJS_W("__patchIoPath"), tTJSVariant(ttstr(file.path.string())));
    CHECK(runtime.eval(TJS_W(
        "(function() {"
        " var file=IO.fopen(__patchIoPath, \"wb+\");"
        " var bytes=String.encode(\"abc中文\");"
        " var written=IO.fwrite(file,bytes), size=IO.ftell(file);"
        " var seek=IO.fseek(file,3,0);"
        " var text=String.decode(IO.fread(file,-1));"
        " var eof=IO.fread(file,1); var close=IO.fclose(file);"
        " return file!=0 && written==9 && size==9 && seek==0 &&"
        "  text==\"中文\" && eof===void && close==0;"
        "})()"
    )).AsInteger() == 1);
    CHECK_THROWS(runtime.eval(TJS_W("IO.ftell(0)")));
}

TEST_CASE("Startup patch retry restores wrappers while preserving framework changes",
           "[kirikiroid-patch]") {
    PatchRuntime runtime;
    runtime.execute(TJS_W(
        "global.__checkpoint=%[count:7,nested:%[label:\"before\"],entries:[1,2]];"
        "__checkpoint.self=__checkpoint;"
    ));
    const auto original = runtime.eval(TJS_W("&Storages.extractStorageExt"));
    tTVPStartupPatchState state(TVPGetScriptEngine());
    runtime.execute(TJS_W(
        "__checkpoint.count=99; __checkpoint.nested.label=\"after\";"
        "__checkpoint.entries.add(3); delete __checkpoint.self;"
        "global.__checkpointAdded=42;"
        "Storages.__checkpointOriginal=&Storages.extractStorageExt;"
        "Storages.extractStorageExt=function(path) { return Storages.__checkpointOriginal(path); };"
    ));
    state.SealAttempt();
    runtime.execute(TJS_W("__checkpoint.entries.add(4); __checkpoint.count=123;"));
    state.PrepareRetry();
    CHECK(runtime.eval(TJS_W("__checkpoint.count")).AsInteger() == 123);
    CHECK(ttstr(runtime.eval(TJS_W("__checkpoint.nested.label"))) == TJS_W("before"));
    CHECK(runtime.eval(TJS_W("__checkpoint.entries.count")).AsInteger() == 3);
    CHECK(runtime.eval(TJS_W("__checkpoint.entries[2]")).AsInteger() == 4);
    CHECK(runtime.eval(TJS_W("__checkpoint.self===__checkpoint")).AsInteger() == 1);
    CHECK(runtime.eval(TJS_W("__checkpointAdded")).AsInteger() == 42);
    CHECK(runtime.eval(TJS_W("Object.hasMember(Storages,\"__checkpointOriginal\")")).AsInteger() == 0);
    const auto restored = runtime.eval(TJS_W("&Storages.extractStorageExt"));
    CHECK(restored.AsObjectNoAddRef() == original.AsObjectNoAddRef());
    CHECK(ttstr(runtime.eval(TJS_W("Storages.extractStorageExt(\"voice.ogg\")")))
        == TJS_W(".ogg"));
}

TEST_CASE("Successful startup patches keep their changes",
           "[kirikiroid-patch]") {
    PatchRuntime runtime;
    runtime.execute(TJS_W("global.__checkpointSuccess=0;"));
    {
        tTVPStartupPatchState state(TVPGetScriptEngine());
        runtime.execute(TJS_W("__checkpointSuccess=42;"));
    }
    CHECK(runtime.eval(TJS_W("__checkpointSuccess")).AsInteger() == 42);
}

TEST_CASE("Startup retry retains plugin registrations and early definitions",
           "[kirikiroid-patch]") {
    PatchRuntime runtime;
    runtime.execute(TJS_W("global.__checkpointHost=%[original:7];"));
    tTVPStartupPatchState state(TVPGetScriptEngine(), true);
    runtime.execute(TJS_W("__checkpointHost.original=99; global.__checkpointScript=1;"));
    {
        tTVPStartupPatchPluginScope plugin;
        runtime.execute(TJS_W(
            "global.__checkpointPlugin=%[value:42];"
            "__checkpointHost.plugin=12;"
        ));
        plugin.Commit();
    }
    runtime.execute(TJS_W("__checkpointPlugin.value=100;"));
    state.SealAttempt();
    state.PrepareRetry();
    CHECK(runtime.eval(TJS_W("__checkpointHost.original")).AsInteger() == 7);
    CHECK(runtime.eval(TJS_W("__checkpointHost.plugin")).AsInteger() == 12);
    CHECK(runtime.eval(TJS_W("__checkpointPlugin.value")).AsInteger() == 42);
    CHECK(runtime.eval(TJS_W("__checkpointScript")).AsInteger() == 1);
}
