cmake_minimum_required(VERSION 3.16)

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/crypto_provider"
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/uECC"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(crypto_provider_test)
