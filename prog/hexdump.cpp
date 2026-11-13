/**
 * prog/hexdump.cpp — hexdump [-s offset] [-n length] [path]
 *
 * Bytes as hex and ASCII, the `hexdump -C` layout:
 *
 *   % hexdump -n 32 /dev/fram
 *   00000000  70 66 73 31 01 00 40 00  80 00 20 00 00 00 00 00  |pfs1..@... .....|
 *   00000010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
 *   00000020
 *
 *   cat /fram/boot | hexdump      piped input
 *   hexdump /fram/boot            a file or device, by path
 *   hexdump -s 0x40 -n 64 /dev/fram
 *   hexdump -s 0x20000000 -n 64 /dev/mem
 *
 * -s and -n take decimal or 0x-hex. The address column is the real
 * offset (for /dev/mem: the real address). The last line is the offset
 * where the dump ended, as in Unix.
 *
 * Identical lines in a row collapse to one "*" (Unix does the same):
 * the mostly empty FRAM fits one screen that way.
 *
 * A path is read with fs_pread(), 256 bytes at a time, so the raw
 * devices (/dev/fram, /dev/mem) can be read anywhere. Output still has
 * to fit one pipe buffer (STAGE_BUF, ~50 lines): if it doesn't, the
 * dump stops at a line boundary and the last line says how to go on:
 *
 *   -- more: hexdump -s 0x320 /dev/mem
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#define LINE_LEN 79   // "00000000  xx .. xx  xx .. xx  |................|\n"
#define HINT_LEN 64   // room kept for the "-- more:" line

// Where the bytes come from: piped input, or a path read in chunks.
static const char *src_in;
static int src_inlen;
static int src_fd;
static char chunk[256];
static uint32_t chunk_off;
static int chunk_len;

// Up to `want` bytes at `off` into line[]. Returns bytes, 0 at the end, -1 on error.
static int get(uint32_t off, uint8_t *line, int want)
{
    if (src_fd < 0) {
        if (off >= (uint32_t)src_inlen) return 0;
        int n = src_inlen - (int)off < want ? src_inlen - (int)off : want;
        memcpy(line, src_in + off, n);
        return n;
    }
    if (chunk_len <= 0 || off < chunk_off || off >= chunk_off + chunk_len) {
        chunk_len = fs_pread(src_fd, chunk, sizeof(chunk), off);
        chunk_off = off;
        if (chunk_len <= 0) return chunk_len;
    }
    int avail = chunk_off + chunk_len - off;
    int n = avail < want ? avail : want;
    memcpy(line, chunk + (off - chunk_off), n);
    return n;
}

static int format_line(char *out, uint32_t off, const uint8_t *b, int n)
{
    char *p = out;
    p += sprintf(p, "%08lx  ", (unsigned long)off);
    for (int i = 0; i < 16; i++) {
        if (i < n) p += sprintf(p, "%02x ", b[i]);
        else       p += sprintf(p, "   ");
        if (i == 7) *p++ = ' ';
    }
    *p++ = ' ';
    *p++ = '|';
    for (int i = 0; i < n; i++) *p++ = (b[i] >= 0x20 && b[i] < 0x7f) ? b[i] : '.';
    *p++ = '|';
    *p++ = '\n';
    return p - out;
}

static int hexdump_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    uint32_t start = 0;
    long count = -1;              // -1: to the end
    const char *path = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)      start = strtoul(argv[++i], 0, 0);
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) count = strtol(argv[++i], 0, 0);
        else path = argv[i];
    }

    src_in = in;
    src_inlen = inlen;
    src_fd = -1;
    chunk_len = 0;
    if (path) {
        src_fd = fs_open(path);
        if (src_fd < 0) return snprintf(out, outlen, "hexdump: %s: not found\n", path);
    }

    uint32_t end = count >= 0 ? start + (uint32_t)count : UINT32_MAX;
    uint32_t off = start;
    uint8_t line[16], prev[16];
    bool have_prev = false, starred = false, truncated = false, failed = false;
    int pos = 0;

    while (off < end) {
        int want = end - off < 16 ? end - off : 16;
        int n = get(off, line, want);
        if (n < 0) { failed = true; break; }
        if (n == 0) break;

        if (have_prev && n == 16 && memcmp(line, prev, 16) == 0) {
            if (!starred) {
                if (outlen - pos < 2 + HINT_LEN) { truncated = true; break; }
                out[pos++] = '*';
                out[pos++] = '\n';
                starred = true;
            }
            off += 16;
            continue;
        }
        if (outlen - pos < LINE_LEN + HINT_LEN) { truncated = true; break; }
        pos += format_line(out + pos, off, line, n);
        memcpy(prev, line, 16);
        have_prev = (n == 16);
        starred = false;
        off += n;
        if (n < want) break;      // short read: the end of the data
    }
    if (src_fd >= 0) fs_close(src_fd);

    int n;
    if (failed)
        n = snprintf(out + pos, outlen - pos, "hexdump: read error at 0x%lx\n", (unsigned long)off);
    else if (truncated) {
        char rest[24] = "";
        if (count >= 0) snprintf(rest, sizeof(rest), " -n %lu", (unsigned long)(end - off));
        if (path) n = snprintf(out + pos, outlen - pos, "-- more: hexdump -s 0x%lx%s %s\n",
                               (unsigned long)off, rest, path);
        else      n = snprintf(out + pos, outlen - pos, "-- more: ... | hexdump -s 0x%lx%s\n",
                               (unsigned long)off, rest);
    }
    else
        n = snprintf(out + pos, outlen - pos, "%08lx\n", (unsigned long)off);
    if (n > 0 && n < outlen - pos) pos += n;
    return pos;
}

static const program_t prog_hexdump = {"hexdump", hexdump_run};

void hexdump_register(void)
{
    prog_register(&prog_hexdump);
}
