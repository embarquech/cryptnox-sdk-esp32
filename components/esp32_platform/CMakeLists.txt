idf_component_register(
    SRCS         "ESP32Platform.cpp"
    INCLUDE_DIRS "include"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp"
    REQUIRES     freertos
)
