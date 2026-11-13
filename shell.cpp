/**
 * shell.cpp — task_shell: a tiny Plan 9/rc-flavored interactive shell
 *
 * Polls stdio non-blocking (getchar_timeout_us(0)), so it fits the
 * cooperative model with no scheduler changes. Echoes what's typed and
 * dispatches complete lines. Talks to stdio directly rather than
 * through klog -- interactive echo needs to be immediate, not batched
 * behind a flush task.
 *
 * Grammar: stage [| stage ...] [< path] [> path] [&]
 *   stage := progname [arg...]
 * `jobs`, `kill %<id>`, `sleep <ms>`, and `script`/`run`/`scripts` (see
 * below) are true shell builtins (they touch the shell's own state,
 * not a text-in/text-out stream); everything else -- including ls now
 * -- is a program (kernel/prog.h) run through the same pipeline
 * machinery (jobs.h), foreground or backgrounded.
 *
 * `sleep` doesn't block -- nothing in this kernel ever does. It just
 * remembers a wake time; task_shell() checks it at the top of every
 * one of its own 30ms ticks and returns immediately (a few
 * microseconds) if not yet time, same as any other task "waiting"
 * for its next scheduled run. Everything else (jobs, tpo, setpoint
 * polling) keeps running completely normally in the meantime -- this
 * is the existing cooperative model, not a new kernel primitive.
 * Characters typed during a sleep aren't lost (or echoed) until it
 * ends -- they just sit in the USB stack's own RX buffer until
 * getchar_timeout_us() starts being called again.
 *
 * `script <name>` / `run <name>` -- named, storable sequences of shell
 * lines, the small step toward real scripts. `script <name>` puts the
 * shell into capture mode: the prompt changes to `> ` and every line
 * typed gets appended to that script instead of being run, until a
 * line containing just `.` ends it (an old ed-style terminator --
 * chosen over inventing `>>`/heredoc grammar). `scripts` lists what's
 * stored.
 *
 * `run <name>` replays the stored lines through the same dispatch()
 * every typed line goes through -- but one line per task_shell() tick,
 * not all at once, and it is genuinely driven by the same resumable
 * `sleeping` flag `sleep` already uses: if a script line is itself
 * `sleep <ms>`, run_step() (below) simply doesn't get called again
 * until that sleep's wake time has passed, same as if you'd typed it
 * by hand. This isn't optional plumbing -- a first version dispatched
 * every line in one synchronous loop, which meant `sleep` inside a
 * script did nothing at all (it only ever sets a flag; something else
 * has to actually wait on it), so a heater or lamp step "after" a
 * sleep line fired immediately instead of after the delay.
 *
 * Scripts are files now (cpu_rp2040): `script <name>` saves to
 * /fram/<name> on the FRAM, so it survives a power cycle -- the thing
 * picoos never had. `run`/`loop <name>` look for /fram/<name> first,
 * then the built-in table (`scripts[SCRIPT_MAX]`, RAM, compiled in);
 * a name with a '/' in it is used as a path as-is. Being plain files,
 * scripts can also be written or extended without `script` at all:
 * `echo date >> /fram/boot`. If the FRAM isn't formatted (`mkfs /fram`),
 * `script` falls back to the RAM table, as in picoos.
 *
 * `watch <ms> <stage> [| stage ...] [< path]` -- repeats a pipeline in
 * the foreground every <ms>, printing every time, until Ctrl-C (ASCII
 * ETX, 0x03). This is real Unix's own `watch`, not a new idea: for
 * something like a monitor line you actually want to watch and then
 * stop watching, backgrounding it with `&` is the wrong tool -- a
 * background job stays quiet (no `>` redirect means no spam, on
 * purpose, for things like `toggle &`), which is exactly backwards for
 * a monitor that's supposed to print every tick, and the only way to
 * stop a background job is `kill %<id>`, remembering an id, instead of
 * the muscle-memory Ctrl-C this exists to give back. `&` inside a
 * watched pipeline is rejected -- watch already *is* the repeat.
 *
 * Ctrl-C is scoped to `watch` alone, not generalized to interrupt
 * `sleep`/`run` too: while watching, every byte is actively polled and
 * discarded except 0x03, which is a real, deliberate behavior change
 * from the normal loop (nothing is preserved for after, the same way
 * real `watch` doesn't queue up your keystrokes) -- fine to introduce
 * for a state that starts brand new in this commit, not something to
 * retrofit onto sleep/run's existing, already-relied-on "nothing
 * typed is ever lost" contract.
 *
 * `loop <name>` -- like `run`, but when the script reaches its end it
 * restarts from the top automatically instead of stopping, forever
 * until Ctrl-C (ASCII ETX, 0x03) -- same convention `watch` already
 * uses, reused rather than reinvented. Exists because this shell has
 * no while/for construct, so a script wanting to redraw something
 * "every second, forever" (e.g. a TFT text-mode status screen) has no
 * other way to repeat itself; a script can't just `run` itself at its
 * own end either, since `run`'s "already running" guard (see below)
 * refuses while `running` is still true, which it is for the entire
 * body of the script currently executing. `loop` reuses that exact
 * `running`/`run_buf`/`run_pos` machinery -- it's still one line per
 * tick, still resumable through `sleep` mid-script -- only adding the
 * restart-instead-of-stop behavior and the Ctrl-C poll needed to break
 * out of it. That poll has to happen from inside the `running` block
 * itself (unlike `watching`'s own, which lives in its own top-level
 * branch): a plain `run` never reads stdin while replaying (nothing
 * typed mid-script is lost, it just waits, per the header comment
 * above), so without this a `loop` would have no way to ever be
 * stopped short of a reset. One consequence worth knowing: Ctrl-C
 * isn't seen until the current script line (or `sleep`) finishes, same
 * granularity tradeoff `watch` already has, and fine for the same
 * reason -- sub-second in practice, not an indefinite hang.
 *
 * A "boot" script comes pre-loaded into scripts[0] -- baked into the
 * firmware image itself (BOOT_SCRIPT_TEXT below), so it's back after
 * every reboot. task_shell() auto-runs "boot" on its very first tick,
 * before the prompt ever appears -- /fram/boot if it exists, which
 * overrides the built-in one, so the board's power-up behavior is a
 * file you edit, not firmware you rebuild. On babywarmer the boot
 * script started the whole heater wiring; the built-in one here is
 * deliberately tiny.
 *
 * `help` -- lists the builtins and the grammar. Builtins don't show up
 * in `ls` (only devices and programs do), so this is where to find them.
 */
