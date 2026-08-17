set(JELLYFRAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "${JELLYFRAME_ROOT}/src/device_runtime_contracts/device_install_transaction.cpp"
        "${JELLYFRAME_ROOT}/src/device_runtime_contracts/device_runtime_protocol.cpp"
    INCLUDE_DIRS
        "${JELLYFRAME_ROOT}/src"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wno-missing-field-initializers
)
