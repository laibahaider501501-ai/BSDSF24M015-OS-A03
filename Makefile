CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude
LDLIBS = -lreadline

SRC = src/main.c src/shell.c src/execute.c
OBJ = obj/main.o obj/shell.o obj/execute.o

TARGET = bin/myshell

all: $(TARGET)

$(TARGET): $(OBJ) | bin
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDLIBS)

obj/%.o: src/%.c include/shell.h
	mkdir -p obj
	$(CC) $(CFLAGS) -c $< -o $@

bin:
	mkdir -p bin

clean:
	rm -rf obj bin

.PHONY: all clean
