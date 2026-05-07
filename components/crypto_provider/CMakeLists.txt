idf_component_register(
    SRCS         "esp32_crypto_provider.cpp"
    INCLUDE_DIRS "include"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp"
    REQUIRES     mbedtls uECC cryptnox_utils esp_hw_support esp_wifi
)

idf_component_optional_requires(PRIVATE bt)
