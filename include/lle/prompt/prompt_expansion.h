/**
 * @file prompt_expansion.h
 * @brief Unified Prompt Expansion Engine
 *
 * Spec 28 Phase 1: Single expansion function that handles bash escapes
 * (\u, \h, \w), zsh escapes (%n, %m, %~), and LLE segment syntax
 * (${directory}, ${git}) in a unified two-pass architecture.
 *
 * Pass 1: Template engine resolves ${...} segments
 * Pass 2: Bash \X and zsh %X escapes expanded in one scan
 */

#ifndef LLE_PROMPT_EXPANSION_H
#define LLE_PROMPT_EXPANSION_H

#include "lle/error_handling.h"
#include "lle/prompt/template.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Runtime values needed by prompt escape sequences
 *
 * Provides context for expanding bash/zsh prompt escapes that depend
 * on shell runtime state (exit status, job count, etc.).
 */
typedef struct lle_prompt_expand_ctx {
    /** Template engine context for LLE segment rendering (NULL = skip) */
    const lle_template_render_ctx_t *template_ctx;

    /** Last command exit status (\?, %?) */
    int last_exit_status;

    /** Number of background jobs (\j, %j) */
    int job_count;

    /** History number of current command (\!) */
    int history_number;

    /** Command number in this session (\#) */
    int command_number;

    /** Terminal color depth: 0=none, 1=8-color, 2=256-color, 3=truecolor */
    int color_depth;
} lle_prompt_expand_ctx_t;

/**
 * @brief Expand a prompt format string to terminal output
 *
 * Accepts format strings containing any mix of:
 *   - LLE segment syntax: ${directory}, ${git.branch}, ${?cond:t:f}
 *   - Bash prompt escapes: \u, \h, \w, \d, \t, etc.
 *   - Zsh prompt escapes:  %n, %m, %~, %D{fmt}, %F{color}, etc.
 *
 * @param format       Format string (PS1/PS2/PROMPT value)
 * @param output       Output buffer for expanded result
 * @param output_size  Size of output buffer
 * @param ctx          Expansion context with runtime values
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_prompt_expand(const char *format, char *output,
                               size_t output_size,
                               const lle_prompt_expand_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LLE_PROMPT_EXPANSION_H */
