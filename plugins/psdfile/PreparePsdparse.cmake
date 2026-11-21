# Keep upstream checkout files intact while carrying fixes for the pinned parser.
function(krkr2_prepare_psdparse source_root)
    set(overlay "${CMAKE_CURRENT_BINARY_DIR}/patched-psdparse")
    file(MAKE_DIRECTORY "${overlay}")
    get_target_property(psdparse_sources psdparse SOURCES)
    foreach(name psdimage.cpp psdparse.cpp)
        set(source "${source_root}/psdparse/${name}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
        file(READ "${source}" contents)
        set(original "${contents}")
        if(name STREQUAL "psdimage.cpp")
            # The ZIP prediction scratch array has an explicit owning type.
            string(REPLACE "    void *buf = 0;\n    if (depth == 8 || depth == 16)"
                "    std::unique_ptr<uint8_t[]> scratch;\n    void *buf = nullptr;\n    if (depth == 8 || depth == 16)" contents "${contents}")
            string(REPLACE "      buf = new uint8_t[dstSize];"
                "      scratch.reset(new uint8_t[dstSize]);\n      buf = scratch.get();" contents "${contents}")
            string(REPLACE "      if (buf != dst) {\n        delete [] buf;\n      }\n" "" contents "${contents}")
            string(REPLACE "      delete [] buf;\n" "" contents "${contents}")
            # Widen the multiply so full-opacity 16/32-bit channels stay in range.
            string(REPLACE "static float f = 1.0f / ((1LL << (sizeof(T) * 8)) - 1);"
                "constexpr uint64_t channelMax = (1ULL << (sizeof(T) * 8)) - 1;" contents "${contents}")
            string(REPLACE "*out++ = (T)(*ap++ * *mp++ * f);"
                "*out++ = static_cast<T>(static_cast<uint64_t>(*ap++) * *mp++ / channelMax);" contents "${contents}")
            if(NOT contents MATCHES "scratch.get\\(\\)" OR NOT contents MATCHES "/ channelMax" OR contents MATCHES "delete \\[\\] buf")
                message(FATAL_ERROR "Update the PSD image overlay for the selected psdparse revision")
            endif()
        else()
            # LayerBlendingRange owns a vector, which needs ordinary assignment.
            string(REPLACE "std::memset(&ex.layerBlendingRange, 0, sizeof(LayerBlendingRange));"
                "ex.layerBlendingRange = LayerBlendingRange{};" contents "${contents}")
        endif()
        if(contents STREQUAL original OR NOT name IN_LIST psdparse_sources)
            message(FATAL_ERROR "Update the ${name} overlay for the selected psdparse revision")
        endif()
        file(CONFIGURE OUTPUT "${overlay}/${name}" CONTENT "${contents}" @ONLY)
        list(REMOVE_ITEM psdparse_sources "${name}")
        list(APPEND psdparse_sources "${overlay}/${name}")
    endforeach()
    set_property(TARGET psdparse PROPERTY SOURCES "${psdparse_sources}")
endfunction()
