# mysh — A UNIX Shell in C

A minimal but fully functional POSIX-style shell written in C, built as a Computer Architecture final project.

---

## Project Structure

```
CompArchitecture-FinalProject/
└── myshell/
    ├── main.c        — main loop: prompt → read → lex → parse → execute
    ├── lexer.c/h     — tokenizer: splits input into tokens
    ├── parser.c/h    — parser: builds a Pipeline from tokens
    ├── executor.c/h  — executor: fork, exec, pipes, redirects, glob expansion
    ├── builtins.c/h  — built-in commands: cd, exit, jobs, fg, bg
    ├── jobs.c/h      — background job list, SIGCHLD handler
    ├── Makefile      — build rules
    └── tests/
        └── run_tests.sh  — automated test suite
```

---

## Build

```bash
cd myshell
make
```

Requires `gcc` and standard POSIX headers. Tested on macOS and Linux (WSL).

To clean build artifacts:

```bash
make clean
```

---

## Run

### Interactive mode

```bash
./mysh
```

The prompt `mysh> ` will appear. Type commands and press Enter.

To exit:
```
mysh> exit
```
or press `Ctrl+D` on an empty line.

### Non-interactive (script / pipe)

```bash
echo 'ls | grep .c' | ./mysh
```

---

## Features

| Feature | Example |
|---|---|
| External commands | `ls -la`, `grep foo file.txt` |
| Pipes | `ls \| grep .c \| wc -l` |
| Input redirect | `sort < input.txt` |
| Output redirect | `ls > out.txt` |
| Append redirect | `echo hi >> log.txt` |
| Stderr redirect | `cmd 2> err.txt` |
| Background jobs | `sleep 10 &` |
| Glob expansion | `cat *.c`, `ls file?.txt` |
| Built-ins | `cd`, `exit`, `jobs`, `fg`, `bg` |

---

## Built-in Commands

| Command | Description |
|---|---|
| `cd [dir]` | Change directory (defaults to `$HOME`) |
| `exit [code]` | Exit the shell with optional status code |
| `jobs` | List background jobs |
| `fg [%n]` | Bring job to foreground |
| `bg [%n]` | Resume stopped job in background |

---

## Architecture

The shell follows a classic pipeline architecture:

```
Input string
    └─► Lexer (lexer.c)      — tokenize raw input
            └─► Parser (parser.c)   — build Command/Pipeline structs
                    └─► Executor (executor.c) — fork, execvp, dup2, waitpid
```

Key system calls used: `fork`, `execvp`, `pipe`, `dup2`, `waitpid`, `setpgid`, `tcsetpgrp`, `sigaction`.

---

## Running Tests

```bash
cd myshell
bash tests/run_tests.sh
```
