CC      = gcc
CFLAGS  = -Wall -Wextra -g
TARGET  = mysh
SRCS    = main.c lexer.c parser.c executor.c builtins.c jobs.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Pattern rule: compile each .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Header dependencies (ensure recompilation on header changes)
main.o:     main.c     lexer.h parser.h executor.h jobs.h
lexer.o:    lexer.c    lexer.h
parser.o:   parser.c   parser.h lexer.h
executor.o: executor.c executor.h parser.h builtins.h jobs.h
builtins.o: builtins.c builtins.h jobs.h
jobs.o:     jobs.c     jobs.h

clean:
	rm -f $(OBJS) $(TARGET)
