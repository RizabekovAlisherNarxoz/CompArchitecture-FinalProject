#include "executor.h"
#include "builtins.h"
#include "jobs.h"
#include "parser.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* Exit status of the last foreground command. */
static int last_status = 0;

/* ------------------------------------------------------------------ *
 * SIGCHLD handler
 *
 * Strategy:
 *   - Loop waitpid(-1, WNOHANG | WUNTRACED) to reap ALL children that
 *     have changed state, preventing zombie accumulation.
 *   - For each reaped pid that is the process-group leader (pid == pgid),
 *     find_job_by_pgid() succeeds and we update the job state directly.
 *   - For non-leader pipeline members (pid != pgid), find_job_by_pgid()
 *     returns NULL; we simply discard — those zombies ARE reaped, but the
 *     job record stays alive until the leader is also reaped.
 *   - SIGCHLD is BLOCKED during foreground waitpid() calls (see
 *     execute_pipeline) so this handler is only live for background events.
 * ------------------------------------------------------------------ */
static void sigchld_handler(int sig)
{
    (void)sig;
    int saved_errno = errno;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        Job *j = find_job_by_pgid(pid);
        if (!j) continue; /* non-leader process — zombie reaped, no job update needed */

        if (WIFSTOPPED(status))
            j->state = JOB_STOPPED;
        else if (WIFEXITED(status) || WIFSIGNALED(status))
            j->state = JOB_DONE;
    }

    errno = saved_errno;
}

/* ------------------------------------------------------------------ *
 * setup_signals
 * ------------------------------------------------------------------ */
void setup_signals(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);

    /*
     * The shell ignores interactive stop/interrupt signals so that
     * Ctrl+C / Ctrl+Z only affect the foreground process group.
     * Children created by fork() inherit SIG_IGN, so we reset them
     * to SIG_DFL in reset_child_signals() before every execvp().
     */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);
    sigaction(SIGTTOU, &sa, NULL);

    /* SIGCHLD: reap zombie children, update job states. */
    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART; /* restart interrupted syscalls automatically */
    sigaction(SIGCHLD, &sa, NULL);
}

/* ------------------------------------------------------------------ *
 * get_last_status
 * ------------------------------------------------------------------ */
int get_last_status(void)
{
    return last_status;
}

/* ------------------------------------------------------------------ *
 * reset_child_signals
 *
 * Called in each child process immediately after fork(), before execvp().
 * Restores signal dispositions to defaults and clears any inherited
 * signal mask so the child behaves like a normal process.
 * ------------------------------------------------------------------ */
static void reset_child_signals(void)
{
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);
    sigaction(SIGTTOU, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);

    /* Clear any signal mask inherited from the parent (e.g. blocked SIGCHLD). */
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
}

/* ------------------------------------------------------------------ *
 * apply_redirects
 *
 * Opens redirect files and dup2()s them onto the standard file
 * descriptors.  Must be called in the child process before execvp().
 * Returns 0 on success, -1 on error (error message already printed).
 * ------------------------------------------------------------------ */
