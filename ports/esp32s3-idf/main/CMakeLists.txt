set(JELLYFRAME_ESP32S3_RESOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../resources/app")
set(JELLYFRAME_ESP32S3_RESOURCE_GENERATOR
    "${CMAKE_CURRENT_LIST_DIR}/../../../tools/package_app.py")
set(JELLYFRAME_ESP32S3_GENERATED_RESOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/jellyframe_esp32s3_generated_resources.cpp")
set(JELLYFRAME_ESP32S3_RESOURCE_REPORT
    "${CMAKE_CURRENT_BINARY_DIR}/jellyframe_esp32s3_resource_report.json")
set(JELLYFRAME_ESP32S3_FONT_GENERATOR
    "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_noto_sans_sc_font_pack.py")
set(JELLYFRAME_ESP32S3_FONT_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_noto_sans_sc_font")
set(JELLYFRAME_ESP32S3_GENERATED_NOTO_FONT_CPP
    "${JELLYFRAME_ESP32S3_FONT_OUTPUT_DIR}/jellyframe_esp32s3_noto_sans_sc_font.cpp")
set(JELLYFRAME_ESP32S3_GENERATED_NOTO_FONT_H
    "${JELLYFRAME_ESP32S3_FONT_OUTPUT_DIR}/jellyframe_esp32s3_noto_sans_sc_font.h")
file(MAKE_DIRECTORY "${JELLYFRAME_ESP32S3_FONT_OUTPUT_DIR}")

set(JELLYFRAME_ESP32S3_SCRIPT_REQUIRES)
if(CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_APP_ACCEPTANCE OR
   CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_SERVICE_ECHO_ACCEPTANCE OR
   CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_FAULT_RECOVERY_ACCEPTANCE)
    list(APPEND JELLYFRAME_ESP32S3_SCRIPT_REQUIRES jellyframe_jerryscript)
endif()

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

    file(GLOB_RECURSE JELLYFRAME_ESP32S3_FONT_INPUTS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../font/source/*.otf"
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

    add_custom_command(
        OUTPUT
            "${JELLYFRAME_ESP32S3_GENERATED_NOTO_FONT_CPP}"
            "${JELLYFRAME_ESP32S3_GENERATED_NOTO_FONT_H}"
        COMMAND "${Python3_EXECUTABLE}" "${JELLYFRAME_ESP32S3_FONT_GENERATOR}"
            --output-dir "${JELLYFRAME_ESP32S3_FONT_OUTPUT_DIR}"
            --sizes 16,20,24
            --bits-per-pixel 1
        DEPENDS
            "${JELLYFRAME_ESP32S3_FONT_GENERATOR}"
            ${JELLYFRAME_ESP32S3_FONT_INPUTS}
        COMMENT "Generating JellyFrame ESP32-S3 Noto Sans SC font pack"
        VERBATIM
    )
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "jellyframe_esp32s3_font.cpp"
        "jellyframe_esp32s3_hal.cpp"
        "jellyframe_esp32s3_image.cpp"
        "jellyframe_esp32s3_input.cpp"
        "jellyframe_esp32s3_resources.cpp"
        "jellyframe_esp32s3_soc_power_acceptance.cpp"
        "jellyframe_esp32s3_script_task_acceptance.cpp"
        "jellyframe_esp32s3_script_app_acceptance.cpp"
        "jellyframe_esp32s3_script_service_acceptance.cpp"
        "jellyframe_esp32s3_script_fault_acceptance.cpp"
        "jellyframe_esp32s3_ui_task.cpp"
        "boards/waveshare_touch_lcd_boards.cpp"
        "boards/waveshare_touch_lcd_169_board.cpp"
        "${JELLYFRAME_ESP32S3_GENERATED_RESOURCES}"
        "${JELLYFRAME_ESP32S3_GENERATED_NOTO_FONT_CPP}"
    INCLUDE_DIRS
        "."
        "${JELLYFRAME_ESP32S3_FONT_OUTPUT_DIR}"
    REQUIRES
        esp_timer
        freertos
        driver
        esp_lcd
        heap
        log
        jellyframe_app_runtime
        jellyframe_render_core
        ${JELLYFRAME_ESP32S3_SCRIPT_REQUIRES}
)
