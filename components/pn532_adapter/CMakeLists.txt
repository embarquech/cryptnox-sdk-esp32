idf_component_register(SRCS "pn532_adapter.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES pn532 driver)

# Expose cryptnox-sdk-cpp headers to dependents of this component.
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${CMAKE_SOURCE_DIR}/cryptnox-sdk-cpp")
