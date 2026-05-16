#ifndef BUILTINS_H
#define BUILTINS_H

/*
 * is_builtin - return 1 if cmd names a built-in command, 0 otherwise.
 */
int is_builtin(const char *cmd);

/*
 * run_builtin - execute the built-in named by argv[0].
 *
 * argv must be NULL-terminated.
 * Returns the exit status of the built-in (0 = success, non-zero = error).
 * For "exit", this function does not return.
 */
int run_builtin(char **argv);

#endif /* BUILTINS_H */
