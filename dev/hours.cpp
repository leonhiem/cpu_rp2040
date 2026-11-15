/**
 * dev/hours.cpp — /dev/hours: the board's power-on hours (counter 0)
 *
 *   cat /dev/hours              -> "1234:30" (hours:minutes), or "corrupt"/"error"
 *   cat /dev/hours/status       -> "ok", "new" (started at 0 this boot),
 *                                  "corrupt" or "error" (see fs/hours.h)
 *   echo set 1234:30 > /dev/hours/ctl   deliberate set (a replaced board
 *                                       taking over the old counter, a
 *                                       factory reset: set 0:00). Also
 *                                       the only way out of "corrupt".
 *
 * The value is plain text a pipe can use: thresh reads "1234:30" as the
 * number 1234, so `cat /dev/hours | thresh 500 > /dev/led` lights the
 * LED after 500 hours (a service interval).
 *
 * Counting uses the RP2040's own timer, never the RTC: setting the clock
 * or a flat RTC backup can't change operating hours. A kernel task (not
 * a job, so `kill` can't stop it) saves once a minute; a power cut loses
 * at most the minute in progress.
 *
 * Counters 1..6 are reserved for later (e.g. heater hours on babywarmer).
 */
#include "fs/hours.h"
#include "kernel/fs.h"
#include "kernel/task.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

#define POWER 0   // counter 0: power-on hours

static int64_t base;   // counter value minus minutes since boot

static uint32_t up_minutes(void)
{
    return to_ms_since_boot(get_absolute_time()) / 60000;
}

static void hours_task(void)
{
    int64_t m = base + up_minutes();
    if (m > (int64_t)hours_minutes(POWER)) hours_save(POWER, (uint32_t)m);
}

static const char *state_name(hours_state_t s)
{
    switch (s) {
    case HOURS_OK:      return "ok";
    case HOURS_NEW:     return "new";
    case HOURS_CORRUPT: return "corrupt";
    default:            return "error";
    }
}

static int hours_read(char *buf, int len)
{
    hours_state_t s = hours_state(POWER);
    if (s == HOURS_CORRUPT || s == HOURS_ERROR) return snprintf(buf, len, "%s\n", state_name(s));
    uint32_t m = hours_minutes(POWER);
    return snprintf(buf, len, "%lu:%02lu\n", (unsigned long)(m / 60), (unsigned long)(m % 60));
}

static int status_read(char *buf, int len)
{
    return snprintf(buf, len, "%s\n", state_name(hours_state(POWER)));
}

// "set H:MM" -- exact syntax required, so a stray write can't change it.
static int ctl_write(const char *buf, int len)
{
    char tmp[32];
    if (len <= 0 || len >= (int)sizeof(tmp)) return -1;
    memcpy(tmp, buf, len);
    tmp[len] = '\0';
    unsigned long h, mm;
    char end;
    int n = sscanf(tmp, "set %lu:%lu%c", &h, &mm, &end);
    if (n < 2 || (n == 3 && end != '\n') || mm > 59 || h > 70000000) return -1;
    uint32_t m = h * 60 + mm;
    if (!hours_set(POWER, m)) return -1;
    base = (int64_t)m - up_minutes();
    return len;
}

static const device_t dev_hours        = {"/dev/hours",        0, 0, hours_read,  0};
static const device_t dev_hours_status = {"/dev/hours/status", 0, 0, status_read, 0};
static const device_t dev_hours_ctl    = {"/dev/hours/ctl",    0, 0, 0,           ctl_write};

void hours_register(void)
{
    hours_init();
    base = (int64_t)hours_minutes(POWER) - up_minutes();
    switch (hours_state(POWER)) {
    case HOURS_NEW:
        printf("hours: new counter, starting at 0:00\n");
        break;
    case HOURS_CORRUPT:
        printf("hours: COUNTER CORRUPT, not counting. Check, then: echo set H:MM > /dev/hours/ctl\n");
        break;
    case HOURS_ERROR:
        printf("hours: can't read the FRAM, not counting this boot\n");
        break;
    default:
        break;
    }
    fs_register(&dev_hours);
    fs_register(&dev_hours_status);
    fs_register(&dev_hours_ctl);
    task_register("hours", hours_task, 1000); // checks every second, saves on each new minute
}
