/**
 * prog/rm.cpp — rm <path...>: remove files
 *
 *   % rm /fram/a.txt /fram/old.log
 *
 * Only files in a mount can go; devices are part of the firmware.
 * Removes what it can and names what it couldn't.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>

static int rm_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen;
    if (argc < 1) return -1;
    int pos = 0;
    for (int i = 0; i < argc; i++) {
        if (fs_remove(argv[i]) == 0) continue;
        int avail = outlen - pos;
        int n = snprintf(out + pos, avail, "rm: %s: no such file\n", argv[i]);
        if (n < 0 || n >= avail) break;
        pos += n;
    }
    return pos;
}

static const program_t prog_rm = {"rm", rm_run};

void rm_register(void)
{
    prog_register(&prog_rm);
}
