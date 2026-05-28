/**
 * @file transient.h
 * @brief LLE Transient Prompt System - Type Definitions
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 25 Section 12 - Transient Prompt System
 * Version: 1.1.0
 *
 * The transient prompt system simplifies previous prompts in the terminal
 * scrollback after commands execute, reducing visual clutter. When enabled,
 * fancy multi-line prompts are replaced with a minimal transient format
 * after the user presses Enter.
 *
 * Example:
 *   Before: [user@host] ~/path (git-branch *) $ echo hello
 *   After:  ❯ echo hello
 *
 * Implementation:
 * The transient replacement is triggered by the LLE_HOOK_LINE_ACCEPTED
 * widget hook, which fires after the user presses Enter but before
 * dc_finalize_input(). The actual rendering is done through
 * dc_apply_transient_prompt() in the display controller, ensuring
 * proper screen buffer state management.
 *
 * This header provides type definitions used by the prompt composer.
 * The implementation lives in the widget system (builtin_widgets.c)
 * and display controller (display_controller.c).
 */

#ifndef LLE_PROMPT_TRANSIENT_H
#define LLE_PROMPT_TRANSIENT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

/** @brief Maximum transient prompt output length */
#define LLE_TRANSIENT_OUTPUT_MAX 256

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================
 */

/**
 * @brief Transient prompt state
 *
 * Tracks transient prompt configuration and statistics.
 * Position tracking is handled by the screen buffer system.
 */
typedef struct lle_transient_state {
    /** @brief Transient prompts are enabled */
    bool enabled;

    /** @brief Statistics: number of transient replacements performed */
    size_t total_replacements;
} lle_transient_state_t;

/* ============================================================================
 * INLINE INITIALIZATION
 * ============================================================================
 */

/**
 * @brief Initialize transient prompt state
 *
 * @param state  State to initialize
 */
static inline void lle_transient_init(lle_transient_state_t *state) {
    if (state) {
        state->enabled = true;
        state->total_replacements = 0;
    }
}

#ifdef __cplusplus
}
#endif

#endif // LLE_PROMPT_TRANSIENT_H
