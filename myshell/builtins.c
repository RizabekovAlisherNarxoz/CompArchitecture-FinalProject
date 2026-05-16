#include "builtins.h"
#include "jobs.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* ------------------------------------------------------------------ *
 * Forward declarations for individual built-ins
 * ------------------------------------------------------------------ */
static int builtin_cd(char **argv);
static int builtin_exit(char **argv);
static int builtin_jobs(char **argv);
static int builtin_fg(char **argv);
static int builtin_bg(char **argv);

/* ------------------------------------------------------------------ *
 * Dispatch table
 * ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    int (*fn)(char **argv);
} BuiltinEntry;

static const BuiltinEntry builtins[] = {
    { "cd",   builtin_cd   },
    { "exit", builtin_exit },
    { "jobs", builtin_jobs },
    { "fg",   builtin_fg   },
    { "bg",   builtin_bg   },
    { NULL,   NULL         }
};

/* ------------------------------------------------------------------ *
 * is_builtin
 * ------------------------------------------------------------------ */
int is_builtin(const char *cmd)
{
    if (!cmd) return 0;
    for (int i = 0; builtins[i].name; i++)
        if (strcmp(cmd, builtins[i].name) == 0)
            return 1;
    return 0;
}

/* ------------------------------------------------------------------ *
 * run_builtin
 * ------------------------------------------------------------------ */
int run_builtin(char **argv)
{
    if (!argv || !argv[0]) return 1;
    for (int i = 0; builtins[i].name; i++)
        if (strcmp(argv[0], builtins[i].name) == 0)
            return builtins[i].fn(argv);
    fprintf(stderr, "mysh: %s: not a built-in\n", argv[0]);
    return 1;
}

/* ================================================================== *
 * Individual built-in implementations
 * ================================================================== */

/* ------------------------------------------------------------------ *
 * cd [dir]
 *
 * With no argument, cd to $HOME.  Prints an error if the target
 * directory does not exist or permission is denied.
 * ------------------------------------------------------------------ */
