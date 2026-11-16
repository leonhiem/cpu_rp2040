// dev/cron.cpp — cron: run lines of /fram/crontab at set times
//
// /fram/crontab, one job per line, the classic five time fields and a
// command (a pipeline, as typed at the prompt):
//
//   # min  hour  day  month  weekday   command
//   *      *     *    *      *         echo tick > /dev/cons
//   */2    *     *    *      *         echo on > /dev/led
//   1-59/2 *     *    *      *         echo off > /dev/led
//   0      8     *    *      1-5       date >> /fram/log
//
// Fields: minute 0-59, hour 0-23, day 1-31, month 1-12, weekday 0-7
// (0 and 7 are Sunday). Each field is `*`, a number, a range `a-b`, a
// step `*` + `/n` or `a-b/n`, or a comma list of those (`0,15,30,45`).
// As in Unix cron: when both day and weekday are restricted (not `*`), a
// line runs if either matches.
//
// Type it with `cat > /fram/crontab`, add lines with `cat >> /fram/crontab`
// (echo can't write lines with `>`, `|`, `&` or `#` until the shell has
// quotes). It's read again every minute,
// so an edit takes effect from the next minute, no restart.
//
//   cat /dev/cron              -> "on" or "off"
//   echo off > /dev/cron       stop running crontab lines (Plan 9 style
//   echo on > /dev/cron        control file; on at power-up)
//
// How it runs: a kernel task, not a job: a job is a program inside the
// pipeline machinery, and cron's whole point is to *start* pipelines;
// they can't nest (the pipe buffers are static). Once a second it reads
// the RTC; on each new minute it runs the matching lines through
// shell_run_cron_line(): pipelines only, no builtins; output discarded
// unless redirected (use /dev/cons to see text on the terminal); a
// trailing `&` starts a background job (e.g. start a thermostat at 08:00).
//
// Nothing runs while /dev/rtc/vl says the time can't be trusted, or
// without a valid RTC reading. The minute in which the board starts is
// skipped (a job runs at the start of its minute, like Unix cron), and
// minutes the clock jumps over (setting it, power off) are not caught up.
// Bad lines are reported once, when the crontab is first read or changed.
#include "kernel/fs.h"
#include "kernel/task.h"
#include "drivers/pcf8563.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

bool shell_run_cron_line(const char *text);

#define CRONTAB "/fram/crontab"

static bool enabled = true;
static long last_minute = -1;          // minutes since 2000, of the last check
static uint32_t last_sum;              // crontab checksum, to report bad lines once

// Does `v` match one field? Field limits lo..hi (weekday: 0..7, 7 = 0).
static bool field_ok(const char *f, int len, int v, int lo, int hi, bool *valid)
{
    *valid = true;
    bool match = false;
    const char *end = f + len;
    while (f < end) {
        const char *comma = (const char *)memchr(f, ',', end - f);
        const char *item_end = comma ? comma : end;
        int a = lo, b = hi, step = 1;
        bool single = false;                 // a plain number, no '*' and no range
        const char *p = f;
        if (*p == '*') {
            p++;
        } else {
            char *q;
            a = strtol(p, &q, 10);
            if (q == p) { *valid = false; return false; }
            p = q;
            b = a;
            single = true;
            if (p < item_end && *p == '-') {
                b = strtol(p + 1, &q, 10);
                if (q == p + 1) { *valid = false; return false; }
                p = q;
                single = false;
            }
        }
        if (p < item_end && *p == '/') {
            char *q;
            step = strtol(p + 1, &q, 10);
            if (q == p + 1 || step < 1) { *valid = false; return false; }
            p = q;
            if (single) b = hi;              // "5/10" = 5, 15, 25, ... (as Vixie cron)
        }
        if (p != item_end || a < lo || b > hi || a > b) { *valid = false; return false; }
        for (int x = a; x <= b; x += step) {
            int y = (hi == 7 && x == 7) ? 0 : x;  // weekday 7 is Sunday too
            if (y == v) match = true;
        }
        f = comma ? comma + 1 : end;
    }
    return match;
}

// Checks one crontab line against time t. Returns 1 = run it (and sets
// *cmd), 0 = not now, -1 = bad line. Blank lines and comments are 0.
static int line_matches(char *line, const rtc_time *t, const char **cmd)
{
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#') return 0;

    static const int lo[5] = { 0, 0, 1, 1, 0 }, hi[5] = { 59, 23, 31, 12, 7 };
    int val[5] = { t->min, t->hour, t->day, t->month, rtc_weekday_of(t->year, t->month, t->day) };
    bool m[5], star[5];
    for (int i = 0; i < 5; i++) {
        while (*p == ' ' || *p == '\t') p++;
        char *f = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (p == f) return -1;                     // fewer than 5 fields
        bool valid;
        m[i] = field_ok(f, p - f, val[i], lo[i], hi[i], &valid);
        if (!valid) return -1;
        star[i] = (p - f == 1 && f[0] == '*');
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return -1;                     // no command
    *cmd = p;

    bool day = (!star[2] && !star[4]) ? (m[2] || m[4]) : (m[2] && m[4]);
    return (m[0] && m[1] && m[3] && day) ? 1 : 0;
}

static void run_crontab(const rtc_time *t)
{
    static char tab[1024 + 1];
    int fd = fs_open(CRONTAB);
    if (fd < 0) return;                            // no crontab: nothing to do
    int n = fs_read(fd, tab, sizeof(tab) - 1);
    fs_close(fd);
    if (n <= 0) return;
    tab[n] = '\0';

    uint32_t sum = 5381;                           // report bad lines once per version
    for (int i = 0; i < n; i++) sum = sum * 33 + (uint8_t)tab[i];
    bool report = (sum != last_sum);
    last_sum = sum;

    int lineno = 0;
    for (char *line = tab; line && *line; ) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        lineno++;
        const char *cmd;
        int r = line_matches(line, t, &cmd);
        if (r < 0 && report) printf("cron: %s line %d: bad, ignored\n", CRONTAB, lineno);
        if (r > 0 && !shell_run_cron_line(cmd))
            printf("cron: %s line %d: not run (builtins can't run from cron)\n", CRONTAB, lineno);
        line = nl ? nl + 1 : 0;
    }
}

static void cron_task(void)
{
    rtc_time t;
    if (!rtc_get(&t) || t.vl || !rtc_valid(&t)) return;
    long minute = ((((long)(t.year - 2000) * 13 + t.month) * 32 + t.day) * 24 + t.hour) * 60 + t.min;
    if (minute == last_minute) return;
    bool first = (last_minute < 0);
    last_minute = minute;
    if (first || !enabled) return;                 // skip the minute we started in
    run_crontab(&t);
}

static int cron_read(char *buf, int len)
{
    return snprintf(buf, len, enabled ? "on\n" : "off\n");
}

static int cron_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { enabled = true;  return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { enabled = false; return len; }
    return -1;
}

static const device_t dev_cron = {"/dev/cron", 0, 0, cron_read, cron_write};

void cron_register(void)
{
    fs_register(&dev_cron);
    task_register("cron", cron_task, 1000);
}
