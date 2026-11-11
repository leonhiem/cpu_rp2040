/**
 * bringup.cpp — first-power-up test for the cpu_rp2040 PCB
 *
 * Standalone, no picoos kernel: proves the board itself before the OS
 * moves onto it. On boot it runs every test once and prints a summary,
 * then stays in a small command loop over USB serial:
 *
 *   all                          run every test again
 *   scan                         I2C bus scan (expect 0x50 FRAM, 0x51 RTC)
 *   fram                         non-destructive FRAM test (all 8 KB)
 *   rtc                          RTC test: registers sane, clock ticking
 *   time                         print RTC date/time once
 *   set YYYY-MM-DD HH:MM:SS      set the RTC
 *   help
 *
 * Meanwhile the alarm LED blinks a 1 Hz heartbeat and every button
 * press/release is printed.
 */

#include <pico/stdlib.h>
#include <cstdio>
#include <cstring>
#include "hardware/i2c.h"
#include "board.h"

#define I2C_TIMEOUT_US 10000

// ---------------------------------------------------------------- I2C

static void i2c_setup(void)
{
    i2c_init(BOARD_I2C, BOARD_I2C_HZ);
    gpio_set_function(BOARD_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_I2C_SCL, GPIO_FUNC_I2C);
    // External 4k7 pull-ups on the board, no internal ones needed.
}

static bool i2c_reserved(uint8_t addr)
{
    return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}

static bool test_scan(void)
{
    bool fram = false, rtc = false;
    int found = 0;
    printf("i2c scan (SDA=GPIO%d SCL=GPIO%d):", BOARD_I2C_SDA, BOARD_I2C_SCL);
    for (int addr = 0; addr < 0x80; addr++) {
        if (i2c_reserved(addr))
            continue;
        uint8_t b;
        if (i2c_read_timeout_us(BOARD_I2C, addr, &b, 1, false, I2C_TIMEOUT_US) >= 0) {
            printf(" 0x%02x", addr);
            found++;
            if (addr == FRAM_I2C_ADDR) fram = true;
            if (addr == RTC_I2C_ADDR) rtc = true;
        }
    }
    printf("%s\n", found ? "" : " nothing found");
    if (!fram) printf("  FRAM not found at 0x%02x\n", FRAM_I2C_ADDR);
    if (!rtc)  printf("  RTC not found at 0x%02x\n", RTC_I2C_ADDR);
    return fram && rtc;
}

// ---------------------------------------------------------------- FRAM
//
// FM24CL64B: 2-byte memory address, no page size, no write delay.

static bool fram_read(uint16_t addr, uint8_t *buf, int len)
{
    uint8_t a[2] = { (uint8_t)(addr >> 8), (uint8_t)addr };
    if (i2c_write_timeout_us(BOARD_I2C, FRAM_I2C_ADDR, a, 2, true, I2C_TIMEOUT_US) != 2)
        return false;
    return i2c_read_timeout_us(BOARD_I2C, FRAM_I2C_ADDR, buf, len, false,
                               I2C_TIMEOUT_US + len * 100) == len;
}

static bool fram_write(uint16_t addr, const uint8_t *buf, int len)
{
    uint8_t tmp[2 + 64];
    if (len > 64)
        return false;
    tmp[0] = addr >> 8;
    tmp[1] = addr;
    memcpy(tmp + 2, buf, len);
    return i2c_write_timeout_us(BOARD_I2C, FRAM_I2C_ADDR, tmp, len + 2, false,
                                I2C_TIMEOUT_US + len * 100) == len + 2;
}

static uint8_t fram_pattern(uint16_t addr)
{
    return (uint8_t)(addr * 7 + 0x5a) ^ (uint8_t)(addr >> 8);
}

