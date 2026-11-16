#include "framfs.h"
#include "board.h"
#include "drivers/fm24cl64.h"
#include <cstdio>
#include <cstring>
#include <stdint.h>

#define BLOCK        64
#define NBLOCKS      (SYSAREA_ADDR / BLOCK)   // stops short of the system area (board.h)
#define NDIR         32
#define DIR_START    1
#define DIR_BLOCKS   (NDIR * 16 / BLOCK)
#define CHAIN_START  (DIR_START + DIR_BLOCKS)
#define CHAIN_BLOCKS ((NBLOCKS * 2 + BLOCK - 1) / BLOCK)
#define DATA_START   (CHAIN_START + CHAIN_BLOCKS)

#define FREE 0x0000      // block 0 is the superblock, never part of a chain
#define END  0xffff

#define VERSION 2   // 2: ends before the system area (2026-11-15). A version-1
                    // filesystem used the last 4 blocks for data, so it must
                    // not be mounted any more: mkfs.

struct dirent {
    char     name[FRAMFS_NAME_MAX + 1];
    uint16_t size;
    uint16_t first;
};
static_assert(sizeof(dirent) == 16, "directory entry must be 16 bytes");

static bool     mounted;
static dirent   dir[NDIR];
static uint16_t chain[NBLOCKS];

static uint16_t dir_addr(int i)   { return DIR_START * BLOCK + i * sizeof(dirent); }
static uint16_t chain_addr(int b) { return CHAIN_START * BLOCK + b * 2; }

static void superblock(uint8_t sb[16])
{
    memset(sb, 0, 16);
    memcpy(sb, "pfs1", 4);
    sb[4] = VERSION;
    sb[6] = BLOCK & 0xff;   sb[7] = BLOCK >> 8;
    sb[8] = NBLOCKS & 0xff; sb[9] = NBLOCKS >> 8;
    sb[10] = NDIR;
}

bool framfs_mount(void)
{
    uint8_t want[16], got[16];
    superblock(want);
    mounted = fram_read(0, got, 16) && memcmp(want, got, 16) == 0
           && fram_read(dir_addr(0), (uint8_t *)dir, sizeof(dir))
           && fram_read(chain_addr(0), (uint8_t *)chain, sizeof(chain));
    return mounted;
}

bool framfs_mkfs(void)
{
    uint8_t sb[16];
    superblock(sb);
    memset(dir, 0, sizeof(dir));
    memset(chain, 0, sizeof(chain));
    mounted = false;
    // Superblock last: a cut mid-format leaves no valid magic, not a half-empty fs.
    if (!fram_write(dir_addr(0), (const uint8_t *)dir, sizeof(dir))
        || !fram_write(chain_addr(0), (const uint8_t *)chain, sizeof(chain))
        || !fram_write(0, sb, 16))
        return false;
    mounted = true;
    return true;
}

static bool valid_name(const char *name)
{
    int n = strlen(name);
    if (n < 1 || n > FRAMFS_NAME_MAX) return false;
    for (int i = 0; i < n; i++)
        if (name[i] <= ' ' || name[i] > '~' || name[i] == '/') return false;
    return true;
}

static int find(const char *name)
{
    for (int i = 0; i < NDIR; i++)
        if (dir[i].name[0] && strcmp(dir[i].name, name) == 0) return i;
    return -1;
}

static bool set_chain(int b, uint16_t next)
{
    chain[b] = next;
    return fram_write(chain_addr(b), (const uint8_t *)&chain[b], 2);
}

// Frees a whole chain. Bounded by NBLOCKS steps, so a corrupt chain
// (a loop) can't hang the system.
// Frees a whole chain. Bounded by NBLOCKS steps, so a corrupt chain
// (a loop) can't hang the system. Never frees a block that a file in the
// directory still reaches: in a healthy filesystem that never happens,
// but if a chain got corrupted into another file's blocks (fsck reports
// "also used by another file"), removing the damaged file must not take
// the healthy one's blocks with it. Called after the directory entry is
// gone or points elsewhere, so the freed file itself isn't counted.
static void free_chain(uint16_t b)
{
    static bool keep[NBLOCKS];
    memset(keep, 0, sizeof(keep));
    for (int i = 0; i < NDIR; i++) {
        if (!dir[i].name[0]) continue;
        uint16_t c = dir[i].first;
        for (int steps = 0; c != END && c >= DATA_START && c < NBLOCKS && steps < NBLOCKS; steps++) {
            if (keep[c]) break;
            keep[c] = true;
            c = chain[c];
        }
    }
    for (int steps = 0; b != END && b >= DATA_START && b < NBLOCKS && steps < NBLOCKS; steps++) {
        uint16_t next = chain[b];
        if (!keep[b]) set_chain(b, FREE);
        b = next;
    }
}

