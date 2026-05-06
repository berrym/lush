/**
 * @file bin_false.c
 * @brief `false` builtin -- always return 1 (failure)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

/**
 * @brief Return failure status
 *
 * Always returns 1 (failure). Used in shell scripts and conditionals.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return Always returns 1
 */
int bin_false(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 1;
}
