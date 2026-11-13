/**
 * dev/fram.cpp — mounts fs/framfs.h at /fram
 *
 * Files on the FRAM show up as /fram/<name>: `echo hi > /fram/a.txt`,
 * `cat /fram/a.txt`, `date >> /fram/log`, `ls /fram`, `rm /fram/a.txt`.
 * A blank or foreign FRAM stays unmounted-but-reachable: everything
 * fails cleanly until `mkfs /fram` -- never formatted automatically, so
 * a glitch at boot can't wipe the files.
 */
#include "fs/framfs.h"
#include "kernel/fs.h"
#include <cstdio>

static int fram_format(void)
{
    return framfs_mkfs() ? 0 : -1;
}

static const mount_t mount_fram = {
    "/fram",
    framfs_read,
    framfs_write,
    framfs_remove,
    framfs_list,
    fram_format,
    framfs_df,
};

void fram_register(void)
{
    if (!framfs_mount())
        printf("fram: no filesystem, run: mkfs /fram\n");
    fs_mount(&mount_fram);
}
