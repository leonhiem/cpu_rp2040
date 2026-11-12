/**
 * prog/uptime.cpp — uptime: time since boot, human-readable
 *
 *   % uptime
 *   up 0d 01:02:03
 *
 * Formatter over /dev/uptime (plain seconds), same split as bin/date
 * over /dev/rtc.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>

static int uptime_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen; (void)argc; (void)argv;
    int fd = fs_open("/dev/uptime");
    if (fd < 0) return -1;
    char buf[16];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';

    unsigned long s = strtoul(buf, 0, 10);
    return snprintf(out, outlen, "up %lud %02lu:%02lu:%02lu\n",
                    s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
}

static const program_t prog_uptime = {"uptime", uptime_run};

void uptime_prog_register(void)
{
    prog_register(&prog_uptime);
}
