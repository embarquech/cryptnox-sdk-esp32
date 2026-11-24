# The entropy source (CW_Utils::fill_secure_random), which reads the radio state
# (SEC-001) and so needs esp_wifi. CW_Utils.cpp itself is in cw_utils, required
# here publicly so every existing user of this component still gets both.
idf_component_register(
    SRCS        "esp32_random.cpp"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp"
    REQUIRES    cw_utils esp_hw_support esp_wifi log)

idf_component_optional_requires(PRIVATE bt)
