#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <readline/readline.h>
#include <readline/history.h>

#define PROMPT "myshell> "
#define IF_PROMPT "if> "

/* Remove leading and trailing spaces. */

static char *trim(char *text)
{
    while (isspace((unsigned char)*text))
        text++;

    char *end = text + strlen(text);

    while (end > text &&
           isspace((unsigned char)end[-1]))
        *--end = '\0';

    return text;
}

/* Check whether a line is exactly a keyword. */

static int is_keyword(const char *line, const char *keyword)
{
    char *copy = strdup(line);

    if (copy == NULL)
        return 0;

    char *clean = trim(copy);
    int result = strcmp(clean, keyword) == 0;

    free(copy);
    return result;
}

/* Detect an ampersand at the end of a command. */

static int detect_background(char *command)
{
    char quote = '\0';
    char *last_non_space = NULL;

    for (char *p = command; *p != '\0'; p++)
    {
        if (quote != '\0')
        {
            if (*p == quote)
                quote = '\0';
        }
        else if (*p == '\'' || *p == '"')
        {
            quote = *p;
        }
        else if (!isspace((unsigned char)*p))
        {
            last_non_space = p;
        }
    }

    if (last_non_space != NULL && *last_non_space == '&')
    {
        *last_non_space = '\0';
        return 1;
    }

    return 0;
}

/* Execute one individual command. */

static int process_command(char *command)
{
    command = trim(command);

    if (*command == '\0')
        return 0;

    int background = detect_background(command);

    command = trim(command);

    if (*command == '\0')
        return 0;

    char **arglist = tokenize(command);

    if (arglist == NULL)
    {
        fprintf(stderr, "Unable to parse command\n");
        return 1;
    }

    if (arglist[0] == NULL)
    {
        free(arglist);
        return 0;
    }

    int status = 0;

    if (handle_builtin(arglist))
    {
        /*
         * Existing built-ins are handled by the shell.
         * Their successful execution is treated as status 0.
         */
        status = 0;
    }
    else
    {
        status = execute(arglist, background);
    }

    for (int i = 0; arglist[i] != NULL; i++)
        free(arglist[i]);

    free(arglist);

    return status;
}

/* Execute a line containing one or more ; separated commands. */

static int process_line(char *line)
{
    int count = 0;

    char **commands = split_commands(line, &count);

    if (commands == NULL)
    {
        fprintf(stderr, "Unable to split commands\n");
        return 1;
    }

    int status = 0;

    for (int i = 0; i < count; i++)
        status = process_command(commands[i]);

    free_commands(commands, count);

    return status;
}

/* Add a line to a dynamically growing block of commands. */

static int add_block_line(char ***block, int *count,
                          int *capacity, const char *line)
{
    if (*count == *capacity)
    {
        int new_capacity = (*capacity == 0) ? 4 : (*capacity * 2);

        char **temp = realloc(
            *block,
            new_capacity * sizeof(char *)
        );

        if (temp == NULL)
            return 0;

        *block = temp;
        *capacity = new_capacity;
    }

    (*block)[*count] = strdup(line);

    if ((*block)[*count] == NULL)
        return 0;

    (*count)++;

    return 1;
}

/* Free a block of stored command lines. */

static void free_block(char **block, int count)
{
    for (int i = 0; i < count; i++)
        free(block[i]);

    free(block);
}

/*
 * Read and execute a multi-line if-then-else-fi block.
 *
 * Example:
 *
 * if true
 * then
 *     echo Success
 * else
 *     echo Failure
 * fi
 */

static void process_if_statement(char *first_line)
{
    char **then_block = NULL;
    char **else_block = NULL;

    int then_count = 0;
    int else_count = 0;

    int then_capacity = 0;
    int else_capacity = 0;

    int in_then = 0;
    int in_else = 0;
    int found_fi = 0;

    char *condition = trim(first_line + 2);

    if (*condition == '\0')
    {
        fprintf(stderr, "Syntax error: missing if condition\n");
        return;
    }

    /*
     * Read the remaining lines until fi is found.
     */

    while (1)
    {
        char *line = readline(IF_PROMPT);

        if (line == NULL)
        {
            fprintf(stderr, "\nSyntax error: missing fi\n");
            break;
        }

        add_history(line);

        char *clean = trim(line);

        if (is_keyword(clean, "then"))
        {
            if (in_then || in_else)
            {
                fprintf(stderr, "Syntax error: unexpected then\n");
                free(line);
                break;
            }

            in_then = 1;
            free(line);
            continue;
        }

        if (is_keyword(clean, "else"))
        {
            if (!in_then || in_else)
            {
                fprintf(stderr, "Syntax error: unexpected else\n");
                free(line);
                break;
            }

            in_else = 1;
            free(line);
            continue;
        }

        if (is_keyword(clean, "fi"))
        {
            found_fi = 1;
            free(line);
            break;
        }

        if (!in_then)
        {
            fprintf(stderr, "Syntax error: expected then\n");
            free(line);
            break;
        }

        if (in_else)
        {
            if (!add_block_line(&else_block,
                                &else_count,
                                &else_capacity,
                                clean))
            {
                fprintf(stderr, "Memory allocation error\n");
                free(line);
                break;
            }
        }
        else
        {
            if (!add_block_line(&then_block,
                                &then_count,
                                &then_capacity,
                                clean))
            {
                fprintf(stderr, "Memory allocation error\n");
                free(line);
                break;
            }
        }

        free(line);
    }

    if (!found_fi || !in_then)
    {
        fprintf(stderr, "Invalid if statement\n");
        free_block(then_block, then_count);
        free_block(else_block, else_count);
        return;
    }

    /*
     * Execute the condition.
     *
     * Exit status 0 means success.
     * Non-zero means failure.
     */

    int condition_status = process_line(condition);

    if (condition_status == 0)
    {
        printf("Condition successful. Executing then block.\n");

        for (int i = 0; i < then_count; i++)
            process_line(then_block[i]);
    }
    else
    {
        printf("Condition failed. Executing else block.\n");

        for (int i = 0; i < else_count; i++)
            process_line(else_block[i]);
    }

    free_block(then_block, then_count);
    free_block(else_block, else_count);
}

/* Main shell loop. */

int main(void)
{
    using_history();

    while (1)
    {
        /* Reap completed background jobs. */

        reap_background_jobs();

        char *input = readline(PROMPT);

        if (input == NULL)
        {
            printf("\nExiting myshell...\n");
            break;
        }

        if (input[0] == '\0')
        {
            free(input);
            continue;
        }

        /* Handle !n history execution. */

        if (input[0] == '!' &&
            isdigit((unsigned char)input[1]))
        {
            int number = atoi(input + 1);

            const char *previous = get_history_command(number);

            if (previous == NULL)
            {
                printf("No such command in history.\n");
                free(input);
                continue;
            }

            char *replacement = strdup(previous);

            if (replacement == NULL)
            {
                perror("strdup");
                free(input);
                continue;
            }

            free(input);
            input = replacement;

            printf("%s\n", input);
        }

        add_history(input);
        add_to_history(input);

        char *clean = trim(input);

        /*
         * If the command begins with if,
         * read and process a multi-line block.
         */

        if (strncmp(clean, "if ", 3) == 0)
        {
            process_if_statement(clean);
        }
        else if (strcmp(clean, "if") == 0)
        {
            process_if_statement(clean);
        }
        else
        {
            process_line(clean);
        }

        free(input);
    }

    return 0;
}
