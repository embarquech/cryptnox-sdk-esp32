idf_component_register(
    SRCS        "uECC_esp32.cpp"
    INCLUDE_DIRS "include"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp"
    REQUIRES    mbedtls cryptnox_utils esp_hw_support esp_wifi
)

idf_component_optional_requires(PRIVATE bt)
