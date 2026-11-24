#include <catch2/catch_test_macros.hpp>

#include "OpenALDeviceFollower.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#define AETHER_TEST_COREAUDIO 1
#include <CoreAudio/CoreAudio.h>
#endif
#endif

namespace {

    // Owns one playback device opened on the default output.
    struct ScopedDevice {
        ALCdevice *device = alcOpenDevice(nullptr);

        ScopedDevice() = default;
        ScopedDevice(const ScopedDevice &) = delete;
        ScopedDevice &operator=(const ScopedDevice &) = delete;

        ~ScopedDevice() {
            if(device != nullptr) {
                aether_openal_untrack_device(device);
                alcCloseDevice(device);
            }
        }
    };

    void RequireFollowingSupport(const ScopedDevice &output) {
        if(output.device == nullptr) {
            SKIP("no OpenAL playback device is available");
        }
        if(aether_openal_default_device_events_available() == 0) {
            SKIP("this OpenAL backend does not report default output "
                 "changes");
        }
    }

    void SimulateDefaultOutputChange() {
        aether_openal_simulate_system_event_for_testing(
            ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT,
            ALC_PLAYBACK_DEVICE_SOFT);
    }

    bool IsConnected(ALCdevice *device) {
        ALCint connected = ALC_FALSE;
        alcGetIntegerv(device, ALC_CONNECTED, 1, &connected);
        return connected == ALC_TRUE;
    }

} // namespace

TEST_CASE("devices are left alone until the default output changes",
          "[openal][follower]") {
    ScopedDevice output;
    RequireFollowingSupport(output);

    CHECK(aether_openal_poll_device(nullptr) == AETHER_OPENAL_POLL_IDLE);
    CHECK(aether_openal_poll_device(output.device) == AETHER_OPENAL_POLL_IDLE);

    aether_openal_track_device(output.device);
    CHECK(aether_openal_poll_device(output.device) == AETHER_OPENAL_POLL_IDLE);
    CHECK(aether_openal_poll_device(output.device) == AETHER_OPENAL_POLL_IDLE);
}

TEST_CASE("one default output change reopens every tracked device once",
          "[openal][follower]") {
    ScopedDevice first;
    ScopedDevice second;
    RequireFollowingSupport(first);
    REQUIRE(second.device != nullptr);
    aether_openal_track_device(first.device);
    aether_openal_track_device(second.device);

    SimulateDefaultOutputChange();

    CHECK(aether_openal_poll_device(first.device) ==
          AETHER_OPENAL_POLL_REOPENED);
    CHECK(aether_openal_poll_device(first.device) == AETHER_OPENAL_POLL_IDLE);
    CHECK(aether_openal_poll_device(second.device) ==
          AETHER_OPENAL_POLL_REOPENED);
    CHECK(aether_openal_poll_device(second.device) == AETHER_OPENAL_POLL_IDLE);
    CHECK(IsConnected(first.device));
    CHECK(IsConnected(second.device));
}

TEST_CASE("unrelated events do not reopen devices", "[openal][follower]") {
    ScopedDevice output;
    RequireFollowingSupport(output);
    aether_openal_track_device(output.device);

    aether_openal_simulate_system_event_for_testing(
        ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT, ALC_CAPTURE_DEVICE_SOFT);
    aether_openal_simulate_system_event_for_testing(
        ALC_EVENT_TYPE_DEVICE_ADDED_SOFT, ALC_PLAYBACK_DEVICE_SOFT);
    aether_openal_simulate_system_event_for_testing(
        ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT, ALC_PLAYBACK_DEVICE_SOFT);

    CHECK(aether_openal_poll_device(output.device) == AETHER_OPENAL_POLL_IDLE);
}

TEST_CASE("an untracked device is not reopened", "[openal][follower]") {
    ScopedDevice output;
    RequireFollowingSupport(output);
    aether_openal_track_device(output.device);
    aether_openal_untrack_device(output.device);

    SimulateDefaultOutputChange();

    CHECK(aether_openal_poll_device(output.device) == AETHER_OPENAL_POLL_IDLE);
}

TEST_CASE("changes that happened before tracking are not replayed",
          "[openal][follower]") {
    ScopedDevice output;
    RequireFollowingSupport(output);

    SimulateDefaultOutputChange();
    aether_openal_track_device(output.device);

    CHECK(aether_openal_poll_device(output.device) == AETHER_OPENAL_POLL_IDLE);
}

TEST_CASE("reopening keeps the context and an active source playing",
          "[openal][follower]") {
    ScopedDevice output;
    RequireFollowingSupport(output);
    ALCcontext *context = alcCreateContext(output.device, nullptr);
    REQUIRE(context != nullptr);
    REQUIRE(alcMakeContextCurrent(context) == ALC_TRUE);

    // One second of silence on a looping source keeps the source playing
    // across the reopen without making any sound.
    constexpr ALsizei kRate = 44100;
    const std::vector<short> silence(kRate, 0);
    ALuint buffer = 0;
    ALuint source = 0;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, AL_FORMAT_MONO16, silence.data(),
                 static_cast<ALsizei>(silence.size() * sizeof(short)), kRate);
    alGenSources(1, &source);
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcei(source, AL_LOOPING, AL_TRUE);
    alSourcePlay(source);
    REQUIRE(alGetError() == AL_NO_ERROR);

    aether_openal_track_device(output.device);
    SimulateDefaultOutputChange();
    CHECK(aether_openal_poll_device(output.device) ==
          AETHER_OPENAL_POLL_REOPENED);

    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    CHECK(alcGetCurrentContext() == context);
    CHECK(alIsSource(source) == AL_TRUE);
    CHECK(state == AL_PLAYING);
    CHECK(IsConnected(output.device));

    alSourceStop(source);
    alDeleteSources(1, &source);
    alDeleteBuffers(1, &buffer);
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(context);
}

