/**
 * @file bin_continue.c
 * @brief `continue` builtin -- skip to next loop iteration
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

/**
 * @brief Continue to the next iteration of enclosing loop
 *
 * Skips the remaining commands in the current loop iteration and
 * continues with the next iteration. An optional positive-integer
 * argument specifies which enclosing loop to continue. Shared
 * implementation lives in builtin_loop_control (builtins.c) -- `break`
 * and `continue` differ only in the verb and the loop-control value,
 * so all of the parse / validate / error reporting is centralized.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional loop level)
 * @return 0 on success, 1 if not in a loop or on invalid argument
 */
int bin_continue(int argc, char **argv) {
    return builtin_loop_control(argc, argv, "continue", LOOP_CONTINUE);
}
