# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Enforce ESP-IDF v5.5 minimum.  Older versions lack mbedTLS APIs and
# FreeRTOS types relied on by this project (SEC-014).
if(IDF_VERSION_MAJOR LESS 5 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR LESS 5))
    message(FATAL_ERROR
        "ESP-IDF v5.5 or later is required.  Found: ${IDF_VERSION}.  "
        "Install the correct version from https://docs.espressif.com/projects/esp-idf/en/v5.5/")
endif()

project(cryptnox-sdk-esp32)
