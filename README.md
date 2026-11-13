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
stage [| stage ...] [< path] [> path | >> path] [&]
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
| `/fram/<name>` | read/write | Files on the FRAM, see [Files on the FRAM](#files-on-the-fram-fram). |

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
| `ls` | `ls [-l] /fram` | List the files on a mount (`-l`: with sizes). |
| `rm` | `rm <path...>` | Remove files. |
| `df` | `df` | Total, used and free space on every mount. |
| `mkfs` | `mkfs /fram` | Format: every file on it is gone. Never happens on its own. |

Builtins (`help` lists them; they're not in `ls`, like `cd` isn't on
`$PATH`): `jobs`, `kill %<id>`, `sleep <ms>`, `script <name>`,
`run <name>`, `loop <name>`, `scripts`, `watch <ms> <pipeline>`.

### Shell rules in one place

- `|`, `<`, `>`, `>>` and `&` don't need spaces around them: `ls|grep dev`
  works the same as `ls | grep dev`.
- `#` at the start of a word starts a comment, to the end of the line
  (handy in script files). Inside a word it's an ordinary character: `a#b`.
- **No quotes yet.** `echo "a | b"` doesn't print `a | b`: the shell sees
  the `|` first. Everything after `>`, `>>`, `|` or `&` belongs to the
  shell, never to `echo`. See [Scripts](#scripts) for what that means
  when writing script files.
- One foreground command at a time. `sleep`, `run`, `loop` and `watch`
  hold the shell until they're done; anything typed or pasted meanwhile
  waits its turn.
- Background jobs (`&`) all run every 150 ms. Up to 20 at once.

### Examples

**Look around**

```
ls                                   # every device, mount and program
ls | grep /dev | wc -l               # how many devices?
ls | grep bin/ | head 5              # the first five programs
cat /dev/rtc/vl                      # can the clock be trusted?
```

**Time**

```
date                                 # Thu Nov 12 18:04:13 2026
echo 2026-11-12 18:04:00 > /dev/rtc  # set the clock (ISO, what you read is what you write)
uptime                               # up 0d 00:01:25
label up = /dev/uptime               # up = 85
```

**Lamp, LED, buttons**

```
echo on > /dev/lamp
cat /dev/lamp                        # on
toggle /dev/buttons/lamp /dev/lamp & # LAMP button flips the lamp (the built-in boot does this)
toggle /dev/buttons/mute /dev/led &  # and MUTE flips the LED
cat /dev/buttons                     # which buttons were pressed since the last look
jobs                                 # [1] toggle /dev/buttons/lamp /dev/lamp &  ...
kill %2                              # stop the MUTE one
```

**Sensors and control, no control-loop code**

```
watch 1000 cat /dev/cputemp                  # Ctrl-C to stop
cat /dev/cputemp | thresh 30 > /dev/led &    # LED on while the chip is at 30 C or more
cat /dev/cputemp | hyst 30 2 > /dev/lamp &   # lamp on below 29, off above 31: a thermostat
cat /dev/cputemp | tee /fram/temp | thresh 30 > /dev/led &   # tee: keep the last reading in a file AND drive the LED
```

**Files**

```
mkfs /fram                           # once, on a new board
echo hello > /fram/a.txt             # create or replace
echo again >> /fram/a.txt            # append
cat /fram/a.txt
cat /fram/a.txt | grep again
ls -l /fram
date >> /fram/log                    # a log, one line per entry
label temp /dev/cputemp >> /fram/log
cat /fram/log | tail 4               # the last two entries
cat /fram/log | wc -l
rm /fram/a.txt
df
```

**Maintenance**

```
echo bootsel > /dev/reboot           # USB bootloader: copy the next .uf2, no BOOTSEL button needed
echo now > /dev/reboot               # plain restart
```

### Files on the FRAM (`/fram`)

The 8 KB FRAM holds a small flat filesystem, mounted at `/fram`. Files
work exactly like devices: `cat`, `>`, `<`, pipes, `tee`. picoos never
had storage that survives a power cycle; this is it.

```
mkfs /fram                       format, once (like format c:)
echo hello > /fram/a.txt         create / replace
date >> /fram/log                append: incremental logfiles
cat /fram/log | wc -l
ls -l /fram
rm /fram/a.txt
df
```

- **Flat names, 1–11 characters**, no directories, no spaces or `/`.
- **At most 32 files, each at most 1 KB** (pipes pass whole 1 KB buffers).
  7360 bytes of space, in 64-byte blocks: a 3-byte file uses 64.
- **Never formatted automatically.** A blank FRAM at boot just says
  `fram: no filesystem, run: mkfs /fram`, so a glitch can't wipe files.
- **Scripts are files.** `script <name>` saves to `/fram/<name>`;
  `run`/`loop <name>` take `/fram/<name>` first, else a built-in one.
  Files can be scripts without `script`: `echo date >> /fram/boot`.
- **`/fram/boot` replaces the built-in boot script** when it exists, so
  what the board does at power-up is a file you edit, not firmware.
  `rm /fram/boot` brings the built-in one back.

### Scripts

A script is a file of shell lines, run one line per shell tick. `sleep`
inside a script really waits; `&` inside a script starts a background
job, exactly as if typed.

```
script hello            # record into /fram/hello, end with a lone '.'
> date
> sleep 1000
> echo one second later
> .
run hello               # once
loop hello              # forever, Ctrl-C to stop
cat /fram/hello         # it's just a file
rm /fram/hello
```

**Your own boot script.** At power-up picoos runs `/fram/boot` if it
exists, otherwise the built-in one (`date` + the LAMP toggle). To make
your own, record it with `script`:

```
script boot
> date                                    # say hello
> toggle /dev/buttons/lamp /dev/lamp &
> toggle /dev/buttons/mute /dev/led &
> cat /dev/cputemp | hyst 30 2 > /dev/lamp &
> .
```

`rm /fram/boot` brings the built-in boot script back.

**`script` vs `echo >>`.** Because there are no quotes yet, `echo` can
only put *simple* lines into a script file:

```
echo date >> /fram/boot                   # fine: appends the line "date"
echo uptime >> /fram/boot                 # fine
```

but **never** a line with `|`, `<`, `>`, `>>`, `&` or `#` in it:

```
echo toggle /dev/buttons/lamp /dev/lamp & >> /fram/boot     # WRONG
```

doesn't write that line into the file. The shell takes `&` and `>>` for
itself: this starts a background job that appends
`toggle /dev/buttons/lamp /dev/lamp` to `/fram/boot` every 150 ms (until
`kill`, or until the file is full at 1 KB). Use `script` for such lines;
it stores what you type, untouched.

**A logger.** `loop` keeps the shell busy until Ctrl-C (it's a
foreground command), but everything else, background jobs included,
keeps running:

```
script logger
> date >> /fram/log
> label temp /dev/cputemp >> /fram/log
> sleep 60000
> .
loop logger             # one entry a minute; Ctrl-C to stop
```

The log stops growing at 1 KB: from then on every pass prints
`/fram/log: write failed`. `rm /fram/log` starts a new one.

### How the filesystem works

`fs/framfs.h`: the FRAM is split into 128 blocks of 64
bytes. Block 0 is a superblock (`pfs1`), then a 32-entry directory (name,
size, first block), then a chain table saying for every block which
block comes next in its file, then 115 data blocks. A file is a chain of
blocks, not one contiguous run, so a file can grow without moving any
other file (the FAT idea). Writes go in an order that survives a power
cut: new data blocks, then their chain, then the directory entry, then
the old blocks are freed. A cut can leak a few blocks (`mkfs` reclaims
them) but never leaves a file pointing at garbage: tested on the PC by
cutting power at every single write of an overwrite. Chain entries are
16-bit, so a bigger FRAM later only needs `FRAM_SIZE` changed in
`board.h` and a `mkfs`.

The kernel side is a **mount** (`kernel/fs.h`): `fs_open("/fram/a.txt")`
finds the mount for `/fram` and hands `a.txt` to the filesystem. Programs
can't tell files from devices. Plan 9's idea of mounting a file server
into the namespace, still in one global namespace.

### Extending picoos

The rule from picoos holds: **a device owns hardware, a program only
ever touches paths** (`fs_open`/`fs_read`/`fs_write`/`fs_close`). No
`ioctl()`: a device that needs another behavior gets another file, as in
Plan 9 (e.g. `/dev/rtc` and `/dev/rtc/vl`, not one file with modes).

**A device** is a `device_t` in `dev/<name>.cpp`: a path, optional
open/close, and `read`/`write` that speak plain text. Template, from
`dev/led.cpp`:

```c++
static int led_read(char *buf, int len)
{
    return snprintf(buf, len, gpio_get(PIN_ALARM_LED) ? "off\n" : "on\n");
}

static int led_write(const char *buf, int len)   // buf is NOT nul-terminated
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { gpio_put(PIN_ALARM_LED, 0); return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { gpio_put(PIN_ALARM_LED, 1); return len; }
    return -1;                                    // rejected
}

static const device_t dev_led = {"/dev/led", 0, 0, led_read, led_write};

void led_register(void) { fs_register(&dev_led); }
```

**A program** is a `program_t` in `prog/<name>.cpp`: piped input + argv
in, text out. Template, from `prog/uptime.cpp`:

```c++
static int uptime_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    int fd = fs_open("/dev/uptime");
    if (fd < 0) return -1;
    char buf[16];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);                                 // always close: files use open-file slots
    if (n < 0) return -1;
    buf[n] = '\0';
    unsigned long s = strtoul(buf, 0, 10);
    return snprintf(out, outlen, "up %lud %02lu:%02lu:%02lu\n", s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
}

static const program_t prog_uptime = {"uptime", uptime_run};

void uptime_prog_register(void) { prog_register(&prog_uptime); }
```

Then: add the file to `CMakeLists.txt`, declare and call its
`*_register()` in `picoos.cpp`. `ls` shows it right away.

**A mount** is a `mount_t` (`kernel/fs.h`): a prefix and the functions
of a filesystem. `dev/fram.cpp` is the example: twelve lines that put
`fs/framfs.h` at `/fram`.

Capacities (2026-11-12): 64 devices (`FS_MAX_DEVICES`, 14 used), 48
programs (`PROG_MAX`, 20 used), 4 mounts (1 used), 4 files open at once,
20 background jobs, 32 tasks (21 used: the shell + the 20 job slots).
Every table says so loudly on the console when it's full, rather than
dropping an entry silently (a lesson picoos learned three times).

### Known limits

- **Pipes pass whole buffers, 1 KB each** (`STAGE_BUF`), not streams. That's
  also why files are at most 1 KB.
- **No quotes, no variables, no `$(...)`** in the shell. `label` covers the
  most common need ("some text: a device's value").
- **One shared poll rate** for all background jobs (150 ms).
- **`hyst` keeps one state for everyone**: two `hyst` jobs at once would
  share it (inherited from picoos, documented in `prog/hyst.cpp`).
- **One core, cooperative**: a slow device read (I2C) holds everything up
  for its duration. Core 1 is still free.

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

**2026-11-12, FRAM filesystem** (`picoos.uf2`): tested on the PC so far
(filesystem against a simulated FRAM incl. power cuts; whole shell
session with `>>`, scripts and `/fram/boot`). On the board:

| Test | Result |
|---|---|
| Blank FRAM at boot: `fram: no filesystem` hint | ok (2026-11-13): `fram: no filesystem, run: mkfs /fram` before the banner; `ls` shows the mount point `/fram/` |
| `mkfs /fram` | ok: `/fram: formatted, 7360 bytes free` |
| Format survives power off/on | ok: after power-on no more `no filesystem` hint, so the superblock was found and `/fram` mounted |
| `df` | ok: `/fram     7360 total     0 used  7360 free` |
| `>`, `>>`, `cat`, `ls -l /fram`, `rm` | ok: `hello`/`again` in `a.txt` (12 bytes); `date` and `label temp` appended to `log` (35 bytes); `rm` removed `a.txt`. `rm` printed an empty line: the shell (from picoos) printed a newline even for no output. Fixed: no output, no line, as in Unix |
| `rm` retest | ok: `echo hi > /fram/t`, `ls -l`, `cat`, `rm /fram/t`: no empty line |
| Files survive a firmware update | ok: after `echo bootsel > /dev/reboot` and flashing a new `picoos.uf2`, `/fram/log` and `/fram/boot` were still there and `boot` ran. The FRAM is separate from the flash: new firmware never touches it |
| `script` → `/fram/<name>`, `run` | ok: `script boot` saved `/fram/boot` (89 bytes, comment included) |
| Power off/on: files and `/fram/boot` survive | ok: `running /fram/boot automatically...`, all three lines ran, the LAMP toggle job started; `log` and `boot` still listed |
| New: `#` comments (`date # hi` runs `date`) | ok: typed and inside `/fram/boot` |
| Stack fix (pipe buffers static): no visible change expected, everything above should behave the same | ok: nothing changed |
| Shell fix found on the PC: lines typed or pasted after `run`/`sleep`/`watch` ran immediately, mixed into the script (inherited from picoos). Now they wait until it's done. Paste e.g. `sleep 2000` + `echo after` in one go | ok: `after` came after the sleep |

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

## Resources and future hardware

### What picoos uses now (measured 2026-11-12)

| | Used | Available | |
|---|---|---|---|
| Flash (W25Q16JV) | 85 KB (`picoos.bin`) | 2048 KB | 4 % |
| RAM | ~37 KB | 264 KB | 14 % |
| Stack, worst path | ~1 KB | 2 KB reserved | ok |
| FRAM | ~1 KB metadata | 8 KB | the real limit |
| GPIO | 11 (I2C 2, LED, lamp, 6 buttons, + ADC temp internal) | 30 | |

RAM breakdown: 13 KB is the 20 background-job slots (a whole parsed
pipeline each), ~6 KB shell and pipe buffers (2 × 1 KB pipe buffers,
1 KB script, 1 KB capture, parsed pipelines), 1.8 KB FRAM filesystem
(directory + chain cache, 1 KB append buffer), ~7.7 KB code copied to RAM by the SDK, ~3 KB USB stack, plus
2 KB heap and 2 KB stack.

**A stack finding, fixed.** Measuring with `-fstack-usage` showed the
deepest call path (shell → `dispatch` → `pipeline_run_once` → a program
→ `printf`) needed ~4 KB of stack, twice the 2 KB the SDK reserves. It
only worked because the overflow ran into the memory set aside for core
1's stack, unused so far. That would have broken the day core 1 got
work (the TFT was always the candidate). Inherited from picoos.
The 2 KB of pipe buffers (`jobs.cpp`) and the parsed pipeline
(`shell.cpp`) are `static` now; the worst path is ~1 KB.

### With babywarmer and the ST7735 TFT back

The picoos build with everything (heater, NTC/ADS1115, 7-segment,
PID/phase/alarm, TFT with its bitmaps) was ~194 KB flash and ~20 KB RAM.
Most of that was warmer- and TFT-specific, so an estimate for all of it
on this board: **~210 KB flash (~10 %), ~45 KB RAM (~17 %)**. The TFT
driver draws directly to the display, no framebuffer (a full 128×160
16-bit framebuffer would be 40 KB, and would still fit).

GPIOs are the tightest resource. babywarmer + TFT on this board:

| GPIO | Use |
|---|---|
| 0, 1 | buzzer, alarm LED |
| 2, 7, 8, 9, 28, 29 | buttons |
| 3, 4, 5 | 7-segment shift register |
| 6 | lamp |
| 11, 16, 17 | SSR, bargraph PWM, safety relay |
| 12, 13 | ADS1115 I2C (babywarmer used i2c0 here) |
| 14, 15 | RTC + FRAM I2C |
| 20–25 | TFT (LED, RST, SCK, MOSI, DC, CS) |
| 26 | current-sense ADC |
| **10, 18, 19, 27** | **free** |

26 of 30 used. The ADS1115 (address `0x48`) can share the board's I2C
bus with the FRAM (`0x50`) and RTC (`0x51`): that frees GPIO 12 and 13,
leaving 6 free.

### Recommendation

**Stay on RP2040 + 2 MB for babywarmer + TFT.** Flash and RAM won't be
the problem: ~10 % and ~17 %. CPU time won't either: babywarmer ran this
on one core with the second idle, and PID at 1 Hz in software floating
point is nothing.

**For the next PCB, in order of value:**

1. **A bigger FRAM in the same SOIC-8 footprint.** The cheapest real
   improvement. A 32 KB part with the same 2-byte addressing (e.g.
   Infineon/Cypress FM24V02A or FM24W256, check the datasheet pinout
   against the FM24CL64B) needs no driver change: set `FRAM_SIZE` to
   32768 in `board.h` and `mkfs /fram`. The filesystem format already
   scales (16-bit chain entries). Parts of 128 KB and up (FM24V10) use
   17-bit addresses, which needs a small driver change.
2. **A flash footprint that takes bigger W25Q parts.** All W25QxxJV share
   the same 8-pin pinout, but the 2×3 mm USON of the W25Q16JVUXIQ isn't
   offered in every size; a WSON-8 6×5 mm or SOIC-8 208-mil footprint
   takes 2–16 MB parts (check availability per size). RP2040 handles up
   to 16 MB; software only needs `PICO_FLASH_SIZE_BYTES`. Only worth it
   for big assets (images, fonts) or a flash filesystem.
3. **RP2350 only when you need what it adds**, not for babywarmer:
   - It is **not pin compatible** with RP2040: RP2040 is QFN-56; RP2350A
     is QFN-60 (30 GPIO) and RP2350B QFN-80 (48 GPIO). Power is different
     too: the RP2350 has an on-chip switching regulator that needs an
     external inductor. It's a new layout, not a chip swap. (The Pico 2
     module is pin compatible with the Pico module; the chips aren't.)
   - What it adds: 520 KB RAM, two Cortex-M33 at 150 MHz with a hardware
     FPU, secure boot, and on the RP2350B, 48 GPIO. RP2354A/B versions
     have 2 MB flash inside the package, so no external flash chip.
   - Early RP2350 silicon (A2 stepping) has erratum E9, about GPIO inputs
     and internal pull-downs; later steppings fix it. Buttons here use
     pull-ups, but check the stepping you buy.
   - Software moves easily: same pico-sdk (`PICO_PLATFORM=rp2350`), and
     picoos only uses plain SDK calls. Two things to check then: the ADC
     channel of the temperature sensor differs on RP2350B, and
     `bringup` gets a pin map for the new board.

   If a redesign happens anyway (more GPIO for a bigger project), RP2350B
   + a bigger FRAM in one go is the natural step.

## License

MIT — see [LICENSE](LICENSE).
