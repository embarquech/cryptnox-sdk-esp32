# CW_Utils on its own: secure_wipe, secure_compare, safe_memcpy and the rest of
# the pure helpers. No networking, so no esp_wifi — a component that needs only
# these (secure_channel) no longer pulls in the Wi-Fi stack. The entropy source,
# which does need the radio (SEC-001), stays in cryptnox_utils.
idf_component_register(
    SRCS         "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp/CW_Utils.cpp"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp"
)
