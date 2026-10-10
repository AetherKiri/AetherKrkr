//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Utilities for Debugging
//---------------------------------------------------------------------------
#ifndef DebugIntfH
#define DebugIntfH

#include "tjsNative.h"
#include "tjs.h"

#include <string>

//---------------------------------------------------------------------------
// global definitions
//---------------------------------------------------------------------------
extern bool TVPAutoLogToFileOnError;
extern bool TVPAutoClearLogOnError;
extern bool TVPLoggingToFile;

extern void TVPSetOnLog(void (*func)(const ttstr &line));

TJS_EXP_FUNC_DEF(void, TVPAddLog, (const ttstr &line));

TJS_EXP_FUNC_DEF(void, TVPAddImportantLog, (const ttstr &line));

//---------------------------------------------------------------------------
// Compatibility receipts
//
// Platform-limited plugin paths must state why a feature is missing instead of
// only returning a failure constant: a silent `false` (or worse, a silent
// `true`) is read by game scripts as "the feature exists but the result was
// empty". Each receipt is emitted once per feature/state with a stable,
// greppable form:
//
//     [Compat] feature=<name> state=<loaded|unimplemented|unavailable|failed> detail=<text>
//
// The receipts are also available as a JSON array for host diagnostics
// (`TVPGetCompatReceiptsJSON`), which engine_get_plugin_debug_info includes.
//---------------------------------------------------------------------------
enum class TJSCompatState {
    Loaded,        // the real implementation is active (no receipt emitted)
    Unimplemented, // a compatibility/mock stub accepts the call without doing the work
    Unavailable,   // this build/runtime provides no implementation at all
    Failed,        // an implementation exists but loading or initialising failed
};

extern const char *TVPCompatStateName(TJSCompatState state);

// Emits the receipt once per (feature, state) pair; later calls are ignored.
extern void TVPAddCompatReceipt(const char *feature, TJSCompatState state,
                                const char *detail);

// Formatted receipts as a JSON array string, for diagnostics payloads.
extern std::string TVPGetCompatReceiptsJSON();

extern ttstr TVPGetLastLog(tjs_uint n);

extern iTJSConsoleOutput *TVPGetTJS2ConsoleOutputGateway();

extern iTJSConsoleOutput *TVPGetTJS2DumpOutputGateway();

extern void TVPTJS2StartDump();

extern void TVPTJS2EndDump();

extern void TVPOnError();

extern ttstr TVPGetImportantLog();

extern void TVPSetLogLocation(const ttstr &loc);

extern ttstr TVPNativeLogLocation;

extern void TVPStartLogToFile(bool clear);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// implement in each platform
//---------------------------------------------------------------------------
// extern void TVPOnErrorHook();
// called from TVPOnError, on system error.
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Debug : TJS Debug Class
//---------------------------------------------------------------------------
class tTJSNC_Debug : public tTJSNativeClass {
public:
    tTJSNC_Debug();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

//---------------------------------------------------------------------------
extern tTJSNativeClass *TVPCreateNativeClass_Debug();
//---------------------------------------------------------------------------

#endif
