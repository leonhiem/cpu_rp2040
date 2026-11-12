/**
 * prog/grep.cpp — grep [-v] <text>: keep lines of piped input containing <text>
 *
 *   % ls | grep rtc
 *   /dev/rtc
 *   /dev/rtc/vl
 *   % ls | grep -v /dev
 *   bin/cat
 *   ...
 *
 * Plain substring match, no regular expressions -- small on purpose.
 * -v inverts: keep the lines that do NOT contain <text>.
 */
#include "kernel/prog.h"
#include <cstring>

// Is needle somewhere in line[0..len)? (line isn't nul-terminated)
static bool contains(const char *line, int len, const char *needle)
{
    int nlen = strlen(needle);
    for (int i = 0; i + nlen <= len; i++)
        if (memcmp(line + i, needle, nlen) == 0) return true;
    return false;
}

static int grep_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    bool invert = false;
    if (argc >= 1 && strcmp(argv[0], "-v") == 0) { invert = true; argc--; argv++; }
    if (argc < 1) return -1;

    int pos = 0;
    int start = 0;
    while (start < inlen) {
        int end = start;
        while (end < inlen && in[end] != '\n') end++;
        int len = end - start;            // without the '\n'
        if (contains(in + start, len, argv[0]) != invert) {
            if (pos + len + 1 > outlen) break;
            memcpy(out + pos, in + start, len);
            pos += len;
            out[pos++] = '\n';
        }
        start = end + 1;
    }
    return pos;
}

static const program_t prog_grep = {"grep", grep_run};

void grep_register(void)
{
    prog_register(&prog_grep);
}