#include "kernel/fs.h"
#include "jobs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define LINE_MAX    96
#define MAX_TOKENS  20

#define SCRIPT_MAX       4
#define SCRIPT_NAME_MAX  16
#define SCRIPT_TEXT_MAX  1024 // a handful of lines' worth; independent of
                               // jobs.h's STAGE_BUF -- different concern
                               // (script storage, not ls's output). Bumped
                               // from 512 -- BOOT_SCRIPT_TEXT below alone
                               // is ~480 bytes, too close to that cap to
                               // leave any real room to grow it further.

typedef struct {
    bool used;
    char name[SCRIPT_NAME_MAX];
    char text[SCRIPT_TEXT_MAX]; // lines joined by '\n', nul-terminated
} script_t;

// Runs at every power-up (see the header comment). Small on purpose:
// say what time it is and wire the LAMP button to the lamp output, as on
// babywarmer -- a two-second check that the RTC, the buttons, background
// jobs and a device write all work.
#define BOOT_SCRIPT_TEXT \
    "date\n" \
    "toggle /dev/buttons/lamp /dev/lamp &\n"

static script_t scripts[SCRIPT_MAX] = {
    { true, "boot", BOOT_SCRIPT_TEXT },
};

static bool sleeping = false;
static absolute_time_t wake_time;

static bool capturing = false;
static char capture_name[SCRIPT_NAME_MAX];
static char capture_buf[SCRIPT_TEXT_MAX];
static int  capture_len = 0;

static bool running = false;      // a script is being replayed, one line per tick
static char run_buf[SCRIPT_TEXT_MAX];
static int  run_pos = 0;          // offset of the next unread line in run_buf
static bool looping = false;      // `loop` instead of `run`: restart run_buf from
                                   // the top when it ends, instead of stopping --
                                   // see the header comment's `loop` entry
static char run_name[JOB_TOK_MAX]; // what run_buf was last loaded from -- needed
                                   // to reload it on a loop restart: run_step()
                                   // turns each '\n' into '\0' as it goes, so
                                   // replaying the same bytes a second time
                                   // without reloading them reads back only the
                                   // first line. (picoos also had strtok() shred
                                   // run_buf; tokenize() works on its own copy now.)

