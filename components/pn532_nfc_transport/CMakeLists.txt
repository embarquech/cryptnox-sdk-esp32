set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cryptnox_sdk_cpp")

idf_component_register(
    SRCS "Pn532NfcTransport.cpp"
    INCLUDE_DIRS "include" "${SDK_DIR}"
    REQUIRES pn532
)
