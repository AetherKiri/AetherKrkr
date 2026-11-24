#include "OpenALDeviceFollower.h"

#include <AL/alc.h>
#if __has_include(<AL/alext.h>)
#include <AL/alext.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

// Fallback declarations for OpenAL headers that predate these extensions.
// The values come from the OpenAL Soft extension specifications; the runtime
// checks below keep older implementations on the no-op path.
#ifndef ALC_APIENTRY
#define ALC_APIENTRY
#endif

#ifndef ALC_CONNECTED
#define ALC_CONNECTED 0x313
#endif

#ifndef ALC_SOFT_reopen_device
typedef ALCboolean(ALC_APIENTRY *LPALCREOPENDEVICESOFT)(ALCdevice *device,
                                                        const ALCchar *name,
                                                        const ALCint *attribs);
#endif

#ifndef ALC_SOFT_system_events
#define ALC_PLAYBACK_DEVICE_SOFT 0x19D4
#define ALC_CAPTURE_DEVICE_SOFT 0x19D5
#define ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT 0x19D6
#define ALC_EVENT_TYPE_DEVICE_ADDED_SOFT 0x19D7
#define ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT 0x19D8
#define ALC_EVENT_SUPPORTED_SOFT 0x19D9
#define ALC_EVENT_NOT_SUPPORTED_SOFT 0x19DA
typedef void(ALC_APIENTRY *ALCEVENTPROCTYPESOFT)(
    ALCenum eventType, ALCenum deviceType, ALCdevice *device, ALCsizei length,
    const ALCchar *message, void *userParam);
typedef ALCenum(ALC_APIENTRY *LPALCEVENTISSUPPORTEDSOFT)(ALCenum eventType,
                                                         ALCenum deviceType);
typedef ALCboolean(ALC_APIENTRY *LPALCEVENTCONTROLSOFT)(ALCsizei count,
                                                        const ALCenum *events,
                                                        ALCboolean enable);
typedef void(ALC_APIENTRY *LPALCEVENTCALLBACKSOFT)(
    ALCEVENTPROCTYPESOFT callback, void *userParam);
#endif

namespace {

    using Clock = std::chrono::steady_clock;

    // A disconnected device is retried at most this often, so a missing
    // output does not turn every poll into a reopen attempt.
    constexpr auto kDisconnectedRetryInterval = std::chrono::milliseconds(500);

    struct DeviceState {
        std::uint64_t seen_generation = 0;
        Clock::time_point next_disconnect_retry{};
    };

    // Bumped from the OpenAL event thread whenever the default playback
    // output changes. Each tracked device remembers the last value it acted
    // on, so one change reopens every tracked device exactly once.
    std::atomic<std::uint64_t> g_generation{ 0 };
    std::atomic<bool> g_events_available{ false };
    std::once_flag g_install_once;
    std::mutex g_mutex;
    std::unordered_map<ALCdevice *, DeviceState> g_devices;

