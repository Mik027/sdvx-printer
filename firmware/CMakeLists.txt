# Copyright (C) 2026 Mik027.
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.13)

# USB device only. pico2 also runs on a Pico 2 W; the radio is unused.
set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(cpm50d_emu C CXX ASM)

pico_sdk_init()

add_executable(cpm50d_emu
    src/main.c
    src/protocol.c
    src/usb_descriptors.c
)

target_include_directories(cpm50d_emu PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_link_libraries(cpm50d_emu
    pico_stdlib
    tinyusb_device
    tinyusb_board
)

# USB is the printer. Do not claim it for stdio.
pico_enable_stdio_usb(cpm50d_emu 0)
pico_enable_stdio_uart(cpm50d_emu 0)

pico_add_extra_outputs(cpm50d_emu)
