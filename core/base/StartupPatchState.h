#pragma once

#include <memory>
#include "tjs.h"

// Records script member bindings and Array contents without evaluating
// properties or duplicating native resources. A failed early prefix stays
// available during startup; immediately before replay, only bindings still
// unchanged by the framework are restored to avoid wrapping them twice.
class tTVPStartupPatchState {
    friend class tTVPStartupPatchPluginScope;
    struct State;
    std::unique_ptr<State> state_;
    std::unique_ptr<tTVPStartupPatchState> attempted_;
    tTJS *engine_;
    tTVPStartupPatchState *previous_ = nullptr;
    bool tracksPlugins_;
    static thread_local tTVPStartupPatchState *active_;
    void PreserveChanges(const tTVPStartupPatchState &before);
public:
    explicit tTVPStartupPatchState(tTJS *engine, bool trackPlugins = false);
    ~tTVPStartupPatchState();
    void SealAttempt();
    void PrepareRetry();
};

// Native plugin registrations outlive a failed script. Retain only the
// bindings changed by the plugin load, excluding surrounding script edits.
class tTVPStartupPatchPluginScope {
    tTVPStartupPatchState *target_;
    std::unique_ptr<tTVPStartupPatchState> before_;
public:
    tTVPStartupPatchPluginScope();
    void Commit();
};
