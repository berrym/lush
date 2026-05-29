/**
 * @file bin_break.c
 * @brief `break` builtin -- exit from enclosing loop
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

/**
 * @brief Break out of enclosing loop
 *
 * Exits from a for, while, or until loop. An optional positive-integer
 * argument specifies how many levels of loops to break out of.
 * Shared implementation lives in builtin_loop_control (builtins.c) --
 * `break` and `continue` differ only in the verb and the loop-control
 * value, so all of the parse / validate / error reporting is centralized.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional loop level)
 * @return 0 on success, 1 if not in a loop or on invalid argument
 */
int bin_break(int argc, char **argv) {
    return builtin_loop_control(argc, argv, "break", LOOP_BREAK);
}
