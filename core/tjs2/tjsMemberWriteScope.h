#pragma once

#include "tjsVariant.h"

namespace TJS {

// Observes raw member replacement on one object for a bounded operation.
// Property setter calls are not raw member replacements.
class tTJSMemberWriteScope {
public:
    using Callback = void (*)(const tjs_char *, const tTJSVariant &,
                              const tTJSVariant &);
private:
    inline static thread_local tTJSMemberWriteScope *active_ = nullptr;
    tTJSMemberWriteScope *previous_;
    iTJSDispatch2 *target_;
    Callback callback_;
    bool notifying_ = false;
public:
    tTJSMemberWriteScope(iTJSDispatch2 *target, Callback callback) :
        previous_(active_), target_(target), callback_(callback) { active_ = this; }
    ~tTJSMemberWriteScope() { active_ = previous_; }
    tTJSMemberWriteScope(const tTJSMemberWriteScope &) = delete;
    tTJSMemberWriteScope &operator=(const tTJSMemberWriteScope &) = delete;

    static void Notify(iTJSDispatch2 *target, const tjs_char *name,
                        const tTJSVariant &previous, const tTJSVariant &value) {
        auto *scope = active_;
        if(!scope || scope->target_ != target || scope->notifying_) return;
        scope->notifying_ = true;
        try { scope->callback_(name, previous, value); }
        catch(...) { scope->notifying_ = false; throw; }
        scope->notifying_ = false;
    }
};

} // namespace TJS
