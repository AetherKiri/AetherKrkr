cmake_minimum_required(VERSION 3.28)

# KiriKiri2 (krkr2) engine runtime, extracted from the AetherKiri main
# repository (AetherKiri/AetherKiri#241). The AetherKiri checkout embeds this
# repository via add_subdirectory() and provides:
#
#   KRKR2_ENGINE_ABI_INCLUDE_DIR
#       The engine_api ABI headers of the consuming checkout
#       (engine_gpu_bridge.h, TextTransform.h). The core visual/utils modules
#       expose these headers to their consumers, and engine sources resolve
#       TVPTransformText/TVPPrefetchText against the host library across the
#       DSO boundary (Phase 2d link flip).
#   KRKR2_PSDFILE_PACKAGE_ROOT
#       The psdfile adapter package (a submodule of the consuming checkout)
#       that the psdfile plugin compiles against.
#
# AETHERKIRI_INTERNAL_ENABLED, BUILD_GPU_BRIDGE and the
# aetherinternal_extend_* commands (when the private package is available)
# propagate from the consuming build; without them the public fallback
# sources compile instead.
#
# Test suites under tests/ and CLI tools under tools/ are wired by the
# consuming checkout (tests/CMakeLists.txt and tools/CMakeLists.txt of the
# AetherKiri repository); this entry only defines the engine targets.
if(NOT DEFINED KRKR2_ENGINE_ABI_INCLUDE_DIR)
    message(FATAL_ERROR
        "krkr2 requires KRKR2_ENGINE_ABI_INCLUDE_DIR: point it at the "
        "engine_api include directory of an AetherKiri checkout")
endif()
if(NOT DEFINED KRKR2_PSDFILE_PACKAGE_ROOT)
    message(FATAL_ERROR
        "krkr2 requires KRKR2_PSDFILE_PACKAGE_ROOT: point it at the psdfile "
        "package of an AetherKiri checkout")
endif()

project(krkr2)

add_subdirectory(external)
add_subdirectory(core)
add_subdirectory(plugins)
