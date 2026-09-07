idf_component_register(
    SRCS "main.cpp" "jellyframe_esp32p4_accel.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer heap log esp_driver_ppa esp_driver_jpeg jellyframe_render_core
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
