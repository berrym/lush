/**
 * @file bin_eval.c
 * @brief `eval` builtin -- parse and execute its arguments as a shell command
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"

/**
 * @brief Evaluate arguments as shell commands
 *
 * Concatenates all arguments into a single command string and
 * executes it in the current shell context.
 *
 * @param argc Argument count
 * @param argv Argument vector with command fragments
 * @return Exit status of the evaluated command, or 0 if no arguments
 */
int bin_eval(int argc, char **argv) {
    if (argc < 2) {
        return 0;
    }

    // Concatenate all arguments
    size_t total_len = 0;
    for (int i = 1; i < argc; i++) {
        total_len += strlen(argv[i]) + 1; // +1 for space
    }

    char *command = malloc(total_len);
    if (!command) {
        return 1;
    }

    command[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            strcat(command, " ");
        }
        strcat(command, argv[i]);
    }

    /* Execute the command string. eval input is its own logical
     * source slice — the joined argv has no surrounding script
     * context, so line 1 is the right starting offset. */
    int result = parse_and_execute(command, 1);

    free(command);
    return result;
}
