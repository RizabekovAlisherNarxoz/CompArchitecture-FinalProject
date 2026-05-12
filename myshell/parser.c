#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Internal: growable argv builder
 * ------------------------------------------------------------------ */

typedef struct {
    char **data;
    int    len;
    int    cap;
} ArgVec;

static int argv_init(ArgVec *v)
{
    v->data = malloc(8 * sizeof(char *));
    if (!v->data) return -1;
    v->len = 0;
    v->cap = 8;
    return 0;
}

static int argv_push(ArgVec *v, char *s)
{
    if (v->len + 1 >= v->cap) { /* +1 to always keep room for NULL sentinel */
        int newcap = v->cap * 2;
        char **tmp = realloc(v->data, (size_t)newcap * sizeof(char *));
        if (!tmp) return -1;
        v->data = tmp;
        v->cap  = newcap;
    }
    v->data[v->len++] = s;
    return 0;
}

/* Seal with NULL sentinel – caller now owns the array. */
static char **argv_finish(ArgVec *v)
{
    v->data[v->len] = NULL;
    return v->data;
}

static void argv_free(ArgVec *v)
{
    /* Note: we do NOT free the individual strings here – they are owned by
     * the Token array and freed by free_tokens().  We only free the pointer
     * array itself. */
    free(v->data);
}

/* ------------------------------------------------------------------ *
 * Internal: growable Command array
 * ------------------------------------------------------------------ */

typedef struct {
    Command *data;
    int      len;
    int      cap;
} CmdVec;

static int cmdvec_init(CmdVec *v)
{
    v->data = malloc(4 * sizeof(Command));
    if (!v->data) return -1;
    v->len = 0;
    v->cap = 4;
    return 0;
}

static int cmdvec_push(CmdVec *v, Command c)
{
    if (v->len == v->cap) {
        int newcap = v->cap * 2;
        Command *tmp = realloc(v->data, (size_t)newcap * sizeof(Command));
        if (!tmp) return -1;
        v->data = tmp;
        v->cap  = newcap;
    }
    v->data[v->len++] = c;
    return 0;
}

/* ------------------------------------------------------------------ *
 * Internal: duplicate a token's value string (for redirect filenames).
 * Token values are owned by the lexer array; we need our own copy so
 * free_pipeline() can safely free them independently of free_tokens().
 * ------------------------------------------------------------------ */
static char *dup_str(const char *s)
{
    if (!s) return NULL;
    char *d = malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}

/* ------------------------------------------------------------------ *
 * Internal: zero-initialise a Command
 * ------------------------------------------------------------------ */
static Command cmd_zero(void)
{
    Command c;
    c.argv    = NULL;
    c.infile  = NULL;
    c.outfile = NULL;
    c.errfile = NULL;
    c.append  = 0;
    return c;
}

/* ------------------------------------------------------------------ *
 * Internal: free a single Command's owned memory
 * ------------------------------------------------------------------ */
static void free_command(Command *c)
{
    if (!c) return;
    /* argv: free the pointer array; strings are copies we dup'd */
    if (c->argv) {
        for (int i = 0; c->argv[i] != NULL; i++)
            free(c->argv[i]);
        free(c->argv);
    }
    free(c->infile);
    free(c->outfile);
    free(c->errfile);
}

/* ------------------------------------------------------------------ *
 * parse
 *
 * Grammar (simplified):
 *
 *   pipeline  ::= command ( PIPE command )* [ BACKGROUND ] NEWLINE|EOF
 *   command   ::= ( WORD | redirect )*
 *   redirect  ::= REDIR_IN  WORD
 *               | REDIR_OUT WORD
 *               | REDIR_APPEND WORD
 *               | REDIR_ERR WORD
 * ------------------------------------------------------------------ */

