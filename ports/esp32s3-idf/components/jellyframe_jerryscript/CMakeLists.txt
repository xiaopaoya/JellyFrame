set(JELLYFRAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(JERRY_ROOT "${JELLYFRAME_ROOT}/third_party/jerryscript")

file(GLOB_RECURSE JELLYFRAME_JERRY_CORE_SOURCES
    "${JERRY_ROOT}/jerry-core/*.c")
list(FILTER JELLYFRAME_JERRY_CORE_SOURCES EXCLUDE REGEX "/tests?/")

configure_file(
    "${JERRY_ROOT}/jerry-core/config.h"
    "${CMAKE_CURRENT_BINARY_DIR}/jerryscript-config.h"
    COPYONLY)

idf_component_register(
    SRCS
        ${JELLYFRAME_JERRY_CORE_SOURCES}
        "jerry_port_esp32s3.c"
    INCLUDE_DIRS
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
