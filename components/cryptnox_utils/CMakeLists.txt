idf_component_register(
    SRCS        "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp/CW_Utils.cpp"
                "esp32_random.cpp"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp"
    REQUIRES    esp_hw_support esp_wifi
)

idf_component_optional_requires(PRIVATE bt)
