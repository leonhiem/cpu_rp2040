/**
 * prog/headtail.cpp — head [n], tail [n]: first / last n lines of piped input
 *
 *   % ls | head 3
 *   % ls | tail 2
 *
 * n defaults to 10, like Unix. Two programs, one file: they're mirror
 * images of each other and share the line counting.
 */
#include "kernel/prog.h"
#include <cstdlib>
#include <cstring>

static int arg_n(int argc, char **argv)
{
    int n = argc >= 1 ? atoi(argv[0]) : 10;
    return n < 0 ? 0 : n;
}

static int copy_out(const char *src, int len, char *out, int outlen)
{
    if (len > outlen) len = outlen;
    memcpy(out, src, len);
    return len;
}

static int head_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    int want = arg_n(argc, argv);
    int end = 0;
    for (int lines = 0; end < inlen && lines < want; end++)
        if (in[end] == '\n') lines++;
    return copy_out(in, end, out, outlen);
}

static int tail_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    int want = arg_n(argc, argv);
    int start = inlen;
    // Skip a final '\n' so it isn't counted as an extra, empty last line.
    int i = (inlen > 0 && in[inlen - 1] == '\n') ? inlen - 1 : inlen;
    int lines = 0;
    while (i > 0) {
        if (in[i - 1] == '\n' && ++lines == want) break;
        i--;
    }
    start = want == 0 ? inlen : i;
    return copy_out(in + start, inlen - start, out, outlen);
}

static const program_t prog_head = {"head", head_run};
static const program_t prog_tail = {"tail", tail_run};

void headtail_register(void)
{
    prog_register(&prog_head);
    prog_register(&prog_tail);
}
