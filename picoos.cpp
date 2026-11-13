/**
 * picoos.cpp — picoos on the cpu_rp2040 board
 *
 * The next version of picoos (github.com/leonhiem/picoos, ported from
 * commit 89c16e5): same kernel (kernel/), shell, pipes and jobs, and the
 * generic programs. Left behind in picoos: everything babywarmer-
 * specific (heater, NTC/ADS1115, 7-segment, TFT, PID/phase/alarm wiring).
 * New here: the board's own devices (LED, lamp, buttons, PCF8563 RTC, die
 * temperature, uptime, reboot), a filesystem on the FRAM mounted at
 * /fram (fs/framfs.h), and more small Unix-style programs.
 *
 * Same rule as always: a device owns hardware, a program only ever
 * touches /dev paths.
 */

#include <pico/stdlib.h>
#include <cstdio>
#include "hardware/watchdog.h"
#include "kernel/task.h"
#include "jobs.h"
#include "board.h"
#include "buttons.h"

// dev/*.cpp
void led_register(void);
void lamp_register(void);
void buttons_register(void);
void button_devices_register(void);
void rtc_register(void);
void uptime_register(void);
void cputemp_register(void);
void reboot_register(void);
void fram_register(void);
void mem_register(void);

// prog/*.cpp
void cat_register(void);
void echo_register(void);
void label_register(void);
void thresh_register(void);
void hyst_register(void);
void toggle_register(void);
void adjust_register(void);
void follow_register(void);
void select_register(void);
void date_register(void);
void uptime_prog_register(void);
void wc_register(void);
void grep_register(void);
void headtail_register(void);
void tee_register(void);
void hexdump_register(void);
void rm_register(void);
void mkfs_register(void);
void df_register(void);
void ls_register(void);

void task_shell(void);

int main()
{
    stdio_init_all();
    board_led_init();
    board_lamp_init();
    board_i2c_init();
    buttons_init();

    // Give a USB terminal a moment to attach so the banner isn't lost.
    absolute_time_t wait = make_timeout_time_ms(3000);
    while (!stdio_usb_connected() && !time_reached(wait))
        sleep_ms(50);
    sleep_ms(200);

    printf(watchdog_caused_reboot() ? "Rebooted by watchdog\n" : "Clean boot\n");

    watchdog_enable(0x7fffff, 1); // 8 seconds (is max)

    // Populate /dev, then bin/, then the shell that lets you poke at it.
    led_register();
    lamp_register();
    buttons_register();
    button_devices_register();
    rtc_register();
    uptime_register();
    cputemp_register();
    reboot_register();
    fram_register();     // mounts /fram, and /dev/fram (raw)
    mem_register();

    cat_register();
    echo_register();
    label_register();
    thresh_register();
    hyst_register();
    toggle_register();
    adjust_register();
    follow_register();
    select_register();
    date_register();
    uptime_prog_register();
    wc_register();
    grep_register();
    headtail_register();
    tee_register();
    hexdump_register();
    rm_register();
    mkfs_register();
    df_register();
    ls_register();
    jobs_init();

    task_register("shell", task_shell, 30); // 30ms: responsive to typing

    while (1) {
        task_run();
        watchdog_update();
    }
    return 0;
}
