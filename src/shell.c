
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/wait.h>
#include <errno.h>

/* ---------------- HISTORY ---------------- */

static char *history[HISTORY_SIZE];
static int history_count = 0;

void add_to_history(const char *command)
{
    if (command == NULL || command[0] == '\0')
        return;

    if (history_count == HISTORY_SIZE) {
        free(history[0]);

        for (int i = 1; i < HISTORY_SIZE; i++)
            history[i - 1] = history[i];

        history_count--;
    }

    history[history_count] = strdup(command);

    if (history[history_count] != NULL)
        history_count++;
}

void print_history(void)
{
    for (int i = 0; i < history_count; i++)
        printf("%d  %s\n", i + 1, history[i]);
}

const char *get_history_command(int number)
{
    if (number < 1 || number > history_count)
        return NULL;

    return history[number - 1];
}

/* ---------------- READ COMMAND ---------------- */

char *read_cmd(char *prompt, FILE *fp)
{
    char *line = NULL;
    size_t size = 0;

    if (fp == stdin)
        printf("%s", prompt);

    if (getline(&line, &size, fp) == -1) {
        free(line);
        return NULL;
    }

    line[strcspn(line, "\n")] = '\0';

    return line;
}

/* ---------------- TOKENIZER ---------------- */

static void add_token(char ***tokens, int *count,
                      int *capacity, const char *value)
{
    if (*count + 1 >= *capacity) {
        *capacity *= 2;

        char **temp = realloc(*tokens,
                              *capacity * sizeof(char *));

        if (temp == NULL)
            return;

        *tokens = temp;
    }

    (*tokens)[*count] = strdup(value);

    if ((*tokens)[*count] != NULL)
        (*count)++;
}

char **tokenize(char *cmdline)
{
    int capacity = 16;
    int count = 0;

    char **tokens = malloc(capacity * sizeof(char *));

    if (tokens == NULL)
        return NULL;

    int i = 0;

    while (cmdline[i] != '\0') {

        while (isspace((unsigned char)cmdline[i]))
            i++;

        if (cmdline[i] == '\0')
            break;

        if (cmdline[i] == '<' ||
            cmdline[i] == '>' ||
            cmdline[i] == '|') {

            char special[2] = {cmdline[i], '\0'};
            add_token(&tokens, &count, &capacity, special);
            i++;
            continue;
        }

        char buffer[4096];
        int j = 0;
        char quote = '\0';

        while (cmdline[i] != '\0') {

            char ch = cmdline[i];

            if (quote == '\0' && isspace((unsigned char)ch))
                break;

            if (quote == '\0' && (ch == '\'' || ch == '"')) {
                quote = ch;
                i++;
                continue;
            }

            if (quote != '\0' && ch == quote) {
                quote = '\0';
                i++;
                continue;
            }

            if (quote == '\0' &&
                (ch == '<' || ch == '>' || ch == '|'))
                break;

            if (j < (int)sizeof(buffer) - 1)
                buffer[j++] = ch;

            i++;
        }

        if (quote != '\0') {
            printf("Syntax error: unmatched quote\n");

            for (int k = 0; k < count; k++)
                free(tokens[k]);

            free(tokens);
            return NULL;
        }

        buffer[j] = '\0';

        if (j > 0)
            add_token(&tokens, &count, &capacity, buffer);

        if (cmdline[i] == '<' ||
            cmdline[i] == '>' ||
            cmdline[i] == '|') {

            char special[2] = {cmdline[i], '\0'};
            add_token(&tokens, &count, &capacity, special);
            i++;
        }
    }

    tokens[count] = NULL;
    return tokens;
}

/* ---------------- VARIABLE STORAGE ---------------- */

typedef struct Variable {
    char *name;
    char *value;
    struct Variable *next;
} Variable;

static Variable *variables = NULL;

static int valid_variable_name(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return 0;

    if (!isalpha((unsigned char)name[0]) && name[0] != '_')
        return 0;

    for (int i = 1; name[i] != '\0'; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_')
            return 0;
    }

    return 1;
}

static const char *get_variable(const char *name)
{
    Variable *current = variables;

    while (current != NULL) {
        if (strcmp(current->name, name) == 0)
            return current->value;

        current = current->next;
    }

    return NULL;
}

static int store_variable(const char *name, const char *value)
{
    Variable *current = variables;

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            char *new_value = strdup(value);

            if (new_value == NULL)
                return 0;

            free(current->value);
            current->value = new_value;
            return 1;
        }

        current = current->next;
    }

    Variable *new_variable = malloc(sizeof(Variable));

    if (new_variable == NULL)
        return 0;

    new_variable->name = strdup(name);
    new_variable->value = strdup(value);

    if (new_variable->name == NULL ||
        new_variable->value == NULL) {

        free(new_variable->name);
        free(new_variable->value);
        free(new_variable);
        return 0;
    }

    new_variable->next = variables;
    variables = new_variable;

    return 1;
}

/* Handle NAME=value without forking. */