// Every cell: save, write pattern, verify, write inverse, verify, restore.
static bool fram_test_cells(void)
{
    const int CHUNK = 32;
    uint8_t orig[CHUNK], pat[CHUNK], got[CHUNK];
    int errors = 0;

    for (int base = 0; base < FRAM_SIZE; base += CHUNK) {
        if (!fram_read(base, orig, CHUNK)) {
            printf("  i2c error reading 0x%04x\n", base);
            return false;
        }
        for (int inv = 0; inv < 2; inv++) {
            for (int i = 0; i < CHUNK; i++)
                pat[i] = fram_pattern(base + i) ^ (inv ? 0xff : 0x00);
            if (!fram_write(base, pat, CHUNK) || !fram_read(base, got, CHUNK)) {
                printf("  i2c error at 0x%04x\n", base);
                fram_write(base, orig, CHUNK);
                return false;
            }
            for (int i = 0; i < CHUNK; i++) {
                if (got[i] != pat[i] && errors++ < 8)
                    printf("  0x%04x: wrote 0x%02x read 0x%02x\n", base + i, pat[i], got[i]);
            }
        }
        if (!fram_write(base, orig, CHUNK) || !fram_read(base, got, CHUNK)
            || memcmp(orig, got, CHUNK) != 0) {
            printf("  could not restore 0x%04x\n", base);
            return false;
        }
    }
    if (errors)
        printf("  %d bad bytes\n", errors);
    return errors == 0;
}

// Address lines: a unique marker at 0 and at every power of two. If an
// address bit is stuck or the chip is smaller than expected, markers
// overwrite each other. The cell test above can't see that, since it
// restores each chunk before moving on.
static bool fram_test_aliasing(void)
{
    uint16_t addrs[16];
    uint8_t orig[16];
    int n = 0;
    bool ok = true;

    addrs[n++] = 0;
    for (uint16_t a = 1; a < FRAM_SIZE; a <<= 1)
        addrs[n++] = a;

    for (int i = 0; i < n; i++)
        if (!fram_read(addrs[i], &orig[i], 1))
            return false;
    for (int i = 0; i < n; i++) {
        uint8_t m = 0xa0 + i;
        if (!fram_write(addrs[i], &m, 1))
            ok = false;
    }
    for (int i = 0; i < n; i++) {
        uint8_t m;
        if (!fram_read(addrs[i], &m, 1) || m != 0xa0 + i) {
            printf("  address 0x%04x aliased (read 0x%02x)\n", addrs[i], m);
            ok = false;
        }
    }
    for (int i = 0; i < n; i++)
        fram_write(addrs[i], &orig[i], 1);
    return ok;
}

static bool test_fram(void)
{
    printf("fram: testing %d bytes (non-destructive)...\n", FRAM_SIZE);
    absolute_time_t t0 = get_absolute_time();
    bool ok = fram_test_cells() && fram_test_aliasing();
    printf("fram: %s (%lld ms)\n", ok ? "ok" : "FAILED",
           absolute_time_diff_us(t0, get_absolute_time()) / 1000);
    return ok;
}

// ---------------------------------------------------------------- RTC
//
// PCF8563: registers 0x02..0x08 = sec min hour day weekday month year,
// BCD. Seconds bit 7 = VL (clock integrity not guaranteed, e.g. after
// the backup supply ran out). Control_1 bit 5 = STOP.

