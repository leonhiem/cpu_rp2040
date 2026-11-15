/**
 * fs/hours.h — power-fail-safe hour counters in the FRAM's system area
 *
 * Operating-hours counters, like a car's odometer: they only go up, and
 * survive power cuts at any moment, firmware updates, and `mkfs /fram`
 * (they live in the system area at the end of the FRAM, board.h, which
 * the filesystem never touches).
 *
 * Layout, SYSAREA_SIZE = 256 bytes:
 *
 *   +0x00  header: "sys1", version, number of counters, ..., crc32
 *   +0x10  counter 0, copy A  \
 *   +0x20  counter 0, copy B   } 16 bytes each:
 *   +0x30  counter 1, copy A   }   seq, minutes, reserved, crc32
 *   ...                       /
 *   +0xe0  counter 6, copy B
 *
 * Why it can't go corrupt:
 *  - Copies A and B are written alternately; each has a sequence number
 *    and a CRC32 (over the counter number + the record, so a record is
 *    never valid in the wrong slot). At startup the valid copy with the
 *    highest sequence number wins. A power cut can only tear the copy
 *    being written; the other is intact and at most one save older.
 *  - hours_save() never goes backwards. Only hours_set() (a deliberate
 *    act, through /dev/hours/ctl) can lower a counter.
 *  - FRAM doesn't wear: a save every minute is fine forever.
 *  - Both copies invalid can't come from one power cut. Such a counter is
 *    CORRUPT: saving stops and it stays visibly corrupt until set by
 *    hand -- never silently restarted at 0, which would hide what
 *    happened.
 *  - The area only counts as new (all counters start at 0) if neither
 *    the header nor any record is valid. A torn header alone is
 *    rewritten; the counters are kept.
 *  - An I2C error at startup is ERROR, not "new": nothing is written,
 *    so a bus glitch can never reset the counters.
 */
#pragma once

#include <stdint.h>

#define HOURS_MAX 7

enum hours_state_t {
    HOURS_OK,        // loaded, counting
    HOURS_NEW,       // blank system area, started at 0 this boot
    HOURS_CORRUPT,   // both copies invalid: not counting until hours_set()
    HOURS_ERROR,     // couldn't read the FRAM at startup: not counting
};

void          hours_init(void);                    // once, at boot
hours_state_t hours_state(int c);
uint32_t      hours_minutes(int c);

// Store a new value: only higher than the current one, never on a
// CORRUPT/ERROR counter. False if refused or the write failed.
bool hours_save(int c, uint32_t minutes);

// Deliberate set (or reset), also lower, also on a CORRUPT counter:
// writes both copies. Not on ERROR (the FRAM wasn't readable).
bool hours_set(int c, uint32_t minutes);
