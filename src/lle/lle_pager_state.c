/**
 * @file lle_pager_state.c
 * @brief Active-mode flag for the LLE pager
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * See include/lle/lle_pager_state.h for rationale. Isolated TU so
 * consumers needing only the probe don't link the input loop.
 */

#include "lle/lle_pager_state.h"

static bool g_in_pager = false;

bool lle_in_pager(void) { return g_in_pager; }

void lle_set_pager_active(bool active) { g_in_pager = active; }
