# Configure the currently selected JerryScript backend. This file is the only
# CMake location that discovers JerryScript headers and libraries.

set(JERRYSCRIPT_ROOT "" CACHE PATH "JerryScript source or install root")
set(JERRYSCRIPT_INCLUDE_DIR "" CACHE PATH "JerryScript include directory")
set(JERRYSCRIPT_LIBRARIES "" CACHE STRING "JerryScript libraries")

if(JERRYSCRIPT_ROOT)
    set(_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR "")
    if(EXISTS "${JERRYSCRIPT_ROOT}/jerry-core/include/jerryscript.h")
        set(_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR "${JERRYSCRIPT_ROOT}/jerry-core/include")
    elseif(EXISTS "${JERRYSCRIPT_ROOT}/include/jerryscript.h")
        set(_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR "${JERRYSCRIPT_ROOT}/include")
    endif()

    if(_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR AND
            (NOT JERRYSCRIPT_INCLUDE_DIR OR
             NOT EXISTS "${JERRYSCRIPT_INCLUDE_DIR}/jerryscript.h"))
        set(JERRYSCRIPT_INCLUDE_DIR "${_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR}" CACHE PATH
            "JerryScript include directory" FORCE)
    endif()

    if(NOT JERRYSCRIPT_LIBRARIES)
        set(_JELLYFRAME_JERRYSCRIPT_LIBRARY_DIRS
            "${JERRYSCRIPT_ROOT}/build/lib/Release"
            "${JERRYSCRIPT_ROOT}/build/lib/MinSizeRel"
            "${JERRYSCRIPT_ROOT}/build/lib/RelWithDebInfo"
            "${JERRYSCRIPT_ROOT}/build/lib/Debug")
        set(_JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES "")
        foreach(_JELLYFRAME_JERRYSCRIPT_LIBRARY_NAME IN ITEMS jerry-core jerry-ext jerry-port)
            unset(_JELLYFRAME_JERRYSCRIPT_LIBRARY CACHE)
            unset(_JELLYFRAME_JERRYSCRIPT_LIBRARY)
            find_library(_JELLYFRAME_JERRYSCRIPT_LIBRARY
                NAMES ${_JELLYFRAME_JERRYSCRIPT_LIBRARY_NAME}
                PATHS ${_JELLYFRAME_JERRYSCRIPT_LIBRARY_DIRS}
                NO_DEFAULT_PATH)
            if(NOT _JELLYFRAME_JERRYSCRIPT_LIBRARY)
                set(_JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES "")
                break()
            endif()
            list(APPEND _JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES
                 "${_JELLYFRAME_JERRYSCRIPT_LIBRARY}")
        endforeach()
        if(_JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES)
            set(JERRYSCRIPT_LIBRARIES "${_JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES}" CACHE STRING
                "JerryScript libraries" FORCE)
        endif()
    endif()
endif()

if(NOT JERRYSCRIPT_INCLUDE_DIR OR NOT JERRYSCRIPT_LIBRARIES)
    message(FATAL_ERROR
        "JELLYFRAME_BUILD_SCRIPTING with JELLYFRAME_SCRIPT_ENGINE=jerryscript requires "
        "JERRYSCRIPT_INCLUDE_DIR and JERRYSCRIPT_LIBRARIES")
endif()
if(NOT EXISTS "${JERRYSCRIPT_INCLUDE_DIR}/jerryscript.h")
    message(FATAL_ERROR
        "JERRYSCRIPT_INCLUDE_DIR does not contain jerryscript.h: ${JERRYSCRIPT_INCLUDE_DIR}\n"
        "If configuring from PowerShell, use $PWD instead of cmd.exe-only %CD%.")
endif()
foreach(_JELLYFRAME_JERRYSCRIPT_LIBRARY IN LISTS JERRYSCRIPT_LIBRARIES)
    if(NOT EXISTS "${_JELLYFRAME_JERRYSCRIPT_LIBRARY}")
        message(FATAL_ERROR
            "JerryScript library does not exist: ${_JELLYFRAME_JERRYSCRIPT_LIBRARY}\n"
            "If configuring from PowerShell, use $PWD instead of cmd.exe-only %CD%.")
    endif()
endforeach()

set(JELLYFRAME_SCRIPT_BACKEND_SOURCES
    src/script/jerryscript_runtime.cpp)
set(JELLYFRAME_SCRIPT_BACKEND_INCLUDE_DIRS "${JERRYSCRIPT_INCLUDE_DIR}")
set(JELLYFRAME_SCRIPT_BACKEND_LIBRARIES ${JERRYSCRIPT_LIBRARIES})
set(JELLYFRAME_SCRIPT_BACKEND_NAME "jerryscript")
