idf_component_register(
    SRCS        "esp32_random.cpp" "cw_utils_c.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp/CW_Utils.cpp"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp" "include"
    REQUIRES    esp_hw_support esp_wifi log)

idf_component_optional_requires(PRIVATE bt)
