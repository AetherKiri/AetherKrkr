cmake_minimum_required(VERSION 3.28)
project(xp3_select LANGUAGES CXX VERSION 1.0.0)

# TVPTransformText/TVPPrefetchText live in the consuming checkout's
# engine_api (Phase 2d) and the transitive core members reference plugin
# symbols (PSB registry, GLES module hooks owned by krkr2plugin's dummy
# stubs): the tjs2+core_base closure is not self-contained. Compile the
# runtime-neutral transform hook TU directly into the tool, mirroring the
# standalone krkr2 test suites, and link the plugin archive like the
# sibling xp3 tool. The source path is injected by the consuming AetherKiri
# build.
if(NOT DEFINED KRKR2_TEXT_TRANSFORM_SOURCE)
    message(FATAL_ERROR
        "the xp3_select tool requires KRKR2_TEXT_TRANSFORM_SOURCE: point it "
        "at the engine_api src/TextTransform.cpp of the consuming AetherKiri "
        "checkout")
endif()

add_executable(${PROJECT_NAME}
    main.cpp
    ${KRKR2_TEXT_TRANSFORM_SOURCE}
)

target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module krkr2plugin)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROGRAM_NAME="${PROJECT_NAME}"
    VERSION="${PROJECT_VERSION}"
)
