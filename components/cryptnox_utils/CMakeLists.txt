set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp")

idf_component_register(
    SRCS "${SDK_DIR}/CryptnoxUtils.cpp" "esp32_random.cpp"
    INCLUDE_DIRS "${SDK_DIR}"
    REQUIRES esp_hw_support esp_wifi)