static bool watching = false;
static pipeline_t watch_pipeline;
static uint32_t watch_interval_ms;
static absolute_time_t watch_next;

static void print_help(void)
{
    printf("grammar:  stage [| stage ...] [< path] [> path] [&]\n"
           "          stage := program [arg...]\n"
           "\n"
           "ls                   list devices (/dev/...) and programs (bin/...)\n"
           "help                 this text\n"
           "jobs                 list background jobs\n"
           "kill %%<id>           stop a background job\n"
           "sleep <ms>           pause the shell, everything else keeps running\n"
           "script <name>        record lines into /fram/<name>, end with '.'\n"
           "run <name>           replay a script (/fram/<name>, else built-in)\n"
           "loop <name>          replay a script forever, Ctrl-C to stop\n"
           "scripts              list built-in scripts (files: ls /fram)\n"
           "watch <ms> <stage..> repeat a pipeline every <ms>, Ctrl-C to stop\n");
}

static void print_prompt(void)
{
    printf(capturing ? "> " : "%% ");
}

// '#' starts a comment. |, <, >, >> and & are words of their own, with or without spaces around
// them -- `ls|grep dev` means the same as `ls | grep dev`, as in any
// Unix shell. picoos only split on whitespace, so `ls|grep` came out as
// one unknown program name. Done by copying the line with spaces put
// around those characters, then splitting on whitespace as before; the
// tokens point into this static copy, valid until the next tokenize().
// dispatch() never nests, and parse_pipeline() copies what it keeps.
static int tokenize(char *line, char *tokens[MAX_TOKENS])
{
    static char spaced[LINE_MAX * 3];
    int len = 0;
    for (const char *c = line; *c && len < (int)sizeof(spaced) - 4; c++) {
        // A word starting with '#' is a comment: the rest of the line is
        // ignored, as in sh/rc. Mid-word '#' (e.g. "a#b") is just a char.
        if (*c == '#' && (c == line || c[-1] == ' ' || c[-1] == '\t')) break;
        if (c[0] == '>' && c[1] == '>') {   // `>>` is one word, not two `>`
            spaced[len++] = ' ';
            spaced[len++] = '>';
            spaced[len++] = '>';
            spaced[len++] = ' ';
            c++;
        } else if (strchr("|<>&", *c)) {
            spaced[len++] = ' ';
            spaced[len++] = *c;
            spaced[len++] = ' ';
        } else {
            spaced[len++] = *c;
        }
    }
    spaced[len] = '\0';

    int n = 0;
    char *tok = strtok(spaced, " \t");
    while (tok && n < MAX_TOKENS) {
        tokens[n++] = tok;
        tok = strtok(0, " \t");
    }
    return n;
}

static bool parse_pipeline(char **tok, int ntok, pipeline_t *p, bool *background)
{
    p->nstages = 1;
    p->stages[0].argc = 0;
    p->has_redirect_out = false;
    p->redirect_append = false;
    p->has_redirect_in = false;
    *background = false;

    pipeline_stage_t *stage = &p->stages[0];

    for (int i = 0; i < ntok; i++) {
        const char *t = tok[i];
        if (strcmp(t, "|") == 0) {
            if (p->nstages >= JOB_MAX_STAGES) { printf("too many pipeline stages\n"); return false; }
            stage = &p->stages[p->nstages++];
            stage->argc = 0;
        } else if (strcmp(t, ">") == 0 || strcmp(t, ">>") == 0) {
            p->redirect_append = (t[1] == '>');
            if (++i >= ntok) { printf("%s needs a path\n", t); return false; }
            strncpy(p->redirect_out, tok[i], JOB_TOK_MAX - 1);
            p->redirect_out[JOB_TOK_MAX - 1] = '\0';
            p->has_redirect_out = true;
        } else if (strcmp(t, "<") == 0) {
            if (++i >= ntok) { printf("< needs a path\n"); return false; }
            strncpy(p->redirect_in, tok[i], JOB_TOK_MAX - 1);
            p->redirect_in[JOB_TOK_MAX - 1] = '\0';
            p->has_redirect_in = true;
        } else if (strcmp(t, "&") == 0) {
            *background = true;
        } else {
            if (stage->argc >= JOB_MAX_ARGS) { printf("too many args in one stage\n"); return false; }
            strncpy(stage->argv[stage->argc], t, JOB_TOK_MAX - 1);
            stage->argv[stage->argc][JOB_TOK_MAX - 1] = '\0';
            stage->argc++;
        }
    }
    return true;
}