Pipeline *parse(Token *tokens, int count)
{
    if (!tokens || count == 0) return NULL;

    CmdVec cv;
    if (cmdvec_init(&cv) < 0) return NULL;

    int background = 0;
    int pos        = 0;   /* current index into tokens[] */

    /* We parse one command segment at a time, separated by PIPE tokens. */
    while (pos < count) {
        TokenType tt = tokens[pos].type;

        /* Stop at end-of-input markers */
        if (tt == TOKEN_EOF || tt == TOKEN_NEWLINE) break;

        /* BACKGROUND at the top level ends the pipeline */
        if (tt == TOKEN_BACKGROUND) {
            background = 1;
            pos++;
            break;
        }

        /* A PIPE between commands – advance past it and start next command */
        if (tt == TOKEN_PIPE) {
            /* PIPE with nothing before it is a syntax error */
            if (cv.len == 0) {
                fprintf(stderr, "mysh: syntax error near unexpected token `|'\n");
                goto err;
            }
            pos++;
            /* Trailing pipe (nothing follows) is also an error */
            if (pos >= count ||
                tokens[pos].type == TOKEN_EOF ||
                tokens[pos].type == TOKEN_NEWLINE) {
                fprintf(stderr, "mysh: syntax error: expected command after `|'\n");
                goto err;
            }
            continue;
        }

        /* Build one Command */
        Command  cmd  = cmd_zero();
        ArgVec   av;
        if (argv_init(&av) < 0) goto err;

        while (pos < count) {
            tt = tokens[pos].type;

            if (tt == TOKEN_EOF    || tt == TOKEN_NEWLINE ||
                tt == TOKEN_PIPE   || tt == TOKEN_BACKGROUND)
                break; /* end of this command segment */

            if (tt == TOKEN_WORD) {
                /* Duplicate the string so free_pipeline owns its own copy */
                char *copy = dup_str(tokens[pos].value);
                if (!copy) { argv_free(&av); free_command(&cmd); goto err; }
                if (argv_push(&av, copy) < 0) {
                    free(copy); argv_free(&av); free_command(&cmd); goto err;
                }
                pos++;
                continue;
            }

            /* Redirection tokens – each must be followed by a WORD */
            if (tt == TOKEN_REDIR_IN  || tt == TOKEN_REDIR_OUT ||
                tt == TOKEN_REDIR_APPEND || tt == TOKEN_REDIR_ERR) {

                TokenType rtype = tt;
                pos++;

                if (pos >= count || tokens[pos].type != TOKEN_WORD) {
                    fprintf(stderr,
                            "mysh: syntax error: expected filename after redirection\n");
                    argv_free(&av); free_command(&cmd); goto err;
                }

                char *fname = dup_str(tokens[pos].value);
                if (!fname) { argv_free(&av); free_command(&cmd); goto err; }
                pos++;

                switch (rtype) {
                    case TOKEN_REDIR_IN:
                        free(cmd.infile);
                        cmd.infile = fname;
                        break;
                    case TOKEN_REDIR_OUT:
                        free(cmd.outfile);
                        cmd.outfile = fname;
                        cmd.append  = 0;
                        break;
                    case TOKEN_REDIR_APPEND:
                        free(cmd.outfile);
                        cmd.outfile = fname;
                        cmd.append  = 1;
                        break;
                    case TOKEN_REDIR_ERR:
                        free(cmd.errfile);
                        cmd.errfile = fname;
                        break;
                    default:
                        free(fname);
                        break;
                }
                continue;
            }

            /* Unknown token in this context – skip with a warning */
            fprintf(stderr, "mysh: warning: unexpected token type %d\n", tt);
            pos++;
        }

        /* Finalise argv */
        cmd.argv = argv_finish(&av);

        /* A command with an empty argv (e.g. bare redirection) is an error */
        if (av.len == 0) {
            fprintf(stderr, "mysh: syntax error: empty command\n");
            free_command(&cmd);
            goto err;
        }

        if (cmdvec_push(&cv, cmd) < 0) {
            free_command(&cmd);
            goto err;
        }

        /* If the next token is BACKGROUND, consume it and stop */
        if (pos < count && tokens[pos].type == TOKEN_BACKGROUND) {
            background = 1;
            pos++;
            break;
        }
    }

    if (cv.len == 0) {
        /* Empty input line – not an error, just nothing to run */
        free(cv.data);
        return NULL;
    }

    Pipeline *pl = malloc(sizeof(Pipeline));
    if (!pl) goto err;

    pl->cmds       = cv.data;
    pl->count      = cv.len;
    pl->background = background;
    return pl;

err:
    for (int i = 0; i < cv.len; i++) free_command(&cv.data[i]);
    free(cv.data);
    return NULL;
}

/* ------------------------------------------------------------------ *
 * free_pipeline
 * ------------------------------------------------------------------ */

void free_pipeline(Pipeline *p)
{
    if (!p) return;
    for (int i = 0; i < p->count; i++) free_command(&p->cmds[i]);
    free(p->cmds);
    free(p);
}
