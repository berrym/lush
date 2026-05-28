/**
 * @file bin_help.c
 * @brief `help` builtin -- list all builtins and their descriptions
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lle/lle_pager.h"

#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Print a list of builtins and their descriptions
 *
 * Builds the full listing into a heap-backed memstream so the output
 * can be handed to lle_pager_present in one shot -- the pager
 * decides at call time whether to paginate (interactive overflow)
 * or to stream the text directly (non-tty, disabled, or content
 * that fits in one screen). The prior implementation printed
 * line-by-line to stderr, which both prevented pagination and
 * diverged from POSIX (help output is informational and belongs
 * on stdout where it can be piped, captured, or read by users).
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return 0 on success
 */
int bin_help(int argc __attribute__((unused)),
             char **argv __attribute__((unused))) {
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *out = open_memstream(&buf, &buf_len);
    if (!out) {
        /// open_memstream allocation failure is rare; fall back to the
        /// classic streaming path so help still produces output even
        /// when we can't paginate. stderr keeps the historical
        /// destination on this fallback so existing scripts that
        /// redirect 2>&1 still capture the listing.
        for (size_t i = 0; i < builtins_count; i++) {
            fprintf(stderr, "\t%-10s%-40s\n", builtins[i].name,
                    builtins[i].doc);
        }
        return 0;
    }

    for (size_t i = 0; i < builtins_count; i++) {
        fprintf(out, "\t%-10s%-40s\n", builtins[i].name, builtins[i].doc);
    }
    fclose(out);

    lle_pager_present(NULL, buf);
    free(buf);
    return 0;
}
