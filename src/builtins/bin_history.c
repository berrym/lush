/**
 * @file bin_history.c
 * @brief `history` builtin -- display or manipulate the command history
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lle/history.h"
#include "lle/lle_pager.h"

/**
 * @brief Display or manipulate the command history
 *
 * Implements the history builtin using the LLE history system.
 * Supports listing history entries and various manipulation options.
 *
 * The bridge returns the full listing as a single allocated buffer,
 * which is handed to lle_pager_present so long histories paginate
 * automatically in interactive shells. Non-tty stdout, the disabled
 * master switch, or a listing that fits in one screen all fall
 * through the pager's own decision tree to a direct write.
 *
 * @param argc Argument count
 * @param argv Argument vector with history options
 * @return 0 on success, 1 on error
 */
int bin_history(int argc, char **argv) {
    char *output = NULL;
    lle_result_t result =
        lle_history_bridge_handle_builtin(argc, argv, &output);

    if (output) {
        lle_pager_present(NULL, output);
        free(output);
    }

    return (result == LLE_SUCCESS) ? 0 : 1;
}