#if defined(AETHER_TEST_COREAUDIO)

namespace {

    AudioObjectPropertyAddress
    GlobalProperty(AudioObjectPropertySelector selector) {
        return { selector, kAudioObjectPropertyScopeGlobal,
                 kAudioObjectPropertyElementMain };
    }

    AudioObjectID DefaultOutput() {
        const AudioObjectPropertyAddress address =
            GlobalProperty(kAudioHardwarePropertyDefaultOutputDevice);
        AudioObjectID device = kAudioObjectUnknown;
        UInt32 size = sizeof(device);
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0,
                                   nullptr, &size, &device);
        return device;
    }

    bool SetDefaultOutput(AudioObjectID device) {
        const AudioObjectPropertyAddress address =
            GlobalProperty(kAudioHardwarePropertyDefaultOutputDevice);
        return AudioObjectSetPropertyData(kAudioObjectSystemObject, &address, 0,
                                          nullptr, sizeof(device),
                                          &device) == noErr;
    }

    bool HasOutputStreams(AudioObjectID device) {
        const AudioObjectPropertyAddress address = {
            kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };
        UInt32 size = 0;
        return AudioObjectGetPropertyDataSize(device, &address, 0, nullptr,
                                              &size) == noErr &&
            size > 0;
    }

    std::vector<AudioObjectID> OutputDevices() {
        const AudioObjectPropertyAddress address =
            GlobalProperty(kAudioHardwarePropertyDevices);
        UInt32 size = 0;
        if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0,
                                          nullptr, &size) != noErr) {
            return {};
        }
        std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
        if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0,
                                      nullptr, &size,
                                      devices.data()) != noErr) {
            return {};
        }
        std::vector<AudioObjectID> outputs;
        for(const AudioObjectID device : devices) {
            if(HasOutputStreams(device)) {
                outputs.push_back(device);
            }
        }
        return outputs;
    }

    std::string DeviceName(AudioObjectID device) {
        const AudioObjectPropertyAddress address =
            GlobalProperty(kAudioObjectPropertyName);
        CFStringRef name = nullptr;
        UInt32 size = sizeof(name);
        if(AudioObjectGetPropertyData(device, &address, 0, nullptr, &size,
                                      &name) != noErr ||
           name == nullptr) {
            return {};
        }
        char buffer[512] = {};
        CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8);
        CFRelease(name);
        return buffer;
    }

    // Puts the user's default output back even when an assertion fails.
    struct DefaultOutputRestorer {
        AudioObjectID original;
        ~DefaultOutputRestorer() {
            if(DefaultOutput() != original) {
                SetDefaultOutput(original);
            }
        }
    };

    AetherOpenALPollResult PollUntilSettled(ALCdevice *device) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while(std::chrono::steady_clock::now() < deadline) {
            const AetherOpenALPollResult result =
                aether_openal_poll_device(device);
            if(result != AETHER_OPENAL_POLL_IDLE) {
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return AETHER_OPENAL_POLL_IDLE;
    }

    std::string CurrentOutputName(ALCdevice *device) {
        const ALCchar *name = alcGetString(device, ALC_ALL_DEVICES_SPECIFIER);
        return name != nullptr ? name : "";
    }

} // namespace

// Hidden by default because it changes the macOS default output device. Run
// it explicitly with the "[hardware]" tag on a machine with at least two
// outputs; the original default output is restored afterwards.
TEST_CASE("switching the macOS default output moves a tracked device",
          "[.][hardware][openal][follower]") {
    ScopedDevice output;
    RequireFollowingSupport(output);

    const AudioObjectID original = DefaultOutput();
    REQUIRE(original != kAudioObjectUnknown);
    AudioObjectID other = kAudioObjectUnknown;
    for(const AudioObjectID device : OutputDevices()) {
        if(device != original) {
            other = device;
            break;
        }
    }
    if(other == kAudioObjectUnknown) {
        SKIP("needs a second audio output device");
    }
    DefaultOutputRestorer restorer{ original };
    aether_openal_track_device(output.device);
    INFO("original output: " << DeviceName(original));
    INFO("other output: " << DeviceName(other));
    CHECK(CurrentOutputName(output.device) == DeviceName(original));

    REQUIRE(SetDefaultOutput(other));
    REQUIRE(PollUntilSettled(output.device) == AETHER_OPENAL_POLL_REOPENED);
    CHECK(CurrentOutputName(output.device) == DeviceName(other));

    REQUIRE(SetDefaultOutput(original));
    REQUIRE(PollUntilSettled(output.device) == AETHER_OPENAL_POLL_REOPENED);
    CHECK(CurrentOutputName(output.device) == DeviceName(original));
}

#endif
