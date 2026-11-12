/**
 * prog/wc.cpp — wc [-l]: count lines, words, bytes of piped input
 *
 *   % ls | wc
 *   25 25 312
 *   % ls | wc -l
 *   25
 */
#include "kernel/prog.h"
#include <cstdio>
#include <cstring>

static int wc_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    int lines = 0, words = 0;
    bool inword = false;
    for (int i = 0; i < inlen; i++) {
        char c = in[i];
        if (c == '\n') lines++;
        bool space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (!space && !inword) words++;
        inword = !space;
    }
    if (argc >= 1 && strcmp(argv[0], "-l") == 0)
        return snprintf(out, outlen, "%d\n", lines);
    return snprintf(out, outlen, "%d %d %d\n", lines, words, inlen);
}

static const program_t prog_wc = {"wc", wc_run};

void wc_register(void)
{
    prog_register(&prog_wc);
}
