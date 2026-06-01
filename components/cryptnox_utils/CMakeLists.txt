idf_component_register(
    SRCS        "esp32_random.cpp"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../cryptnox_sdk_cpp"
    REQUIRES    esp_hw_support esp_wifi log)

idf_component_optional_requires(PRIVATE bt)
