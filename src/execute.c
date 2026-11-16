#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_COMMANDS 32
#define MAX_ARGS 128

/*
 * Execute one command with optional input and output redirection.
 */
static void execute_command(char** args)
{
    char* input_file = NULL;
    char* output_file = NULL;

    char* command_args[MAX_ARGS];
    int arg_count = 0;

    /*
     * Separate command arguments from redirection operators.
     */
    for (int i = 0; args[i] != NULL; i++)
    {
        if (strcmp(args[i], "<") == 0)
        {
            if (args[i + 1] == NULL || input_file != NULL)
            {
                fprintf(stderr, "myshell: invalid input redirection\n");
                _exit(EXIT_FAILURE);
            }

            input_file = args[++i];
        }
        else if (strcmp(args[i], ">") == 0)
        {
            if (args[i + 1] == NULL || output_file != NULL)
            {
                fprintf(stderr, "myshell: invalid output redirection\n");
                _exit(EXIT_FAILURE);
            }

            output_file = args[++i];
        }
        else
        {
            if (arg_count >= MAX_ARGS - 1)
            {
                fprintf(stderr, "myshell: too many arguments\n");
                _exit(EXIT_FAILURE);
            }

            command_args[arg_count++] = args[i];
        }
    }

    command_args[arg_count] = NULL;

    if (arg_count == 0)
    {
        fprintf(stderr, "myshell: missing command\n");
        _exit(EXIT_FAILURE);
    }

    /*
     * Input redirection: < filename
     */
    if (input_file != NULL)
    {
        int fd = open(input_file, O_RDONLY);

        if (fd < 0)
        {
            perror(input_file);
            _exit(EXIT_FAILURE);
        }

        if (dup2(fd, STDIN_FILENO) < 0)
        {
            perror("dup2");
            close(fd);
            _exit(EXIT_FAILURE);
        }

        close(fd);
    }

    /*
     * Output redirection: > filename
     */
    if (output_file != NULL)
    {
        int fd = open(output_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      0644);

        if (fd < 0)
        {
            perror(output_file);
            _exit(EXIT_FAILURE);
        }

        if (dup2(fd, STDOUT_FILENO) < 0)
        {
            perror("dup2");
            close(fd);
            _exit(EXIT_FAILURE);
        }

        close(fd);
    }

    /*
     * Execute the external command.
     */
    execvp(command_args[0], command_args);

    perror(command_args[0]);
    _exit(EXIT_FAILURE);
}

/*
 * Execute a pipeline containing one or more commands.
 *
 * Example:
 * cat file.txt | grep root | wc -l
 */
static void execute_pipeline(char** args)
{
    char** commands[MAX_COMMANDS];
    int command_count = 0;

    commands[command_count++] = args;

    /*
     * Divide the arguments into commands using |.
     */
    for (int i = 0; args[i] != NULL; i++)
    {
        if (strcmp(args[i], "|") == 0)
        {
            args[i] = NULL;

            if (args[i + 1] == NULL ||
                command_count >= MAX_COMMANDS)
            {
                fprintf(stderr, "myshell: invalid pipe\n");
                return;
            }

            commands[command_count++] = &args[i + 1];
        }
    }

    /*
     * Check for empty commands.
     */
    for (int i = 0; i < command_count; i++)
    {
        if (commands[i][0] == NULL)
        {
            fprintf(stderr, "myshell: empty command in pipe\n");
            return;
        }
    }

    int pipes[MAX_COMMANDS - 1][2];
    pid_t pids[MAX_COMMANDS];

    /*
     * Create the required pipes.
     */
    for (int i = 0; i < command_count - 1; i++)
    {
        if (pipe(pipes[i]) < 0)
        {
            perror("pipe");
            return;
        }
    }

    /*
     * Create one child process for each command.
     */
    for (int i = 0; i < command_count; i++)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("fork");

            /*
             * Close all pipe descriptors if fork fails.
             */
            for (int j = 0; j < command_count - 1; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            return;
        }

        if (pid == 0)
        {
            /*
             * Child process.
             *
             * Connect the previous pipe's read end to stdin.
             */
            if (i > 0)
            {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0)
                {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
            }

            /*
             * Connect the next pipe's write end to stdout.
             */
            if (i < command_count - 1)
            {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0)
                {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
            }

            /*
             * Close all original pipe descriptors in the child.
             */
            for (int j = 0; j < command_count - 1; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /*
             * Apply any < or > redirection and execute command.
             */
            execute_command(commands[i]);
        }

        pids[i] = pid;
    }

    /*
     * Parent process must close every pipe descriptor.
     */
    for (int i = 0; i < command_count - 1; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /*
     * Wait for all child processes to finish.
     */
    for (int i = 0; i < command_count; i++)
    {
        waitpid(pids[i], NULL, 0);
    }
}

/*
 * Main execution function called by main.c.
 */
void execute(char** arglist)
{
    if (arglist == NULL || arglist[0] == NULL)
    {
        return;
    }

    execute_pipeline(arglist);
}
