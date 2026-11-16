/**
 * dev/fram.cpp — mounts fs/framfs.h at /fram
 *
 * Files on the FRAM show up as /fram/<name>: `echo hi > /fram/a.txt`,
 * `cat /fram/a.txt`, `date >> /fram/log`, `ls /fram`, `rm /fram/a.txt`.
 * A blank or foreign FRAM stays unmounted-but-reachable: everything
 * fails cleanly until `mkfs /fram` -- never formatted automatically, so
 * a glitch at boot can't wipe the files.
 *
 * Also /dev/fram: the raw chip, all FRAM_SIZE bytes, superblock and
 * directory included -- for looking under the filesystem with hexdump:
 *
 *   hexdump /dev/fram              superblock, directory, chain table, data
 *   hexdump -s 0x40 -n 64 /dev/fram   the first four directory entries
 *
 * Read-only on purpose: a stray `>` must not be able to wreck the
 * filesystem. `cat /dev/fram` works but prints binary; use hexdump.
 */
#include "fs/framfs.h"
#include "kernel/fs.h"
#include "drivers/fm24cl64.h"
#include "board.h"
#include <cstdio>
#include <cstring>

static int fram_format(void)
{
    return framfs_mkfs() ? 0 : -1;
}

static int fram_fsck(char *report, int len)
{
    framfs_check r;
    report[0] = '\0';
    if (framfs_fsck(&r, report, len) < 0) return -1;
    return strlen(report);
}

static const mount_t mount_fram = {
    "/fram",
    framfs_read,
    framfs_write,
    framfs_remove,
    framfs_list,
    fram_format,
    framfs_df,
    framfs_rename,
    fram_fsck,
};

static int raw_pread(char *buf, int len, uint32_t offset)
{
    if (offset >= FRAM_SIZE) return 0;
    if (len > (int)(FRAM_SIZE - offset)) len = FRAM_SIZE - offset;
    return fram_read(offset, (uint8_t *)buf, len) ? len : -1;
}

static int raw_read(char *buf, int len)
{
    return raw_pread(buf, len, 0);
}

static const device_t dev_fram = {"/dev/fram", 0, 0, raw_read, 0, raw_pread};

void fram_register(void)
{
    fs_register(&dev_fram);
    if (!framfs_mount()) {
        printf("fram: no filesystem, run: mkfs /fram\n");
    } else {
        // Every boot: a quick check from the RAM cache (no FRAM reads).
        // Silent when all is well; frees blocks a power cut left behind.
        framfs_check r;
        framfs_fsck(&r, 0, 0);
        if (r.freed)   printf("fram: freed %d lost block(s), left by a power cut during a write\n", r.freed);
        if (r.damaged) printf("fram: %d damaged file(s), run: fsck /fram\n", r.damaged);
    }
    fs_mount(&mount_fram);
}
