#include "jobs.h"
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include <cstdio>
#include <cstring>

void pipeline_run_once(pipeline_t *p, bool print_if_no_redirect)
{
    // static, not on the stack: 2 KB here made the deepest call path
    // (shell -> dispatch -> this -> a program -> printf) ~4 KB, double the
    // 2 KB stack the SDK reserves; it only worked by spilling into core 1's
    // stack area (unused so far). Safe as static: one core, cooperative
    // tasks, and nothing calls pipeline_run_once() from inside a pipeline.
    static char buf_a[STAGE_BUF];
    static char buf_b[STAGE_BUF];
    char *cur = buf_a, *other = buf_b;
    int cur_len = 0;

    if (p->has_redirect_in) {
        const char *in_path = pipeline_word(p, p->redirect_in);
        int fd = fs_open(in_path);
        if (fd < 0) { printf("%s: not found\n", in_path); return; }
        int n = fs_read(fd, cur, STAGE_BUF);
        fs_close(fd);
        cur_len = n < 0 ? 0 : n;
    }

    for (int i = 0; i < p->nstages; i++) {
        pipeline_stage_t *st = &p->stages[i];
        if (st->argc == 0) { printf("empty pipeline stage\n"); return; }

        const char *name = pipeline_arg(p, i, 0);
        const program_t *prog = prog_find(name);
        if (!prog) { printf("%s: not found (try 'ls')\n", name); return; }

        char *argv_rest[JOB_MAX_ARGS];
        // Programs only read their arguments (none writes into them), so
        // they can point straight into the pipeline's text.
        for (int a = 1; a < st->argc; a++) argv_rest[a - 1] = (char *)pipeline_arg(p, i, a);

        int n = prog->run(cur, cur_len, st->argc - 1, argv_rest, other, STAGE_BUF);
        if (n < 0) { printf("%s: error\n", name); return; }

        char *tmp = cur; cur = other; other = tmp;
        cur_len = n;
    }

    if (p->has_redirect_out) {
        const char *out_path = pipeline_word(p, p->redirect_out);
        int fd = fs_open(out_path, p->redirect_append ? FS_APPEND : FS_CREATE);
        if (fd < 0) { printf("%s: not found\n", out_path); return; }
        int n = fs_write(fd, cur, cur_len);
        fs_close(fd);
        // Say so in the foreground (e.g. a full file, a bad value for a
        // device); background jobs stay quiet, as they always have, so a
        // job retrying every JOB_POLL_MS can't flood the terminal.
        if (n < 0 && print_if_no_redirect) printf("%s: write failed\n", out_path);
    } else if (print_if_no_redirect) {
        for (int i = 0; i < cur_len; i++) putchar(cur[i]);
        // No output, no line: `rm`, a `grep` without matches, `cat` of an
        // empty file print nothing, as in Unix. (picoos printed an empty
        // line for those.) Output without a final '\n' still gets one.
        if (cur_len > 0 && cur[cur_len - 1] != '\n') putchar('\n');
    }
}

typedef struct {
    bool active;
    pipeline_t p;
} job_slot_t;

static job_slot_t slots[MAX_JOBS];

static void run_slot(int i)
{
    if (!slots[i].active) return;
    pipeline_run_once(&slots[i].p, false); // background: no spam without a > redirect
}

// One trampoline per job slot, generated mechanically -- see jobs.h's
// header comment for why each slot still needs its own distinct
// function. JOB_SLOTS must list exactly MAX_JOBS entries, X(0)..X(N-1);
// growing MAX_JOBS means adding more X(n) tokens here, in both places
// below, nothing else.
#define JOB_SLOTS \
    X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  X(8)  X(9) \
    X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19)

#define X(n) static void job_task_##n(void) { run_slot(n); }
JOB_SLOTS
#undef X

void jobs_init(void)
{
    for (int i = 0; i < MAX_JOBS; i++) slots[i].active = false;
#define X(n) task_register("job" #n, job_task_##n, JOB_POLL_MS);
    JOB_SLOTS
#undef X
}

int job_start(const pipeline_t *p)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!slots[i].active) {
            slots[i].p = *p;
            slots[i].active = true;
            return i + 1;
        }
    }
    return -1;
}

void job_list(void)
{
    bool any = false;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!slots[i].active) continue;
        any = true;
        printf("[%d] ", i + 1);
        for (int s = 0; s < slots[i].p.nstages; s++) {
            if (s > 0) printf("| ");
            for (int a = 0; a < slots[i].p.stages[s].argc; a++) {
                // Words with spaces or shell characters were quoted when
                // typed: show them quoted, so the listing can be typed again.
                const char *w = pipeline_arg(&slots[i].p, s, a);
                bool q = !*w || strpbrk(w, " \t|<>&#'\"");
                printf(q ? (strchr(w, '"') ? "'%s' " : "\"%s\" ") : "%s ", w);
            }
        }
        if (slots[i].p.has_redirect_out)
            printf("%s %s ", slots[i].p.redirect_append ? ">>" : ">", pipeline_word(&slots[i].p, slots[i].p.redirect_out));
        printf("&\n");
    }
    if (!any) printf("no background jobs\n");
}

bool job_kill(int id)
{
    if (id < 1 || id > MAX_JOBS || !slots[id - 1].active) return false;
    slots[id - 1].active = false;
    return true;
}
