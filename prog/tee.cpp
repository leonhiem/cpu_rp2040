/**
 * prog/tee.cpp — tee <path>: write piped input to a device AND pass it on
 *
 *   % cat /dev/cputemp | tee /dev/somewhere | thresh 40 > /dev/led
 *
 * Lets one value feed two places in a single pipeline, where `>` can
 * only redirect the last stage's output to one device.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstring>

static int tee_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    if (argc < 1) return -1;
    int fd = fs_open(argv[0]);
    if (fd < 0) return -1;
    fs_write(fd, in, inlen);
    fs_close(fd);

    int n = inlen < outlen ? inlen : outlen;
    memcpy(out, in, n);
    return n;
}

static const program_t prog_tee = {"tee", tee_run};

void tee_register(void)
{
    prog_register(&prog_tee);
}
