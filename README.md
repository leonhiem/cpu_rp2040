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
- **Unconfigured pins have a pull-down: an LED to 3V3 on one glows.**
  Before picoos drove GPIO6, a test LED there (3V3 → LED → 220 Ω →
  GPIO6) glowed dimly, with 1.5 V on the pin. At reset every RP2040 GPIO
  is an input with a ~50 kΩ pull-down, which lets a few tens of µA
  through the LED. Outputs like this are now switched off first thing at
  boot (`board_lamp_init()`, also in `bringup`).
- **The push buttons have no external pull-ups:** the RP2040's internal
  pull-ups are switched on in software (`buttons_init()`): 3.3 V
  released, 0 V pressed.

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

Flash a `.uf2` the usual way (hold BOOTSEL, plug in USB, copy the file),
or from a running picoos: `echo bootsel > /dev/reboot`.

Two firmwares come out of one build:

- `build/picoos.uf2`: the OS (below)
- `build/bringup.uf2`: the board test (further below)

## picoos

The next version of [picoos](https://github.com/leonhiem/picoos):
a Plan 9-flavored little OS. Everything is a file, small programs,
pipes, background jobs, a `%` shell. Same kernel, shell and generic
programs as picoos; the babywarmer-specific parts (heater, NTC sensors,
7-segment, TFT, PID/phase/alarm wiring) stayed behind. New: this board's
own devices and more small Unix-style programs. Small commands, many of
them, each doing one thing.

Connect a USB serial terminal; type `help` for the builtins and grammar,
`ls` for every device and program.

```
stage [| stage ...] [< path] [> path] [&]
stage := program [arg...]
```

At every power-up the built-in `boot` script prints the date and wires
the LAMP button to the lamp output, as on babywarmer
(`toggle /dev/buttons/lamp /dev/lamp &`).

### Devices (`/dev/...`)

| Device | R/W | What it does |
|---|---|---|
| `/dev/led` | read/write | The on-board LED (GPIO1). `on`/`off`. |
| `/dev/lamp` | read/write | The lamp output (GPIO6, babywarmer's baby-light). `on`/`off`. Active-low for now (`PIN_LAMP_ON` in `board.h`). |
| `/dev/buttons` | read | Names of all buttons pressed since the last read, read-and-clear. |
| `/dev/buttons/<name>` | read | One button (`up`, `down`, `mute`, `manual`, `start`, `lamp`): `1` if pressed since the last read, else `0`. |
| `/dev/rtc` | read/write | The RTC: `YYYY-MM-DD HH:MM:SS`. Write the same format to set it. |
| `/dev/rtc/vl` | read | `on` if the RTC time can't be trusted (backup ran flat), else `off`. |
| `/dev/uptime` | read | Seconds since boot. |
| `/dev/cputemp` | read | RP2040 die temperature, °C (rough, a few degrees). Each read averages 16 ADC samples. |
| `/dev/reboot` | write | `now` reboots, `bootsel` reboots into the USB bootloader. |

### Programs (`bin/...`)

| Program | Usage | What it does |
|---|---|---|
| `ls` | `ls` | List every device and program. |
| `cat` | `cat [path]` | Read a device, or pass piped input through. |
| `echo` | `echo <words...>` | Print its arguments. |
| `date` | `date` | `/dev/rtc`, human-readable: `Wed Nov 11 22:18:00 2026`. |
| `uptime` | `uptime` | `/dev/uptime`, human-readable: `up 0d 01:02:03`. |
| `wc` | `wc [-l]` | Count lines, words, bytes of piped input (`-l`: lines only). |
| `grep` | `grep [-v] <text>` | Keep piped lines containing `<text>` (`-v`: not containing). Plain substring, no regex. |
| `head` | `head [n]` | First n lines (default 10). |
| `tail` | `tail [n]` | Last n lines (default 10). |
| `tee` | `tee <path>` | Write piped input to a device and pass it on. |
| `label` | `label <word...> <path>` | Words followed by a device's value: `label temp = /dev/cputemp`. |
| `thresh` | `thresh <n or path>` | Piped number `>=` threshold → `on`, else `off`. |
| `hyst` | `hyst <n or path> <band>` | Like `thresh` with a hysteresis band, heating polarity (`on` when below). |
| `toggle` | `toggle <button> <target>` | If the button was pressed, flip the target `on`/`off`. Run with `&`. |
| `adjust` | `adjust <gate> <on-target> <off-target> <step>` | UP/DOWN steps whichever target the gate selects. Run with `&`. |
| `follow` | `follow <gate> <on-source> <off-source> <target>` | Copy whichever source the gate selects to the target. Run with `&`. |
| `select` | `select <state> <label>=<source> ...` | Output the source whose label matches the state device's value. |

Builtins (`help` lists them; they're not in `ls`, like `cd` isn't on
`$PATH`): `jobs`, `kill %<id>`, `sleep <ms>`, `script <name>`,
`run <name>`, `loop <name>`, `scripts`, `watch <ms> <pipeline>`.

### Recipes

```
date
echo 2026-11-11 22:18:00 > /dev/rtc        # set the clock
ls | grep /dev | wc -l                     # how many devices?
watch 1000 cat /dev/cputemp                # Ctrl-C to stop
cat /dev/cputemp | thresh 40 > /dev/led &  # LED on when the chip is warm
label up = /dev/uptime                     # "up = 1234"
echo bootsel > /dev/reboot                 # ready for the next .uf2
```

`|`, `<`, `>` and `&` don't need spaces around them: `ls|grep dev`
works the same as `ls | grep dev`.

Scripts are still RAM-only, as in picoos; storing them in the FRAM is
the next step.

### Test log

**2026-11-12, first picoos build on the board** (`picoos.uf2`):

| Test | Result |
|---|---|
| Boot | ok: clean boot, banner, `boot` script ran automatically (`date`, then `toggle ... &` started as job `[1]`) |
| `help`, `ls` | ok: 13 devices, 17 programs listed |
| `cat /dev/rtc`, `date`, `cat /dev/rtc/vl` | ok: agree with each other, VL `off`; the RTC kept time across the power-off (supercap) |
| `cat /dev/uptime`, `uptime` | ok: 70 s, then `up 0d 00:01:25` |
| `cat /dev/cputemp` | ok: 19.2 °C (cool room) |
| `ls \| grep cpu`, `ls \| grep dev` | ok: `/dev/cputemp`; all 13 `/dev` entries |
| `ls \| grep dev \| wc -l` | ok: `13` (three stages) |
| `ls\|grep dev` (no spaces) | **bug**: `ls\|grep: not found`. The shell, inherited from picoos, only split words on spaces. Fixed: `\|`, `<`, `>`, `&` are now words of their own (`tokenize()` in `shell.cpp`) |
| `ls\|grep dev`, `ls\|grep dev\|wc -l` retest | ok: same 14 lines as with spaces (now including `/dev/lamp`); `14` |
| LAMP button toggles `/dev/led` (GPIO1) | ok: GPIO1 toggles on each press (multimeter) |
| LED on GPIO6 | dimly lit, 1.5 V on the pin. Not the toggle racing: picoos didn't drive GPIO6 at all, so the pin floated on its internal pull-down (see Hardware notes). Added `/dev/lamp` on GPIO6 (as babywarmer), switched off at boot; `boot` now toggles `/dev/lamp` with the LAMP button |
| `/dev/lamp` retest | ok: LED off after boot, LAMP button toggles it, `cat /dev/lamp` matches what's lit (active-low, `PIN_LAMP_ON 0`, confirmed) |
| `watch 1000 cat /dev/cputemp`, Ctrl-C | ok: one reading per second (19.2–21.1 °C), Ctrl-C prints `(watch stopped)` and returns to the prompt. Readings jump in ~0.5 °C steps: that's one ADC count (3.3 V / 4096 / 1.721 mV/°C ≈ 0.47 °C), single samples, no averaging. Changed: `/dev/cputemp` now averages 16 samples per read, inside the driver (no knobs, no `ioctl()`) |
| `/dev/cputemp` retest (16-sample average) | ok: 20.4–20.6 °C over 14 s, moving by 0.1 at a time instead of jumping 0.5–1.4 °C |
| `echo bootsel > /dev/reboot` | ok: prints `rebooting into USB bootloader`, the USB drive appears, a new `.uf2` copied onto it boots. That boot reports `Rebooted by watchdog`: expected, the bootloader restarts the chip through the watchdog after a UF2 copy |
| `bringup.uf2` at 400 kHz, shared drivers | ok: scan finds `0x50` + `0x51`, full FRAM test passes in 1606 ms (was 6116 ms at 100 kHz), RTC ticks. `control_1`/`control_2` now read `0x00`: picoos's `rtc_init()` had set them, as intended (they powered up `0x08`/`0x80` on the new chip). So 4k7 pull-ups are fine at 400 kHz on this board |

## `bringup` — board test

Kept as a permanent test program: flash it whenever a board needs
checking.

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
