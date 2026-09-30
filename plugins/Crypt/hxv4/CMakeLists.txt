find_package(unofficial-sodium CONFIG REQUIRED)

get_filename_component(KRKR2_ENGINE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(KRKR2_CORE_PATH "${KRKR2_ENGINE_ROOT}/core")

add_library(hxv4_decoder STATIC
    Hxv4Decoder.cpp
    Hxv4Crypto.cpp
)

target_link_libraries(hxv4_decoder PRIVATE
    unofficial-sodium::sodium
    krkr2plugin
)

target_include_directories(hxv4_decoder PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${KRKR2_CORE_PATH}/plugin
    ${KRKR2_CORE_PATH}/base
    ${KRKR2_CORE_PATH}/base/impl
    ${KRKR2_CORE_PATH}/tjs2
    ${KRKR2_CORE_PATH}/common
    ${KRKR2_CORE_PATH}/utils
)