# Offline assets for consumer tests only. Contextual installed-package tests
# deliberately use public headers, without the Core source/private include tree.
set(JELLYFRAME_CONTEXTUAL_TEST_FIXTURES_ROOT "" CACHE PATH
    "Optional offline Core contextual fixture directory for installed consumer tests")

function(jellyframe_enable_shaped_test_fixtures target)
    set(_root "${JELLYFRAME_CONTEXTUAL_TEST_FIXTURES_ROOT}")
    if(NOT _root AND JELLYFRAME_RENDER_CORE_SOURCE_DIR AND JELLYFRAME_ENABLE_CONTEXTUAL_LAYOUT)
        set(_root "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures")
    endif()
    if(_root)
        file(TO_CMAKE_PATH "${_root}" _root)
        set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/contextual-fixtures")
        file(MAKE_DIRECTORY "${_generated}")
        set(_header "#pragma once\nnamespace jellyframe::runtime_test_shaped_text {\nstruct FixtureDigests { const char *face, *font, *catalog, *companion, *tables, *eligibility; };\ninline constexpr FixtureDigests fixture_digests[] = {\n")
        foreach(_face regular medium bold)
            set(_row "{\"${_face}\"")
            foreach(_file "contextual_font/${_face}.jffont2"
                    "shaping_executor_catalog/${_face}.jfcat1"
                    "shaping_resource/${_face}.jfshr2"
                    "shaping_normalization/unicode14.bin"
                    "shaping_normalization/${_face}.eligibility")
                file(SHA256 "${_root}/${_file}" _digest)
                string(APPEND _row ",\"${_digest}\"")
            endforeach()
            string(APPEND _header "${_row}},\n")
        endforeach()
        string(APPEND _header "};\n}\n")
        file(WRITE "${_generated}/runtime_contextual_fixture_digests.h" "${_header}")
        target_include_directories(${target} PRIVATE "${_generated}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/app_runtime/tests")
        target_compile_definitions(${target} PRIVATE JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT="${_root}")
    elseif(JELLYFRAME_RENDER_CORE_SOURCE_DIR AND JELLYFRAME_ENABLE_TEXT_SHAPING)
        file(TO_CMAKE_PATH "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures" _root)
        target_include_directories(${target} PRIVATE "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/app_runtime/tests")
        target_compile_definitions(${target} PRIVATE JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT="${_root}")
    endif()
endfunction()
