
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

#define PROMPT "myshell> "
#define IF_PROMPT "if> "

/* ---------------- STRING HELPERS ---------------- */

static char *trim(char *text)
{
    while (isspace((unsigned char)*text))
        text++;

    char *end = text + strlen(text);

    while (end > text && isspace((unsigned char)end[-1]))
        end--;

    *end = '\0';

    return text;
}

static int is_keyword(char *line, const char *keyword)
{
    char *copy = strdup(line);

    if (copy == NULL)
        return 0;

    char *clean = trim(copy);

    int result = (strcmp(clean, keyword) == 0);

    free(copy);

    return result;
}

/* ---------------- BACKGROUND COMMAND CHECK ---------------- */

static int detect_background(char *command)
{
    int length = strlen(command);
    char quote = '\0';

    for (int i = 0; i < length; i++) {

        if (command[i] == '\'' || command[i] == '"') {

            if (quote == '\0')
                quote = command[i];

            else if (quote == command[i])
                quote = '\0';
        }
    }

    if (quote != '\0')
        return 0;

    int end = length - 1;

    while (end >= 0 &&
           isspace((unsigned char)command[end]))
        end--;

    if (end >= 0 && command[end] == '&') {

        command[end] = '\0';

        return 1;
    }

    return 0;
}

/* ---------------- EXECUTE ONE COMMAND ---------------- */

static int process_command(char *command)
{
    command = trim(command);

    if (command[0] == '\0')
        return 0;

    int background = detect_background(command);

    char **arglist = tokenize(command);

    if (arglist == NULL)
        return 1;

    if (arglist[0] == NULL) {
        free(arglist);
        return 0;
    }

    /* Check variable assignment before execution. */

    if (handle_assignment(arglist)) {

        for (int i = 0; arglist[i] != NULL; i++)
            free(arglist[i]);

        free(arglist);

        return 0;
    }

    /* Expand variables before executing commands. */

    expand_variables(arglist);

    /* Check built-in commands. */

    if (handle_builtin(arglist)) {

        for (int i = 0; arglist[i] != NULL; i++)
            free(arglist[i]);

        free(arglist);

        return 0;
    }

    /* Execute external commands. */

    int status = execute(arglist, background);

    for (int i = 0; arglist[i] != NULL; i++)
        free(arglist[i]);

    free(arglist);

    return status;
}

/* ---------------- COMMAND CHAINING ---------------- */

static int process_line(char *line)
{
    int count = 0;

    char **commands = split_commands(line, &count);

    if (commands == NULL)
        return 1;

    int status = 0;

    for (int i = 0; i < count; i++) {
        status = process_command(commands[i]);
    }

    free_commands(commands, count);

    return status;
}

/* ---------------- IF-THEN-ELSE-FI ---------------- */

static void add_block_line(
    char ***block,
    int *count,
    int *capacity,
    const char *line)
{
    if (*count >= *capacity) {

        int new_capacity = (*capacity) * 2;

        char **temp = realloc(
            *block,
            new_capacity * sizeof(char *)
        );

        if (temp == NULL) {
            printf("Memory allocation error\n");
            return;
        }

        *block = temp;
        *capacity = new_capacity;
    }

    (*block)[*count] = strdup(line);

    if ((*block)[*count] != NULL)
        (*count)++;
}

static void free_block(char **block, int count)
{
    if (block == NULL)
        return;

    for (int i = 0; i < count; i++)
        free(block[i]);

    free(block);
}

static void process_if_statement(char *first_line)
{
    char *condition = trim(first_line + 2);

    if (condition[0] == '\0') {
        printf("Syntax error: missing condition\n");
        return;
    }

    /* Separate capacities for the two blocks. */

    int then_capacity = 8;
    int else_capacity = 8;

    int then_count = 0;
    int else_count = 0;

    char **then_block = malloc(
        then_capacity * sizeof(char *)
    );

    char **else_block = malloc(
        else_capacity * sizeof(char *)
    );

    if (then_block == NULL || else_block == NULL) {

        printf("Memory allocation error\n");

        free(then_block);
        free(else_block);

        return;
    }

    int in_then = 0;
    int in_else = 0;
    int found_fi = 0;
    int syntax_error = 0;

    /* Read lines until fi is entered. */

    while (1) {

        char *line = readline(IF_PROMPT);

        if (line == NULL)
            break;

        char *clean = trim(line);

        if (is_keyword(clean, "then")) {

            if (in_then || in_else) {

                syntax_error = 1;

                free(line);
                break;
            }

            in_then = 1;
        }

        else if (is_keyword(clean, "else")) {

            if (!in_then || in_else) {

                syntax_error = 1;

                free(line);
                break;
            }

            in_then = 0;
            in_else = 1;
        }

        else if (is_keyword(clean, "fi")) {

            found_fi = 1;

            free(line);
            break;
        }

        else if (in_then) {

            add_block_line(
                &then_block,
                &then_count,
                &then_capacity,
                clean
            );
        }

        else if (in_else) {

            add_block_line(
                &else_block,
                &else_count,
                &else_capacity,
                clean
            );
        }

        else {

            printf("Syntax error: expected then\n");

            syntax_error = 1;

            free(line);
            break;
        }

        free(line);
    }

    /* Validate the complete if statement. */

    if (!found_fi ||
        (!in_then && !in_else) ||
        syntax_error) {

        printf("Invalid if statement\n");

        free_block(then_block, then_count);
        free_block(else_block, else_count);

        return;
    }

    /* Execute condition and check its exit status. */

    int status = process_line(condition);

    if (status == 0) {

        /* Condition successful: execute then block. */

        for (int i = 0; i < then_count; i++) {
            process_line(then_block[i]);
        }
    }

    else {

        /* Condition unsuccessful: execute else block. */

        for (int i = 0; i < else_count; i++) {
            process_line(else_block[i]);
        }
    }

    free_block(then_block, then_count);
    free_block(else_block, else_count);
}

/* ---------------- MAIN FUNCTION ---------------- */

int main(void)
{
    using_history();

    while (1) {

        /* Reap completed background jobs. */

        reap_background_jobs();

        char *line = readline(PROMPT);

        if (line == NULL) {
            printf("\n");
            break;
        }

        char *clean = trim(line);

        if (clean[0] == '\0') {
            free(line);
            continue;
        }

        /* Handle !n history command. */

        if (clean[0] == '!' &&
            isdigit((unsigned char)clean[1])) {

            int number = atoi(clean + 1);

            const char *old_command =
                get_history_command(number);

            if (old_command == NULL) {

                printf("No such command in history\n");

                free(line);
                continue;
            }

            char *copy = strdup(old_command);

            if (copy == NULL) {
                free(line);
                continue;
            }

            printf("%s\n", copy);

            add_history(copy);
            add_to_history(copy);

            if (strncmp(copy, "if ", 3) == 0 ||
                strcmp(copy, "if") == 0) {

                process_if_statement(copy);
            }

            else {
                process_line(copy);
            }

            free(copy);
            free(line);

            continue;
        }

        /* Save command in both history systems. */

        add_history(clean);
        add_to_history(clean);

        /* Check whether the command starts an if statement. */

        if (strncmp(clean, "if ", 3) == 0 ||
            strcmp(clean, "if") == 0) {

            process_if_statement(clean);
        }

        else {

            process_line(clean);
        }

        free(line);
    }

    return 0;
}
