cmake_minimum_required(VERSION 3.5)

include(../pico-sdk/pico_sdk_init.cmake)

project(picogame C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(picogame
    display.c images.c cpu.c gpu.c ipu.c main.c pong.c
)

pico_enable_stdio_usb(picogame 1)

target_link_libraries(picogame pico_stdlib hardware_spi pico_multicore pico_util)

pico_add_extra_outputs(picogame)
