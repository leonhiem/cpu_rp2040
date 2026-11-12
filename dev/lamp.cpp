/**
 * dev/lamp.cpp — /dev/lamp: read/write, the lamp output (GPIO6)
 *
 * From picoos's dev/lamp.cpp (babywarmer's baby-light), unchanged except
 * for polarity: PIN_LAMP_ON in board.h says which level means "on".
 *
 * write /dev/lamp on   -> lamp on
 * write /dev/lamp off  -> lamp off
 * cat /dev/lamp         -> "on\n" or "off\n", the current state
 * Anything else written is ignored (returns -1: bad command).
 *
 * Reads the pin back via gpio_get() rather than tracking a separate
 * software flag -- RP2040 reads an output-configured pin's actual
 * driven level, so there's one source of truth (the hardware) instead
 * of two things that could drift out of sync.
 */
#include "board.h"
#include "kernel/fs.h"
#include "hardware/gpio.h"
#include <cstdio>
#include <cstring>

static int lamp_read(char *buf, int len)
{
    return snprintf(buf, len, gpio_get(PIN_LAMP) == PIN_LAMP_ON ? "on\n" : "off\n");
}

static int lamp_write(const char *buf, int len)
{
    // buf isn't guaranteed nul-terminated by the caller; compare by length.
    if (len >= 2 && strncmp(buf, "on", 2) == 0) {
        gpio_put(PIN_LAMP, PIN_LAMP_ON);
        return len;
    }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) {
        gpio_put(PIN_LAMP, !PIN_LAMP_ON);
        return len;
    }
    return -1;
}

static const device_t dev_lamp = {
    "/dev/lamp",
    0,             // no open needed -- board_lamp_init() already configured PIN_LAMP
    0,             // no close needed
    lamp_read,
    lamp_write,
};

void lamp_register(void)
{
    fs_register(&dev_lamp);
}
