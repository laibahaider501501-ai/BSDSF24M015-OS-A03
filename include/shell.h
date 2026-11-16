
#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

// Constants
#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30
#define HISTORY_SIZE 20
#define PROMPT "FCIT> "

// Function prototypes
char* read_cmd(char* prompt, FILE* fp);
char** tokenize(char* cmdline);
int execute(char** arglist);

// Built-in command handler
int handle_builtin(char** arglist);

// History functions
void add_to_history(const char* command);
void print_history(void);
const char* get_history_command(int number);

#endif
