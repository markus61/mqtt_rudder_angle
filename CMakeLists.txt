cmake_minimum_required(VERSION 3.21)

# Silicognition ManT1S: ESP32-PICO-V3-02 + Microchip LAN8670 10BASE-T1S PHY
set(SUPPORTED_TARGETS esp32)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mant1s_angle)
