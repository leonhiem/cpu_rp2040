/**
 * dev/rtc.cpp — /dev/rtc and /dev/rtc/vl: the PCF8563T real-time clock
 *
 * cat /dev/rtc                          -> "2026-11-11 22:18:00\n"
 * echo 2026-11-11 22:18:00 > /dev/rtc   -> set the clock (clears VL)
 * cat /dev/rtc/vl                       -> "on" if the time can't be
 *                                          trusted (backup ran flat), else "off"
 *
 * ISO order on purpose: sortable, unambiguous, and exactly what you type
 * to set it -- what you read is what you write. Human-friendly output
 * ("Wed Nov 11 ...") is bin/date's job, not the device's.
 *
 * VL is its own device rather than a suffix on the time: one file, one
 * value, so `cat /dev/rtc` always parses the same way.
 */
#include "drivers/pcf8563.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static int rtc_dev_read(char *buf, int len)
{
    rtc_time t;
    if (!rtc_get(&t)) return -1;
    int n = rtc_format(buf, len, &t);
    if (n < 0 || n + 1 >= len) return -1;
    buf[n++] = '\n';
    return n;
}

static int rtc_dev_write(const char *buf, int len)
{
    char tmp[32];
    if (len <= 0 || len >= (int)sizeof(tmp)) return -1;
    memcpy(tmp, buf, len);
    tmp[len] = '\0';
    rtc_time t;
    if (!rtc_parse(tmp, &t) || !rtc_set(&t)) return -1;
    return len;
}

static int vl_read(char *buf, int len)
{
    rtc_time t;
    if (!rtc_get(&t)) return -1;
    return snprintf(buf, len, t.vl ? "on\n" : "off\n");
}

static const device_t dev_rtc    = {"/dev/rtc",    0, 0, rtc_dev_read, rtc_dev_write};
static const device_t dev_rtc_vl = {"/dev/rtc/vl", 0, 0, vl_read,      0};

void rtc_register(void)
{
    if (!rtc_init())
        printf("rtc: init failed (no answer at 0x51?)\n");
    fs_register(&dev_rtc);
    fs_register(&dev_rtc_vl);
}
