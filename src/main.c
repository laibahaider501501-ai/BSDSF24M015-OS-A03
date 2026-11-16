
#include "shell.h"

int main() {

    char* cmdline;
    char** arglist;

    while ((cmdline = read_cmd(PROMPT, stdin)) != NULL) {

        // Check whether the command is a history re-execution request
        if (cmdline[0] == '!') {

            char* endptr;

            errno = 0;

            // Extract the number after !
            long number = strtol(cmdline + 1, &endptr, 10);

            // Check whether the history number is valid
            if (errno != 0 ||
                endptr == cmdline + 1 ||
                *endptr != '\0' ||
                number < 1 ||
                number > HISTORY_SIZE) {

                printf("Invalid history command.\n");
                free(cmdline);
                continue;
            }

            // Retrieve the previous command
            const char* previous = get_history_command((int) number);

            if (previous == NULL) {

                printf("No command found at history number %ld.\n",
                       number);

                free(cmdline);
                continue;
            }

            // Make a copy of the retrieved command
            char* expanded = strdup(previous);

            if (expanded == NULL) {

                perror("strdup failed");
                free(cmdline);
                continue;
            }

            // Replace !n with the actual previous command
            free(cmdline);
            cmdline = expanded;

            printf("%s\n", cmdline);
        }

        // Store the actual command in history
        add_to_history(cmdline);

        // Tokenize the command
        if ((arglist = tokenize(cmdline)) != NULL) {

            // Check whether it is a built-in command
            if (handle_builtin(arglist) == 0) {

                // Execute external commands
                execute(arglist);
            }

            // Free memory allocated by tokenize()
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }

            free(arglist);
        }

        free(cmdline);
    }

    printf("\nShell exited.\n");

    return 0;
}
