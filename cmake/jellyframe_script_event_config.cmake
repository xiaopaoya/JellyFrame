include_guard(GLOBAL)
option(JELLYFRAME_ENABLE_R3_SCRIPT_EVENTS
    "Opt in to Core v1 dispatch completion/failure and shared script Events" OFF)
get_filename_component(_jellyframe_event_module_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set_property(GLOBAL PROPERTY JELLYFRAME_SCRIPT_EVENT_MODULE_ROOT "${_jellyframe_event_module_root}")

function(jellyframe_configure_script_events target visibility core_target)
    if(JELLYFRAME_ENABLE_R3_SCRIPT_EVENTS)
        set(policy 1)
    else()
        set(policy 0)
    endif()
    target_compile_definitions(${target} ${visibility} JELLYFRAME_SCRIPT_R3_EVENTS=${policy})
    get_property(module_root GLOBAL PROPERTY JELLYFRAME_SCRIPT_EVENT_MODULE_ROOT)
    get_target_property(core_includes ${core_target} INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(core_definitions ${core_target} INTERFACE_COMPILE_DEFINITIONS)
    set(probe_includes)
    foreach(path IN LISTS core_includes)
        if(path MATCHES "^\\$<BUILD_INTERFACE:(.*)>$")
            list(APPEND probe_includes "${CMAKE_MATCH_1}")
        elseif(IS_ABSOLUTE "${path}")
            list(APPEND probe_includes "${path}")
        endif()
    endforeach()
    # Selected Core includes must precede Runtime's bundled legacy Core.
    list(APPEND probe_includes "${module_root}/src")
    set(probe_definitions "-DJELLYFRAME_SCRIPT_R3_EVENTS=${policy}")
    foreach(definition IN LISTS core_definitions)
        if(definition MATCHES "^[A-Za-z_][A-Za-z0-9_]*(=.*)?$")
            list(APPEND probe_definitions "-D${definition}")
        endif()
    endforeach()
    if(NOT ESP_PLATFORM)
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_INCLUDES ${probe_includes})
        set(CMAKE_REQUIRED_DEFINITIONS ${probe_definitions})
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
        set(CMAKE_CXX_STANDARD 17)
        # Deliberately revalidate each configure; provider/profile/policy may
        # change inside a mutable development build directory.
        unset(JELLYFRAME_SCRIPT_EVENT_POLICY_VALID CACHE)
        check_cxx_source_compiles(
            "#include \"script/script_event_capabilities.h\"\nint main() { return 0; }"
            JELLYFRAME_SCRIPT_EVENT_POLICY_VALID)
        if(NOT JELLYFRAME_SCRIPT_EVENT_POLICY_VALID)
            message(FATAL_ERROR
                "Selected Core/RTTI cannot provide script Event policy ${policy}; see the capability diagnostic in CMake's compile log")
        endif()
    endif()
    # Cross builds validate the same header in the real Xtensa translation
    # unit. No link-time probe of an unbuilt IDF component is attempted here.
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script-event-policy-${target}.txt"
        "r3Events=${policy}\ncoreTarget=${core_target}\ncoreIncludes=${core_includes}\ncoreDefinitions=${core_definitions}\n")
endfunction()
