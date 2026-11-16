#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

static void execute_command(char **args)
{
    int input_fd = -1;
    int output_fd = -1;

    char **clean_args = calloc(1024, sizeof(char *));

    if (clean_args == NULL)
    {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    int count = 0;

    /* Remove redirection operators and their filenames. */

    for (int i = 0; args[i] != NULL; i++)
    {
        if (strcmp(args[i], "<") == 0)
        {
            if (args[i + 1] == NULL)
            {
                fprintf(stderr, "Missing input filename\n");
                exit(EXIT_FAILURE);
            }

            if (input_fd != -1)
                close(input_fd);

            input_fd = open(args[++i], O_RDONLY);

            if (input_fd < 0)
            {
                perror("open input");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(args[i], ">") == 0)
        {
            if (args[i + 1] == NULL)
            {
                fprintf(stderr, "Missing output filename\n");
                exit(EXIT_FAILURE);
            }

            if (output_fd != -1)
                close(output_fd);

            output_fd = open(args[++i],
                             O_WRONLY | O_CREAT | O_TRUNC,
                             0644);

            if (output_fd < 0)
            {
                perror("open output");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            if (count >= 1023)
            {
                fprintf(stderr, "Too many command arguments\n");
                exit(EXIT_FAILURE);
            }

            clean_args[count++] = args[i];
        }
    }

    if (input_fd != -1)
    {
        if (dup2(input_fd, STDIN_FILENO) < 0)
        {
            perror("dup2 input");
            exit(EXIT_FAILURE);
        }

        close(input_fd);
    }

    if (output_fd != -1)
    {
        if (dup2(output_fd, STDOUT_FILENO) < 0)
        {
            perror("dup2 output");
            exit(EXIT_FAILURE);
        }

        close(output_fd);
    }

    if (clean_args[0] == NULL)
        exit(EXIT_SUCCESS);

    execvp(clean_args[0], clean_args);

    perror("Command execution failed");
    exit(EXIT_FAILURE);
}

static void execute_pipeline(char **args)
{
    char **commands[100];
    int command_count = 1;

    commands[0] = args;

    /* Split the arguments at every pipe operator. */

    for (int i = 0; args[i] != NULL; i++)
    {
        if (strcmp(args[i], "|") == 0)
        {
            if (command_count >= 100 ||
                args[i + 1] == NULL ||
                args[i - 1] == NULL)
            {
                fprintf(stderr, "Invalid pipe command\n");
                return;
            }

            args[i] = NULL;
            commands[command_count++] = &args[i + 1];
        }
    }

    int pipes[99][2];
    pid_t pids[100];

    for (int i = 0; i < command_count - 1; i++)
    {
        if (pipe(pipes[i]) < 0)
        {
            perror("pipe");
            return;
        }
    }

    int started = 0;

    for (int i = 0; i < command_count; i++)
    {
        pids[i] = fork();

        if (pids[i] < 0)
        {
            perror("fork");
            break;
        }

        if (pids[i] == 0)
        {
            if (i > 0)
            {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0)
                {
                    perror("dup2 input pipe");
                    exit(EXIT_FAILURE);
                }
            }

            if (i < command_count - 1)
            {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0)
                {
                    perror("dup2 output pipe");
                    exit(EXIT_FAILURE);
                }
            }

            for (int j = 0; j < command_count - 1; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execute_command(commands[i]);
        }

        started++;
    }

    /* Close all pipe descriptors in the parent. */

    for (int i = 0; i < command_count - 1; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* Wait for the processes that were successfully started. */

    for (int i = 0; i < started; i++)
        waitpid(pids[i], NULL, 0);
}

void execute(char **arglist, int background)
{
    if (arglist == NULL || arglist[0] == NULL)
        return;

    if (background)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("fork");
            return;
        }

        if (pid == 0)
        {
            execute_pipeline(arglist);
            exit(EXIT_SUCCESS);
        }

        add_job(pid, arglist[0]);
        return;
    }

    execute_pipeline(arglist);
}
