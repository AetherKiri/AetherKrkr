cmake_minimum_required(VERSION 3.16)
project(TestKrkrSound LANGUAGES CXX)

# Wired by the consuming checkout's tests/CMakeLists.txt, like tests/tjs2.
# The suite opens real OpenAL playback devices and skips itself when the
# machine has none, so it is safe on headless CI runners.
find_package(OpenAL CONFIG REQUIRED)
if(COMMAND aetherkiri_sanitize_openal_link)
    aetherkiri_sanitize_openal_link()
endif()

add_executable(openal-device-follower
    openal_device_follower.cpp
)

target_link_libraries(openal-device-follower
    PRIVATE
        Catch2::Catch2WithMain
        aether_openal_device_follower
        OpenAL::OpenAL
)

if(APPLE AND NOT IOS)
    # The hidden "[hardware]" case switches the macOS default output.
    target_link_libraries(openal-device-follower PRIVATE
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
endif()

catch_discover_tests(openal-device-follower)
