
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

/* Store the last 20 commands */
static char* history[HISTORY_SIZE];
static int history_count = 0;

/* Add a command to history */
void add_to_history(const char* command)
{
    if (command == NULL || command[0] == '\0')
    {
        return;
    }

    /* Remove the oldest command when history is full */
    if (history_count == HISTORY_SIZE)
    {
        free(history[0]);

        for (int i = 1; i < HISTORY_SIZE; i++)
        {
            history[i - 1] = history[i];
        }

        history_count--;
    }

    history[history_count] = strdup(command);

    if (history[history_count] != NULL)
    {
        history_count++;
    }
}

/* Display stored commands */
void print_history(void)
{
    for (int i = 0; i < history_count; i++)
    {
        printf("%d  %s\n", i + 1, history[i]);
    }
}

/* Retrieve a command using its history number */
const char* get_history_command(int number)
{
    if (number < 1 || number > history_count)
    {
        return NULL;
    }

    return history[number - 1];
}

/* Read a command from the input stream */
char* read_cmd(char* prompt, FILE* fp)
{
    char* line = NULL;
    size_t size = 0;

    if (fp == stdin && prompt != NULL)
    {
        printf("%s", prompt);
        fflush(stdout);
    }

    if (getline(&line, &size, fp) == -1)
    {
        free(line);
        return NULL;
    }

    /* Remove trailing newline */
    line[strcspn(line, "\n")] = '\0';

    return line;
}

/* Add a token safely to the token array */
static int add_token(char*** tokens, int* count,
                     int* capacity, const char* value)
{
    if (*count >= *capacity - 1)
    {
        int new_capacity = (*capacity) * 2;

        char** temp = realloc(*tokens,
                              new_capacity * sizeof(char*));

        if (temp == NULL)
        {
            return 0;
        }

        *tokens = temp;
        *capacity = new_capacity;
    }

    (*tokens)[*count] = strdup(value);

    if ((*tokens)[*count] == NULL)
    {
        return 0;
    }

    (*count)++;

    return 1;
}

/* Tokenize the command and recognize <, > and | */
char** tokenize(char* cmdline)
{
    if (cmdline == NULL)
    {
        return NULL;
    }

    int capacity = 128;
    int count = 0;

    char** tokens = malloc(capacity * sizeof(char*));

    if (tokens == NULL)
    {
        perror("malloc");
        return NULL;
    }

    int i = 0;

    while (cmdline[i] != '\0')
    {
        /* Skip spaces, tabs and newlines */
        if (cmdline[i] == ' ' ||
            cmdline[i] == '\t' ||
            cmdline[i] == '\n')
        {
            i++;
            continue;
        }

        /* Recognize redirection and pipe operators */
        if (cmdline[i] == '<' ||
            cmdline[i] == '>' ||
            cmdline[i] == '|')
        {
            char operator_text[2];

            operator_text[0] = cmdline[i];
            operator_text[1] = '\0';

            if (!add_token(&tokens, &count,
                           &capacity, operator_text))
            {
                perror("token allocation");
                goto error;
            }

            i++;
        }
        else
        {
            char word[1024];
            int j = 0;
            char quote = '\0';

            while (cmdline[i] != '\0')
            {
                char ch = cmdline[i];

                /* Start a quoted string */
                if (quote == '\0' &&
                    (ch == '\'' || ch == '"'))
                {
                    quote = ch;
                    i++;
                    continue;
                }

                /* End a quoted string */
                if (quote != '\0' && ch == quote)
                {
                    quote = '\0';
                    i++;
                    continue;
                }

                /* Stop at whitespace or an operator */
                if (quote == '\0' &&
                    (ch == ' ' || ch == '\t' ||
                     ch == '\n' || ch == '<' ||
                     ch == '>' || ch == '|'))
                {
                    break;
                }

                if (j < (int)sizeof(word) - 1)
                {
                    word[j++] = ch;
                }

                i++;
            }

            if (quote != '\0')
            {
                fprintf(stderr, "myshell: unmatched quote\n");
                goto error;
            }

            word[j] = '\0';

            if (j > 0)
            {
                if (!add_token(&tokens, &count,
                               &capacity, word))
                {
                    perror("token allocation");
                    goto error;
                }
            }
        }
    }

    tokens[count] = NULL;

    return tokens;

error:
    for (int k = 0; k < count; k++)
    {
        free(tokens[k]);
    }

    free(tokens);

    return NULL;
}

/* Handle shell built-in commands */
int handle_builtin(char** arglist)
{
    if (arglist == NULL || arglist[0] == NULL)
    {
        return 0;
    }

    /* exit command */
    if (strcmp(arglist[0], "exit") == 0)
    {
        exit(0);
    }

    /* cd command */
    if (strcmp(arglist[0], "cd") == 0)
    {
        const char* directory;

        if (arglist[1] == NULL)
        {
            directory = getenv("HOME");

            if (directory == NULL)
            {
                fprintf(stderr, "myshell: HOME not set\n");
                return 1;
            }
        }
        else
        {
            directory = arglist[1];
        }

        if (chdir(directory) != 0)
        {
            perror("cd");
        }

        return 1;
    }

    /* help command */
    if (strcmp(arglist[0], "help") == 0)
    {
        printf("\nAvailable built-in commands:\n");
        printf("  exit       Exit the shell\n");
        printf("  cd         Change directory\n");
        printf("  help       Display this help message\n");
        printf("  jobs       Display job control status\n");
        printf("  history    Display command history\n\n");

        return 1;
    }

    /* jobs command */
    if (strcmp(arglist[0], "jobs") == 0)
    {
        printf("Job control not yet implemented.\n");
        return 1;
    }

    /* history command */
    if (strcmp(arglist[0], "history") == 0)
    {
        print_history();
        return 1;
    }

    /* Not a built-in command */
    return 0;
}
