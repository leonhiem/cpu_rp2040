/**
 * prog/ls.cpp — ls [-l] [path]: list devices, mounts, programs, or files
 *
 *   ls            every device, every mount point ("/fram/"), every program
 *   ls /fram      the files on that mount, one path per line
 *   ls -l /fram   the same with sizes in bytes
 *
 * ls is a program too, not a shell builtin -- a text-out filter with no
 * input, same shape as everything else. Devices print as their own path
 * (e.g. "/dev/lamp"); programs print with a "bin/" prefix (e.g.
 * "bin/cat") so the namespaces stay visually distinct in one listing.
 * Files are left out of plain `ls` on purpose: there can be 32 of them,
 * and the whole listing has to fit one STAGE_BUF.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

// snprintf() returns how many bytes it *would* write if the buffer were
// big enough, not how many it actually wrote -- trusting that return
// value to advance pos overcounts once the buffer runs low, so later code
// (pipeline_run_once) prints past the real content into whatever
// uninitialized memory follows it. n >= avail means truncation happened:
// stop rather than count (or include) a truncated entry.
static bool add(char *out, int outlen, int *pos, const char *fmt, const char *a, int b = 0)
{
    int avail = outlen - *pos;
    int n = snprintf(out + *pos, avail, fmt, a, b);
    if (n < 0 || n >= avail) return false;
    *pos += n;
    return true;
}

static int list_mount(const char *path, bool sizes, char *out, int outlen)
{
    char name[16];
    int size, f, t;
    int pos = 0;
    // Not a mount, or not formatted: say so, rather than an empty listing --
    // "no files" isn't the same thing.
    if (fs_df(path, &f, &t) < 0)
        return snprintf(out, outlen, "ls: %s: not a mount, or no filesystem yet (mkfs %s)\n", path, path);
    for (int i = 0; fs_list(path, i, name, sizeof(name), &size) == 0; i++) {
        char full[40];
        snprintf(full, sizeof(full), "%s/%s", path, name);
        if (!add(out, outlen, &pos, sizes ? "%-24s %d\n" : "%s\n", full, size)) break;
    }
    return pos;
}

static int ls_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;

    bool sizes = false;
    if (argc >= 1 && strcmp(argv[0], "-l") == 0) { sizes = true; argc--; argv++; }
    if (argc >= 1) {
        // Allow "ls /fram/" as well as "ls /fram".
        char path[32];
        snprintf(path, sizeof(path), "%s", argv[0]);
        int n = strlen(path);
        if (n > 1 && path[n - 1] == '/') path[n - 1] = '\0';
        return list_mount(path, sizes, out, outlen);
    }

    int pos = 0;
    for (int i = 0; i < fs_count(); i++)
        if (!add(out, outlen, &pos, "%s\n", fs_name(i))) return pos;
    for (int i = 0; i < fs_mount_count(); i++)
        if (!add(out, outlen, &pos, "%s/\n", fs_mount_name(i))) return pos;
    for (int i = 0; i < prog_count(); i++)
        if (!add(out, outlen, &pos, "bin/%s\n", prog_name(i))) return pos;
    return pos;
}

static const program_t prog_ls = {"ls", ls_run};

void ls_register(void)
{
    prog_register(&prog_ls);
}
