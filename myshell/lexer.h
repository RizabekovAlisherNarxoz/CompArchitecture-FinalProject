#ifndef LEXER_H
#define LEXER_H

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,         /* | */
    TOKEN_REDIR_IN,     /* < */
    TOKEN_REDIR_OUT,    /* > */
    TOKEN_REDIR_APPEND, /* >> */
    TOKEN_REDIR_ERR,    /* 2> */
    TOKEN_BACKGROUND,   /* & */
    TOKEN_NEWLINE,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    char     *value; /* heap-allocated; NULL for non-WORD tokens */
} Token;

/*
 * tokenize - lex a single input line into an array of Tokens.
 *
 * Returns a heap-allocated array of *count Tokens.
 * The caller must free the array with free_tokens().
 * Returns NULL on allocation failure.
 */
Token *tokenize(const char *line, int *count);

/*
 * free_tokens - release all memory owned by a token array.
 */
void free_tokens(Token *tokens, int count);

#endif /* LEXER_H */
