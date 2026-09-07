cmake_minimum_required(VERSION 3.16)

# This is intentionally a separate ESP-IDF application.  It must not alter
# the host-neutral Render Core or the established ESP32-S3 bring-up project.
set(IDF_TARGET esp32p4)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(jellyframe_esp32p4_bringup)
