#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <readline/readline.h>
#include <readline/history.h>

#define PROMPT "myshell> "

/* Remove leading and trailing whitespace. */

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

/*
 * Detect an ampersand at the end of a command.
 * The ampersand is removed before tokenization.
 */

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

/* Execute one command from the input line. */

static void process_command(char *command)
{
    command = trim(command);

    if (*command == '\0')
        return;

    int background = detect_background(command);

    command = trim(command);

    if (*command == '\0')
        return;

    char **arglist = tokenize(command);

    if (arglist == NULL)
    {
        fprintf(stderr, "Unable to parse command\n");
        return;
    }

    if (arglist[0] == NULL)
    {
        free(arglist);
        return;
    }

    /*
     * Built-ins run inside the shell process.
     * External commands use execute().
     */

    if (!handle_builtin(arglist))
        execute(arglist, background);

    for (int i = 0; arglist[i] != NULL; i++)
        free(arglist[i]);

    free(arglist);
}

int main(void)
{
    using_history();

    while (1)
    {
        /*
         * Reap completed background processes before
         * displaying the next prompt.
         */

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

        /* Handle history re-execution: !n */

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

        /*
         * Split the input into commands separated by ;
         * Commands are then executed one by one.
         */

        int count = 0;
        char **commands = split_commands(input, &count);

        if (commands == NULL)
        {
            fprintf(stderr, "Unable to split commands\n");
            free(input);
            continue;
        }

        for (int i = 0; i < count; i++)
        {
            process_command(commands[i]);
        }

        free_commands(commands, count);
        free(input);
    }

    return 0;
}
