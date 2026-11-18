cmake_minimum_required(VERSION 3.21)
project(jfsg_installed_qualification LANGUAGES C CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(runtime_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(JFSG_CORE_INCLUDE_DIR "${runtime_root}/src" CACHE PATH "Core public include directory")
add_subdirectory(../package_signature signature)
set(store "${runtime_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_device_image_store.cpp")
add_library(jfsg_host_store OBJECT "${store}")
# Redirect only this separately compiled test object to the fixture key router.
# The production adapter translation unit is compiled unchanged, with its empty allowlist.
target_compile_definitions(jfsg_host_store PRIVATE
    verify_device_package_signature=verify_host_fixture_signature)
target_include_directories(jfsg_host_store PRIVATE stubs "${runtime_root}/src" "${JFSG_CORE_INCLUDE_DIR}")
target_compile_features(jfsg_host_store PRIVATE cxx_std_17)
target_link_libraries(jfsg_host_store PRIVATE jfsg_mbedcrypto)
add_executable(jfsg_installed_tests signed_install_tests.cpp $<TARGET_OBJECTS:jfsg_host_store>
    "${runtime_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_package_signature.cpp"
    "${runtime_root}/src/device_runtime_contracts/device_bundle.cpp"
    "${runtime_root}/src/device_runtime_contracts/device_install_transaction.cpp"
    "${runtime_root}/src/device_runtime_contracts/device_signed_package.cpp")
target_include_directories(jfsg_installed_tests PRIVATE stubs ../package_signature
    "${runtime_root}/src" "${runtime_root}/ports/esp32s3-idf/main" "${JFSG_CORE_INCLUDE_DIR}")
target_compile_definitions(jfsg_installed_tests PRIVATE JELLYFRAME_PACKAGE_SIGNATURE_TESTING=1)
target_compile_features(jfsg_installed_tests PRIVATE cxx_std_17)
target_link_libraries(jfsg_installed_tests PRIVATE jfsg_mbedcrypto)
set(fixtures "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
add_custom_command(OUTPUT "${fixtures}/manifest.json"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_fixtures.py" "${fixtures}"
    DEPENDS generate_fixtures.py "${runtime_root}/tools/sign_app_package.py"
        "${runtime_root}/tests/tool_regression/signed_package_tests.py"
        "${runtime_root}/tools/package_app.py" "${runtime_root}/tools/app_registry.py")
add_custom_target(jfsg_install_fixtures DEPENDS "${fixtures}/manifest.json")
add_dependencies(jfsg_installed_tests jfsg_install_fixtures)
foreach(case valid transaction reboot tamper boot_signature interrupted rollback wrong_key allocation production capacity capacity_empty_partition)
    add_test(NAME jfsg_install_${case} COMMAND jfsg_installed_tests ${case} "${fixtures}")
endforeach()
# Compile the exact store again with the opt-in cap, never substitute its
# size checks or storage logic. The small-partition case proves configuration
# cannot enlarge physical slots or cause writes before admission refusal.
add_library(jfsg_host_store_large OBJECT "${store}")
target_compile_definitions(jfsg_host_store_large PRIVATE
    CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB=1024
    verify_device_package_signature=verify_host_fixture_signature)
target_include_directories(jfsg_host_store_large PRIVATE stubs "${runtime_root}/src" "${JFSG_CORE_INCLUDE_DIR}")
target_compile_features(jfsg_host_store_large PRIVATE cxx_std_17)
target_link_libraries(jfsg_host_store_large PRIVATE jfsg_mbedcrypto)
add_executable(jfsg_installed_large_tests signed_install_tests.cpp $<TARGET_OBJECTS:jfsg_host_store_large>
    "${runtime_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_package_signature.cpp"
    "${runtime_root}/src/device_runtime_contracts/device_bundle.cpp"
    "${runtime_root}/src/device_runtime_contracts/device_install_transaction.cpp"
    "${runtime_root}/src/device_runtime_contracts/device_signed_package.cpp")
target_include_directories(jfsg_installed_large_tests PRIVATE stubs ../package_signature
    "${runtime_root}/src" "${runtime_root}/ports/esp32s3-idf/main" "${JFSG_CORE_INCLUDE_DIR}")
target_compile_definitions(jfsg_installed_large_tests PRIVATE JELLYFRAME_PACKAGE_SIGNATURE_TESTING=1
    CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB=1024 JFSG_HOST_PARTITION_BYTES=3145728)
target_compile_features(jfsg_installed_large_tests PRIVATE cxx_std_17)
target_link_libraries(jfsg_installed_large_tests PRIVATE jfsg_mbedcrypto)
add_dependencies(jfsg_installed_large_tests jfsg_install_fixtures)
foreach(case capacity capacity_small_partition)
    add_test(NAME jfsg_large_${case} COMMAND jfsg_installed_large_tests ${case} "${fixtures}")
endforeach()
add_test(NAME jfsg_signing_tool COMMAND "${Python3_EXECUTABLE}"
    "${runtime_root}/tests/tool_regression/signed_package_tests.py")
find_program(JFSG_LLVM_NM NAMES llvm-nm REQUIRED)
add_test(NAME jfsg_production_symbol_boundary COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_production_symbols.py" "${JFSG_LLVM_NM}"
    $<TARGET_OBJECTS:jfsg_signature_production>)