struct rtc_time {
    int year, month, day, weekday, hour, min, sec;
    bool vl;
};

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0f); }
static uint8_t bin2bcd(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static bool rtc_read_regs(uint8_t reg, uint8_t *buf, int len)
{
    if (i2c_write_timeout_us(BOARD_I2C, RTC_I2C_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1)
        return false;
    return i2c_read_timeout_us(BOARD_I2C, RTC_I2C_ADDR, buf, len, false, I2C_TIMEOUT_US) == len;
}

static bool rtc_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_write_timeout_us(BOARD_I2C, RTC_I2C_ADDR, b, 2, false, I2C_TIMEOUT_US) == 2;
}

static bool rtc_get(rtc_time *t)
{
    uint8_t r[7];
    if (!rtc_read_regs(0x02, r, 7))
        return false;
    t->vl      = r[0] & 0x80;
    t->sec     = bcd2bin(r[0] & 0x7f);
    t->min     = bcd2bin(r[1] & 0x7f);
    t->hour    = bcd2bin(r[2] & 0x3f);
    t->day     = bcd2bin(r[3] & 0x3f);
    t->weekday = r[4] & 0x07;
    t->month   = bcd2bin(r[5] & 0x1f);
    t->year    = 2000 + bcd2bin(r[6]) + ((r[5] & 0x80) ? 100 : 0);
    return true;
}

static bool rtc_valid(const rtc_time *t)
{
    return t->sec < 60 && t->min < 60 && t->hour < 24 && t->day >= 1 && t->day <= 31
        && t->month >= 1 && t->month <= 12;
}

// 0 = Sunday (Sakamoto)
static int weekday_of(int y, int m, int d)
{
    static const int k[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y--;
    return (y + y / 4 - y / 100 + y / 400 + k[m - 1] + d) % 7;
}

static bool rtc_set(const rtc_time *t)
{
    uint8_t b[8] = {
        0x02,
        bin2bcd(t->sec),            // writing seconds clears VL
        bin2bcd(t->min),
        bin2bcd(t->hour),
        bin2bcd(t->day),
        (uint8_t)weekday_of(t->year, t->month, t->day),
        (uint8_t)(bin2bcd(t->month) | (t->year >= 2100 ? 0x80 : 0)),
        bin2bcd(t->year % 100),
    };
    // Stop the clock while writing so the new second starts cleanly.
    if (!rtc_write_reg(0x00, 0x20))
        return false;
    bool ok = i2c_write_timeout_us(BOARD_I2C, RTC_I2C_ADDR, b, 8, false, I2C_TIMEOUT_US) == 8;
    return rtc_write_reg(0x00, 0x00) && ok;
}

static void print_time(const rtc_time *t)
{
    static const char *wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "???" };
    printf("%04d-%02d-%02d %02d:%02d:%02d %s%s\n", t->year, t->month, t->day,
           t->hour, t->min, t->sec, wd[t->weekday], t->vl ? "  (VL: time not trusted, use set)" : "");
}

static bool test_rtc(void)
{
    uint8_t ctl[2];
    rtc_time a, b;

    if (!rtc_read_regs(0x00, ctl, 2) || !rtc_get(&a)) {
        printf("rtc: i2c error\nrtc: FAILED\n");
        return false;
    }
    printf("rtc: control_1=0x%02x control_2=0x%02x\n", ctl[0], ctl[1]);
    printf("rtc: ");
    print_time(&a);
    if (ctl[0] & 0x20) {
        printf("rtc: STOP bit set, clock halted (set clears it)\nrtc: FAILED\n");
        return false;
    }
    if (!rtc_valid(&a)) {
        printf("rtc: registers out of range\nrtc: FAILED\n");
        return false;
    }

    // Must tick within ~1s; allow 2.5s.
    absolute_time_t deadline = make_timeout_time_ms(2500);
    do {
        sleep_ms(50);
        if (!rtc_get(&b)) {
            printf("rtc: i2c error\nrtc: FAILED\n");
            return false;
        }
    } while (b.sec == a.sec && !time_reached(deadline));

    if (b.sec == a.sec) {
        printf("rtc: seconds not advancing (crystal?)\nrtc: FAILED\n");
        return false;
    }
    printf("rtc: ticking (%02d -> %02d)\n", a.sec, b.sec);
    printf("rtc: %s\n", a.vl ? "ok, but VL set: time needs setting" : "ok");
    return true;
}

static bool parse_set(const char *s, rtc_time *t)
{
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &t->year, &t->month, &t->day,
               &t->hour, &t->min, &t->sec) != 6)
        return false;
    return t->year >= 2000 && t->year <= 2199 && rtc_valid(t);
}

// ---------------------------------------------------------------- buttons

static const struct { int pin; const char *name; } buttons[] = {
    { BUTTON_PIN_MUTE,   "mute"   },
    { BUTTON_PIN_UP,     "up"     },
    { BUTTON_PIN_DOWN,   "down"   },
    { BUTTON_PIN_MANUAL, "manual" },
    { BUTTON_PIN_START,  "start"  },
    { BUTTON_PIN_LAMP,   "lamp"   },
};
#define NBUTTONS (sizeof(buttons) / sizeof(buttons[0]))

