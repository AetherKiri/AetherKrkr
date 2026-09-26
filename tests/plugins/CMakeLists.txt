cmake_minimum_required(VERSION 3.16)
project(TestPlugins LANGUAGES CXX)

# Self-contained layout: this directory was extracted from the AetherKiri
# checkout's tests/unit-tests/plugins; engine paths are repository-root
# relative and the suite is wired by the consuming checkout's
# tests/CMakeLists.txt (which also provides TEST_CONFIG_DIR for the shared
# test_config.h).
get_filename_component(KRKR2_TESTS_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# TVPTransformText/TVPPrefetchText live in the consuming checkout's
# engine_api (Phase 2d): these standalone krkr2 tests link krkr2core without
# engine_api, so the runtime-neutral transform hook is compiled directly into
# them (the header path arrives through core_utils_module's public includes).
# The source path is injected by the consuming AetherKiri build.
if(NOT DEFINED KRKR2_TEXT_TRANSFORM_SOURCE)
    message(FATAL_ERROR
        "krkr2 plugin tests require KRKR2_TEXT_TRANSFORM_SOURCE: point it "
        "at the engine_api src/TextTransform.cpp of the consuming AetherKiri "
        "checkout")
endif()
set(_ENGINE_API_TEXT_TRANSFORM_SOURCE "${KRKR2_TEXT_TRANSFORM_SOURCE}")

add_executable(motionplayer-dll
    main.cpp
    motionplayer-dll.cpp
    psb-media.cpp
    registry.cpp
    kbad-data-pack.cpp
    textrender-timing.cpp
    text-stream.cpp
    xp3-cx-decoder.cpp
    font-compat.cpp
    font-baseline.cpp
    godot-texture.cpp
    godot-runtime-timing.cpp
    krkrgles-aliases.cpp
    kag-load-contract.cpp
    decode-arena.cpp
    render-simd.cpp
    script-compatibility.cpp
    project-storage.cpp
)

set(_KRKR2PLUGIN_LINK_ITEM "$<LINK_LIBRARY:WHOLE_ARCHIVE,krkr2plugin>")

target_sources(motionplayer-dll PRIVATE ${_ENGINE_API_TEXT_TRANSFORM_SOURCE})

target_link_libraries(motionplayer-dll
    PRIVATE
        Catch2::Catch2
    PUBLIC
        krkr2core
        ${_KRKR2PLUGIN_LINK_ITEM}
)

target_include_directories(motionplayer-dll PRIVATE
    "${TEST_CONFIG_DIR}"
    "${KRKR2_TESTS_ROOT}/core/visual"
    "${KRKR2_TESTS_ROOT}/plugins"
)

if(AETHERKIRI_INTERNAL_ENABLED)
    find_package(lz4 CONFIG REQUIRED)
    target_link_libraries(motionplayer-dll PRIVATE lz4::lz4)
    target_compile_definitions(motionplayer-dll PRIVATE
        AETHERKIRI_EXPECT_INTERNAL_EMOTE=1)
endif()

if(AETHERKIRI_INTERNAL_ENABLED AND NOT BUILD_GPU_BRIDGE)
    target_compile_definitions(motionplayer-dll PRIVATE
        AETHERKIRI_EXPECT_INTERNAL_GRAPHICS=1)
endif()

if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_krkr2plugin_tests)
    aetherinternal_extend_krkr2plugin_tests(motionplayer-dll)
endif()

catch_discover_tests(motionplayer-dll)

# Isolate registration-order tests from the shared plugin world used by the
# larger suite: these deliberately load and unload the graphics module.
add_executable(gles-load-order main.cpp gles-load-order.cpp
    ${_ENGINE_API_TEXT_TRANSFORM_SOURCE})
target_link_libraries(gles-load-order PRIVATE Catch2::Catch2 krkr2core
    ${_KRKR2PLUGIN_LINK_ITEM})
catch_discover_tests(gles-load-order)

add_executable(layer-pointer-gesture main.cpp layer-pointer-gesture.cpp
    ${_ENGINE_API_TEXT_TRANSFORM_SOURCE})
target_link_libraries(layer-pointer-gesture PRIVATE Catch2::Catch2 krkr2core
    ${_KRKR2PLUGIN_LINK_ITEM})
catch_discover_tests(layer-pointer-gesture)
