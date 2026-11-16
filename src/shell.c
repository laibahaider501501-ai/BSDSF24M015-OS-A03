#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/wait.h>
#include <errno.h>

/* ---------------- COMMAND HISTORY ---------------- */

static char *history[HISTORY_SIZE];
static int history_count = 0;

void add_to_history(const char *command)
{
    if (command == NULL || command[0] == '\0')
        return;

    if (history_count == HISTORY_SIZE)
    {
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

/* ---------------- COMMAND INPUT ---------------- */

char *read_cmd(char *prompt, FILE *fp)
{
    char *line = NULL;
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

    line[strcspn(line, "\n")] = '\0';

    return line;
}

/* ---------------- TOKENIZATION ---------------- */

static int add_token(char ***tokens, int *count,
                     int *capacity, const char *word)
{
    if (*count + 1 >= *capacity)
    {
        int new_capacity = (*capacity == 0) ? 8 : (*capacity * 2);

        char **temp = realloc(*tokens,
                              new_capacity * sizeof(char *));

        if (temp == NULL)
            return 0;

        *tokens = temp;
        *capacity = new_capacity;
    }

    (*tokens)[*count] = strdup(word);

    if ((*tokens)[*count] == NULL)
        return 0;

    (*count)++;
    (*tokens)[*count] = NULL;

    return 1;
}

char **tokenize(char *cmdline)
{
    if (cmdline == NULL)
        return NULL;

    char **tokens = NULL;
    int count = 0;
    int capacity = 0;

    char word[1024];
    int word_length = 0;

    char quote = '\0';

    for (int i = 0; ; i++)
    {
        char c = cmdline[i];

        if (quote != '\0')
        {
            if (c == '\0')
            {
                fprintf(stderr, "Error: unmatched quote\n");
                goto failure;
            }

            if (c == quote)
            {
                quote = '\0';
            }
            else
            {
                if (word_length < 1023)
                    word[word_length++] = c;
            }

            continue;
        }

        if (c == '\'' || c == '"')
        {
            quote = c;
            continue;
        }

        if (c == ' ' || c == '\t' ||
            c == '<' || c == '>' || c == '|' || c == '\0')
        {
            if (word_length > 0)
            {
                word[word_length] = '\0';

                if (!add_token(&tokens, &count,
                               &capacity, word))
                    goto failure;

                word_length = 0;
            }

            if (c == '<' || c == '>' || c == '|')
            {
                char operator_token[2] = {c, '\0'};

                if (!add_token(&tokens, &count,
                               &capacity, operator_token))
                    goto failure;
            }

            if (c == '\0')
                break;
        }
        else
        {
            if (word_length < 1023)
                word[word_length++] = c;
        }
    }

    if (tokens == NULL)
    {
        tokens = calloc(1, sizeof(char *));

        if (tokens == NULL)
            return NULL;
    }

    return tokens;

failure:
    if (tokens != NULL)
    {
        for (int i = 0; i < count; i++)
            free(tokens[i]);

        free(tokens);
    }

    return NULL;
}

/* ---------------- COMMAND CHAINING ---------------- */

/*
 * Split a command line at semicolons.
 * Semicolons inside quotes are not treated as separators.
 */

char **split_commands(char *line, int *count)
{
    *count = 0;

    if (line == NULL)
        return NULL;

    int capacity = 4;

    char **commands = malloc(capacity * sizeof(char *));

    if (commands == NULL)
        return NULL;

    char *start = line;
    char quote = '\0';

    for (char *p = line; ; p++)
    {
        char c = *p;

        if (quote != '\0')
        {
            if (c == quote)
                quote = '\0';
        }
        else if (c == '\'' || c == '"')
        {
            quote = c;
        }
        else if (c == ';' || c == '\0')
        {
            char saved = c;
            *p = '\0';

            while (isspace((unsigned char)*start))
                start++;

            char *end = start + strlen(start);

            while (end > start &&
                   isspace((unsigned char)end[-1]))
                *--end = '\0';

            if (*start != '\0')
            {
                if (*count == capacity)
                {
                    capacity *= 2;

                    char **temp = realloc(
                        commands,
                        capacity * sizeof(char *)
                    );

                    if (temp == NULL)
                    {
                        free_commands(commands, *count);
                        return NULL;
                    }

                    commands = temp;
                }

                commands[*count] = strdup(start);

                if (commands[*count] == NULL)
                {
                    free_commands(commands, *count);
                    return NULL;
                }

                (*count)++;
            }

            if (saved == '\0')
                break;

            start = p + 1;
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

/* ---------------- BACKGROUND JOB MANAGEMENT ---------------- */

typedef struct
{
    pid_t pid;
    char command[256];
} Job;

static Job jobs[MAX_JOBS];
static int job_count = 0;

void add_job(pid_t pid, const char *command)
{
    if (job_count >= MAX_JOBS)
    {
        fprintf(stderr, "Maximum background jobs reached\n");
        return;
    }

    jobs[job_count].pid = pid;

    snprintf(jobs[job_count].command,
             sizeof(jobs[job_count].command),
             "%s", command);

    job_count++;

    printf("[Background process started] PID: %d\n", pid);
}

void reap_background_jobs(void)
{
    int status;
    pid_t pid;

    /*
     * WNOHANG means waitpid does not block.
     * It returns immediately if no child has finished.
     */

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        for (int i = 0; i < job_count; i++)
        {
            if (jobs[i].pid == pid)
            {
                printf("\n[Completed] PID: %d  Command: %s\n",
                       jobs[i].pid,
                       jobs[i].command);

                /* Remove completed job from the list. */
                for (int j = i + 1; j < job_count; j++)
                    jobs[j - 1] = jobs[j];

                job_count--;
                break;
            }
        }
    }

    if (pid == -1 && errno != ECHILD && errno != EINTR)
        perror("waitpid");
}

void print_jobs(void)
{
    reap_background_jobs();

    if (job_count == 0)
    {
        printf("No background jobs running.\n");
        return;
    }

    printf("Background Jobs:\n");

    for (int i = 0; i < job_count; i++)
    {
        printf("[%d] PID: %d  Command: %s\n",
               i + 1,
               jobs[i].pid,
               jobs[i].command);
    }
}

/* ---------------- BUILT-IN COMMANDS ---------------- */

int handle_builtin(char **arglist)
{
    if (arglist == NULL || arglist[0] == NULL)
        return 1;

    if (strcmp(arglist[0], "exit") == 0)
    {
        printf("Exiting myshell...\n");
        exit(0);
    }

    if (strcmp(arglist[0], "cd") == 0)
    {
        const char *path = arglist[1];

        if (path == NULL)
            path = getenv("HOME");

        if (path == NULL || chdir(path) != 0)
            perror("cd");

        return 1;
    }

    if (strcmp(arglist[0], "help") == 0)
    {
        printf("Available commands:\n");
        printf("exit       - Exit the shell\n");
        printf("cd         - Change directory\n");
        printf("help       - Display help\n");
        printf("history    - Display command history\n");
        printf("jobs       - Display background jobs\n");
        printf("command ; command - Run commands sequentially\n");
        printf("command &  - Run command in background\n");

        return 1;
    }

    if (strcmp(arglist[0], "history") == 0)
    {
        print_history();
        return 1;
    }

    if (strcmp(arglist[0], "jobs") == 0)
    {
        print_jobs();
        return 1;
    }

    return 0;
}
