/**
 * prog/df.cpp — df: space on every mount
 *
 *   % df
 *   /fram     7360 total   1216 used   6144 free
 *
 * Space comes in 64-byte blocks, so a 3-byte file uses 64.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>

static int df_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen; (void)argc; (void)argv;
    int pos = 0;
    for (int i = 0; i < fs_mount_count(); i++) {
        const char *m = fs_mount_name(i);
        int f, t, n, avail = outlen - pos;
        if (fs_df(m, &f, &t) < 0)
            n = snprintf(out + pos, avail, "%-8s no filesystem (mkfs %s)\n", m, m);
        else
            n = snprintf(out + pos, avail, "%-8s %5d total %5d used %5d free\n", m, t, t - f, f);
        if (n < 0 || n >= avail) break;
        pos += n;
    }
    return pos;
}

static const program_t prog_df = {"df", df_run};

void df_register(void)
{
    prog_register(&prog_df);
}
