#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

/*
 * setup_signals - install shell signal handlers.
 *
 * The shell itself ignores SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, and SIGTTOU.
 * A SIGCHLD handler is installed to reap background zombie children and
 * update the job list.
 *
 * Must be called once at startup, before the main loop.
 */
void setup_signals(void);

/*
 * execute - lex → parse result handed off here for execution.
 *
 * Handles:
 *   - Built-in commands (no fork)
 *   - Single external commands (fork → execvp → waitpid)
 *   - Multi-stage pipelines (N-1 pipes, N forks)
 *   - Background pipelines (register in job list, no wait)
 *   - File redirections via dup2
 *   - Terminal control via tcsetpgrp
 */
void execute(Pipeline *p);

/*
 * get_last_status - return the exit status of the last foreground command.
 * Useful for implementing $? in the main loop.
 */
int get_last_status(void);

#endif /* EXECUTOR_H */
