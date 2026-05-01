idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES pn532 epd driver cryptnox_utils crypto_provider esp32_logger
                                secure_channel pn532_nfc_transport)
