/**
 * kernel/fs.h — everything is a file
 *
 * A minimal Plan 9-flavored device namespace: a flat table of named
 * devices, each speaking open()/close()/read()/write() over plain text.
 * No ioctl(). Reading a device produces a fresh line of text on every
 * call (there's no seek/stream position — doesn't make sense for a live
 * sensor); writing sends it a text command it parses itself.
 *
 * Deliberately global/flat rather than per-task namespaces (Plan 9's
 * mount/bind machinery) — that's solving a multi-process isolation
 * problem this single-address-space microcontroller doesn't have yet.
 * A real stretch goal if picoos ever grows actual isolated processes,
 * not something to build speculatively now.
 */
#pragma once

#define FS_MAX_DEVICES 64 // 2026-08-16: bumped from 16 after it silently
                          // dropped /dev/setpoint -- registered 17th, past
                          // the old cap, with no warning at all. See fs.cpp.
                          // 2026-08-19: bumped from 32 -- dev/pid.cpp's six
                          // devices landed exactly on that cap (32/32 used,
                          // zero headroom), caught only by grepping the
                          // registration count before it became the same
                          // bug again.
                          // 2026-08-23: bumped from 48 -- /dev/tft/*'s seven
                          // devices would have landed at 47/48, one spare;
                          // given real headroom this time instead of just
                          // enough, same lesson as every prior bump here.
#define FS_NAME_MAX    32

typedef struct {
    const char *name;                          // e.g. "/dev/lamp"
    int  (*open)(void);                        // most devices: no-op, return 0
    void (*close)(void);                       // most devices: no-op
    int  (*read)(char *buf, int len);           // returns bytes written to buf, or -1
    int  (*write)(const char *buf, int len);    // returns bytes consumed, or -1
} device_t;

// Register a device. Call before fs_open() can find it (typically from
// main(), once per device, at startup).
void fs_register(const device_t *dev);

/* ── mounts (cpu_rp2040) ────────────────────────────────────────────
 * A device is one fixed file. A mount is a whole set of files that come
 * and go at runtime -- the FRAM's /fram/<name> -- served by one
 * filesystem below a path prefix. fs_open("/fram/a.txt") finds the
 * mount for "/fram" and hands it "a.txt". Programs can't tell the
 * difference: cat, tee, `>` all work on files exactly as on devices.
 * The Plan 9 idea of mounting a file server into the namespace, without
 * per-process namespaces (still one global namespace).
 *
 * Files keep the device model: one read returns the whole file, a write
 * replaces it -- or appends, for an fd opened with FS_APPEND (`>>`).
 */
typedef struct {
    const char *prefix;                                          // "/fram", no trailing '/'
    int (*read)(const char *name, char *buf, int len);           // bytes, or -1: no such file
    int (*write)(const char *name, const char *buf, int len, bool append);
    int (*remove)(const char *name);
    int (*list)(int index, char *name, int namelen, int *size);  // -1 past the end
    int (*format)(void);
    int (*df)(int *free_bytes, int *total_bytes);
} mount_t;

#define FS_MAX_MOUNTS 4
#define FS_MAX_OPEN   4   // open files at once (devices don't use these)

void fs_mount(const mount_t *m);

// Open modes. Devices ignore them; they only matter for files.
#define FS_RDWR   0   // existing file only (the default: what cat, thresh, ... use)
#define FS_CREATE 1   // `>`:  create if missing, first write replaces the contents
#define FS_APPEND 2   // `>>`: create if missing, every write appends

// Look up a device by exact path, or a file in a mount, and open it.
// Returns a small fd (>= 0) on success, -1 if no such device/file is
// there or its open() fails.
int fs_open(const char *path, int mode = FS_RDWR);

// Whole-mount operations, Unix syscall-style: path is a file
// ("/fram/a.txt") for fs_remove, the mount point ("/fram") for the rest.
int fs_remove(const char *path);
int fs_format(const char *path);
int fs_df(const char *path, int *free_bytes, int *total_bytes);
int fs_list(const char *path, int index, char *name, int namelen, int *size);
int fs_mount_count(void);
const char *fs_mount_name(int index);

void fs_close(int fd);

// Both return the number of bytes produced/consumed, or -1 on a bad fd.
int fs_read(int fd, char *buf, int len);
int fs_write(int fd, const char *buf, int len);

// For `ls`: iterate registered devices by index, 0..fs_count()-1.
// Returns the device's path, or NULL past the end.
int fs_count(void);
const char *fs_name(int index);
