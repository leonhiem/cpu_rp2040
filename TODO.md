# TODO

Ideas and open items for picoos on cpu_rp2040. Nothing here is decided
until it's picked; the notes are first thoughts on how each would fit,
not a design. Newest ideas first.

## Ideas (2026-11-13)

### 1a. Config files

Now that `/fram` survives power cycles and firmware updates, settings
can live in files instead of in the firmware.

- A lot of this already works without new code, because the boot script
  is a file: `/fram/boot` can hold `echo 36.5 > /dev/setpoint`, and
  `cat /fram/setpoint > /dev/setpoint` restores a value saved earlier
  with `cat /dev/setpoint > /fram/setpoint`. Worth trying that way
  first, and seeing what's actually missing.
- What's missing then is probably the *saving*: something that writes a
  value back to a file when it changes (e.g. after UP/DOWN adjusted the
  setpoint), without wearing anything out (FRAM doesn't wear, so writing
  on every change is fine).
- Formats to choose from: one value per file (`/fram/setpoint` holds
  `36.5`; Plan 9 style, works with `cat` and `>` today) or `key=value`
  lines in one file with small `get`/`set` programs. One value per file
  fits picoos best; the 32-file directory limit is the cost.
- No directories yet, so naming carries the grouping: `pid.kp`,
  `pid.ti`, ... (11 characters max).

### 1b. cron

A crontab on the FRAM, driven by the RTC: run commands at set times.

- `/fram/crontab`, classic five fields plus a command:
  `min hour day month weekday command`, with `*`, numbers, lists
  (`1,15`) and steps (`*/5`). Write it with `script crontab` (`echo`
  can't write lines with `>`, `|`, `&` in them until the shell has quotes).
- A kernel task, not a job: checks `/dev/rtc` once a second, and on
  each new minute reads the crontab and runs the matching lines through
  the same pipeline machinery as the shell. Unkillable by `kill`, like
  the TFT's flush task was.
- Solves a known limit: a logger today needs `loop` and holds the
  shell. With cron, `*/1 * * * * date >> /fram/log` runs in the
  background.
- Questions to decide: where output goes (nowhere unless redirected, as
  real cron mails it; or a `/fram/cron.log`); what to do while
  `/dev/rtc/vl` says the time can't be trusted (probably: run nothing);
  shell builtins (`sleep`, `run`) in cron lines or only programs (a
  line could be `run <script>` to get both).
- Minute changes missed while the CPU was busy (a long I2C transfer) are
  impossible at a 1 s check, but a reboot at :59 could skip a minute;
  fine for logging, worth a note.

### 2. hexdump — done 2026-11-13

Built as below: `hexdump [-s] [-n] [path]`, `fs_pread()`, read-only
`/dev/fram` and `/dev/mem`, STAGE_BUF 4 KB. See the README. Original notes:

#### (original idea)

`hexdump` in the classic `hexdump -C` layout: offset, 16 bytes in hex,
the same bytes as ASCII:

```
00000000  70 66 73 31 01 00 40 00  80 00 20 00 00 00 00 00  |pfs1..@... .....|
```

- For files and anything else, as a filter: `cat /fram/boot | hexdump`.
- For the raw FRAM and for memory, devices are needed: a read-only
  `/dev/fram` (the whole 8 KB, under the filesystem) and maybe
  `/dev/mem`. Read-only on purpose: a stray `>` must not wreck the
  filesystem.
- The catch: picoos devices have no offsets (one read = the whole
  value) and pipes carry 1 KB. A hexdump line turns 16 bytes into ~78
  characters, so one pipe holds a dump of only ~200 bytes. Options:
  `hexdump <path> <offset> <length>` reading the device itself (simple);
  or a window device, `echo 4096 > /dev/fram/offset` then
  `cat /dev/fram | hexdump`, the same seek-then-read pattern as the TFT's
  `/dev/tft/seek` (`lseek()` + `read()`). Also a reason to look at
  streaming pipes (see below).
- Nice first use: look at the superblock (`pfs1`), the directory
  entries, and a file's chain, to see the filesystem the README
  describes.

### 3. Choosing what to build in

picoos grows by many small commands; a project shouldn't have to carry
all of them.

- Plan 9 did exactly this: a kernel configuration file listing which
  devices and parts go into a kernel. picoos could have one list, e.g.
  in `CMakeLists.txt` or a `picoos.conf`, naming the devices and
  programs to build; registration in `picoos.cpp` generated from the
  same list, so adding a program stays a one-line change.
- Measure before cutting: most programs are a few hundred bytes of
  flash, which won't matter with 2 MB (see the README's resources
  section: 4 % used). The real RAM costs are elsewhere: the 20 job slots
  (13 KB), the pipe/script buffers, the FRAM filesystem's caches. Making
  those sizes configurable (`MAX_JOBS`, `STAGE_BUF`, ...) would save
  more than leaving out `grep`.
- Whole features worth a switch: the FRAM filesystem, cron, the TFT,
  `bringup`-style tests.
