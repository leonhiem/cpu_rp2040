#include "framfs.h"
#include "board.h"
#include "drivers/fm24cl64.h"
#include <cstring>
#include <stdint.h>

#define BLOCK        64
#define NBLOCKS      (FRAM_SIZE / BLOCK)
#define NDIR         32
#define DIR_START    1
#define DIR_BLOCKS   (NDIR * 16 / BLOCK)
#define CHAIN_START  (DIR_START + DIR_BLOCKS)
#define CHAIN_BLOCKS ((NBLOCKS * 2 + BLOCK - 1) / BLOCK)
#define DATA_START   (CHAIN_START + CHAIN_BLOCKS)

#define FREE 0x0000      // block 0 is the superblock, never part of a chain
#define END  0xffff

#define VERSION 1

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
static void free_chain(uint16_t b)
{
    for (int steps = 0; b != END && b >= DATA_START && b < NBLOCKS && steps < NBLOCKS; steps++) {
        uint16_t next = chain[b];
        set_chain(b, FREE);
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
