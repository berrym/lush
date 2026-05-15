/**
 * @file bin_true.c
 * @brief `true` builtin -- always return 0 (success)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

/**
 * @brief Return success status
 *
 * Always returns 0 (success). Used in shell scripts and conditionals.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return Always returns 0
 */
int bin_true(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}
