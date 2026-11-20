set(jfsg_package_trust_generator "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_package_trust.py")

function(jfsg_configure_package_trust target manifest)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/jfsg-package-trust")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${jfsg_package_trust_generator}")
    set(manifest_args)
    if(NOT manifest STREQUAL "")
        get_filename_component(manifest "${manifest}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")
        list(APPEND manifest_args --manifest "${manifest}")
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${jfsg_package_trust_generator}" --output-dir "${output}" ${manifest_args}
        RESULT_VARIABLE result
        ERROR_VARIABLE error)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "${error} (generator result: ${result})")
    endif()
    target_include_directories(${target} PRIVATE "${output}")
    target_compile_definitions(${target} PRIVATE JELLYFRAME_ESP32S3_GENERATED_PACKAGE_TRUST=1)
endfunction()
