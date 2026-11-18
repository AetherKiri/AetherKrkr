#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>

#include "tjsCommHead.h"
#include "SysInitIntf.h"
#include "TickCount.h"

namespace {
bool engineCleanupCompleted = false;

void requireEngineCleanupAtProcessExit() {
    if(!engineCleanupCompleted) {
        std::fputs("Plugin runner exited without completing engine cleanup\n",
                   stderr);
        std::abort();
    }
}
} // namespace

TEST_CASE("plugin runner completes engine cleanup before process exit") {
    REQUIRE_FALSE(TVPSystemUninitCalled);
    REQUIRE(std::atexit(requireEngineCleanupAtProcessExit) == 0);

    // This callback runs after the engine's worker shutdown handlers. The
    // process-exit check also covers the runner, after Catch has returned.
    TVPAddAtExitHandler(TVP_ATEXIT_PRI_CLEANUP + 1, [] {
        engineCleanupCompleted = true;
    });
    TVPStartTickCount();
}
