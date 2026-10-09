cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Pico board")
include(pico_sdk_import.cmake)

project(felica_dump C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

set(RCS660S_TX_PIN 16 CACHE STRING "Pico TX GPIO connected to RC-S660/S RXD")
set(RCS660S_RX_PIN 17 CACHE STRING "Pico RX GPIO connected to RC-S660/S TXD")

add_executable(felica_dump main.cpp driver/RCS660S.cpp)
target_compile_definitions(felica_dump PRIVATE
    RCS660S_TX_PIN=${RCS660S_TX_PIN}
    RCS660S_RX_PIN=${RCS660S_RX_PIN})
target_compile_options(felica_dump PRIVATE -Wall -Wextra)
target_link_libraries(felica_dump pico_stdlib hardware_uart)
pico_enable_stdio_usb(felica_dump 1)
pico_enable_stdio_uart(felica_dump 0)
pico_add_extra_outputs(felica_dump)
