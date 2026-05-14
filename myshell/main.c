#include "executor.h"
#include "jobs.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INPUT_MAX 4096

/*
 * notify_done_jobs - print completion notices for any jobs that finished
 * since the last prompt and remove them from the job list.
 *
 * Called at the top of each prompt iteration so the user sees notices
 * before typing the next command, matching bash/dash behaviour.
 */
static void notify_done_jobs(void)
{
    update_jobs();

    /* Collect done ids first; removing during traversal is unsafe. */
    int done[256];
    int ndone = 0;

    for (int id = 1; id < 1024 && ndone < 256; id++) {
        Job *j = find_job_by_id(id);
        if (!j) break;
        if (j->state == JOB_DONE) {
            printf("[%d] Done       %s\n", j->id, j->cmdstr);
            done[ndone++] = id;
        }
    }

    for (int i = 0; i < ndone; i++)
        remove_job(done[i]);
}

int main(void)
{
    /*
     * Install shell signal handlers exactly once at startup.
     * This makes the shell ignore SIGINT/SIGQUIT/SIGTSTP/SIGTTIN/SIGTTOU
     * and installs the SIGCHLD reaper.
     */
    setup_signals();

    /*
     * Put the shell into its own process group and claim the terminal
     * so that child pipelines can hand it back later.
     */
    pid_t shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0) {
        /* Ignore EPERM — already a process group leader (e.g. login shell). */
    }
    if (isatty(STDIN_FILENO)) {
        if (tcsetpgrp(STDIN_FILENO, shell_pgid) < 0)
            perror("mysh: tcsetpgrp (init)");
    }

    char line[INPUT_MAX];

    while (1) {
        /* Notify user of any background jobs that completed. */
        notify_done_jobs();

        /* Print prompt only for interactive (tty) sessions. */
        if (isatty(STDIN_FILENO)) {
            fputs("mysh> ", stdout);
            fflush(stdout);
        }

        /* Read a line; EOF (Ctrl+D) exits cleanly. */
        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (isatty(STDIN_FILENO))
                putchar('\n'); /* move cursor to next line */
            break;
        }

        /* Strip trailing newline so the lexer sees a clean string. */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        /* Skip blank lines. */
        if (line[0] == '\0') continue;

        /* Lex → Parse → Execute */
        int    count  = 0;
        Token *tokens = tokenize(line, &count);
        if (!tokens) {
            fprintf(stderr, "mysh: tokenizer error\n");
            continue;
        }

        Pipeline *pl = parse(tokens, count);
        free_tokens(tokens, count);

        if (!pl) continue; /* empty or syntax error — message already printed */

        execute(pl);
        free_pipeline(pl);
    }

    return get_last_status();
}
