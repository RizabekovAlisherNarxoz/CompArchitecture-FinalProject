#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Dynamic token array helpers
 * ------------------------------------------------------------------ */

#define INIT_CAPACITY 16

typedef struct {
    Token *data;
    int    len;
    int    cap;
} TokenVec;

static int vec_init(TokenVec *v)
{
    v->data = malloc(INIT_CAPACITY * sizeof(Token));
    if (!v->data) return -1;
    v->len = 0;
    v->cap = INIT_CAPACITY;
    return 0;
}

static int vec_push(TokenVec *v, Token t)
{
    if (v->len == v->cap) {
        int newcap = v->cap * 2;
        Token *tmp = realloc(v->data, (size_t)newcap * sizeof(Token));
        if (!tmp) return -1;
        v->data = tmp;
        v->cap  = newcap;
    }
    v->data[v->len++] = t;
    return 0;
}

/* ------------------------------------------------------------------ *
 * Internal helpers
 * ------------------------------------------------------------------ */

/* Make a TOKEN_WORD from [start, end). Returns -1 on alloc failure. */
static int push_word(TokenVec *v, const char *start, const char *end)
{
    if (end <= start) return 0; /* empty – skip */

    size_t len = (size_t)(end - start);
    char  *val = malloc(len + 1);
    if (!val) return -1;
    memcpy(val, start, len);
    val[len] = '\0';

    Token t = { TOKEN_WORD, val };
    if (vec_push(v, t) < 0) {
        free(val);
        return -1;
    }
    return 0;
}

static int push_simple(TokenVec *v, TokenType type)
{
    Token t = { type, NULL };
    return vec_push(v, t);
}

/* ------------------------------------------------------------------ *
 * tokenize
 * ------------------------------------------------------------------ */

Token *tokenize(const char *line, int *count)
{
    TokenVec v;
    if (vec_init(&v) < 0) return NULL;

    const char *p = line;

    while (*p != '\0') {
        /* Skip horizontal whitespace */
        if (*p == ' ' || *p == '\t') {
            p++;
            continue;
        }

        /* Newline */
        if (*p == '\n') {
            if (push_simple(&v, TOKEN_NEWLINE) < 0) goto oom;
            p++;
            continue;
        }

        /* REDIR_APPEND >> */
        if (p[0] == '>' && p[1] == '>') {
            if (push_simple(&v, TOKEN_REDIR_APPEND) < 0) goto oom;
            p += 2;
            continue;
        }

        /* REDIR_ERR  2> */
        if (p[0] == '2' && p[1] == '>') {
            if (push_simple(&v, TOKEN_REDIR_ERR) < 0) goto oom;
            p += 2;
            continue;
        }

        /* Single-character operators */
        if (*p == '|') { if (push_simple(&v, TOKEN_PIPE)      < 0) goto oom; p++; continue; }
        if (*p == '<') { if (push_simple(&v, TOKEN_REDIR_IN)   < 0) goto oom; p++; continue; }
        if (*p == '>') { if (push_simple(&v, TOKEN_REDIR_OUT)  < 0) goto oom; p++; continue; }
        if (*p == '&') { if (push_simple(&v, TOKEN_BACKGROUND) < 0) goto oom; p++; continue; }

        /* Quoted string – single quotes: no escaping, take literally */
        if (*p == '\'') {
            p++; /* skip opening quote */
            const char *start = p;
            while (*p != '\'' && *p != '\0') p++;
            if (push_word(&v, start, p) < 0) goto oom;
            if (*p == '\'') p++; /* skip closing quote */
            continue;
        }

        /* Quoted string – double quotes: honour \" and \\ only */
        if (*p == '"') {
            p++; /* skip opening quote */
            /* Collect into a growable buffer */
            size_t bufsz = 64, buflen = 0;
            char  *buf   = malloc(bufsz);
            if (!buf) goto oom;

            while (*p != '"' && *p != '\0') {
                char ch;
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) {
                    p++;          /* skip backslash */
                    ch = *p++;
                } else {
                    ch = *p++;
                }
                if (buflen + 1 >= bufsz) {
                    bufsz *= 2;
                    char *tmp = realloc(buf, bufsz);
                    if (!tmp) { free(buf); goto oom; }
                    buf = tmp;
                }
                buf[buflen++] = ch;
            }
            buf[buflen] = '\0';
            if (*p == '"') p++; /* skip closing quote */

            Token t = { TOKEN_WORD, buf };
            if (vec_push(&v, t) < 0) { free(buf); goto oom; }
            continue;
        }

        /* Unquoted word: consume until whitespace or operator */
        {
            const char *start = p;
            while (*p != '\0' &&
                   *p != ' '  && *p != '\t' && *p != '\n' &&
                   *p != '|'  && *p != '<'  && *p != '>'  &&
                   *p != '&'  && *p != '\'' && *p != '"') {
                /*
                 * Special case: "2>" is only an operator when the '2' is
                 * not part of a longer word token that has already started.
                 * We break if we see '2' followed immediately by '>' at the
                 * very beginning of the word (handled above in the outer loop)
                 * but NOT in the middle of a word like "fd2>".  So here we
                 * just advance normally.
                 */
                p++;
            }
            if (push_word(&v, start, p) < 0) goto oom;
        }
    }

    /* Always terminate with EOF */
    if (push_simple(&v, TOKEN_EOF) < 0) goto oom;

    *count = v.len;
    return v.data;

oom:
    /* Free everything we allocated so far */
    for (int i = 0; i < v.len; i++) free(v.data[i].value);
    free(v.data);
    *count = 0;
    return NULL;
}

/* ------------------------------------------------------------------ *
 * free_tokens
 * ------------------------------------------------------------------ */

void free_tokens(Token *tokens, int count)
{
    if (!tokens) return;
    for (int i = 0; i < count; i++) free(tokens[i].value);
    free(tokens);
}
