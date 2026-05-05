idf_component_register(
    SRCS        "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp/CW_SecureChannel.cpp"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp"
    REQUIRES    cryptnox_utils uECC
)