// Finds a used script slot by name, or -1. Shared by capture_line()'s
// "am I redefining an existing script" check, the `run` builtin, and
// task_shell()'s own boot-time auto-run lookup.
static int find_script(const char *name)
{
    for (int i = 0; i < SCRIPT_MAX; i++) {
        if (scripts[i].used && strcmp(scripts[i].name, name) == 0) return i;
    }
    return -1;
}

// Loads script <name> into run_buf: a path as-is if it contains '/',
// else /fram/<name>, else the built-in table. Remembers the name in
// run_name, for loop restarts. False if it's nowhere.
static bool load_script(const char *name)
{
    char path[JOB_TOK_MAX];
    if (strchr(name, '/')) snprintf(path, sizeof(path), "%s", name);
    else                   snprintf(path, sizeof(path), "/fram/%s", name);

    int n = -1;
    int fd = fs_open(path);
    if (fd >= 0) {
        n = fs_read(fd, run_buf, SCRIPT_TEXT_MAX - 1);
        fs_close(fd);
    }
    if (n >= 0) {
        run_buf[n] = '\0';
    } else {
        int slot = find_script(name);
        if (slot < 0) return false;
        strncpy(run_buf, scripts[slot].text, SCRIPT_TEXT_MAX - 1);
        run_buf[SCRIPT_TEXT_MAX - 1] = '\0';
    }
    snprintf(run_name, sizeof(run_name), "%s", name);
    run_pos = 0;
    return true;
}

// Saves a captured script as /fram/<name>. False if there's no
// filesystem (or it's full), so the caller can fall back to RAM.
static bool save_script_file(const char *name, const char *text, int len)
{
    char path[JOB_TOK_MAX];
    snprintf(path, sizeof(path), "/fram/%s", name);
    int fd = fs_open(path, FS_CREATE);
    if (fd < 0) return false;
    int n = fs_write(fd, text, len);
    fs_close(fd);
    return n == len;
}

// Appends one typed line to the script currently being captured, or
// finalizes it on a lone ".". Not called while a script is running --
// run replays through dispatch(), never through capture.
static void capture_line(const char *line)
{
    if (strcmp(line, ".") == 0) {
        capture_buf[capture_len] = '\0';
        capturing = false;
        if (save_script_file(capture_name, capture_buf, capture_len)) {
            printf("saved /fram/%s (%d bytes)\n", capture_name, capture_len);
            return;
        }
        printf("could not save /fram/%s (no filesystem? full? name over 11 chars?), keeping it in RAM only\n", capture_name);
        int slot = find_script(capture_name);
        if (slot < 0) {
            for (int i = 0; i < SCRIPT_MAX; i++) {
                if (!scripts[i].used) { slot = i; break; }
            }
        }
        if (slot < 0) {
            printf("script: table full (max %d), '%s' not saved\n", SCRIPT_MAX, capture_name);
        } else {
            strncpy(scripts[slot].name, capture_name, SCRIPT_NAME_MAX - 1);
            scripts[slot].name[SCRIPT_NAME_MAX - 1] = '\0';
            strncpy(scripts[slot].text, capture_buf, SCRIPT_TEXT_MAX - 1);
            scripts[slot].text[SCRIPT_TEXT_MAX - 1] = '\0';
            scripts[slot].used = true;
            printf("script '%s' saved in RAM (%d bytes)\n", capture_name, capture_len);
        }
        return;
    }

    int llen = strlen(line);
    if (capture_len + llen + 1 >= SCRIPT_TEXT_MAX) { // +1 for the '\n' this line adds
        printf("script: buffer full, line dropped\n");
        return;
    }
    memcpy(capture_buf + capture_len, line, llen);
    capture_len += llen;
    capture_buf[capture_len++] = '\n';
}