int framfs_read(const char *name, char *buf, int len)
{
    if (!mounted) return -1;
    int i = find(name);
    if (i < 0) return -1;
    int n = dir[i].size < len ? dir[i].size : len;
    uint16_t b = dir[i].first;
    for (int pos = 0; pos < n; pos += BLOCK) {
        if (b == END || b < DATA_START || b >= NBLOCKS) return -1; // corrupt chain
        int chunk = n - pos < BLOCK ? n - pos : BLOCK;
        if (!fram_read(b * BLOCK, (uint8_t *)buf + pos, chunk)) return -1;
        b = chain[b];
    }
    return n;
}

static int write_whole(const char *name, const char *buf, int len)
{
    int i = find(name);
    if (i < 0) {
        for (i = 0; i < NDIR && dir[i].name[0]; i++) {}
        if (i == NDIR) return -1;                       // directory full
    }

    // Pick free blocks for the new copy. The old copy stays intact until
    // the directory entry points at the new one.
    int need = (len + BLOCK - 1) / BLOCK;
    uint16_t blocks[FRAMFS_MAX_FILE / BLOCK];
    int got = 0;
    for (int b = DATA_START; b < NBLOCKS && got < need; b++)
        if (chain[b] == FREE) blocks[got++] = b;
    if (got < need) return -1;                          // no space

    for (int k = 0; k < need; k++) {
        int chunk = len - k * BLOCK < BLOCK ? len - k * BLOCK : BLOCK;
        if (!fram_write(blocks[k] * BLOCK, (const uint8_t *)buf + k * BLOCK, chunk)) return -1;
    }
    for (int k = 0; k < need; k++)
        if (!set_chain(blocks[k], k + 1 < need ? blocks[k + 1] : END)) return -1;

    uint16_t old = dir[i].name[0] ? dir[i].first : END;
    dirent e;
    memset(&e, 0, sizeof(e));
    strcpy(e.name, name);
    e.size = len;
    e.first = need ? blocks[0] : END;
    if (!fram_write(dir_addr(i), (const uint8_t *)&e, sizeof(e))) return -1;
    dir[i] = e;

    free_chain(old);
    return len;
}

int framfs_write(const char *name, const char *buf, int len, bool append)
{
    if (!mounted || !valid_name(name) || len < 0) return -1;
    if (!append) return len <= FRAMFS_MAX_FILE ? write_whole(name, buf, len) : -1;

    // Append = read the old contents, add, write the whole file again.
    // Files are at most 1 KB, so this costs ~25 ms and keeps one write path.
    static char whole[FRAMFS_MAX_FILE];
    int old = framfs_read(name, whole, sizeof(whole));
    if (old < 0) old = 0;                               // new file
    if (old + len > FRAMFS_MAX_FILE) return -1;         // file full
    memcpy(whole + old, buf, len);
    return write_whole(name, whole, old + len) < 0 ? -1 : len;
}

int framfs_remove(const char *name)
{
    if (!mounted) return -1;
    int i = find(name);
    if (i < 0) return -1;
    uint16_t first = dir[i].first;
    dirent e;
    memset(&e, 0, sizeof(e));
    if (!fram_write(dir_addr(i), (const uint8_t *)&e, sizeof(e))) return -1;
    dir[i] = e;
    free_chain(first);
    return 0;
}

int framfs_rename(const char *from, const char *to)
{
    if (!mounted || !valid_name(to)) return -1;
    int i = find(from);
    if (i < 0) return -1;
    if (strcmp(from, to) == 0) return 0;
    // Replace an existing target first: a power cut between the two steps
    // loses the old target, never the file being renamed.
    if (find(to) >= 0 && framfs_remove(to) < 0) return -1;
    dirent e = dir[i];
    memset(e.name, 0, sizeof(e.name));
    strcpy(e.name, to);
    // Only the name changes: size and first block are written back as they
    // were, in the same single 16-byte write as any directory update.
    if (!fram_write(dir_addr(i), (const uint8_t *)&e, sizeof(e))) return -1;
    dir[i] = e;
    return 0;
}

int framfs_list(int index, char *name, int namelen, int *size)
{
    if (!mounted) return -1;
    for (int i = 0; i < NDIR; i++) {
        if (!dir[i].name[0]) continue;
        if (index-- > 0) continue;
        strncpy(name, dir[i].name, namelen - 1);
        name[namelen - 1] = '\0';
        *size = dir[i].size;
        return 0;
    }
    return -1;
}

