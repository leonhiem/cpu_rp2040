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
#define BOARD_I2C_HZ       400000 // PCF8563's max; bring-up first passed at 100k

#define RTC_I2C_ADDR       0x51  // PCF8563T, fixed address
#define FRAM_I2C_ADDR      0x50  // FM24CL64B, A2..A0 = 0
#define FRAM_SIZE          8192  // 64 Kbit; WP hardwired to GND

// PCF8563 notes (details in README.md, "Hardware notes"):
// - INT and CLKOUT: pads only, not wired to a GPIO yet.
// - CLKOUT defaults ON (32.768 kHz): turn it off at init (reg 0x0D = 0x00),
//   it only drains the backup supercap.
// - control_2 bits 7..6 are unused and power up random (seen 0x80/0xc0):
//   write control_2 explicitly at init.
// - Seconds bit 7 = VL: time not trusted (VDD fell below ~0.9 V). Writing
//   the time clears it.
// - Backup: 10 F / 3 V supercap on VDD, charged from 3V3 via 220R + 1N4007.
//   Clock runs down to ~1.0 V.

// LED: same as babywarmer's alarm LED, inverted in hardware (0 = lit).
// The GPIO sinks the LED: anode -> 3V3, cathode -> 220R -> GPIO1.
//
// LESSON (2026-11-11): the anode was first soldered to +5V. That "works"
// (LED blinks), but RP2040 GPIOs are NOT 5V tolerant (abs max IOVDD+0.5V):
// whenever the pin is high or unconfigured (every reset/boot), 5V leaks
// through the LED into the pin's ESD diode, back into 3V3. Any external
// load on a GPIO goes to 3V3, never to +5V -- use a transistor/MOSFET if
// something really needs 5V. Red LED at 3V3/220R is ~7mA, so the pin's
// drive strength is raised to 8mA (default is 4mA).
#define PIN_ALARM_LED      1

// Lamp output, GPIO6 like babywarmer's baby-light. babywarmer drove it
// active-high (1 = on); on this board a test LED is wired the same way
// as the one on GPIO1 (3V3 -> LED -> resistor -> GPIO6), so 0 = lit.
// Flip PIN_LAMP_ON to 1 for an active-high load (e.g. a transistor).
//
// LESSON (2026-11-12): before picoos configured GPIO6, that LED glowed
// dimly with 1.5 V on the pin -- an unconfigured RP2040 pin is an input
// with a ~50k internal pull-down, enough to pass a few tens of uA through
// an LED wired to 3V3. Outputs like this get set to "off" first thing at
// boot (board_lamp_init()).
#define PIN_LAMP           6
#define PIN_LAMP_ON        0

// Push buttons, active low, internal pull-ups.
#define BUTTON_PIN_MUTE    2
#define BUTTON_PIN_UP      7
#define BUTTON_PIN_DOWN    8
#define BUTTON_PIN_MANUAL  9
#define BUTTON_PIN_START  28
#define BUTTON_PIN_LAMP   29

// drivers/board.cpp
void board_i2c_init(void);
void board_led_init(void);   // configured, off
void board_lamp_init(void);  // configured, off
