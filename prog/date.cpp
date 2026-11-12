/**
 * prog/date.cpp — date: the RTC's time, human-readable
 *
 *   % date
 *   Wed Nov 11 22:18:00 2026
 *
 * Just a formatter over /dev/rtc (which speaks ISO "YYYY-MM-DD HH:MM:SS"),
 * same split as real Unix: the device holds the value, the program
 * dresses it up. Setting the time is the device's job, not date's:
 * `echo 2026-11-11 22:18:00 > /dev/rtc`.
 *
 * If /dev/rtc/vl says the time can't be trusted, says so on a second
 * line rather than printing a confident-looking wrong date.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "drivers/pcf8563.h" // rtc_parse()/rtc_weekday_of() only -- pure functions, no I2C
#include <cstdio>

static int read_dev(const char *path, char *buf, int len)
{
    int fd = fs_open(path);
    if (fd < 0) return -1;
    int n = fs_read(fd, buf, len - 1);
    fs_close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    return n;
}

static int date_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen; (void)argc; (void)argv;
    static const char *wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char buf[32];
    rtc_time t;
    if (read_dev("/dev/rtc", buf, sizeof(buf)) < 0 || !rtc_parse(buf, &t)) return -1;

    int n = snprintf(out, outlen, "%s %s %2d %02d:%02d:%02d %04d\n",
                     wd[rtc_weekday_of(t.year, t.month, t.day)], mon[t.month - 1],
                     t.day, t.hour, t.min, t.sec, t.year);
    if (n < 0 || n >= outlen) return -1;

    if (read_dev("/dev/rtc/vl", buf, sizeof(buf)) >= 2 && buf[0] == 'o' && buf[1] == 'n') {
        int m = snprintf(out + n, outlen - n,
                         "(not trusted: echo YYYY-MM-DD HH:MM:SS > /dev/rtc)\n");
        if (m > 0 && m < outlen - n) n += m;
    }
    return n;
}

static const program_t prog_date = {"date", date_run};

void date_register(void)
{
    prog_register(&prog_date);
}
