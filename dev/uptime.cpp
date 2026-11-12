/**
 * dev/uptime.cpp — /dev/uptime: read-only, whole seconds since boot
 *
 * A plain number, so it pipes into thresh/hyst like any sensor. The
 * "up 1d 02:03:04" formatting is bin/uptime's job.
 */
#include "kernel/fs.h"
#include "pico/time.h"
#include <cstdio>

static int uptime_read(char *buf, int len)
{
    return snprintf(buf, len, "%lu\n", (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000));
}

static const device_t dev_uptime = {"/dev/uptime", 0, 0, uptime_read, 0};

void uptime_register(void)
{
    fs_register(&dev_uptime);
}
