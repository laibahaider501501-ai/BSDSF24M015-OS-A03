
#include "shell.h"

// Array to store command history
static char* history[HISTORY_SIZE];
static int history_count = 0;


// Add a command to history
void add_to_history(const char* command) {

    if (command == NULL || command[0] == '\0') {
        return;
    }

    // Make a separate copy of the command
    char* copy = strdup(command);

    if (copy == NULL) {
        perror("strdup failed");
        return;
    }

    // If history is full, remove the oldest command
    if (history_count == HISTORY_SIZE) {

        free(history[0]);

        for (int i = 0; i < HISTORY_SIZE - 1; i++) {
            history[i] = history[i + 1];
        }

        history_count--;
    }

    // Store the new command
    history[history_count] = copy;
    history_count++;
}


// Display all stored commands
void print_history(void) {

    printf("\nCommand History:\n");
    printf("----------------\n");

    for (int i = 0; i < history_count; i++) {
        printf("%d  %s\n", i + 1, history[i]);
    }

    printf("\n");
}


// Retrieve a command using its history number
const char* get_history_command(int number) {

    if (number < 1 || number > history_count) {
        return NULL;
    }

    return history[number - 1];
}


// Read a command from the user
char* read_cmd(char* prompt, FILE* fp) {

    printf("%s", prompt);

    char* cmdline = (char*) malloc(sizeof(char) * MAX_LEN);

    if (cmdline == NULL) {
        perror("malloc failed");
        exit(1);
    }

    int c, pos = 0;

    while ((c = getc(fp)) != EOF) {

        if (c == '\n') {
            break;
        }

        if (pos < MAX_LEN - 1) {
            cmdline[pos++] = c;
        }
    }

    if (c == EOF && pos == 0) {
        free(cmdline);
        return NULL;
    }

    cmdline[pos] = '\0';

    return cmdline;
}


// Convert the command into arguments
char** tokenize(char* cmdline) {

    if (cmdline == NULL || cmdline[0] == '\0' ||
        cmdline[0] == '\n') {
        return NULL;
    }

    char** arglist = (char**) malloc(sizeof(char*) * (MAXARGS + 1));

    if (arglist == NULL) {
        perror("malloc failed");
        exit(1);
    }

    for (int i = 0; i < MAXARGS + 1; i++) {

        arglist[i] = (char*) malloc(sizeof(char) * ARGLEN);

        if (arglist[i] == NULL) {
            perror("malloc failed");

            for (int j = 0; j < i; j++) {
                free(arglist[j]);
            }

            free(arglist);
            exit(1);
        }

        bzero(arglist[i], ARGLEN);
    }

    char* cp = cmdline;
    char* start;
    int len;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {

        while (*cp == ' ' || *cp == '\t') {
            cp++;
        }

        if (*cp == '\0') {
            break;
        }

        start = cp;
        len = 0;

        while (*cp != '\0' && *cp != ' ' && *cp != '\t') {
            len++;
            cp++;
        }

        if (len >= ARGLEN) {

            printf("Error: Argument is too long.\n");

            for (int i = 0; i < MAXARGS + 1; i++) {
                free(arglist[i]);
            }

            free(arglist);
            return NULL;
        }

        strncpy(arglist[argnum], start, len);
        arglist[argnum][len] = '\0';

        argnum++;
    }

    if (argnum == 0) {

        for (int i = 0; i < MAXARGS + 1; i++) {
            free(arglist[i]);
        }

        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;

    return arglist;
}


// Handle built-in commands
int handle_builtin(char** arglist) {

    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    // Built-in command: exit
    if (strcmp(arglist[0], "exit") == 0) {

        printf("Exiting shell...\n");
        exit(0);
    }

    // Built-in command: cd
    else if (strcmp(arglist[0], "cd") == 0) {

        char* directory;

        if (arglist[1] == NULL) {

            directory = getenv("HOME");

            if (directory == NULL) {
                fprintf(stderr,
                        "cd: HOME environment variable not set\n");
                return 1;
            }
        }
        else {
            directory = arglist[1];
        }

        if (chdir(directory) != 0) {
            perror("cd");
        }

        return 1;
    }

    // Built-in command: help
    else if (strcmp(arglist[0], "help") == 0) {

        printf("\n");
        printf("Available built-in commands:\n");
        printf("----------------------------\n");
        printf("exit    : Exit the shell\n");
        printf("cd      : Change the current directory\n");
        printf("help    : Display this help message\n");
        printf("jobs    : Display job control status\n");
        printf("history : Display command history\n");
        printf("\n");

        return 1;
    }

    // Built-in command: jobs
    else if (strcmp(arglist[0], "jobs") == 0) {

        printf("Job control not yet implemented.\n");

        return 1;
    }

    // Built-in command: history
    else if (strcmp(arglist[0], "history") == 0) {

        print_history();

        return 1;
    }

    // Not a built-in command
    return 0;
}
