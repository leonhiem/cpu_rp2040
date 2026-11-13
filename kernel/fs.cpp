#include "fs.h"
#include <cstdio>
#include <cstring>

static const device_t *devices[FS_MAX_DEVICES];
static int device_count = 0;

void fs_register(const device_t *dev)
{
    if (device_count >= FS_MAX_DEVICES) {
        // Loud on purpose: this exact silent-return used to drop
        // /dev/setpoint with no trace at all once the table filled up.
        // main() calls every *_register() before the shell starts, and
        // stdio_init_all() has already run by then, so this printf is
        // guaranteed to actually reach the console.
        printf("fs_register: table full (%d), dropped %s\n", FS_MAX_DEVICES, dev->name);
        return;
    }
    devices[device_count++] = dev;
}

static int find(const char *path)
{
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i]->name, path) == 0) return i;
    }
    return -1;
}

/* ── mounts ── */

static const mount_t *mounts[FS_MAX_MOUNTS];
static int mount_count = 0;

// Open files get fds from FILE_FD up, so they never collide with device
// fds (which are just device-table indexes, 0..FS_MAX_DEVICES-1).
#define FILE_FD 100

static struct {
    bool used;
    const mount_t *m;
    char name[FS_NAME_MAX];
    int mode;
} files[FS_MAX_OPEN];

void fs_mount(const mount_t *m)
{
    if (mount_count >= FS_MAX_MOUNTS) {
        printf("fs_mount: table full (%d), dropped %s\n", FS_MAX_MOUNTS, m->prefix);
        return;
    }
    mounts[mount_count++] = m;
}

// "/fram" or "/fram/<rest>" -> that mount; *rest = "" or "<rest>".
static const mount_t *find_mount(const char *path, const char **rest)
{
    for (int i = 0; i < mount_count; i++) {
        int n = strlen(mounts[i]->prefix);
        if (strncmp(path, mounts[i]->prefix, n) != 0) continue;
        if (path[n] == '\0')  { *rest = path + n;     return mounts[i]; }
        if (path[n] == '/')   { *rest = path + n + 1; return mounts[i]; }
    }
    return 0;
}

static int file_slot(int fd)
{
    int s = fd - FILE_FD;
    return (s >= 0 && s < FS_MAX_OPEN && files[s].used) ? s : -1;
}

int fs_open(const char *path, int mode)
{
    int i = find(path);
    if (i >= 0) {
        if (devices[i]->open && devices[i]->open() != 0) return -1;
        return i; // the fd is just the device's table index -- no per-open
                  // state needed for these devices, and nothing here opens
                  // the same device concurrently from two places at once.
    }

    const char *name;
    const mount_t *m = find_mount(path, &name);
    if (!m || !*name || strlen(name) >= FS_NAME_MAX) return -1;
    if (mode == FS_RDWR) {
        char probe;
        if (m->read(name, &probe, 0) < 0) return -1;   // must exist
    }
    for (int s = 0; s < FS_MAX_OPEN; s++) {
        if (files[s].used) continue;
        files[s].used = true;
        files[s].m = m;
        strcpy(files[s].name, name);
        files[s].mode = mode;
        return FILE_FD + s;
    }
    printf("fs_open: too many open files (%d)\n", FS_MAX_OPEN);
    return -1;
}

void fs_close(int fd)
{
    int s = file_slot(fd);
    if (s >= 0) { files[s].used = false; return; }
    if (fd < 0 || fd >= device_count) return;
    if (devices[fd]->close) devices[fd]->close();
}

int fs_read(int fd, char *buf, int len)
{
    int s = file_slot(fd);
    if (s >= 0) return files[s].m->read(files[s].name, buf, len);
    if (fd < 0 || fd >= device_count || !devices[fd]->read) return -1;
    return devices[fd]->read(buf, len);
}

int fs_write(int fd, const char *buf, int len)
{
    int s = file_slot(fd);
    if (s >= 0) {
        // FS_RDWR and FS_CREATE replace the file on the first write; later
        // writes on the same fd append, like successive write()s in Unix.
        int n = files[s].m->write(files[s].name, buf, len, files[s].mode == FS_APPEND);
        if (n >= 0) files[s].mode = FS_APPEND;
        return n;
    }
    if (fd < 0 || fd >= device_count || !devices[fd]->write) return -1;
    return devices[fd]->write(buf, len);
}

int fs_pread(int fd, char *buf, int len, uint32_t offset)
{
    if (file_slot(fd) < 0 && fd >= 0 && fd < device_count && devices[fd]->pread)
        return devices[fd]->pread(buf, len, offset);

    static char whole[1024]; // device values are short; files are <= 1 KB
    int n = fs_read(fd, whole, sizeof(whole));
    if (n < 0) return -1;
    if (offset >= (uint32_t)n) return 0;
    int m = n - (int)offset < len ? n - (int)offset : len;
    memcpy(buf, whole + offset, m);
    return m;
}

int fs_remove(const char *path)
{
    const char *name;
    const mount_t *m = find_mount(path, &name);
    return (m && *name) ? m->remove(name) : -1;
}

int fs_format(const char *path)
{
    const char *name;
    const mount_t *m = find_mount(path, &name);
    return (m && !*name) ? m->format() : -1;
}

int fs_df(const char *path, int *free_bytes, int *total_bytes)
{
    const char *name;
    const mount_t *m = find_mount(path, &name);
    return (m && !*name) ? m->df(free_bytes, total_bytes) : -1;
}

int fs_list(const char *path, int index, char *name, int namelen, int *size)
{
    const char *rest;
    const mount_t *m = find_mount(path, &rest);
    return (m && !*rest) ? m->list(index, name, namelen, size) : -1;
}

int fs_mount_count(void)
{
    return mount_count;
}

const char *fs_mount_name(int index)
{
    return (index >= 0 && index < mount_count) ? mounts[index]->prefix : 0;
}

int fs_count(void)
{
    return device_count;
}

const char *fs_name(int index)
{
    if (index < 0 || index >= device_count) return 0;
    return devices[index]->name;
}
