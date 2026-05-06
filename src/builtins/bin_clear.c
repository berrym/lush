/**
 * @file bin_clear.c
 * @brief `clear` builtin -- clear the terminal screen
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "display_integration.h"

/**
 * @brief Clear the terminal screen
 *
 * Uses the display integration layer to clear the screen.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return Always returns 0
 */
int bin_clear(int argc __attribute__((unused)),
              char **argv __attribute__((unused))) {
    display_integration_clear_screen();
    return 0;
}
