/**
 * board.h — cpu_rp2040 PCB pin map and on-board peripherals
 *
 * The general-purpose RP2040 board derived from the babywarmer PCB:
 * RP2040, power supply, crystal, QSPI flash (same size as babywarmer),
 * CD4093/CD4013 on/off logic (pure hardware, no GPIO involved), USB,
 * plus a PCF8563T RTC and an FM24CL64B FRAM sharing one I2C bus.
 *
 * Button and LED GPIOs match babywarmer/picoos (../picoos/warmer.h).
 */
#pragma once

#include "hardware/i2c.h"

// On-board I2C bus: RTC + FRAM. 4k7 pull-ups on the board.
#define BOARD_I2C          i2c1
#define BOARD_I2C_SDA      14
#define BOARD_I2C_SCL      15
#define BOARD_I2C_HZ       100000

#define RTC_I2C_ADDR       0x51  // PCF8563T, fixed address
#define FRAM_I2C_ADDR      0x50  // FM24CL64B, A2..A0 = 0
#define FRAM_SIZE          8192  // 64 Kbit; WP hardwired to GND

// PCF8563 INT and CLKOUT: pads only, not wired to a GPIO yet.

// LED: same as babywarmer's alarm LED, inverted in hardware (0 = lit).
#define PIN_ALARM_LED      1

// Push buttons, active low, internal pull-ups.
#define BUTTON_PIN_MUTE    2
#define BUTTON_PIN_UP      7
#define BUTTON_PIN_DOWN    8
#define BUTTON_PIN_MANUAL  9
#define BUTTON_PIN_START  28
#define BUTTON_PIN_LAMP   29
