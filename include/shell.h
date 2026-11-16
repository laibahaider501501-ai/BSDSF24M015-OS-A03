#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <sys/types.h>

#define HISTORY_SIZE 20
#define MAX_JOBS 100

/* Command input and parsing */
char *read_cmd(char *prompt, FILE *fp);
char **tokenize(char *cmdline);

/* Command history */
void add_to_history(const char *command);
void print_history(void);
const char *get_history_command(int number);

/* Built-in commands */
int handle_builtin(char **arglist);

/* Command chaining */
char **split_commands(char *line, int *count);
void free_commands(char **commands, int count);

/* Background jobs */
void add_job(pid_t pid, const char *command);
void reap_background_jobs(void);
void print_jobs(void);

/* Command execution */
void execute(char **arglist, int background);

#endif
