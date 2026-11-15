/**
 * dev/cons.cpp — /dev/cons: the console, as a file (Plan 9's name)
 *
 *   echo hello > /dev/cons         prints "hello" on the terminal
 *   * * * * * date > /dev/cons     (in /fram/crontab) the time, every minute
 *
 * Write-only for now: text goes to the USB serial terminal, as is. The
 * way for cron lines and background jobs, whose output is otherwise
 * discarded, to say something. Text can land in the middle of what's
 * being typed at the prompt; the line being typed is not lost (the shell
 * still has it), only the screen looks mixed.
 *
 * Later (see TODO.md, the 4" display): reading /dev/cons gives the
 * keyboard, and writing it also shows on the TFT console.
 */
#include "kernel/fs.h"
#include <cstdio>

static int cons_write(const char *buf, int len)
{
    fwrite(buf, 1, len, stdout);
    fflush(stdout);
    return len;
}

static const device_t dev_cons = {"/dev/cons", 0, 0, 0, cons_write};

void cons_register(void)
{
    fs_register(&dev_cons);
}
