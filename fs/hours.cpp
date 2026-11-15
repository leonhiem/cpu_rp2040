#include "hours.h"
#include "board.h"
#include "drivers/fm24cl64.h"
#include <cstring>

#define VERSION 1

struct record {
    uint32_t seq;
    uint32_t minutes;
    uint32_t reserved;   // 0; room for flags later without a format change
    uint32_t crc;
};
static_assert(sizeof(record) == 16, "record must be 16 bytes");
static_assert(16 + HOURS_MAX * 2 * sizeof(record) <= SYSAREA_SIZE, "system area too small");

static hours_state_t state[HOURS_MAX];
static uint32_t value[HOURS_MAX];
static uint32_t seq[HOURS_MAX];
static int      next_copy[HOURS_MAX];   // which copy the next save goes to

static uint16_t rec_addr(int c, int copy) { return SYSAREA_ADDR + 16 + (c * 2 + copy) * 16; }

static uint32_t crc32(uint32_t crc, const uint8_t *p, int n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
    }
    return ~crc;
}

static uint32_t rec_crc(int c, const record *r)
{
    uint8_t idx = c;
    return crc32(crc32(0, &idx, 1), (const uint8_t *)r, 12);
}

static void make_header(uint8_t h[16])
{
    memset(h, 0, 16);
    memcpy(h, "sys1", 4);
    h[4] = VERSION;
    h[5] = HOURS_MAX;
    uint32_t crc = crc32(0, h, 12);
    memcpy(h + 12, &crc, 4);
}

static bool write_rec(int c, int copy, uint32_t s, uint32_t minutes)
{
    record r = { s, minutes, 0, 0 };
    r.crc = rec_crc(c, &r);
    return fram_write(rec_addr(c, copy), (const uint8_t *)&r, sizeof(r));
}

void hours_init(void)
{
    uint8_t area[SYSAREA_SIZE];
    if (!fram_read(SYSAREA_ADDR, area, sizeof(area))) {
        for (int c = 0; c < HOURS_MAX; c++) state[c] = HOURS_ERROR;
        return;
    }

    uint8_t want[16];
    make_header(want);
    bool header_ok = memcmp(area, want, 16) == 0;
    bool any_valid = false;

    for (int c = 0; c < HOURS_MAX; c++) {
        // memcpy, not a cast into area[]: the Cortex-M0+ faults on
        // unaligned 32-bit loads, and area[] has no alignment guarantee.
        record a, b;
        memcpy(&a, area + 16 + c * 32, sizeof(a));
        memcpy(&b, area + 16 + c * 32 + 16, sizeof(b));
        bool va = a.crc == rec_crc(c, &a);
        bool vb = b.crc == rec_crc(c, &b);
        any_valid |= va || vb;
        if (!va && !vb) {
            state[c] = HOURS_CORRUPT;
            value[c] = 0;
            continue;
        }
        // Highest sequence number wins: it's the newest save or set.
        bool use_a = va && (!vb || a.seq >= b.seq);
        const record &best = use_a ? a : b;
        state[c]     = HOURS_OK;
        value[c]     = best.minutes;
        seq[c]       = best.seq;
        next_copy[c] = use_a ? 1 : 0;
    }

    if (!header_ok && !any_valid) {
        // Blank (or never-used) system area: start every counter at 0.
        // Records first, header last: a cut in between leaves valid
        // records, so the next boot keeps them and just rewrites the header.
        for (int c = 0; c < HOURS_MAX; c++) {
            write_rec(c, 0, 1, 0);
            state[c] = HOURS_NEW;
            value[c] = 0;
            seq[c] = 1;
            next_copy[c] = 1;
        }
        fram_write(SYSAREA_ADDR, want, 16);
    } else if (!header_ok) {
        // No header but some valid records: either a first-time init was
        // cut short (the header is written last), or the header alone got
        // damaged. Valid counters are kept. A counter without any valid
        // record was simply never reached by that init: start it at 0 --
        // calling it CORRUPT would be a false alarm (a lost header AND a
        // corrupt counter at once would take two independent faults).
        for (int c = 0; c < HOURS_MAX; c++) {
            if (state[c] != HOURS_CORRUPT) continue;
            write_rec(c, 0, 1, 0);
            state[c] = HOURS_NEW;
            seq[c] = 1;
            next_copy[c] = 1;
        }
        fram_write(SYSAREA_ADDR, want, 16);
    }
}

hours_state_t hours_state(int c)
{
    return (c >= 0 && c < HOURS_MAX) ? state[c] : HOURS_ERROR;
}

uint32_t hours_minutes(int c)
{
    return (c >= 0 && c < HOURS_MAX) ? value[c] : 0;
}

bool hours_save(int c, uint32_t minutes)
{
    if (c < 0 || c >= HOURS_MAX) return false;
    if (state[c] != HOURS_OK && state[c] != HOURS_NEW) return false;
    if (minutes <= value[c]) return minutes == value[c];   // never backwards
    if (!write_rec(c, next_copy[c], seq[c] + 1, minutes)) return false;
    seq[c]++;
    value[c] = minutes;
    next_copy[c] ^= 1;
    return true;
}

bool hours_set(int c, uint32_t minutes)
{
    if (c < 0 || c >= HOURS_MAX || state[c] == HOURS_ERROR) return false;
    // Both copies, newest last: a cut after the first leaves that one as
    // the valid newest; the old value can't come back either way.
    int first = next_copy[c];
    if (!write_rec(c, first, seq[c] + 1, minutes)) return false;
    if (!write_rec(c, first ^ 1, seq[c] + 2, minutes)) return false;
    seq[c] += 2;
    value[c] = minutes;
    next_copy[c] = first;           // the copy written first is now the older one
    state[c] = HOURS_OK;
    return true;
}