static int builtin_cd(char **argv)
{
    const char *dir = argv[1];

    if (!dir) {
        dir = getenv("HOME");
        if (!dir) {
            fprintf(stderr, "mysh: cd: HOME not set\n");
            return 1;
        }
    }

    if (chdir(dir) != 0) {
        fprintf(stderr, "mysh: cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ *
 * exit [code]
 *
 * Exits the shell with the given numeric code (default 0).
 * This function never returns.
 * ------------------------------------------------------------------ */
static int builtin_exit(char **argv)
{
    int code = 0;

    if (argv[1]) {
        char *end;
        long v = strtol(argv[1], &end, 10);
        if (*end != '\0') {
            fprintf(stderr, "mysh: exit: %s: numeric argument required\n", argv[1]);
            code = 2;
        } else {
            code = (int)(v & 0xFF); /* match bash: only low 8 bits */
        }
    }

    exit(code);
}

/* ------------------------------------------------------------------ *
 * jobs
 *
 * Reaps any finished children, then lists active/stopped jobs.
 * ------------------------------------------------------------------ */
static int builtin_jobs(char **argv)
{
    (void)argv; /* unused */
    update_jobs();
    print_jobs();
    return 0;
}

/* ------------------------------------------------------------------ *
 * fg [id]
 *
 * Bring job [id] (default: most recent) to the foreground:
 *   1. Give the job's process group control of the terminal.
 *   2. Send SIGCONT to the process group.
 *   3. Wait for it to stop or finish (blocking waitpid).
 *   4. Reclaim terminal control for the shell when it returns.
 * ------------------------------------------------------------------ */
static int builtin_fg(char **argv)
{
    update_jobs();

    /* Determine target job id */
    int id = -1;
    if (argv[1]) {
        char *end;
        id = (int)strtol(argv[1], &end, 10);
        if (*end != '\0' || id <= 0) {
            fprintf(stderr, "mysh: fg: %s: invalid job id\n", argv[1]);
            return 1;
        }
    } else {
        /* No id supplied – find the most recently added non-done job */
        Job *last = NULL;
        for (int i = 1; i < 1024; i++) {
            Job *j = find_job_by_id(i);
            if (!j) break;
            if (j->state != JOB_DONE) last = j;
        }
        if (!last) {
            fprintf(stderr, "mysh: fg: no current job\n");
            return 1;
        }
        id = last->id;
    }

    Job *j = find_job_by_id(id);
    if (!j) {
        fprintf(stderr, "mysh: fg: %d: no such job\n", id);
        return 1;
    }
    if (j->state == JOB_DONE) {
        fprintf(stderr, "mysh: fg: job %d has already terminated\n", id);
        remove_job(id);
        return 1;
    }

    /* Print the command being foregrounded (bash-compatible) */
    printf("%s\n", j->cmdstr);

    /* Hand terminal to the job's process group */
    if (tcsetpgrp(STDIN_FILENO, j->pgid) < 0)
        perror("mysh: fg: tcsetpgrp");

    /* Resume if stopped */
    if (kill(-(j->pgid), SIGCONT) < 0)
        perror("mysh: fg: SIGCONT");

    j->state = JOB_RUNNING;

    /* Wait for the job to stop or exit (blocking) */
    int status = 0;
    pid_t ret;
    do {
        ret = waitpid(-(j->pgid), &status, WUNTRACED);
    } while (ret < 0 && errno == EINTR);

    /* Reclaim terminal for the shell */
    if (tcsetpgrp(STDIN_FILENO, getpgrp()) < 0)
        perror("mysh: fg: tcsetpgrp (restore)");

    if (ret < 0) {
        if (errno == ECHILD) {
            /*
             * The SIGCHLD handler raced with us and already reaped the
             * child.  Treat the job as done — not an error.
             */
            j->state = JOB_DONE;
            remove_job(id);
            return 0;
        }
        perror("mysh: fg: waitpid");
        return 1;
    }

    if (WIFSTOPPED(status)) {
        j->state = JOB_STOPPED;
        printf("\n[%d] Stopped    %s\n", j->id, j->cmdstr);
    } else {
        j->state = JOB_DONE;
        remove_job(id);
    }

    return 0;
}

/* ------------------------------------------------------------------ *
 * bg [id]
 *
 * Resume a stopped job in the background:
 *   1. Send SIGCONT to the process group.
 *   2. Mark the job as RUNNING (do not wait for it).
 * ------------------------------------------------------------------ */
static int builtin_bg(char **argv)
{
    update_jobs();

    int id = -1;
    if (argv[1]) {
        char *end;
        id = (int)strtol(argv[1], &end, 10);
        if (*end != '\0' || id <= 0) {
            fprintf(stderr, "mysh: bg: %s: invalid job id\n", argv[1]);
            return 1;
        }
    } else {
        /* Find most recently stopped job */
        Job *last = NULL;
        for (int i = 1; i < 1024; i++) {
            Job *j = find_job_by_id(i);
            if (!j) break;
            if (j->state == JOB_STOPPED) last = j;
        }
        if (!last) {
            fprintf(stderr, "mysh: bg: no stopped job\n");
            return 1;
        }
        id = last->id;
    }

    Job *j = find_job_by_id(id);
    if (!j) {
        fprintf(stderr, "mysh: bg: %d: no such job\n", id);
        return 1;
    }
    if (j->state == JOB_RUNNING) {
        fprintf(stderr, "mysh: bg: job %d is already running\n", id);
        return 0;
    }
    if (j->state == JOB_DONE) {
        fprintf(stderr, "mysh: bg: job %d has already terminated\n", id);
        remove_job(id);
        return 1;
    }

    printf("[%d] %s &\n", j->id, j->cmdstr);

    if (kill(-(j->pgid), SIGCONT) < 0) {
        perror("mysh: bg: SIGCONT");
        return 1;
    }

    j->state = JOB_RUNNING;
    return 0;
}
