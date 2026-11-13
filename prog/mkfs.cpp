/**
 * prog/mkfs.cpp — mkfs <mount>: format a filesystem (every file on it is gone)
 *
 *   % mkfs /fram
 *   /fram: formatted, 7360 bytes free
 *
 * Like `format c:`. Never happens on its own: a blank FRAM at boot only
 * gets a "run: mkfs /fram" hint, so a glitch can't wipe anything.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>

static int mkfs_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen;
    if (argc < 1) return snprintf(out, outlen, "usage: mkfs /fram\n");
    int f, t;
    if (fs_format(argv[0]) < 0) return snprintf(out, outlen, "mkfs: %s: failed\n", argv[0]);
    fs_df(argv[0], &f, &t);
    return snprintf(out, outlen, "%s: formatted, %d bytes free\n", argv[0], f);
}

static const program_t prog_mkfs = {"mkfs", mkfs_run};

void mkfs_register(void)
{
    prog_register(&prog_mkfs);
}