static int apply_redirects(Command *cmd)
{
    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "mysh: %s: %s\n", cmd->infile, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("mysh: dup2 stdin");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->outfile, flags, 0644);
        if (fd < 0) {
            fprintf(stderr, "mysh: %s: %s\n", cmd->outfile, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("mysh: dup2 stdout");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (cmd->errfile) {
        int fd = open(cmd->errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "mysh: %s: %s\n", cmd->errfile, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            perror("mysh: dup2 stderr");
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}

/* ------------------------------------------------------------------ *
 * build_cmdstr
 *
 * Construct a human-readable command string from a Pipeline for use
 * in job list display (e.g. "cat file | grep foo &").
 * Caller must free() the returned string.
 * ------------------------------------------------------------------ */
static char *build_cmdstr(Pipeline *p)
{
    /* Calculate the exact buffer size needed. */
    size_t total = 1; /* NUL terminator */
    for (int i = 0; i < p->count; i++) {
        if (i > 0) total += 3; /* " | " */
        for (int j = 0; p->cmds[i].argv[j]; j++)
            total += strlen(p->cmds[i].argv[j]) + (j > 0 ? 1 : 0); /* word + space */
    }
    if (p->background) total += 2; /* " &" */

    char *s = malloc(total);
    if (!s) return NULL;
    s[0] = '\0';

    for (int i = 0; i < p->count; i++) {
        if (i > 0) strcat(s, " | ");
        for (int j = 0; p->cmds[i].argv[j]; j++) {
            if (j > 0) strcat(s, " ");
            strcat(s, p->cmds[i].argv[j]);
        }
    }
    if (p->background) strcat(s, " &");
    return s;
}

/* ------------------------------------------------------------------ *
 * close_all_pipes - close every end of every pipe in the parent
 * ------------------------------------------------------------------ */
static void close_all_pipes(int (*fds)[2], int npipes)
{
    for (int i = 0; i < npipes; i++) {
        close(fds[i][0]);
        close(fds[i][1]);
    }
}

/* ------------------------------------------------------------------ *
 * execute_pipeline
 *
 * Core execution engine.  Handles both single-command and multi-command
 * (piped) pipelines as well as foreground vs. background dispatch.
 *
 * Signal-safety notes
 * -------------------
 * SIGCHLD is BLOCKED (via sigprocmask) for the entire fork loop and
 * during the foreground waitpid loop.  This prevents the SIGCHLD
 * handler from racing with:
 *   a) our fork() calls (child exits before parent records its pid)
 *   b) our blocking waitpid() calls (handler reaps child first → ECHILD)
 *
 * Children inherit the blocked mask but immediately clear it in
 * reset_child_signals() before execvp(), so they receive all signals
 * normally.
 *
 * After the foreground wait completes we restore the previous signal
 * mask, which lets any pending SIGCHLD fire for background job events.
 * ------------------------------------------------------------------ */
static void execute_pipeline(Pipeline *p)
{
    int n = p->count;

    /* -------------------------------------------------------------- *
     * Create n-1 pipes connecting adjacent commands.
     * fds[i][0] = read end  (stdin  of command i+1)
     * fds[i][1] = write end (stdout of command i)
     * -------------------------------------------------------------- */
    int (*fds)[2] = NULL;
    int npipes    = n - 1;

    if (npipes > 0) {
        fds = malloc((size_t)npipes * sizeof(int[2]));
        if (!fds) { perror("mysh: malloc"); return; }

        for (int i = 0; i < npipes; i++) {
            if (pipe(fds[i]) < 0) {
                perror("mysh: pipe");
                for (int k = 0; k < i; k++) {
                    close(fds[k][0]);
                    close(fds[k][1]);
                }
                free(fds);
                return;
            }
        }
    }

    pid_t *pids = malloc((size_t)n * sizeof(pid_t));
    if (!pids) {
        perror("mysh: malloc");
        if (fds) { close_all_pipes(fds, npipes); free(fds); }
        return;
    }

    /* Block SIGCHLD for the duration of the fork loop. */
    sigset_t chld_mask, prev_mask;
    sigemptyset(&chld_mask);
    sigaddset(&chld_mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &chld_mask, &prev_mask);

    pid_t pgid = 0; /* process group id, set to first child's pid */

    /* -------------------------------------------------------------- *
     * Fork one child per command.
     * -------------------------------------------------------------- */
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("mysh: fork");
            /* Terminate already-forked children. */
            for (int k = 0; k < i; k++)
                kill(pids[k], SIGTERM);
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);
            if (fds) { close_all_pipes(fds, npipes); free(fds); }
            free(pids);
            return;
        }

        if (pid == 0) {
            /* ======================================================
             * CHILD PROCESS
             * ====================================================== */

            /*
             * Reset signals (restores SIG_DFL dispositions) and
             * clears the inherited SIGCHLD-blocked mask so the child
             * receives all signals normally.
             */
            reset_child_signals();

            /*
             * Put this process into the pipeline's process group.
             * First child: setpgid(0,0) creates a new group with
             * pgid == own pid.  Subsequent children join it.
             * We do this in both parent and child to avoid a race
             * where execvp runs before the parent calls setpgid.
             */
            if (setpgid(0, pgid) < 0 && errno != EPERM) {
                /* EPERM is harmless: means the process already exec'd */
                perror("mysh: setpgid (child)");
            }

            /* Connect stdin to the read end of the previous pipe. */
            if (i > 0 && fds) {
                if (dup2(fds[i-1][0], STDIN_FILENO) < 0) {
                    perror("mysh: dup2 pipe stdin");
                    exit(1);
                }
            }

            /* Connect stdout to the write end of the next pipe. */
            if (i < n-1 && fds) {
                if (dup2(fds[i][1], STDOUT_FILENO) < 0) {
                    perror("mysh: dup2 pipe stdout");
                    exit(1);
                }
            }

            /* Close all pipe fd copies in the child. */
            if (fds) close_all_pipes(fds, npipes);

            /*
             * Apply explicit file redirections AFTER pipe setup so
             * that e.g. "cmd < in | ..." overrides the pipe's stdin.
             */
            if (apply_redirects(&p->cmds[i]) < 0)
                exit(1);

            /* Built-ins in a pipeline position run in the child. */
            if (is_builtin(p->cmds[i].argv[0]))
                exit(run_builtin(p->cmds[i].argv));

            execvp(p->cmds[i].argv[0], p->cmds[i].argv);
            fprintf(stderr, "mysh: %s: %s\n",
                    p->cmds[i].argv[0], strerror(errno));
            exit(127);
        }

        /* ======================================================
         * PARENT PROCESS — record child and maintain pgid
         * ====================================================== */
        pids[i] = pid;
        if (pgid == 0) pgid = pid; /* first child sets the group id */

        /*
         * Set from the parent side too (mirror of the child's setpgid)
         * to close the race window before the child calls execvp.
         */
        if (setpgid(pid, pgid) < 0 && errno != EACCES && errno != EPERM) {
            perror("mysh: setpgid (parent)");
        }
    }

    /* Close pipe ends in the parent — they are only for children. */
    if (fds) { close_all_pipes(fds, npipes); free(fds); }

    /* -------------------------------------------------------------- *
     * Background pipeline: register job and return immediately.
     * -------------------------------------------------------------- */
    if (p->background) {
        char *cmdstr = build_cmdstr(p);
        Job  *j      = add_job(pgid, cmdstr ? cmdstr : "?", JOB_RUNNING);
        free(cmdstr);
        if (j) printf("[%d] %d\n", j->id, (int)pgid);
        /* Restore signal mask — pending SIGCHLD events can now fire. */
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        free(pids);
        return;
    }

    /* -------------------------------------------------------------- *
     * Foreground pipeline: transfer terminal control, then wait.
     *
     * SIGCHLD stays BLOCKED throughout the waitpid loop so that the
     * SIGCHLD handler cannot race with us and steal a child's exit
     * status.  waitpid() is a kernel-level wait and does NOT need
     * SIGCHLD to be unblocked in order to observe child state changes.
     * -------------------------------------------------------------- */
    if (tcsetpgrp(STDIN_FILENO, pgid) < 0 && errno != ENOTTY)
        perror("mysh: tcsetpgrp");

    int any_stopped = 0;
    int final_status = 0;

    for (int i = 0; i < n; i++) {
        int st = 0;
        pid_t ret;
        do {
            ret = waitpid(pids[i], &st, WUNTRACED);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            if (errno != ECHILD) /* ECHILD: already reaped by handler — not an error */
                perror("mysh: waitpid");
            continue;
        }

        if (WIFSTOPPED(st))    any_stopped = 1;
        if (i == n - 1)        final_status = st; /* rightmost command's status */
    }

    /* Reclaim terminal for the shell. */
    if (tcsetpgrp(STDIN_FILENO, getpgrp()) < 0 && errno != ENOTTY)
        perror("mysh: tcsetpgrp (restore)");

    /* Restore signal mask; pending SIGCHLD events fire now. */
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    /* -------------------------------------------------------------- *
     * Update last_status and handle stop / signal termination.
     * -------------------------------------------------------------- */
    if (any_stopped) {
        char *cmdstr = build_cmdstr(p);
        Job  *j      = add_job(pgid, cmdstr ? cmdstr : "?", JOB_STOPPED);
        free(cmdstr);
        if (j) printf("\n[%d]+ Stopped    %s\n", j->id, j->cmdstr);
        /* Keep last_status unchanged — a stop doesn't set an exit code. */
    } else if (WIFEXITED(final_status)) {
        last_status = WEXITSTATUS(final_status);
    } else if (WIFSIGNALED(final_status)) {
        last_status = 128 + WTERMSIG(final_status);
        /* Print newline so the next prompt starts on a clean line. */
        if (WTERMSIG(final_status) != SIGPIPE)
            fputc('\n', stderr);
    }

    free(pids);
}

/* ------------------------------------------------------------------ *
 * execute — public entry point
 * ------------------------------------------------------------------ */
void execute(Pipeline *p)
{
    if (!p || p->count == 0) return;

    /*
     * A single built-in command at the top level runs directly in the
     * shell process (no fork).  Built-ins in a pipeline position (e.g.
     * "echo hi | cat") are handled by execute_pipeline where they run
     * inside a forked child.
     */
    if (p->count == 1 && is_builtin(p->cmds[0].argv[0])) {
        last_status = run_builtin(p->cmds[0].argv);
        return;
    }

    execute_pipeline(p);
}
