#pragma once

// Keeps OpenAL Soft playback devices on the system default output.
//
// OpenAL Soft binds a device opened with a null name to whichever output was
// the default at open time. When the user later switches outputs (for example
// to Bluetooth headphones that connected after the game started), the device
// keeps rendering to the old output. ALC_SOFT_system_events reports default
// output changes, and ALC_SOFT_reopen_device moves a device in place while
// preserving its contexts, sources, buffers and playback offsets.
//
// The event callback is process-wide, so every OpenAL user in the process
// (KiriKiri2 and the runtime bridges) must share this module instead of
// installing a callback of its own.
//
// Devices are passed as untyped pointers because the KiriKiri2 sound backend
// compiles against the Apple OpenAL framework headers on macOS, which name the
// opaque ALCdevice struct differently from OpenAL Soft's headers.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AetherOpenALPollResult {
    // Nothing to do: the device is still on the default output.
    AETHER_OPENAL_POLL_IDLE = 0,
    // The device now renders to the current default output.
    AETHER_OPENAL_POLL_REOPENED = 1,
    // A reopen was needed but OpenAL rejected it; the device keeps its
    // previous output.
    AETHER_OPENAL_POLL_FAILED = 2,
} AetherOpenALPollResult;

// Starts following the default output for `device` (an ALCdevice*). Call it
// right after the device was opened with a null device name.
void aether_openal_track_device(void *device);

// Stops following `device`. Call it before alcCloseDevice.
void aether_openal_untrack_device(void *device);

// Reopens `device` on the current default output when the default changed
// since the previous poll, or when the device reports it was disconnected.
// Call it periodically from the thread that drives the device, never from
// inside an OpenAL callback.
AetherOpenALPollResult aether_openal_poll_device(void *device);

// Returns non-zero when the OpenAL implementation reports default output
// changes. Without that support aether_openal_poll_device never reopens.
int aether_openal_default_device_events_available(void);

// Delivers an ALC_SOFT_system_events notification as if OpenAL had raised
// it. Exposed for tests only.
void aether_openal_simulate_system_event_for_testing(int event_type,
                                                     int device_type);

#ifdef __cplusplus
}
#endif
