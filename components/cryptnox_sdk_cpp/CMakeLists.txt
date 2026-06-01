idf_component_register(
    SRCS
        "CW_SecureChannel.cpp"
        "CW_Utils.cpp"
        "CryptnoxWallet.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        uECC
)

# CW_DEBUG_LOGGING defaults to 0 (off). Enabling it in production builds leaks
# session state over UART — only uncomment during local development and never
# ship a firmware with this flag set (SEC-012).
# target_compile_definitions(${COMPONENT_LIB} PRIVATE CW_DEBUG_LOGGING=1)
