cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(cpu_rp2040 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function
)

# bringup: standalone board test (I2C scan, FRAM, RTC, LED, buttons),
# no picoos kernel involved.
add_executable(bringup
    bringup.cpp
)
target_include_directories(bringup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bringup pico_stdlib hardware_i2c)
pico_enable_stdio_usb(bringup 1)
pico_enable_stdio_uart(bringup 0)
pico_add_extra_outputs(bringup)
