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
        -Wno-stringop-truncation # shell.cpp's strncpy(dst, src, N-1) + explicit '\0' is the
                                 # intended truncate-and-terminate idiom, not a bug. picoos
                                 # never saw this: its add_compile_options came after its
                                 # targets, so -Wall wasn't actually applied there.
)

# board_drivers: the board's own chips (RTC, FRAM, I2C bus, LED), shared
# by bringup and picoos so both talk to the hardware the same way.
add_library(board_drivers
    drivers/board.cpp
    drivers/pcf8563.cpp
    drivers/fm24cl64.cpp
)
target_include_directories(board_drivers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(board_drivers pico_stdlib hardware_i2c)

# picoos_kernel: the reusable multitasking core, as in picoos. Kept as
# its own library target, separate from any one application's sources.
add_library(picoos_kernel
    kernel/task.cpp
    kernel/klog.cpp
    kernel/fs.cpp
    kernel/prog.cpp
)
target_include_directories(picoos_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(picoos_kernel pico_stdlib)

# bringup: standalone board test (I2C scan, FRAM, RTC, LED, buttons),
# no picoos kernel involved.
add_executable(bringup
    bringup.cpp
)
target_link_libraries(bringup board_drivers pico_stdlib hardware_i2c)
pico_enable_stdio_usb(bringup 1)
pico_enable_stdio_uart(bringup 0)
pico_add_extra_outputs(bringup)

# picoos: the OS on this board.
add_executable(picoos
    picoos.cpp
    shell.cpp
    jobs.cpp
    buttons.cpp
    dev/led.cpp
    dev/lamp.cpp
    dev/buttons.cpp
    dev/rtc.cpp
    dev/uptime.cpp
    dev/cputemp.cpp
    dev/reboot.cpp
    prog/cat.cpp
    prog/echo.cpp
    prog/label.cpp
    prog/thresh.cpp
    prog/hyst.cpp
    prog/toggle.cpp
    prog/adjust.cpp
    prog/follow.cpp
    prog/select.cpp
    prog/date.cpp
    prog/uptime.cpp
    prog/wc.cpp
    prog/grep.cpp
    prog/headtail.cpp
    prog/tee.cpp
    prog/ls.cpp
)
target_link_libraries(picoos picoos_kernel board_drivers pico_stdlib
    hardware_i2c hardware_adc hardware_gpio)
# UART stdio off: its default pins are GPIO0/1, and GPIO1 is the LED.
pico_enable_stdio_usb(picoos 1)
pico_enable_stdio_uart(picoos 0)
pico_add_extra_outputs(picoos)
