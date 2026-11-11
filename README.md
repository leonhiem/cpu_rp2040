# cpu_rp2040

A small, general-purpose RP2040 board, and the next version of
[picoos](https://github.com/leonhiem/picoos) running on it.

The PCB is derived from the babywarmer board: same RP2040, power
supply, crystal, QSPI flash, CD4093/CD4013 on/off logic and USB, with
two header rows exposing all GPIOs, plus:

- **PCF8563T** real-time clock (I2C1, address `0x51`; `INT`/`CLKOUT` on pads only)
- **FM24CL64B** 8 KB FRAM (I2C1, address `0x50`; `WP` tied to GND)

Pin map: [`board.h`](board.h).

## Hardware notes

- **GPIOs are 3.3 V only, never wire a load to +5 V.** The first test
  LED had its anode on +5 V (cathode via 220 Ω to GPIO1). It blinked
  fine, but whenever the pin is high or unconfigured (every reset and
  boot) 5 V leaks through the LED into the pin's protection diode. RP2040
  pins tolerate at most IOVDD + 0.5 V. Fixed by moving the anode to 3V3.
  Anything that really needs 5 V gets a transistor or MOSFET in between.

### RTC (PCF8563T)

- **Voltages:** the clock keeps time down to about **1.0 V**. The I2C
  interface needs at least 1.8 V, but that only matters while the board
  is powered, and then VDD comes from 3V3 anyway.
- **VL flag** (bit 7 of the seconds register): set when VDD has dropped
  below about 0.9 V, i.e. the time can't be trusted. Expect it on first
  power-up of a new board, or after the backup cap ran flat. Writing the
  time (`set`) clears it. A clear VL after a power-off means the backup
  held.
- **First power-up contents are garbage:** the first board woke up at
  `2000-12-14 06:02:20` (random registers), and `control_2` read
  `0x80`/`0xc0`: bits 7–6 are unused and come up random. The
  alarm/timer bits (AF, TF, AIE, TIE) were 0. A driver should write
  `control_2` explicitly at init, not trust its power-on value.
- **CLKOUT is on by default** (32.768 kHz square wave on its pad). The
  pad isn't wired to anything, so a driver should switch it off
  (register `0x0D` = `0x00`): it costs extra backup current for nothing.
- **INT and CLKOUT** are on pads only, not connected to a GPIO yet. A
  wire to a free GPIO later makes RTC alarms/timer interrupts possible.
- Bring-up result (2026-11-11): found at `0x51`, ticking, `set` works
  and clears VL. Kept the time over 15 minutes unplugged on the
  supercap (charged to ~1.8 V at the time), with VL still clear.

### RTC backup supercap

- **Circuit:** follows the PCF8563 application note, which uses 1 F. This
  board uses **10 F / 3 V** instead (that's what was at hand): charged
  from 3V3 through **220 Ω + 1N4007** in series, the cap sitting directly
  on the PCF8563's VDD. The diode does two jobs: its drop keeps the cap
  at or below its 3 V rating (it ends up around 2.8–2.9 V, as the drop
  shrinks at low current), and it blocks the cap from discharging back
  into the 3V3 rail when the board is off.
- **Charging takes hours:** RC = 10 F × 220 Ω = 2200 s (~37 min), and the
  tail is slower still as the diode drop falls off. Initial charging
  current is about 15 mA from 3V3. Don't judge the backup until the cap
  is fully charged.
- **The cap is useful well before it's full:** since the clock runs down
  to ~1.0 V, a cap at 1.8 V already backs up the RTC. Each extra volt
  just buys more time.
- **How long it lasts:** the PCF8563 draws about 0.25 µA in backup (with
  CLKOUT off). 10 F from 2.9 V down to 1.0 V is ~19 C, which on paper is
  years. In practice the supercap's own leakage dominates, so expect
  weeks to months. Measure it rather than trusting the estimate: power
  off, wait, power on, `rtc`, check VL.
- 10× the application note's capacitance means 10× the charge time and
  roughly 10× the backup time.

This repo started fresh (2026-11-10). picoos's history up to here lives
in its own repo, at commit `89c16e5`.

## Building

Same toolchain setup as picoos (Slackware paths, adjust for your system):

```sh
export PICO_SDK_PATH=/home/leon/pico/pico-sdk
export PICO_TOOLCHAIN_PATH=/home/leon/pico/pico-sdk/toolchain/gcc-arm-none-eabi-10.3-2021.10
export CMAKE_FIND_ROOT_PATH=$PICO_TOOLCHAIN_PATH
export CMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
export CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
export CMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY

mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

Flash a `.uf2` the usual way (hold BOOTSEL, plug in USB, copy the file).

## `bringup` — board test

`build/bringup.uf2` is a standalone test, no OS. On boot (it waits up
to 5 s for a USB serial terminal to attach) it runs:

- an I2C scan, expecting `0x50` (FRAM) and `0x51` (RTC)
- a non-destructive test of all 8 KB of FRAM: every byte is written
  with a pattern and its inverse, then restored, plus an address-line
  aliasing check
- an RTC check: control registers, sane date/time fields, seconds
  actually ticking, and the VL (voltage-low) flag

then prints a summary and drops into a `>` prompt:

| Command | |
|---|---|
| `all` | run every test again |
| `scan` / `fram` / `rtc` | run one test |
| `time` | print the RTC date/time |
| `set YYYY-MM-DD HH:MM:SS` | set the RTC (clears VL) |

Meanwhile the alarm LED (GPIO1) blinks at 1 Hz and every push-button
press/release is printed (mute, up, down, manual, start, lamp).

## License

MIT — see [LICENSE](LICENSE).