int framfs_fsck(framfs_check *r, char *out, int outlen)
{
    *r = framfs_check{};
    if (!mounted) return -1;
    int pos = 0;
    auto say = [&](const char *fmt, const char *name, int a, int b) {
        if (!out || pos >= outlen) return;
        int n = snprintf(out + pos, outlen - pos, fmt, name, a, b);
        if (n > 0 && n < outlen - pos) pos += n; else pos = outlen;
    };

    static uint8_t owner[NBLOCKS];             // 0: no file yet, else directory index + 1
    static bool bad_file[NDIR];
    memset(owner, 0, sizeof(owner));
    memset(bad_file, 0, sizeof(bad_file));
    for (int i = 0; i < NDIR; i++) {
        if (!dir[i].name[0]) continue;
        r->files++;
        char name[FRAMFS_NAME_MAX + 1];
        memcpy(name, dir[i].name, FRAMFS_NAME_MAX);
        name[FRAMFS_NAME_MAX] = '\0';
        bool bad = false;
        if (!valid_name(name)) { say("%sdamaged: directory entry %d: bad name\n", "", i, 0); bad = true; }
        for (int j = 0; j < i && !bad; j++)
            if (dir[j].name[0] && strncmp(dir[j].name, dir[i].name, FRAMFS_NAME_MAX) == 0) {
                say("damaged: %s: the name appears twice\n", name, 0, 0);
                bad = true;
            }
        if (!bad && dir[i].size > FRAMFS_MAX_FILE) { say("damaged: %s: size %d over 1 KB\n", name, dir[i].size, 0); bad = true; }
        if (!bad) {
            int need = (dir[i].size + BLOCK - 1) / BLOCK;
            uint16_t b = dir[i].first;
            if (need == 0 && b != END) { say("damaged: %s: empty but has a first block %d\n", name, b, 0); bad = true; }
            for (int k = 0; k < need && !bad; k++) {
                if (b < DATA_START || b >= NBLOCKS) { say("damaged: %s: block %d is out of range\n", name, b, 0); bad = true; break; }
                if (chain[b] == FREE)                { say("damaged: %s: block %d is marked free\n", name, b, 0); bad = true; break; }
                if (owner[b]) {
                    // Two files share a block: which one is wrong can't be
                    // told from here, so name both and mark both.
                    int o = owner[b] - 1;
                    char other[FRAMFS_NAME_MAX + 1];
                    memcpy(other, dir[o].name, FRAMFS_NAME_MAX);
                    other[FRAMFS_NAME_MAX] = '\0';
                    if (out && pos < outlen) {
                        int n = snprintf(out + pos, outlen - pos,
                                         "damaged: %s and %s share block %d (check both with cat, rm the wrong one)\n",
                                         other, name, b);
                        if (n > 0 && n < outlen - pos) pos += n; else pos = outlen;
                    }
                    bad_file[o] = true;
                    bad = true;
                    break;
                }
                owner[b] = i + 1;
                r->used++;
                if (k == need - 1 && chain[b] != END) { say("damaged: %s: chain longer than the size (%d bytes)\n", name, dir[i].size, 0); bad = true; }
                b = chain[b];
            }
        }
        if (bad) bad_file[i] = true;
    }
    for (int i = 0; i < NDIR; i++) if (bad_file[i]) r->damaged++;

    for (int b = DATA_START; b < NBLOCKS; b++) {
        if (chain[b] == FREE) r->free++;
        else if (!owner[b]) r->lost++;
    }
    // Free lost blocks only when every file is fine: with a damaged file,
    // a "lost" block may be the rest of its chain -- leave the decision.
    if (r->lost && !r->damaged) {
        for (int b = DATA_START; b < NBLOCKS; b++)
            if (chain[b] != FREE && !owner[b] && set_chain(b, FREE)) r->freed++;
        r->free += r->freed;
    }

    say("%s%d files, %d blocks in use, ", "", r->files, r->used);
    say("%s%d free", "", r->free, 0);
    if (r->lost) {
        if (r->freed) say("%s, %d lost blocks freed\n", "", r->freed, 0);
        else          say("%s, %d lost blocks NOT freed (fix the damaged files first)\n", "", r->lost, 0);
    } else {
        say("%s, no lost blocks\n", "", 0, 0);
    }
    if (r->damaged) say("%s%d damaged file(s), see above: rm what's damaged, then run fsck again\n", "", r->damaged, 0);
    else             say("%sok\n", "", 0, 0);
    return r->damaged;
}

int framfs_df(int *free_bytes, int *total_bytes)
{
    if (!mounted) return -1;
    int nfree = 0;
    for (int b = DATA_START; b < NBLOCKS; b++)
        if (chain[b] == FREE) nfree++;
    *free_bytes = nfree * BLOCK;
    *total_bytes = (NBLOCKS - DATA_START) * BLOCK;
    return 0;
}
