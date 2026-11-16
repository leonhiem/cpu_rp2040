/**
 * prog/cpmv.cpp — cp <from> <to>, mv <from> <to>
 *
 *   cp /fram/boot /fram/boot.old     copy a file
 *   cp /dev/rtc /fram/when           a device's value into a file
 *   cp /fram/boot /fram              target is a mount: same name there
 *                                    (here: refused, it's the same file)
 *   mv /fram/boot.old /fram/boot     rename (replaces an existing boot)
 *
 * cp reads <from> (a file or a device, at most 1 KB: the file size
 * limit) and writes <to>, the same as `cat <from> > <to>`, so it has the
 * filesystem's power-cut safety: <to> ends up old or new, never half.
 *
 * mv renames within one mount: on /fram only the name in the file's
 * 16-byte directory entry is rewritten (one write, no data copied). An
 * existing <to> is replaced, as Unix mv does. Devices can't be moved,
 * and neither can files between mounts: use cp, then rm.
 *
 * Both say nothing on success, as on Unix, and name the problem
 * otherwise. Two paths that are the same file are refused.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

#define COPY_MAX 1024

// If `to` is a mount point ("/fram" or "/fram/"), make it <mount>/<name
// of from>, as `cp file dir` does on Unix. Writes the result in buf.
static const char *target(const char *from, const char *to, char *buf, int len)
{
    for (int i = 0; i < fs_mount_count(); i++) {
        const char *m = fs_mount_name(i);
        int n = strlen(m);
        if (strncmp(to, m, n) == 0 && (to[n] == '\0' || (to[n] == '/' && to[n + 1] == '\0'))) {
            const char *base = strrchr(from, '/');
            snprintf(buf, len, "%s/%s", m, base ? base + 1 : from);
            return buf;
        }
    }
    return to;
}

static int cp_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen;
    if (argc < 2) return snprintf(out, outlen, "usage: cp <from> <to>\n");
    char tbuf[40];
    const char *to = target(argv[0], argv[1], tbuf, sizeof(tbuf));
    if (strcmp(argv[0], to) == 0) return snprintf(out, outlen, "cp: %s and %s are the same file\n", argv[0], to);

    static char data[COPY_MAX];
    int fd = fs_open(argv[0]);
    if (fd < 0) return snprintf(out, outlen, "cp: %s: not found\n", argv[0]);
    int n = fs_read(fd, data, sizeof(data));
    fs_close(fd);
    if (n < 0) return snprintf(out, outlen, "cp: %s: can't read\n", argv[0]);

    fd = fs_open(to, FS_CREATE);
    int w = fd >= 0 ? fs_write(fd, data, n) : -1;
    if (fd >= 0) fs_close(fd);
    if (w != n) return snprintf(out, outlen, "cp: %s: write failed (full? name over 11 chars?)\n", to);
    return 0;
}

static int mv_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen;
    if (argc < 2) return snprintf(out, outlen, "usage: mv <from> <to>\n");
    char tbuf[40];
    const char *to = target(argv[0], argv[1], tbuf, sizeof(tbuf));
    if (strcmp(argv[0], to) == 0) return snprintf(out, outlen, "mv: %s and %s are the same file\n", argv[0], to);
    if (fs_rename(argv[0], to) < 0)
        return snprintf(out, outlen,
                        "mv: %s -> %s: failed (not found? a device? another mount: cp + rm; name over 11 chars?)\n",
                        argv[0], to);
    return 0;
}

static const program_t prog_cp = {"cp", cp_run};
static const program_t prog_mv = {"mv", mv_run};

void cpmv_register(void)
{
    prog_register(&prog_cp);
    prog_register(&prog_mv);
}
