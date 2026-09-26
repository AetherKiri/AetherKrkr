cmake_minimum_required(VERSION 3.16)
project(TestTJS2 LANGUAGES CXX)

# Self-contained layout: this directory was extracted from the AetherKiri
# checkout's tests/unit-tests/tjs2; engine paths are repository-root relative
# and the suite is wired by the consuming checkout's tests/CMakeLists.txt.
get_filename_component(KRKR2_TESTS_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(tjs2-missing
    missing.cpp
    assign-structure.cpp
    variant-array-stack.cpp
    gles-contract.cpp
    main.cpp
    stubs.cpp
    "${KRKR2_TESTS_ROOT}/core/plugin/PluginCallTracer.cpp"
)

target_include_directories(tjs2-missing PRIVATE
    "${KRKR2_TESTS_ROOT}/core/base/impl"
    "${KRKR2_TESTS_ROOT}/plugins"
)

target_link_libraries(tjs2-missing
    PRIVATE
        Catch2::Catch2
        tjs2
)

catch_discover_tests(tjs2-missing)
