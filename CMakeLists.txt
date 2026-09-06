cmake_minimum_required(VERSION 3.21)

# Silicognition ManT1S: ESP32-PICO-V3-02 + Microchip LAN8671 10BASE-T1S PHY
set(SUPPORTED_TARGETS esp32)

# Set IDF_PATH from environment or default to your installation directory
if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "$ENV{HOME}/.espressif/v6.1/esp-idf")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
project(mant1s_angle)
