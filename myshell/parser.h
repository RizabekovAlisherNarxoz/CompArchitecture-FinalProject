#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

/*
 * Command - one stage of a pipeline.
 *
 *   argv     NULL-terminated argument vector (argv[0] is the program name).
 *   infile   path to redirect stdin from, or NULL.
 *   outfile  path to redirect stdout to, or NULL.
 *   errfile  path to redirect stderr to, or NULL.
 *   append   non-zero when outfile should be opened with O_APPEND (>>).
 */
typedef struct {
    char **argv;
    char  *infile;
    char  *outfile;
    char  *errfile;
    int    append;
} Command;

/*
 * Pipeline - a list of commands connected by pipes.
 *
 *   cmds       array of Command structs.
 *   count      number of commands in the array.
 *   background non-zero if the pipeline was terminated with '&'.
 */
typedef struct {
    Command *cmds;
    int      count;
    int      background;
} Pipeline;

/*
 * parse - build a Pipeline from the token array produced by tokenize().
 *
 * Returns a heap-allocated Pipeline on success, or NULL on syntax error or
 * allocation failure.  The caller must release it with free_pipeline().
 */
Pipeline *parse(Token *tokens, int count);

/*
 * free_pipeline - release all memory owned by a Pipeline.
 */
void free_pipeline(Pipeline *p);

#endif /* PARSER_H */
