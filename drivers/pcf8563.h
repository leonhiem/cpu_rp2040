/**
 * drivers/pcf8563.h — PCF8563T real-time clock on the board's I2C bus
 *
 * Registers 0x02..0x08 = sec min hour day weekday month year, BCD.
 * Seconds bit 7 = VL (clock integrity not guaranteed: VDD fell below
 * ~0.9 V). Control_1 bit 5 = STOP. See README.md "Hardware notes".
 *
 * Shared by bringup (raw register tests) and picoos (dev/rtc.cpp).
 * Blocking I2C calls, each well under a millisecond at 400 kHz.
 */
#pragma once

#include <stdint.h>

struct rtc_time {
    int year, month, day, weekday, hour, min, sec; // weekday: 0 = Sunday
    bool vl;
};

// One-time setup for picoos: control_1/control_2 to known values (their
// unused bits power up random), CLKOUT off (pad unused, it only drains
// the backup supercap). Leaves the time itself alone. Not used by
// bringup, which wants to see the raw power-on state.
bool rtc_init(void);

bool rtc_read_regs(uint8_t reg, uint8_t *buf, int len);
bool rtc_write_reg(uint8_t reg, uint8_t val);

bool rtc_get(rtc_time *t);
bool rtc_set(const rtc_time *t);   // computes weekday itself; clears VL

bool rtc_valid(const rtc_time *t); // fields in range
int  rtc_weekday_of(int y, int m, int d);

// "YYYY-MM-DD HH:MM:SS" <-> rtc_time. parse also range-checks
// (years 2000-2199, what the chip can hold).
bool rtc_parse(const char *s, rtc_time *t);
int  rtc_format(char *buf, int len, const rtc_time *t);
