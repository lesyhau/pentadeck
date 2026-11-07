cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pentadeck)

# A copy of the app image in build/bin after every build (the one OTA and the settings page take)
set(PENTADECK_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PENTADECK_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin" "${PENTADECK_BIN_DIR}/"
    COMMENT "Copying ${CMAKE_PROJECT_NAME}.bin to build/bin"
    VERBATIM)
