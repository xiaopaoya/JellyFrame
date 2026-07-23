set(JELLYFRAME_ESP32S3_RESOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../resources/app")
set(JELLYFRAME_ESP32S3_RESOURCE_GENERATOR
    "${CMAKE_CURRENT_LIST_DIR}/../../../tools/package_app.py")
set(JELLYFRAME_ESP32S3_GENERATED_RESOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/jellyframe_esp32s3_generated_resources.cpp")
set(JELLYFRAME_ESP32S3_RESOURCE_REPORT
    "${CMAKE_CURRENT_BINARY_DIR}/jellyframe_esp32s3_resource_report.json")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    file(GLOB_RECURSE JELLYFRAME_ESP32S3_RESOURCE_INPUTS CONFIGURE_DEPENDS
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.css"
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.html"
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.js"
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.json"
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.bmp"
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.png"
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.jpg"
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.jpeg"
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.gif"
        "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}/*.webp"
    )

    add_custom_command(
        OUTPUT
            "${JELLYFRAME_ESP32S3_GENERATED_RESOURCES}"
            "${JELLYFRAME_ESP32S3_RESOURCE_REPORT}"
        COMMAND "${Python3_EXECUTABLE}" "${JELLYFRAME_ESP32S3_RESOURCE_GENERATOR}"
            --root "${JELLYFRAME_ESP32S3_RESOURCE_ROOT}"
            --output-cpp "${JELLYFRAME_ESP32S3_GENERATED_RESOURCES}"
            --report "${JELLYFRAME_ESP32S3_RESOURCE_REPORT}"
            --namespace "jellyframe_esp32s3"
        DEPENDS
            "${JELLYFRAME_ESP32S3_RESOURCE_GENERATOR}"
            ${JELLYFRAME_ESP32S3_RESOURCE_INPUTS}
        COMMENT "Generating JellyFrame ESP32-S3 resource bundle"
        VERBATIM
    )
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "jellyframe_esp32s3_font.cpp"
        "jellyframe_esp32s3_noto_sans_sc_font.cpp"
        "jellyframe_esp32s3_hal.cpp"
        "jellyframe_esp32s3_image.cpp"
        "jellyframe_esp32s3_input.cpp"
        "jellyframe_esp32s3_resources.cpp"
        "jellyframe_esp32s3_soc_power_acceptance.cpp"
        "jellyframe_esp32s3_ui_task.cpp"
        "boards/waveshare_touch_lcd_boards.cpp"
        "boards/waveshare_touch_lcd_169_board.cpp"
        "${JELLYFRAME_ESP32S3_GENERATED_RESOURCES}"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_timer
        freertos
        driver
        esp_lcd
        heap
        log
        jellyframe_app_runtime
        jellyframe_render_core
)
