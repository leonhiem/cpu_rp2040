/**
 * prog/fsck.cpp — fsck <mount>: check a filesystem, fix what's safe
 *
 *   % fsck /fram
 *   /fram: 4 files, 5 blocks in use, 106 free, no lost blocks
 *   ok
 *
 * Checks every file's directory entry and block chain, and finds "lost"
 * blocks: marked in use, but no file reaches them (what a power cut in
 * the middle of a write can leave behind). Lost blocks are freed, but
 * only when every file checked out fine; damaged files are reported and
 * left alone (rm them, then fsck again). See fs/framfs.h.
 *
 * Also runs by itself at every boot, silently unless it finds something.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>

static int fsck_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen;
    if (argc < 1) return snprintf(out, outlen, "usage: fsck /fram\n");
    int pos = snprintf(out, outlen, "%s: ", argv[0]);
    int n = fs_fsck(argv[0], out + pos, outlen - pos);
    if (n < 0) return snprintf(out, outlen, "fsck: %s: not a mount, or not formatted\n", argv[0]);
    return pos + n;
}

static const program_t prog_fsck = {"fsck", fsck_run};

void fsck_register(void)
{
    prog_register(&prog_fsck);
}
