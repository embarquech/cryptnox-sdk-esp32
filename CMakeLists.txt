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

# Workaround: GCC 14.2.0 (xtensa-esp-elf esp-14.2.0_20260121) ICE in
# esp_lcd_panel_rgb.c during the IRA (Integrated Register Allocator) RTL pass
# at -Og.  The project does not use the RGB LCD path; compile the whole
# esp_lcd component at -O0 to avoid the segfault without affecting our code.
if(TARGET __idf_esp_lcd)
    target_compile_options(__idf_esp_lcd PRIVATE $<$<COMPILE_LANGUAGE:C>:-O0>)
endif()