static void dispatch(char *line)
{
    char *tok[MAX_TOKENS];
    int ntok = tokenize(line, tok);
    if (ntok == 0) return; // empty line

    if (strcmp(tok[0], "help") == 0) {
        print_help();
        return;
    }
    if (strcmp(tok[0], "jobs") == 0) {
        job_list();
        return;
    }
    if (strcmp(tok[0], "kill") == 0) {
        if (ntok < 2) { printf("usage: kill %%<jobid>\n"); return; }
        const char *arg = tok[1];
        if (*arg == '%') arg++;
        if (!job_kill(atoi(arg))) printf("kill: no such job\n");
        return;
    }
    if (strcmp(tok[0], "sleep") == 0) {
        if (ntok < 2) { printf("usage: sleep <ms>\n"); return; }
        int ms = atoi(tok[1]);
        if (ms < 0) ms = 0;
        wake_time = make_timeout_time_ms(ms);
        sleeping = true;
        return; // no prompt yet -- task_shell() prints one once awake
    }
    if (strcmp(tok[0], "script") == 0) {
        if (ntok < 2) { printf("usage: script <name>\n"); return; }
        strncpy(capture_name, tok[1], SCRIPT_NAME_MAX - 1);
        capture_name[SCRIPT_NAME_MAX - 1] = '\0';
        capture_len = 0;
        capturing = true;
        printf("capturing '%s' -- end with a line containing just '.'\n", capture_name);
        return; // prompt becomes "> " until capture ends
    }
    if (strcmp(tok[0], "scripts") == 0) {
        // Built-in/RAM ones only: scripts on the FRAM are plain files,
        // listed by `ls /fram` along with everything else stored there.
        for (int i = 0; i < SCRIPT_MAX; i++)
            if (scripts[i].used) printf("%s (built-in/RAM)\n", scripts[i].name);
        printf("files: ls /fram\n");
        return;
    }
    if (strcmp(tok[0], "run") == 0) {
        if (ntok < 2) { printf("usage: run <name>\n"); return; }
        if (running) { printf("run: already running a script\n"); return; }
        if (!load_script(tok[1])) { printf("run: no such script '%s'\n", tok[1]); return; }
        running = true;
        return; // task_shell() drives it from here, one line per tick -- see run_step()
    }
    if (strcmp(tok[0], "loop") == 0) {
        if (ntok < 2) { printf("usage: loop <name>\n"); return; }
        if (running) { printf("loop: already running a script\n"); return; }
        if (!load_script(tok[1])) { printf("loop: no such script '%s'\n", tok[1]); return; }
        running = true;
        looping = true;
        printf("looping '%s' -- Ctrl-C to stop\n", tok[1]);
        return; // task_shell() drives it from here -- see run_step() and the
                 // `if (running)` block's own looping/Ctrl-C handling
    }
    if (strcmp(tok[0], "watch") == 0) {
        if (ntok < 3) { printf("usage: watch <ms> <stage> [| stage ...] [< path]\n"); return; }
        int ms = atoi(tok[1]);
        if (ms < 50) ms = 50; // sane floor -- also protects against a typo like "watch heater"
        bool background;
        if (!parse_pipeline(tok + 2, ntok - 2, &watch_pipeline, &background)) return;
        if (watch_pipeline.stages[0].argc == 0) return;
        if (background) printf("watch: '&' ignored -- watch already repeats on its own\n");
        watch_interval_ms = (uint32_t)ms;
        watch_next = get_absolute_time(); // fire the first line immediately
        watching = true;
        printf("watching every %dms -- Ctrl-C to stop\n", ms);
        return; // no normal prompt -- watch prints its own lines until stopped
    }
    if (tok[0][0] == '\0') return;
    if (strcmp(tok[0], "|") == 0 || strcmp(tok[0], ">") == 0 || strcmp(tok[0], ">>") == 0) {
        printf("empty pipeline\n");
        return;
    }

    static pipeline_t p;   // ~650 bytes: static for the same reason as jobs.cpp's buffers
    bool background;
    if (!parse_pipeline(tok, ntok, &p, &background)) return;
    if (p.stages[0].argc == 0) return; // e.g. line was just "&"

    if (background) {
        int id = job_start(&p);
        if (id < 0) printf("no free job slots (max %d)\n", MAX_JOBS);
        else printf("[%d]\n", id);
    } else {
        pipeline_run_once(&p, true); // foreground: always show the result
    }
}

// Runs exactly one line of the in-progress script, advancing run_pos
// past it, and clears `running` once the script is out of lines.
// Called at most once per task_shell() tick -- if the line dispatches
// to `sleep`, this simply isn't called again until that sleep's wake
// time passes (same top-of-function check `sleeping` already gets),
// so a script's sleep behaves exactly like a typed one.
static void run_step(void)
{
    if (run_buf[run_pos] == '\0') { running = false; return; }

    char *ln = run_buf + run_pos;
    char *nl = strchr(ln, '\n');
    if (nl) { *nl = '\0'; run_pos = (int)(nl - run_buf) + 1; }
    else     { run_pos += strlen(ln); } // last line (shouldn't lack a trailing '\n', but safe)

    printf("%% %s\n", ln);
    dispatch(ln);
}

