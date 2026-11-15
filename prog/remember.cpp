/**
 * prog/remember.cpp — remember <device> <file>: keep a file equal to a device
 *
 * Reads the device and the file; if they differ (or the file doesn't
 * exist yet), writes the device's value to the file. Otherwise does
 * nothing. Meant to run as a background job, so a setting survives a
 * power cycle:
 *
 *   cat /fram/lamp > /dev/lamp          # in /fram/boot: restore first...
 *   remember /dev/lamp /fram/lamp &     # ...then keep saving every change
 *
 * Restore FIRST: started the other way round, remember would save the
 * device's power-up default over the stored value before it's restored.
 * (The very first time, when the file doesn't exist yet, the restore
 * line fails harmlessly and remember creates the file.)
 *
 * Every change is saved, whatever made it: a button through
 * toggle/adjust, an `echo ... >`, a script. Run once in the foreground
 * it saves (or reports "unchanged") and stops.
 *
 * Stateless on purpose: it compares with the file, not with a value it
 * keeps, so any number of remember jobs never share state (the trap
 * prog/hyst.cpp has). Cost per tick: one device read and a few bytes
 * read from the FRAM. FRAM doesn't wear, so writing on every change is
 * fine; a power cut mid-write leaves the old or the new value (the
 * filesystem's write order), never half of one.
 *
 * NOT for devices whose read changes them: /dev/buttons and
 * /dev/buttons/<name> are read-and-clear, so remember would swallow
 * button presses. Values that change all the time (/dev/rtc,
 * /dev/uptime) work, but write the file every second: pointless.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

#define VALUE_MAX 128

static int read_path(const char *path, char *buf, int len)
{
    int fd = fs_open(path);
    if (fd < 0) return -1;
    int n = fs_read(fd, buf, len);
    fs_close(fd);
    return n;
}

static int remember_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen;
    if (argc < 2) return snprintf(out, outlen, "usage: remember <device> <file>\n");

    char dev[VALUE_MAX], file[VALUE_MAX];
    int dn = read_path(argv[0], dev, sizeof(dev));
    if (dn <= 0) return -1;                          // no such device, or nothing to save

    int fn = read_path(argv[1], file, sizeof(file)); // -1: file doesn't exist yet
    if (fn == dn && memcmp(dev, file, dn) == 0)
        return snprintf(out, outlen, "unchanged\n");

    int fd = fs_open(argv[1], FS_CREATE);
    if (fd < 0) return -1;
    int wn = fs_write(fd, dev, dn);
    fs_close(fd);
    if (wn != dn) return -1;                         // FRAM full, no filesystem, ...
    return snprintf(out, outlen, "saved %s\n", argv[1]);
}

static const program_t prog_remember = {"remember", remember_run};

void remember_register(void)
{
    prog_register(&prog_remember);
}
