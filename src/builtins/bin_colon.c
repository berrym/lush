/**
 * @file bin_colon.c
 * @brief `:` (null command) builtin
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

/**
 * @brief Null command - does nothing and returns success
 *
 * Used for parameter expansions and as a no-op placeholder.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return Always returns 0 (success)
 */
int bin_colon(int argc __attribute__((unused)),
              char **argv __attribute__((unused))) {
    if (getenv("PARAM_EXPANSION_DEBUG")) {
        fprintf(stderr, "DEBUG: colon builtin received %d arguments:\n", argc);
        for (int i = 0; i < argc; i++) {
            fprintf(stderr, "  argv[%d] = '%s'\n", i,
                    argv[i] ? argv[i] : "(null)");
        }
    }
    return 0;
}
