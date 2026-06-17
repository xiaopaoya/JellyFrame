set(JELLYFRAME_ESP32S3_RESOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../resources/app")
set(JELLYFRAME_ESP32S3_RESOURCE_GENERATOR
    "${CMAKE_CURRENT_LIST_DIR}/../../../tools/package_app.py")
set(JELLYFRAME_ESP32S3_GENERATED_RESOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/jellyframe_esp32s3_generated_resources.cpp")
set(JELLYFRAME_ESP32S3_RESOURCE_REPORT
    "${CMAKE_CURRENT_BINARY_DIR}/jellyframe_esp32s3_resource_report.json")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${JELLYFRAME_ESP32S3_RESOURCE_GENERATOR}"
        --root "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}"
        --output-cpp "${JELLYFRAME_ESP32S3_GENERATED_RESOURCES}"
        --report "${JELLYFRAME_ESP32S3_RESOURCE_REPORT}"
        --namespace "jellyframe_esp32s3"
    RESULT_VARIABLE JELLYFRAME_ESP32S3_RESOURCE_GENERATE_RESULT
)

if(NOT JELLYFRAME_ESP32S3_RESOURCE_GENERATE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate JellyFrame ESP32-S3 resource bundle")
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "jellyframe_esp32s3_font.cpp"
        "jellyframe_esp32s3_hal.cpp"
        "jellyframe_esp32s3_input.cpp"
        "jellyframe_esp32s3_resources.cpp"
        "${JELLYFRAME_ESP32S3_GENERATED_RESOURCES}"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_timer
        freertos
        heap
        log
        jellyframe_core
)
