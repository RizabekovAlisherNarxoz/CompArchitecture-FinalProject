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


1. [Структура проекта](#структура-проекта)
2. [Как собрать](#как-собрать)
3. [Как запустить](#как-запустить)
4. [Что умеет шелл](#что-умеет-шелл)
5. [Встроенные команды](#встроенные-команды)
6. [Примеры использования](#примеры-использования)
7. [Автотесты](#автотесты)
8. [Архитектура](#архитектура)
9. [AI-инструменты](#ai-инструменты)
10. [Известные ограничения](#известные-ограничения)

---

## Структура проекта

```
CompArchitecture-FinalProject/
└── myshell/
    ├── main.c        — главный цикл шелла (prompt → read → lex → parse → execute)
    ├── lexer.c/h     — лексер: разбивает строку на токены
    ├── parser.c/h    — парсер: строит Pipeline из токенов
    ├── executor.c/h  — исполнитель: fork, exec, пайпы, редиректы
    ├── builtins.c/h  — встроенные команды: cd, exit, jobs, fg, bg
    ├── jobs.c/h      — список фоновых задач, SIGCHLD-обработчик
    ├── Makefile      — правила сборки
    ├── README.md     — документация (английский)
    └── tests/
        └── run_tests.sh  — автоматические тесты
```

---

## Как собрать

```bash
cd myshell
make
```

После этого появится исполняемый файл `mysh`.

Чтобы удалить скомпилированные файлы:

```bash
make clean
```

**Требования:** компилятор `gcc`, стандартные POSIX-заголовки (есть по умолчанию на macOS и Linux).

---

## Как запустить

### Интерактивный режим

```bash
./mysh
```

Появится приглашение `mysh> `. Вводи команды и нажимай Enter.

Чтобы выйти — любой из трёх способов:
```
mysh> exit
mysh> exit 0
# или нажать Ctrl+D на пустой строке
```

### Неинтерактивный режим (скрипт / пайп)

```bash
echo 'ls | grep .c' | ./mysh
./mysh < myscript.sh
```

---

## Что умеет шелл

| Возможность | Пример |
|---|---|
| Внешние команды | `ls -la`, `grep`, `cat` |
| Пайпы (любое количество) | `ls \| sort \| head -5` |
| Redirect stdin | `cat < file.txt` |
| Redirect stdout (перезапись) | `echo hi > out.txt` |
| Redirect stdout (дозапись) | `echo hi >> out.txt` |
| Redirect stderr | `ls /bad 2> err.txt` |
| Фоновые задачи | `sleep 10 &` |
| Одинарные кавычки | `echo 'hello world'` |
| Двойные кавычки | `echo "hello \"world\""` |
| Ctrl-C | убивает только текущую команду, шелл остаётся |
| Ctrl-Z | останавливает текущую команду, можно продолжить через `fg`/`bg` |

---

## Встроенные команды

Эти команды выполняются **без fork** — прямо в процессе шелла:

| Команда | Описание |
|---|---|
| `cd [dir]` | Перейти в директорию. Без аргумента — переход в `$HOME` |
| `exit [код]` | Выйти из шелла с указанным кодом (по умолчанию 0) |
| `jobs` | Показать список фоновых/остановленных задач |
| `fg [id]` | Перевести задачу на передний план |
| `bg [id]` | Продолжить остановленную задачу в фоне |

---

## Примеры использования

### Базовые команды
```
mysh> echo hello world
hello world

mysh> ls -la | head -3
```

### Пайп из трёх команд
```
mysh> printf 'c\nb\na\n' | sort | head -2
a
b
```

### Редиректы
```
mysh> echo test > /tmp/out.txt
mysh> cat < /tmp/out.txt
test
mysh> echo line2 >> /tmp/out.txt
mysh> cat /tmp/out.txt
test
line2
```

### Фоновые задачи
```
mysh> sleep 10 &
[1] 12345
mysh> sleep 20 &
[2] 12346
mysh> jobs
[1] Running    sleep 10 &
[2] Running    sleep 20 &
mysh> fg 1         # выводит sleep 10 на передний план
# Ctrl-Z          # останавливаем
mysh> bg 1         # продолжаем в фоне
mysh> jobs
[1] Running    sleep 10 &
```

### cd
```
mysh> cd /tmp
mysh> pwd
/private/tmp
mysh> cd             # → $HOME
mysh> pwd
/Users/alisherrizabekov
```

---

## Автотесты

```bash
cd myshell
bash tests/run_tests.sh
```

Ожидаемый вывод:
```
=== mysh test suite ===
...
==================================
  Results: 20 passed, 0 failed, 0 skipped
==================================
```

Тесты покрывают: базовые команды, пайпы, все виды редиректов, встроенные команды `cd` и `exit`, обработку ошибок, фоновые задачи.

---

## Архитектура

Шелл работает по классической схеме **Lex → Parse → Execute**:

```
Строка ввода
      │
      ▼
  [ lexer.c ]   →   Token[]
      │
      ▼
  [ parser.c ]  →   Pipeline { Command[] }
      │
      ▼
  [ executor.c ]
      ├── встроенная команда? → builtins.c (без fork)
      ├── одна внешняя команда → fork → execvp → waitpid
      └── пайп → N-1 pipe() + N fork() + dup2() + waitpid
```

**Управление сигналами:**
- Шелл игнорирует `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`
- Каждый дочерний процесс сразу после `fork()` вызывает `setpgid(0,0)` и сбрасывает сигналы в `SIG_DFL`
- `SIGCHLD`-обработчик автоматически пожинает зомби и обновляет статусы задач

**Терминал:**
- `tcsetpgrp()` передаёт управление терминалом активной группе процессов
- После завершения foreground-задачи терминал возвращается шеллу

---

## AI-инструменты

- **GitHub Copilot** — inline-автодополнение во время написания кода
- **Claude Sonnet 4.6** (через GitHub Copilot Chat) — генерация файлов, архитектурные решения, review сигнальной безопасности, написание тестов

---

## Известные ограничения

| Ограничение | Примечание |
|---|---|
| Нет `$?` и переменных | Подстановка переменных не реализована |
| Нет `&&` / `\|\|` | Условные цепочки команд не поддерживаются |
| Нет `$(cmd)` | Подстановка вывода команды не реализована |
| Нет glob (`*.c`) | Маски файлов передаются в `execvp` как есть |
| Нет истории команд | Стрелки вверх/вниз не работают (нет readline) |
| Нет Tab-completion | По той же причине |
