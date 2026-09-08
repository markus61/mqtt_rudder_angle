cmake_minimum_required(VERSION 3.21)

# Silicognition ManT1S: ESP32-PICO-V3-02 + Microchip LAN8671 10BASE-T1S PHY
set(SUPPORTED_TARGETS esp32)

# The sourced ESP-IDF environment is the single source of truth for which IDF
# to build against. Don't fall back to a hardcoded path: IDF_PATH alone is not a
# usable environment (PATH, IDF_PYTHON_ENV_PATH and IDF_TOOLS_PATH also matter),
# so a guess here only turns a clear error into a confusing one later.
if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR
        "IDF_PATH is not set. Source the ESP-IDF environment first "
        "(e.g. '. $HOME/.espressif/v6.1/esp-idf/export.sh'), or build from "
        "VS Code with the ESP-IDF extension.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
project(mant1s_angle)

if(IDF_VERSION_MAJOR LESS 6)
    message(FATAL_ERROR "This project requires ESP-IDF v6.1 or newer (found ${IDF_VER}).")
endif()

idf_build_get_property(python PYTHON)

add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND
        ${python}
        "${CMAKE_SOURCE_DIR}/tools/write_build_info.py"
        --image "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
        --build-dir "${CMAKE_BINARY_DIR}"
    COMMENT "Writing application build information JSON"
    VERBATIM
)