int handle_assignment(char **arglist)
{
    if (arglist == NULL || arglist[0] == NULL)
        return 0;

    /* An assignment must be the only token. */

    if (arglist[1] != NULL)
        return 0;

    char *equals = strchr(arglist[0], '=');

    if (equals == NULL)
        return 0;

    int name_length = (int)(equals - arglist[0]);

    char name[256];

    if (name_length <= 0 ||
        name_length >= (int)sizeof(name)) {
        printf("Invalid variable name\n");
        return 1;
    }

    strncpy(name, arglist[0], name_length);
    name[name_length] = '\0';

    if (!valid_variable_name(name)) {
        printf("Invalid variable name\n");
        return 1;
    }

    const char *value = equals + 1;

    if (!store_variable(name, value))
        printf("Error: could not store variable\n");

    return 1;
}

/* Expand arguments beginning with $. */

void expand_variables(char **arglist)
{
    if (arglist == NULL)
        return;

    for (int i = 0; arglist[i] != NULL; i++) {

        if (arglist[i][0] != '$')
            continue;

        const char *name = arglist[i] + 1;

        if (!valid_variable_name(name))
            continue;

        const char *value = get_variable(name);

        /* Undefined variables expand to an empty string. */

        if (value == NULL)
            value = "";

        char *expanded = strdup(value);

        if (expanded == NULL) {
            printf("Error: memory allocation failed\n");
            continue;
        }

        free(arglist[i]);
        arglist[i] = expanded;
    }
}

/* Display all stored variables. */

static void print_variables(void)
{
    Variable *current = variables;

    if (current == NULL) {
        printf("No variables defined.\n");
        return;
    }

    while (current != NULL) {
        printf("%s=%s\n", current->name, current->value);
        current = current->next;
    }
}

/* ---------------- COMMAND SPLITTING ---------------- */

char **split_commands(char *line, int *count)
{
    int capacity = 8;
    *count = 0;

    char **commands = malloc(capacity * sizeof(char *));

    if (commands == NULL)
        return NULL;

    char buffer[4096];
    int j = 0;
    char quote = '\0';

    for (int i = 0; ; i++) {
        char ch = line[i];

        if (ch == '\'' || ch == '"') {
            if (quote == '\0')
                quote = ch;
            else if (quote == ch)
                quote = '\0';
        }

        if ((ch == ';' && quote == '\0') || ch == '\0') {

            buffer[j] = '\0';

            char *start = buffer;

            while (isspace((unsigned char)*start))
                start++;

            char *end = start + strlen(start);

            while (end > start &&
                   isspace((unsigned char)end[-1]))
                end--;

            *end = '\0';

            if (*start != '\0') {

                if (*count + 1 >= capacity) {
                    capacity *= 2;

                    char **temp = realloc(
                        commands, capacity * sizeof(char *));

                    if (temp == NULL) {
                        free_commands(commands, *count);
                        return NULL;
                    }

                    commands = temp;
                }

                commands[*count] = strdup(start);

                if (commands[*count] == NULL) {
                    free_commands(commands, *count);
                    return NULL;
                }

                (*count)++;
            }

            j = 0;

            if (ch == '\0')
                break;

        } else if (j < (int)sizeof(buffer) - 1) {
            buffer[j++] = ch;
        }
    }

    return commands;
}

void free_commands(char **commands, int count)
{
    if (commands == NULL)
        return;

    for (int i = 0; i < count; i++)
        free(commands[i]);

    free(commands);
}

/* ---------------- BACKGROUND JOBS ---------------- */

typedef struct {
    pid_t pid;
    char command[256];
} Job;

static Job jobs[MAX_JOBS];
static int job_count = 0;

void add_job(pid_t pid, const char *command)
{
    if (job_count >= MAX_JOBS) {
        printf("Maximum number of jobs reached\n");
        return;
    }

    jobs[job_count].pid = pid;

    snprintf(jobs[job_count].command,
             sizeof(jobs[job_count].command),
             "%s", command);

    job_count++;

    printf("[%d] %d\n", job_count, pid);
}

void reap_background_jobs(void)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

        for (int i = 0; i < job_count; i++) {

            if (jobs[i].pid == pid) {
                printf("\n[%d] Done  %s\n",
                       i + 1, jobs[i].command);

                for (int j = i; j < job_count - 1; j++)
                    jobs[j] = jobs[j + 1];

                job_count--;
                break;
            }
        }
    }
}

void print_jobs(void)
{
    reap_background_jobs();

    if (job_count == 0) {
        printf("No background jobs.\n");
        return;
    }

    for (int i = 0; i < job_count; i++) {
        printf("[%d] Running  %d  %s\n",
               i + 1, jobs[i].pid, jobs[i].command);
    }
}

/* ---------------- BUILT-IN COMMANDS ---------------- */

int handle_builtin(char **arglist)
{
    if (arglist == NULL || arglist[0] == NULL)
        return 1;

    if (strcmp(arglist[0], "exit") == 0) {
        exit(0);
    }

    if (strcmp(arglist[0], "cd") == 0) {
        const char *directory = arglist[1];

        if (directory == NULL)
            directory = getenv("HOME");

        if (directory == NULL || chdir(directory) != 0)
            perror("cd");

        return 1;
    }

    if (strcmp(arglist[0], "help") == 0) {
        printf("Available commands:\n");
        printf("exit, cd, help, jobs, history, set\n");
        printf("You can also use variables: NAME=value and $NAME\n");
        return 1;
    }

    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        return 1;
    }

    if (strcmp(arglist[0], "set") == 0) {
        print_variables();
        return 1;
    }

    return 0;
}
