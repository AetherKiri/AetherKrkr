cmake_minimum_required(VERSION 3.28)
project(xp3 LANGUAGES CXX VERSION 1.0.0)

# TVPTransformText/TVPPrefetchText live in the consuming checkout's
# engine_api (Phase 2d): the standalone xp3 tool links the engine targets
# without the engine_api library, so the runtime-neutral transform hook TU
# is compiled directly into it (the header path arrives through
# krkr2plugin -> krkr2core -> core_utils_module's public includes). The
# source path is injected by the consuming AetherKiri build, mirroring the
# standalone krkr2 test suites.
if(NOT DEFINED KRKR2_TEXT_TRANSFORM_SOURCE)
    message(FATAL_ERROR
        "the xp3 tool requires KRKR2_TEXT_TRANSFORM_SOURCE: point it at "
        "the engine_api src/TextTransform.cpp of the consuming AetherKiri "
        "checkout")
endif()

add_executable(${PROJECT_NAME}
    main.cpp
    ${KRKR2_TEXT_TRANSFORM_SOURCE}
)

find_package(argparse CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE argparse::argparse)

target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_environ_module krkr2plugin)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROGRAM_NAME="${PROJECT_NAME}"
    VERSION="${PROJECT_VERSION}"
)