static bool btn_stable[NBUTTONS], btn_prev[NBUTTONS];

static void buttons_setup(void)
{
    for (unsigned i = 0; i < NBUTTONS; i++) {
        gpio_init(buttons[i].pin);
        gpio_pull_up(buttons[i].pin);
    }
}

// Called every 20ms: two equal samples in a row = debounced.
static void buttons_poll(void)
{
    for (unsigned i = 0; i < NBUTTONS; i++) {
        bool pressed = !gpio_get(buttons[i].pin);
        if (pressed == btn_prev[i] && pressed != btn_stable[i]) {
            btn_stable[i] = pressed;
            printf("button %s %s\n", buttons[i].name, pressed ? "pressed" : "released");
        }
        btn_prev[i] = pressed;
    }
}

// ---------------------------------------------------------------- main

static void run_all(void)
{
    bool scan = test_scan();
    bool fram = test_fram();
    bool rtc = test_rtc();
    printf("\nsummary: i2c %s, fram %s, rtc %s\n\n",
           scan ? "ok" : "FAIL", fram ? "ok" : "FAIL", rtc ? "ok" : "FAIL");
}

static void help(void)
{
    printf("commands: all scan fram rtc time help\n"
           "          set YYYY-MM-DD HH:MM:SS\n"
           "press any button to see it reported\n");
}

static void dispatch(char *line)
{
    rtc_time t;
    if (!strcmp(line, "all"))       run_all();
    else if (!strcmp(line, "scan")) test_scan();
    else if (!strcmp(line, "fram")) test_fram();
    else if (!strcmp(line, "rtc"))  test_rtc();
    else if (!strcmp(line, "time")) {
        if (rtc_get(&t)) print_time(&t);
        else printf("rtc: i2c error\n");
    } else if (!strncmp(line, "set ", 4)) {
        if (!parse_set(line + 4, &t))
            printf("usage: set YYYY-MM-DD HH:MM:SS\n");
        else if (!rtc_set(&t))
            printf("rtc: i2c error\n");
        else if (rtc_get(&t))
            print_time(&t);
    } else if (!strcmp(line, "help")) help();
    else if (line[0]) printf("?%s\n", line);
}

int main(void)
{
    stdio_init_all();

    gpio_init(PIN_ALARM_LED);
    gpio_set_dir(PIN_ALARM_LED, GPIO_OUT);
    gpio_set_drive_strength(PIN_ALARM_LED, GPIO_DRIVE_STRENGTH_8MA); // ~7mA, see board.h
    gpio_put(PIN_ALARM_LED, 1); // off (inverted)

    buttons_setup();
    i2c_setup();

    // Give a terminal a moment to attach so the boot report isn't lost.
    absolute_time_t wait = make_timeout_time_ms(5000);
    while (!stdio_usb_connected() && !time_reached(wait))
        sleep_ms(50);
    sleep_ms(200);

    printf("\ncpu_rp2040 bring-up\n\n");
    run_all();
    help();

    char line[64];
    int len = 0;
    absolute_time_t next_blink = get_absolute_time();
    absolute_time_t next_poll = get_absolute_time();
    bool led = false;

    printf("> ");
    for (;;) {
        if (time_reached(next_blink)) {
            led = !led;
            gpio_put(PIN_ALARM_LED, !led);
            next_blink = make_timeout_time_ms(500);
        }
        if (time_reached(next_poll)) {
            buttons_poll();
            next_poll = make_timeout_time_ms(20);
        }

        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT)
            continue;
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[len] = 0;
            dispatch(line);
            len = 0;
            printf("> ");
        } else if ((c == 0x08 || c == 0x7f) && len > 0) {
            len--;
            printf("\b \b");
        } else if (c >= 0x20 && c < 0x7f && len < (int)sizeof(line) - 1) {
            line[len++] = c;
            putchar(c);
        }
    }
}
