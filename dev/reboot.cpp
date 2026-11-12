/**
 * dev/reboot.cpp — /dev/reboot: write-only, restart the board
 *
 * echo now > /dev/reboot       -> plain reboot (via the watchdog)
 * echo bootsel > /dev/reboot   -> reboot into the RP2040's USB bootloader,
 *                                 the mass-storage drive you copy a .uf2
 *                                 onto -- same as holding BOOTSEL at
 *                                 power-up, without touching the board
 *
 * Named after Plan 9's own /dev/reboot. A word is required, not just any
 * write, so a stray redirect can't reset the board by accident.
 */
#include "kernel/fs.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include <cstdio>
#include <cstring>

static int reboot_write(const char *buf, int len)
{
    if (len >= 3 && strncmp(buf, "now", 3) == 0) {
        printf("rebooting\n");
        sleep_ms(100); // let USB flush the line
        watchdog_reboot(0, 0, 0);
        for (;;) {}
    }
    if (len >= 7 && strncmp(buf, "bootsel", 7) == 0) {
        printf("rebooting into USB bootloader\n");
        sleep_ms(100);
        reset_usb_boot(0, 0);
    }
    return -1;
}

static const device_t dev_reboot = {"/dev/reboot", 0, 0, 0, reboot_write};

void reboot_register(void)
{
    fs_register(&dev_reboot);
}