    void ALC_APIENTRY OnSystemEvent(ALCenum event_type, ALCenum device_type,
                                    ALCdevice *, ALCsizei, const ALCchar *,
                                    void *) noexcept {
        // Runs on an OpenAL-internal thread. Only record the change here;
        // the threads that own the devices reopen them while polling.
        if(device_type == ALC_PLAYBACK_DEVICE_SOFT &&
           event_type == ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT) {
            g_generation.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void InstallEventCallback() {
        if(alcIsExtensionPresent(nullptr, "ALC_SOFT_system_events") !=
           ALC_TRUE) {
            return;
        }
        const auto is_supported = reinterpret_cast<LPALCEVENTISSUPPORTEDSOFT>(
            alcGetProcAddress(nullptr, "alcEventIsSupportedSOFT"));
        const auto control = reinterpret_cast<LPALCEVENTCONTROLSOFT>(
            alcGetProcAddress(nullptr, "alcEventControlSOFT"));
        const auto set_callback = reinterpret_cast<LPALCEVENTCALLBACKSOFT>(
            alcGetProcAddress(nullptr, "alcEventCallbackSOFT"));
        if(is_supported == nullptr || control == nullptr ||
           set_callback == nullptr) {
            return;
        }
        // Backends without device change notifications (for example iOS
        // RemoteIO) report the event as unsupported.
        if(is_supported(ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT,
                        ALC_PLAYBACK_DEVICE_SOFT) != ALC_EVENT_SUPPORTED_SOFT) {
            return;
        }
        set_callback(OnSystemEvent, nullptr);
        const ALCenum events[] = { ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT };
        if(control(1, events, ALC_TRUE) != ALC_TRUE) {
            set_callback(nullptr, nullptr);
            return;
        }
        g_events_available.store(true, std::memory_order_release);
    }

    void EnsureInstalled() {
        std::call_once(g_install_once, InstallEventCallback);
    }

    bool IsDisconnected(ALCdevice *device) {
        ALCint connected = ALC_TRUE;
        alcGetIntegerv(device, ALC_CONNECTED, 1, &connected);
        return connected == ALC_FALSE;
    }

    bool ReopenOnDefaultOutput(ALCdevice *device) {
        if(alcIsExtensionPresent(device, "ALC_SOFT_reopen_device") !=
           ALC_TRUE) {
            return false;
        }
        const auto reopen = reinterpret_cast<LPALCREOPENDEVICESOFT>(
            alcGetProcAddress(device, "alcReopenDeviceSOFT"));
        // A null device name selects the current system default output.
        return reopen != nullptr &&
            reopen(device, nullptr, nullptr) == ALC_TRUE;
    }

} // namespace

extern "C" {

void aether_openal_track_device(void *opaque) {
    auto *device = static_cast<ALCdevice *>(opaque);
    if(device == nullptr) {
        return;
    }
    EnsureInstalled();
    std::lock_guard<std::mutex> lock(g_mutex);
    DeviceState &state = g_devices[device];
    state.seen_generation = g_generation.load(std::memory_order_acquire);
    state.next_disconnect_retry = Clock::time_point{};
}

void aether_openal_untrack_device(void *opaque) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_devices.erase(static_cast<ALCdevice *>(opaque));
}

AetherOpenALPollResult aether_openal_poll_device(void *opaque) {
    auto *device = static_cast<ALCdevice *>(opaque);
    // Without change notifications there is no reliable signal to act on;
    // platforms such as iOS keep handling their own device lifecycle.
    if(device == nullptr ||
       !g_events_available.load(std::memory_order_acquire)) {
        return AETHER_OPENAL_POLL_IDLE;
    }

    const std::uint64_t generation =
        g_generation.load(std::memory_order_acquire);
    bool default_changed = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto found = g_devices.find(device);
        if(found == g_devices.end()) {
            return AETHER_OPENAL_POLL_IDLE;
        }
        if(found->second.seen_generation != generation) {
            found->second.seen_generation = generation;
            default_changed = true;
        }
    }

    if(!default_changed) {
        if(!IsDisconnected(device)) {
            return AETHER_OPENAL_POLL_IDLE;
        }
        const Clock::time_point now = Clock::now();
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto found = g_devices.find(device);
        if(found == g_devices.end() ||
           now < found->second.next_disconnect_retry) {
            return AETHER_OPENAL_POLL_IDLE;
        }
        found->second.next_disconnect_retry = now + kDisconnectedRetryInterval;
    }

    return ReopenOnDefaultOutput(device) ? AETHER_OPENAL_POLL_REOPENED
                                         : AETHER_OPENAL_POLL_FAILED;
}

int aether_openal_default_device_events_available(void) {
    EnsureInstalled();
    return g_events_available.load(std::memory_order_acquire) ? 1 : 0;
}

void aether_openal_simulate_system_event_for_testing(int event_type,
                                                     int device_type) {
    OnSystemEvent(static_cast<ALCenum>(event_type),
                  static_cast<ALCenum>(device_type), nullptr, 0, nullptr,
                  nullptr);
}

} // extern "C"
