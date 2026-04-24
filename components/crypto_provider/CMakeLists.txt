idf_component_register(
    SRCS         "esp32_crypto_provider.cpp"
    INCLUDE_DIRS "include"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp"
    REQUIRES     mbedtls uECC esp_hw_support
)
