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

# CW_DEBUG_LOGGING defaults to 0 (off). Enabling it in production builds leaks
# session state over UART — only uncomment during local development and never
# ship a firmware with this flag set (SEC-012).
target_compile_definitions(${COMPONENT_LIB} PRIVATE CW_DEBUG_LOGGING=1)
