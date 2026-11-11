if(CMAKE_BUILD_EARLY_EXPANSION)
    # The requirements subprocess has no JELLYFRAME_BUILD_SCRIPTING cache value.
    idf_component_register(REQUIRES esp_timer freertos heap log)
    return()
endif()

if(NOT JELLYFRAME_BUILD_SCRIPTING)
    idf_component_register()
    return()
endif()

set(JELLYFRAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
if(NOT JELLYFRAME_JERRYSCRIPT_ROOT)
    set(JELLYFRAME_JERRYSCRIPT_ROOT "${JELLYFRAME_ROOT}/third_party/jerryscript")
endif()
get_filename_component(JERRY_ROOT "${JELLYFRAME_JERRYSCRIPT_ROOT}" ABSOLUTE)
if(NOT EXISTS "${JERRY_ROOT}/jerry-core/config.h" OR
   NOT EXISTS "${JERRY_ROOT}/jerry-core/include/jerryscript.h")
    message(FATAL_ERROR "JELLYFRAME_JERRYSCRIPT_ROOT is not a JerryScript source tree: ${JERRY_ROOT}")
endif()

file(GLOB_RECURSE JELLYFRAME_JERRY_CORE_SOURCES
    "${JERRY_ROOT}/jerry-core/*.c")
list(FILTER JELLYFRAME_JERRY_CORE_SOURCES EXCLUDE REGEX "/tests?/")

# Use the same reviewed patch as host CI, without changing the caller's source
# tree. Only these three pinned files are copied into the build directory.
set(JELLYFRAME_JERRY_PATCH_TOOLS "${JELLYFRAME_ROOT}/tools/dependencies")
set(JELLYFRAME_JERRY_PATCH_ROOT "${CMAKE_CURRENT_BINARY_DIR}/weakref-patched")
set(JELLYFRAME_JERRY_PATCH_FILES
    "jerry-core/ecma/base/ecma-helpers.h"
    "jerry-core/ecma/base/ecma-helpers.c"
    "jerry-core/ecma/operations/ecma-objects.c")
foreach(JELLYFRAME_JERRY_PATCH_FILE IN LISTS JELLYFRAME_JERRY_PATCH_FILES)
    configure_file(
        "${JERRY_ROOT}/${JELLYFRAME_JERRY_PATCH_FILE}"
        "${JELLYFRAME_JERRY_PATCH_ROOT}/${JELLYFRAME_JERRY_PATCH_FILE}"
        COPYONLY)
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${JELLYFRAME_JERRY_PATCH_TOOLS}/patch_jerryscript.py"
    "${JELLYFRAME_JERRY_PATCH_TOOLS}/jerryscript-weakref-initialization.json"
    "${JELLYFRAME_JERRY_PATCH_TOOLS}/jerryscript-weakref-initialization.patch")
if(PYTHON)
    set(JELLYFRAME_JERRY_PATCH_PYTHON "${PYTHON}")
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(JELLYFRAME_JERRY_PATCH_PYTHON "${Python3_EXECUTABLE}")
endif()
execute_process(
    COMMAND "${JELLYFRAME_JERRY_PATCH_PYTHON}"
        "${JELLYFRAME_JERRY_PATCH_TOOLS}/patch_jerryscript.py"
        --source-root "${JELLYFRAME_JERRY_PATCH_ROOT}"
    RESULT_VARIABLE JELLYFRAME_JERRY_PATCH_RESULT
    OUTPUT_VARIABLE JELLYFRAME_JERRY_PATCH_OUTPUT
    ERROR_VARIABLE JELLYFRAME_JERRY_PATCH_ERROR)
if(NOT JELLYFRAME_JERRY_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "JerryScript WeakRef patch failed: ${JELLYFRAME_JERRY_PATCH_OUTPUT}${JELLYFRAME_JERRY_PATCH_ERROR}")
endif()
string(STRIP "${JELLYFRAME_JERRY_PATCH_OUTPUT}" JELLYFRAME_JERRY_PATCH_OUTPUT)
message(STATUS "${JELLYFRAME_JERRY_PATCH_OUTPUT}")
foreach(JELLYFRAME_JERRY_PATCH_FILE IN LISTS JELLYFRAME_JERRY_PATCH_FILES)
    if(JELLYFRAME_JERRY_PATCH_FILE MATCHES "\\.c$")
        list(REMOVE_ITEM JELLYFRAME_JERRY_CORE_SOURCES "${JERRY_ROOT}/${JELLYFRAME_JERRY_PATCH_FILE}")
        list(APPEND JELLYFRAME_JERRY_CORE_SOURCES "${JELLYFRAME_JERRY_PATCH_ROOT}/${JELLYFRAME_JERRY_PATCH_FILE}")
    endif()
endforeach()

configure_file(
    "${JERRY_ROOT}/jerry-core/config.h"
    "${CMAKE_CURRENT_BINARY_DIR}/jerryscript-config.h"
    COPYONLY)

idf_component_register(
    SRCS
        ${JELLYFRAME_JERRY_CORE_SOURCES}
        "jerry_port_esp32s3.c"
    INCLUDE_DIRS
        "${JELLYFRAME_JERRY_PATCH_ROOT}/jerry-core/ecma/base"
        "${JERRY_ROOT}/jerry-core/include"
        "${JERRY_ROOT}/jerry-core"
        "${JERRY_ROOT}/jerry-core/api"
        "${JERRY_ROOT}/jerry-core/debugger"
        "${JERRY_ROOT}/jerry-core/ecma/base"
        "${JERRY_ROOT}/jerry-core/ecma/builtin-objects"
        "${JERRY_ROOT}/jerry-core/ecma/builtin-objects/typedarray"
        "${JERRY_ROOT}/jerry-core/ecma/operations"
        "${JERRY_ROOT}/jerry-core/jcontext"
        "${JERRY_ROOT}/jerry-core/jmem"
        "${JERRY_ROOT}/jerry-core/jrt"
        "${JERRY_ROOT}/jerry-core/lit"
        "${JERRY_ROOT}/jerry-core/parser/js"
        "${JERRY_ROOT}/jerry-core/parser/regexp"
        "${JERRY_ROOT}/jerry-core/vm"
        "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES
        esp_timer
        freertos
        heap
        log
    WHOLE_ARCHIVE
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    JERRY_CPOINTER_32_BIT=0
    JERRY_DEBUGGER=0
    JERRY_ERROR_MESSAGES=0
    JERRY_EXTERNAL_CONTEXT=0
    JERRY_FUNCTION_TO_STRING=0
    JERRY_LINE_INFO=0
    JERRY_LOGGING=0
    JERRY_MEM_STATS=0
    JERRY_PARSER=1
    JERRY_PROMISE_CALLBACK=0
    JERRY_REGEXP_DUMP_BYTE_CODE=0
    JERRY_REGEXP_STRICT_MODE=0
    JERRY_SNAPSHOT_EXEC=0
    JERRY_SNAPSHOT_SAVE=0
    JERRY_SYSTEM_ALLOCATOR=0
    JERRY_VALGRIND=0
    JERRY_VM_HALT=1
    JERRY_VM_THROW=0
    JERRY_GLOBAL_HEAP_SIZE=256
    JERRY_STACK_LIMIT=0
    JERRY_GC_MARK_LIMIT=8
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-maybe-uninitialized
)
