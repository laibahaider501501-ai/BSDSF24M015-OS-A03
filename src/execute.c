#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Convert a wait status into the command's exit code. */

static int get_exit_code(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

    return 1;
}

/* Execute one command, including input/output redirection. */

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
                fprintf(stderr, "Too many arguments\n");
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

/*
 * Execute commands connected by pipes.
 * Return the exit status of the final command in the pipeline.
 */

static int execute_pipeline(char **args)
{
    char **commands[100];
    int command_count = 1;

    commands[0] = args;

    for (int i = 0; args[i] != NULL; i++)
    {
        if (strcmp(args[i], "|") == 0)
        {
            if (command_count >= 100 ||
                args[i + 1] == NULL ||
                i == 0)
            {
                fprintf(stderr, "Invalid pipe command\n");
                return 1;
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

            for (int j = 0; j < i; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            return 1;
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

    for (int i = 0; i < command_count - 1; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    int last_status = 1;

    for (int i = 0; i < started; i++)
    {
        int status;
        pid_t result;

        do
        {
            result = waitpid(pids[i], &status, 0);
        }
        while (result < 0 && errno == EINTR);

        if (result < 0)
        {
            perror("waitpid");
            continue;
        }

        if (i == command_count - 1)
            last_status = get_exit_code(status);
    }

    return last_status;
}

/* Main execution function. */

int execute(char **arglist, int background)
{
    if (arglist == NULL || arglist[0] == NULL)
        return 0;

    if (background)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("fork");
            return 1;
        }

        if (pid == 0)
        {
            int status = execute_pipeline(arglist);
            exit(status);
        }

        add_job(pid, arglist[0]);

        /* The parent does not wait for a background command. */
        return 0;
    }

    return execute_pipeline(arglist);
}
