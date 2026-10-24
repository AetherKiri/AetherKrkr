#include "StartupPatchState.h"
#include "tjsArray.h"
#include "tjsError.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using Name = std::basic_string<tjs_char>;

Name BindingName(const ttstr &name) {
    return name.IsEmpty() ? Name() : Name(name.c_str(), name.GetLen());
}

struct Binding {
    ttstr name;
    tjs_uint32 flags;
    tTJSVariant value;
};

class CollectBindings final : public tTJSDispatch {
    std::vector<Binding> &bindings_;
public:
    explicit CollectBindings(std::vector<Binding> &bindings) : bindings_(bindings) {}
    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                        tTJSVariant *result, tjs_int count, tTJSVariant **args,
                        iTJSDispatch2 *) override {
        if(count < 3) return TJS_E_BADPARAMCOUNT;
        bindings_.push_back({ttstr(*args[0]),
            static_cast<tjs_uint32>(args[1]->AsInteger()), *args[2]});
        if(result) *result = true;
        return TJS_S_OK;
    }
};

tjs_error ReadBindings(iTJSDispatch2 *object, std::vector<Binding> &bindings) {
    auto *collector = new CollectBindings(bindings);
    tTJSVariant callback(collector, collector);
    collector->Release();
    auto closure = callback.AsObjectClosureNoAddRef();
    return object->EnumMembers(TJS_IGNOREPROP, &closure, object);
}

tTJSArrayNI *ArrayInstance(iTJSDispatch2 *object) {
    iTJSNativeInstance *instance = nullptr;
    if(TJS_SUCCEEDED(object->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, TJSGetArrayClassID(), &instance)))
        return static_cast<tTJSArrayNI *>(instance);
    return nullptr;
}

} // namespace

struct tTVPStartupPatchState::State {
    struct Object {
        tTJSVariant owner;
        std::vector<Binding> bindings;
        std::vector<tTJSVariant> items;
        bool array = false;
    };
    std::vector<Object> objects;
};

thread_local tTVPStartupPatchState *tTVPStartupPatchState::active_ = nullptr;

tTVPStartupPatchState::tTVPStartupPatchState(tTJS *engine, bool trackPlugins) :
    state_(std::make_unique<State>()), engine_(engine), tracksPlugins_(trackPlugins) {
    std::vector<tTJSVariant> pending{
        tTJSVariant(engine->GetGlobalNoAddRef(), engine->GetGlobalNoAddRef())};
    std::set<iTJSDispatch2 *> visited;
    auto queue = [&](const tTJSVariant &value) {
        if(value.Type() != tvtObject) return;
        const auto closure = value.AsObjectClosureNoAddRef();
        if(closure.Object) pending.emplace_back(closure.Object, closure.Object);
        if(closure.ObjThis && closure.ObjThis != closure.Object)
            pending.emplace_back(closure.ObjThis, closure.ObjThis);
    };
    while(!pending.empty()) {
        State::Object saved;
        saved.owner = pending.back();
        pending.pop_back();
        auto *object = saved.owner.AsObjectNoAddRef();
        if(!visited.insert(object).second) continue;
        if(auto *array = ArrayInstance(object)) {
            saved.array = true;
            saved.items = array->Items;
        }
        const auto status = ReadBindings(object, saved.bindings);
        if(TJS_FAILED(status)) {
            if(status != TJS_E_NOTIMPL && status != TJS_E_INVALIDTYPE &&
               status != TJS_E_INVALIDOBJECT)
                TJSThrowFrom_tjs_error(status, nullptr);
            if(!saved.array) continue;
        }
        for(const auto &binding : saved.bindings) queue(binding.value);
        for(const auto &item : saved.items) queue(item);
        state_->objects.push_back(std::move(saved));
    }
    if(tracksPlugins_) {
        previous_ = active_;
        active_ = this;
    }
}

tTVPStartupPatchState::~tTVPStartupPatchState() {
    if(tracksPlugins_) active_ = previous_;
}

void tTVPStartupPatchState::PreserveChanges(const tTVPStartupPatchState &before) {
    tTVPStartupPatchState after(engine_);
    std::map<iTJSDispatch2 *, const State::Object *> prior;
    std::map<iTJSDispatch2 *, size_t> original;
    for(const auto &object : before.state_->objects)
        prior.emplace(object.owner.AsObjectNoAddRef(), &object);
    for(size_t i = 0; i < state_->objects.size(); ++i)
        original.emplace(state_->objects[i].owner.AsObjectNoAddRef(), i);
    for(auto &object : after.state_->objects) {
        auto *identity = object.owner.AsObjectNoAddRef();
        const auto priorObject = prior.find(identity);
        if(priorObject == prior.end()) {
            const auto entry = original.find(identity);
            if(entry != original.end()) state_->objects[entry->second] = std::move(object);
            else {
                original.emplace(identity, state_->objects.size());
                state_->objects.push_back(std::move(object));
            }
            continue;
        }
        auto entry = original.find(identity);
        if(entry == original.end()) continue;
        auto &saved = state_->objects[entry->second];
        std::map<Name, const Binding *> oldBindings, newBindings;
        for(const auto &binding : priorObject->second->bindings)
            oldBindings.emplace(BindingName(binding.name), &binding);
        for(const auto &binding : object.bindings)
            newBindings.emplace(BindingName(binding.name), &binding);
        auto &bindings = saved.bindings;
        for(const auto &[name, old] : oldBindings) {
            if(!newBindings.count(name))
                bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                    [&](const Binding &value) { return BindingName(value.name) == name; }),
                    bindings.end());
        }
        for(const auto &[name, value] : newBindings) {
            const auto old = oldBindings.find(name);
            if(old != oldBindings.end() && old->second->flags == value->flags &&
               old->second->value.DiscernCompare(value->value)) continue;
            auto found = std::find_if(bindings.begin(), bindings.end(),
                [&](const Binding &binding) { return BindingName(binding.name) == name; });
            if(found == bindings.end()) bindings.push_back(*value);
            else *found = *value;
        }
        if(object.array) {
            const auto &oldItems = priorObject->second->items;
            const bool unchanged = oldItems.size() == object.items.size() &&
                std::equal(oldItems.begin(), oldItems.end(), object.items.begin(),
                    [](const tTJSVariant &a, const tTJSVariant &b) { return a.DiscernCompare(b); });
            if(!unchanged) saved.items = object.items;
        }
    }
}

