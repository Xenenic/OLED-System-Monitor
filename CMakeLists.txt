cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)
set(PICO_COMPILER pico_arm_cortex_m33_gcc)
set(PICO_PLATFORM rp2350-arm-s)
set(PROJECT_NAME oled-system-monitor)

include(pico_sdk_import.cmake)
project(${PROJECT_NAME} C CXX ASM)
pico_sdk_init()

add_executable(${PROJECT_NAME}
    src/main.c
    src/ssd1309.c
    src/font.c
)

pico_set_program_version(${PROJECT_NAME} "1.0.2")

target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_multicore
    boot_uf2_headers
    hardware_pio
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_vreg
)

pico_enable_stdio_uart(${PROJECT_NAME} 1)
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_add_extra_outputs(${PROJECT_NAME})
