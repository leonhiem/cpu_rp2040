/**
 * dev/led.cpp — /dev/led: read/write, the on-board LED (GPIO1)
 *
 * echo on > /dev/led    -> LED on
 * echo off > /dev/led   -> LED off
 * cat /dev/led          -> "on\n" or "off\n"
 *
 * Same shape as picoos's /dev/lamp: the pin's driven level is read back
 * with gpio_get(), no separate software flag. The LED is wired inverted
 * (cathode to the GPIO, see board.h), so 0 = lit -- hidden in here, the
 * namespace only ever speaks on/off.
 */
#include "board.h"
#include "kernel/fs.h"
#include "hardware/gpio.h"
#include <cstdio>
#include <cstring>

static int led_read(char *buf, int len)
{
    return snprintf(buf, len, gpio_get(PIN_ALARM_LED) ? "off\n" : "on\n");
}

static int led_write(const char *buf, int len)
{
    // buf isn't guaranteed nul-terminated by the caller; compare by length.
    if (len >= 2 && strncmp(buf, "on", 2) == 0) {
        gpio_put(PIN_ALARM_LED, 0);
        return len;
    }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) {
        gpio_put(PIN_ALARM_LED, 1);
        return len;
    }
    return -1;
}

static const device_t dev_led = {"/dev/led", 0, 0, led_read, led_write};

void led_register(void)
{
    fs_register(&dev_led);
}