- Real numbers to design it around are in the README ("If space ever gets
  tight"): job slots 13 KB RAM, pipe buffers 6 KB RAM, hexdump 1.5 KB
  flash, /dev/mem + /dev/fram + fs_pread under 1 KB flash together.

### 4. Hour counter (power-fail safe)

An operating-hours counter, like a car's odometer: babywarmer needs one
(service intervals, warranty), and so will other systems. It must never
go corrupt, never go backwards, and survive anything: power cuts at any
moment, firmware updates, and `mkfs /fram`.

Why counters go corrupt elsewhere, and what that means here:

- **Wear.** Many counters live in EEPROM or flash, rated ~100k–1M writes
  per cell. Saving every minute wears out a cell within a few years; then
  the value goes bad. **The FRAM doesn't have this problem**: ~10^14
  writes, so saving every minute for thousands of years is fine. No
  wear-leveling needed.
- **Torn writes.** A power cut halfway through writing a multi-byte
  value leaves half old, half new bytes: a random number. This one
  *does* apply to FRAM (it writes byte by byte as the I2C transfer
  arrives). The classic fix:
  - Keep **two copies** (A and B), each a record of
    `{sequence number, minutes, CRC}`.
  - Write them **alternately**: A, then B, then A, ...
  - At power-up, check both CRCs and take the valid record with the
    highest sequence number.
  - A cut can only tear the copy being written; the other copy is intact
    and at most one step older. Worst case: one minute lost, never a
    corrupt or reset counter.
- **Formatting.** If the counter were a file, `mkfs /fram` would erase
  it. So it should live in a small **reserved area of the FRAM outside
  the filesystem** (e.g. the last block), which `mkfs` never touches.
  That takes one block from the filesystem: a format change, so best
  decided before there are files worth keeping. (A `/fram` file would
  otherwise be safe against power cuts: the filesystem's write order
  already guarantees old-or-new.)

Further thoughts:

- **Count with the RP2040's own timer, not the RTC.** Setting the clock,
  or a flat RTC backup, must not change operating hours.
- **Resolution:** minutes in a 32-bit number covers ~8000 years. Saved
  once a minute, a power cut loses at most the minute in progress.
- **As files:** e.g. a read-only `/dev/hours` showing `1234.5`, or
  `1234:30` (hours:minutes). Setting it (a replaced board taking over
  the old counter, or a factory reset) should be deliberate and hard to
  do by accident: a separate `/dev/hours/ctl` taking `set 1234`, in
  Plan 9 style, rather than a writable `/dev/hours`.
- **More than one counter?** Board powered on vs. heater actually
  heating are different numbers for babywarmer. A counter could be gated
  by a device ("count while `/dev/heater` > 0"), the same gate idea as
  `follow`. Possibly a few counters: `/dev/hours/power`,
  `/dev/hours/heater`.
- **Proving it:** the same PC test as the filesystem (cut power at every
  write, check the value after remount), and on the board a script or
  relay that cuts power hundreds of times, then checks the counter only
  ever went up.

## Known open items

- **babywarmer back, later**: cpu_rp2040 (73 × 54 mm, 2 × 20-pin
  headers, USB-C powered) will be the *daughter board* of the next
  babywarmer PCB. Buzzer, LEDs, 7-segment, TFT, ADC front end and power
  supply live on that carrier board, so the heater, NTC/ADS1115,
  7-segment and PID/phase/alarm code from picoos waits until it exists.
  Pins stay as on babywarmer (buttons, TFT, heater, 7-segment). The
  ADS1115 goes on GPIO12 (SDA) / GPIO13 (SCL), which the headers export;
  the RTC/FRAM bus on GPIO14/15 stays local to this board.
- **ST7735 TFT, on core 1**: the stack fix (2026-11-12) made core 1 safe
  to use. `klog`'s "no locks needed" assumption ends there (see
  `kernel/klog.h`); anything shared between cores needs a look.
- **Quotes in the shell**: `echo "a > b"`. Would let `echo` write any
  line into a file, crontabs included.
- **Streaming or larger pipes**: whole 1 KB buffers limit file size,
  `ls` output, hexdump.
- **More arguments per stage**: `JOB_MAX_ARGS` is 6 (5 arguments), so
  `echo` takes at most 5 words. Each extra argument costs
  4 stages × 24 bytes × 20 job slots ≈ 2 KB RAM; a smarter layout (one
  shared text buffer per pipeline instead of fixed 24-byte slots) would
  allow more for less.
- **`shred` / wiping `rm`**: `rm` frees blocks but leaves the bytes (see
  the README's "What `rm` does").
- **Per-job poll rate**: all background jobs run every 150 ms.
- **`hyst` shares one state** between all its users (from picoos).
- **`fsck /fram`**: reclaim blocks leaked by a power cut mid-write
  (today only `mkfs` does, by wiping everything).
- **Directories on `/fram`**: not yet, by choice.
- **Next PCB**: bigger FRAM in the same SOIC-8 footprint (32 KB, no
  driver change), a flash footprint for bigger W25Q parts; RP2350 only
  if more GPIO/RAM/FPU is needed. See the README's resources section.
