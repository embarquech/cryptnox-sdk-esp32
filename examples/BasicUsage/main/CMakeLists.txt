idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES pn532 driver cryptnox_utils crypto_provider esp32_logger
             cryptnox_sdk_cpp pn532_nfc_transport esp32_platform
             nvs_flash esp_wifi esp_netif esp_event
)
