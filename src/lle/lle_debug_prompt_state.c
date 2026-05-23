/**
 * @file lle_debug_prompt_state.c
 * @brief Active-prompt flag for the debugger's break-prompt
 *
 * Holds a single boolean: "is an lle_readline_no_history() call
 * currently in flight on behalf of the (lush-debug) break-prompt?"
 *
 * Kept in its own translation unit -- not in lle_readline.c -- so that
 * completion sources can read the flag (via lle_in_debug_prompt())
 * without their .o file dragging lle_readline.c.o into the link.
 * lle_readline.c.o references several shell-level symbols
 * (check_and_clear_sigint_flag, continuation_needs_continuation, ...)
 * that LLE-isolated test binaries do not link; co-locating the flag
 * with lle_readline would chain those dependencies through to every
 * consumer of the flag.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/lle_readline.h"

static bool g_in_debug_prompt = false;

bool lle_in_debug_prompt(void) { return g_in_debug_prompt; }

void lle_set_debug_prompt_active(bool active) {
    g_in_debug_prompt = active;
}