tTVPStartupPatchPluginScope::tTVPStartupPatchPluginScope() :
    target_(tTVPStartupPatchState::active_) {
    if(target_) before_ = std::make_unique<tTVPStartupPatchState>(target_->engine_);
}

void tTVPStartupPatchPluginScope::Commit() {
    if(target_) target_->PreserveChanges(*before_);
}

void tTVPStartupPatchState::SealAttempt() {
    attempted_ = std::make_unique<tTVPStartupPatchState>(engine_);
    if(tracksPlugins_) {
        active_ = previous_;
        tracksPlugins_ = false;
    }
}

void tTVPStartupPatchState::PrepareRetry() {
    if(!attempted_) return;
    std::map<iTJSDispatch2 *, const State::Object *> attempted;
    for(const auto &object : attempted_->state_->objects)
        attempted.emplace(object.owner.AsObjectNoAddRef(), &object);
    auto same = [](const Binding &a, const Binding &b) {
        return a.flags == b.flags && a.value.DiscernCompare(b.value);
    };
    auto sameItems = [](const std::vector<tTJSVariant> &a,
                        const std::vector<tTJSVariant> &b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
            [](const tTJSVariant &x, const tTJSVariant &y) { return x.DiscernCompare(y); });
    };
    for(const auto &saved : state_->objects) {
        auto *object = saved.owner.AsObjectNoAddRef();
        const auto entry = attempted.find(object);
        if(entry == attempted.end()) continue;
        const auto &partial = *entry->second;
        std::vector<Binding> current;
        const auto status = ReadBindings(object, current);
        if(TJS_FAILED(status) && !(saved.array && status == TJS_E_NOTIMPL))
            TJSThrowFrom_tjs_error(status, nullptr);
        std::map<Name, const Binding *> original, changes, existing;
        for(const auto &binding : saved.bindings)
            original.emplace(BindingName(binding.name), &binding);
        for(const auto &binding : partial.bindings)
            changes.emplace(BindingName(binding.name), &binding);
        for(const auto &binding : current)
            existing.emplace(BindingName(binding.name), &binding);
        for(const auto &[name, binding] : changes) {
            const auto found = existing.find(name);
            if(found == existing.end() || !same(*found->second, *binding)) continue;
            const auto before = original.find(name);
            if(before != original.end()) {
                if(same(*before->second, *binding)) continue;
                const auto restored = object->PropSet(
                    TJS_MEMBERENSURE | TJS_IGNOREPROP | before->second->flags,
                    before->second->name.c_str(), nullptr, &before->second->value, object);
                if(TJS_FAILED(restored)) TJSThrowFrom_tjs_error(restored, binding->name.c_str());
            } else if(object != engine_->GetGlobalNoAddRef()) {
                // Keep early global definitions: the framework may already
                // own instances of those classes. Replacement classes inherit
                // their static members when the deferred script runs again.
                const auto removed = object->DeleteMember(
                    0, binding->name.c_str(), nullptr, object);
                if(TJS_FAILED(removed) && removed != TJS_E_MEMBERNOTFOUND)
                    TJSThrowFrom_tjs_error(removed, binding->name.c_str());
            }
        }
        for(const auto &[name, binding] : original) {
            if(changes.count(name) || existing.count(name)) continue;
            const auto restored = object->PropSet(
                TJS_MEMBERENSURE | TJS_IGNOREPROP | binding->flags,
                binding->name.c_str(), nullptr, &binding->value, object);
            if(TJS_FAILED(restored)) TJSThrowFrom_tjs_error(restored, binding->name.c_str());
        }
        if(saved.array) {
            auto *array = ArrayInstance(object);
            if(!array) TJSThrowFrom_tjs_error(TJS_E_INVALIDOBJECT, nullptr);
            if(sameItems(array->Items, partial.items)) {
                array->Items = saved.items;
            } else if(partial.items.size() >= saved.items.size() &&
                      std::equal(saved.items.begin(), saved.items.end(), partial.items.begin(),
                          [](const tTJSVariant &x, const tTJSVariant &y) { return x.DiscernCompare(y); })) {
                // Startup may append its own callbacks to an existing list.
                // Remove the failed prefix's appended entries only.
                auto next = array->Items.begin();
                for(size_t i = saved.items.size(); i < partial.items.size(); ++i) {
                    auto found = std::find_if(next, array->Items.end(), [&](const tTJSVariant &value) {
                        return value.DiscernCompare(partial.items[i]);
                    });
                    if(found != array->Items.end()) next = array->Items.erase(found);
                }
            }
        }
    }
}
