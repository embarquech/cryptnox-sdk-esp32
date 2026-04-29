idf_component_register(
    SRCS        "uECC_esp32.cpp"
    INCLUDE_DIRS "include"
    REQUIRES    mbedtls esp_hw_support esp_wifi
)

idf_component_optional_requires(PRIVATE bt)
