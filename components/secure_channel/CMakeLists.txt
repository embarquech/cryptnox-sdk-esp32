set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../cryptnox-sdk-cpp")

idf_component_register(
    SRCS
        "${SDK_DIR}/CW_SecureChannel.cpp"
        "${SDK_DIR}/CW_Utils.cpp"
        "${SDK_DIR}/CryptnoxWallet.cpp"
    INCLUDE_DIRS
        "${SDK_DIR}"
    REQUIRES
        uECC
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC CW_DEBUG_LOGGING=1)
