
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>

/*
 * Store executable command names found in PATH.
 * These are used for command completion.
 */
static char **command_matches = NULL;
static size_t command_count = 0;
static size_t command_index = 0;


/* Free previously collected command names. */
static void free_command_matches(void)
{
    size_t i;

    for (i = 0; i < command_count; i++) {
        free(command_matches[i]);
    }

    free(command_matches);

    command_matches = NULL;
    command_count = 0;
    command_index = 0;
}


/* Check whether a command name is already stored. */
static int command_exists(const char *name)
{
    size_t i;

    for (i = 0; i < command_count; i++) {
        if (strcmp(command_matches[i], name) == 0) {
            return 1;
        }
    }

    return 0;
}


/* Add an executable command to the completion list. */
static void add_command_match(const char *name)
{
    char **new_matches;

    if (command_exists(name)) {
        return;
    }

    new_matches = realloc(
        command_matches,
        (command_count + 1) * sizeof(char *)
    );

    if (new_matches == NULL) {
        return;
    }

    command_matches = new_matches;

    command_matches[command_count] = strdup(name);

    if (command_matches[command_count] == NULL) {
        return;
    }

    command_count++;
}


/*
 * Search every directory in PATH and collect executable
 * commands beginning with the text typed by the user.
 *
 * Readline calls this function repeatedly:
 * state == 0 means start a new search.
 */
static char *command_generator(const char *text, int state)
{
    if (state == 0) {
        const char *path;
        char *path_copy;
        char *saveptr = NULL;
        char *directory;

        free_command_matches();

        path = getenv("PATH");

        if (path == NULL) {
            return NULL;
        }

        path_copy = strdup(path);

        if (path_copy == NULL) {
            return NULL;
        }

        directory = strtok_r(path_copy, ":", &saveptr);

        while (directory != NULL) {
            DIR *dir;
            struct dirent *entry;

            /* An empty PATH component represents current directory. */
            if (directory[0] == '\0') {
                directory = ".";
            }

            dir = opendir(directory);

            if (dir != NULL) {
                while ((entry = readdir(dir)) != NULL) {
                    char *full_path;
                    size_t path_length;
                    struct stat file_info;

                    /* Check whether the name matches the typed prefix. */
                    if (strncmp(
                            entry->d_name,
                            text,
                            strlen(text)
                        ) != 0) {
                        continue;
                    }

                    path_length =
                        strlen(directory) +
                        strlen(entry->d_name) + 2;

                    full_path = malloc(path_length);

                    if (full_path == NULL) {
                        continue;
                    }

                    snprintf(
                        full_path,
                        path_length,
                        "%s/%s",
                        directory,
                        entry->d_name
                    );

                    /*
                     * Only complete regular executable files.
                     * stat() also follows executable symlinks.
                     */
                    if (stat(full_path, &file_info) == 0 &&
                        S_ISREG(file_info.st_mode) &&
                        access(full_path, X_OK) == 0) {

                        add_command_match(entry->d_name);
                    }

                    free(full_path);
                }

                closedir(dir);
            }

            directory = strtok_r(NULL, ":", &saveptr);
        }

        free(path_copy);
    }

    /* Return one matching command at a time. */
    if (command_index < command_count) {
        return strdup(command_matches[command_index++]);
    }

    return NULL;
}


/*
 * Decide which type of completion to use.
 *
 * First word: executable command completion from PATH.
 * Other words: filename completion.
 */
static char **shell_completion(
    const char *text,
    int start,
    int end
)
{
    int i;
    int command_position = 1;

    (void)end;

    /*
     * Check whether everything before the current word
     * contains only spaces or tabs.
     */
    for (i = 0; i < start; i++) {
        if (!isspace((unsigned char)rl_line_buffer[i])) {
            command_position = 0;
            break;
        }
    }

    /*
     * If completing the first word and no slash is present,
     * search executable commands in PATH.
     */
    if (command_position && strchr(text, '/') == NULL) {
        char **matches;

        matches = rl_completion_matches(
            text,
            command_generator
        );

        /*
         * If no command matches, try filename completion.
         */
        if (matches != NULL) {
            return matches;
        }
    }

    /* Complete filenames and paths. */
    return rl_completion_matches(
        text,
        rl_filename_completion_function
    );
}


/* Handle !n command-history re-execution. */
static char *process_history_reference(char *command)
{
    char *endptr;
    long number;
    const char *old_command;

    if (command[0] != '!') {
        return command;
    }

    number = strtol(command + 1, &endptr, 10);

    if (command[1] == '\0' ||
        *endptr != '\0' ||
        number <= 0 ||
        number > INT_MAX) {

        printf("Invalid history command.\n");
        free(command);
        return NULL;
    }

    old_command = get_history_command((int)number);

    if (old_command == NULL) {
        printf("History entry not found.\n");
        free(command);
        return NULL;
    }

    /* Make a separate copy before freeing the old input. */
    {
        char *new_command = strdup(old_command);

        free(command);

        if (new_command == NULL) {
            perror("strdup");
            return NULL;
        }

        printf("%s\n", new_command);

        return new_command;
    }
}


int main(void)
{
    char *cmdline;
    char **arglist;

    /*
     * Tell GNU Readline to use our completion function.
     */
    rl_attempted_completion_function = shell_completion;

    /* Explicitly bind the Tab key to Readline completion. */
    rl_bind_key('\t', rl_complete);

    /* Add a space after a completed command or filename. */
    rl_completion_append_character = ' ';

    while (1) {

        /*
         * Readline handles input, editing, and arrow-key history.
         */
        cmdline = readline(PROMPT);

        /* Ctrl+D exits the shell. */
        if (cmdline == NULL) {
            printf("\n");
            break;
        }

        /* Ignore empty commands. */
        if (cmdline[0] == '\0') {
            free(cmdline);
            continue;
        }

        /* Handle !n before tokenization. */
        cmdline = process_history_reference(cmdline);

        if (cmdline == NULL) {
            continue;
        }

        /* Ignore a command if history reference was invalid. */
        if (cmdline[0] == '\0') {
            free(cmdline);
            continue;
        }

        /*
         * Store the command in both GNU Readline history
         * and our existing Feature 3 history.
         */
        add_history(cmdline);
        add_to_history(cmdline);

        /* Convert the command line into arguments. */
        arglist = tokenize(cmdline);

        if (arglist != NULL) {

            /*
             * Built-in commands must execute in the shell process.
             */
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }
        }

        free(cmdline);
    }

    free_command_matches();

    clear_history();

    return 0;
}