void task_shell(void)
{
    static char line[LINE_MAX];
    static int  line_len = 0;
    static bool banner_shown = false;

    if (!banner_shown) {
        printf("\npicoos on cpu_rp2040\n");
        printf("  ls            list /dev and bin/\n");
        printf("  help          builtins and grammar\n");
        banner_shown = true;

        // Auto-run 'boot': /fram/boot if it exists, else the built-in one.
        // scripts[0] is always seeded with "boot" (see BOOT_SCRIPT_TEXT
        // above), so load_script() only fails here if that ever changes.
        if (load_script("boot")) {
            int fd = fs_open("/fram/boot");
            printf("running %s automatically...\n", fd >= 0 ? "/fram/boot" : "built-in 'boot'");
            fs_close(fd);
            running = true; // picked up by the `if (running)` block below,
                             // same tick -- first line runs immediately
        } else {
            print_prompt();
        }
    }

    if (sleeping) {
        if (absolute_time_diff_us(get_absolute_time(), wake_time) > 0) return; // not yet
        sleeping = false;
        if (!running) print_prompt(); // a script's own sleep resumes silently, no interactive prompt
        // fall through: either process input that arrived while asleep,
        // or (if running) let the block below continue the script now
    }

    if (running) {
        run_step();
        if (!running) {
            if (looping) {
                // Script reached its end -- restart it instead of stopping.
                // run_buf must be reloaded, not just rewound: run_step()
                // turned every '\n' into '\0' on the way through (see
                // run_name). Reloading also picks up an edited script
                // file on the next pass. Next tick's run_step() then
                // starts at line 1 fresh.
                running = load_script(run_name);
                if (!running) { looping = false; printf("loop: '%s' is gone\n", run_name); print_prompt(); }
            } else if (!sleeping) {
                print_prompt(); // script just finished, and didn't end mid-sleep
            }
        }
        if (looping) {
            // A plain `run` never reads stdin while replaying (see header
            // comment) -- but a loop never stops on its own, so it needs
            // its own Ctrl-C poll, same convention `watching` below uses.
            int c;
            while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
                if (c == 3) { // Ctrl-C
                    looping = false;
                    running = false;
                    printf("(loop stopped)\n");
                    print_prompt();
                    break;
                }
                // anything else typed while looping is discarded, on purpose --
                // same reasoning as `watching`'s own poll below
            }
        }
        return; // script lines advance one per tick; don't also read keyboard input this tick
    }

    if (watching) {
        if (absolute_time_diff_us(get_absolute_time(), watch_next) <= 0) {
            pipeline_run_once(&watch_pipeline, true); // foreground: always prints
            watch_next = make_timeout_time_ms(watch_interval_ms);
        }
        int c;
        while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
            if (c == 3) { // Ctrl-C
                watching = false;
                printf("(watch stopped)\n");
                print_prompt();
                break;
            }
            // anything else typed while watching is discarded, on purpose --
            // see the header comment for why this doesn't preserve type-ahead
        }
        return; // watching owns this tick; don't also run the normal command loop
    }

    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[line_len] = '\0';
            if (capturing) capture_line(line);
            else dispatch(line);
            line_len = 0;
            // sleep/run/watch set their own flag; each prints its own prompt once done instead
            if (!sleeping && !running && !watching) print_prompt();
            // ...and owns the shell from here: stop reading, leave whatever
            // else was typed (or pasted) in the USB buffer until it's done.
            // picoos kept reading, so a pasted line after `run x` or
            // `sleep 1000` ran right away, mixed into the script.
            else break;
        } else if (c == 8 || c == 127) { // backspace / DEL
            if (line_len > 0) {
                line_len--;
                printf("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7f && line_len < LINE_MAX - 1) {
            // Printable ASCII only -- silently drop anything else (e.g.
            // stray bytes from USB CDC enumeration/terminal noise right
            // at connect time, which is exactly what showed up as a
            // garbage glyph right after the boot prompt).
            line[line_len++] = (char)c;
            putchar(c);
        }
    }
}
