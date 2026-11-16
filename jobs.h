/**
 * jobs.h — pipeline execution + background job control
 *
 * A pipeline is a chain of program stages (kernel/prog.h) with optional
 * `<`/`>` device redirects. pipeline_run_once() runs it synchronously
 * once, either printing the result to the terminal or writing it to a
 * `>` device -- except backgrounded jobs with no `>` redirect, which
 * print nothing: a job like `toggle ... &` normally has nothing to say
 * ("idle") on most ticks, and printing that to the terminal every
 * JOB_POLL_MS would spam it badly. Foreground commands always print
 * (there's no other way to see the result); a background job with a
 * `>` still writes there every tick same as always.
 *
 * Backgrounding (`&`) doesn't fork -- there's no process model here,
 * just kernel/task.h's cooperative scheduler. A fixed pool of MAX_JOBS
 * slots is registered as real tasks once, at boot (jobs_init()), each
 * normally a no-op; starting a job fills a slot and it starts running
 * on its own schedule, "killing" it just flips the slot back to
 * inactive. kernel/task.h itself is untouched -- no task_unregister()
 * needed, and no way to pass a slot index through its plain function
 * pointer, so each slot still needs its own distinct trampoline
 * function. jobs.cpp generates them via an X-macro now (was hand-
 * duplicated one-by-one through MAX_JOBS=4 -- fine at 4, not at the
 * 8+ concurrent jobs the phase-machine wiring plus UI jobs actually
 * need) -- growing MAX_JOBS is a one-line list edit in jobs.cpp now,
 * not retyping N near-identical functions.
 *
 * All jobs share one fixed poll interval (JOB_POLL_MS) -- not
 * independently tunable per job yet. 150ms was chosen for button-press
 * responsiveness (toggle); still plenty slow enough for a thermostat.
 */
#pragma once

#include <stdint.h>

#define JOB_MAX_STAGES 4
#define JOB_MAX_ARGS   16 // per stage, slot 0 is the program name itself.
                          // 2026-11-16 (cpu_rp2040): 6 -> 16. Words are no
                          // longer fixed 24-byte slots but positions in the
                          // pipeline's text (below), so an extra argument
                          // costs 1 byte, not 24.
#define PIPE_TEXT_MAX  192 // all words of one pipeline, each nul-terminated,
                           // back to back. A typed line is at most 95
                           // characters; even "a|a|a|..." (a word per
                           // character) fits.
#define JOB_TOK_MAX    64 // longest path the shell keeps elsewhere (cat >
                          // target, script name); was also the per-word slot
                          // size (24) until 2026-11-16
#define MAX_JOBS       20 // must match jobs.cpp's JOB_SLOTS X-macro list, entry-for-entry.
                          // Bumped from 12 -- the boot script alone is about to
                          // reach 11 jobs, and LED wiring (the next piece) will
                          // add more; given real headroom this time, same lesson
                          // as every other capacity bump this session.
#define STAGE_BUF     4096 // 2026-11-13 (cpu_rp2040): 1024 -> 4096 for hexdump,
                            // whose 79-char lines per 16 bytes fit only ~192
                            // bytes of dump in 1 KB (~800 in 4 KB). Costs 6 KB
                            // RAM (two static buffers in jobs.cpp). Files stay
                            // at most 1 KB (fs/framfs.h), independently.
                            // ls's own listing grows with every device/program
                            // added -- 256 silently truncated it once (the bug
                            // that surfaced the whole snprintf-overcounting
                            // class of fix), then 512 hit the same wall again,
                            // cleanly this time (the fixed ls just stops rather
                            // than corrupt anything), once dev/pid.cpp's six
                            // devices pushed the real listing to 538 bytes.
                            // Sized with real headroom this time instead of
                            // matching the count exactly.
#define JOB_POLL_MS    150

// A parsed pipeline keeps the line's words once, in text[], and refers
// to them by position (offset into text[]). Until 2026-11-16 every word
// had its own fixed 24-byte slot: a quoted word longer than 23 characters
// would have been cut off, and 20 job slots x 4 stages x 6 slots cost
// 13 KB. Now a word can be as long as the line, a stage can have 16
// words, and a pipeline is ~270 bytes.
typedef struct {
    uint8_t argv[JOB_MAX_ARGS];    // offsets into the pipeline's text[]
    uint8_t argc;
} pipeline_stage_t;

typedef struct {
    char    text[PIPE_TEXT_MAX];
    uint8_t text_len;
    uint8_t nstages;
    pipeline_stage_t stages[JOB_MAX_STAGES];
    bool    has_redirect_out;
    bool    redirect_append;       // `>>` instead of `>`
    uint8_t redirect_out;          // offset into text[]
    bool    has_redirect_in;
    uint8_t redirect_in;           // offset into text[]
} pipeline_t;

static inline const char *pipeline_word(const pipeline_t *p, uint8_t off) { return p->text + off; }
static inline const char *pipeline_arg(const pipeline_t *p, int stage, int i) { return p->text + p->stages[stage].argv[i]; }

void pipeline_run_once(pipeline_t *p, bool print_if_no_redirect);

void jobs_init(void);
int  job_start(const pipeline_t *p);  // returns job id (1..MAX_JOBS), or -1 if all slots busy
void job_list(void);
bool job_kill(int id);
