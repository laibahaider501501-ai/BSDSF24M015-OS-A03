CC = gcc

CFLAGS = -Wall -Wextra -g -Iinclude

SRC = src/main.c src/shell.c src/execute.c

OBJ = obj/main.o obj/shell.o obj/execute.o

TARGET = bin/myshell

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

obj/%.o: src/%.c include/shell.h
	mkdir -p obj
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): | bin

bin:
	mkdir -p bin

clean:
	rm -rf obj bin

.PHONY: all clean
