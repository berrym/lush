/**
 * @file bin_exit.c
 * @brief `exit` builtin -- terminate the shell with optional exit code
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"

#include <stdlib.h>

/**
 * @brief Exit the shell (builtin command)
 *
 * Sets the exit_flag to terminate the main loop gracefully,
 * allowing proper cleanup of parser, executor, and other resources.
 * EXIT traps are executed in the main loop after normal cleanup.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional exit code)
 * @return The exit code (though the shell will exit before this matters)
 */
int bin_exit(int argc, char **argv) {
    int exit_code = last_exit_status; /* Default to last command's status */

    /* Parse exit code argument if provided */
    if (argc > 1) {
        exit_code = atoi(argv[1]);
    }

    /* Set exit flag to break main loop - allows proper cleanup */
    exit_flag = true;

    /* Store exit code for use after main loop exits */
    last_exit_status = exit_code;

    return exit_code;
}
