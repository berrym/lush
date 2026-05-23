/**
 * @file multiline_manager.c
 * @brief LLE Multiline Manager Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 03 Phase 7 - Multiline Buffer Operations
 *
 * This module wraps the existing input_continuation.c parser to provide
 * LLE-specific multiline state tracking. It delegates core shell construct
 * parsing to the proven shared parser while adding buffer integration and
 * per-line state management.
 *
 * Design Philosophy:
 * - Reuse proven code (input_continuation.c) rather than duplicate
 * - Maintain consistency between LLE and main input system
 * - Add LLE-specific features (line tracking, buffer integration)
 * - Expose exact API required by Spec 03
 */

#include "input_continuation.h"
#include "lle/buffer_management.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/performance.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * MULTILINE CONTEXT - LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Initialize a new multiline context
 * @param ctx Pointer to store the created context
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_context_init(lle_multiline_context_t **ctx,
                                        lush_memory_pool_t *memory_pool) {
    if (!ctx || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate context structure
    lle_multiline_context_t *context =
        lle_pool_alloc(sizeof(lle_multiline_context_t));
    if (!context) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Initialize fields
    memset(context, 0, sizeof(lle_multiline_context_t));
    context->memory_pool = memory_pool;

    // Allocate and initialize core parser state
    continuation_state_t *core_state =
        lle_pool_alloc(sizeof(continuation_state_t));
    if (!core_state) {
        lle_pool_free(context);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    continuation_state_init(core_state);
    context->core_state = core_state;

    // Initialize LLE-specific fields
    context->current_construct = NULL;
    context->construct_start_line = 0;
    context->construct_start_offset = 0;
    context->nesting_level = 0;
    context->construct_complete = true;
    context->needs_continuation = false;
    context->expected_terminator = NULL;
    context->cache_key = 0;
    context->cache_valid = false;

    *ctx = context;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a multiline context and free resources
 * @param ctx The context to destroy
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if ctx is NULL
 */
lle_result_t lle_multiline_context_destroy(lle_multiline_context_t *ctx) {
    if (!ctx) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Clean up core parser state
    if (ctx->core_state) {
        continuation_state_cleanup((continuation_state_t *)ctx->core_state);
        lle_pool_free(ctx->core_state);
    }

    // Free allocated strings
    if (ctx->current_construct) {
        lle_pool_free(ctx->current_construct);
    }
    if (ctx->expected_terminator) {
        lle_pool_free(ctx->expected_terminator);
    }

    // Free context itself
    lle_pool_free(ctx);

    return LLE_SUCCESS;
}

/**
 * @brief Reset a multiline context for reuse
 * @param ctx The context to reset
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if ctx is NULL
 */
lle_result_t lle_multiline_context_reset(lle_multiline_context_t *ctx) {
    if (!ctx) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Reset core parser state
    if (ctx->core_state) {
        continuation_state_cleanup((continuation_state_t *)ctx->core_state);
        continuation_state_init((continuation_state_t *)ctx->core_state);
    }

    // Free and reset LLE-specific fields
    if (ctx->current_construct) {
        lle_pool_free(ctx->current_construct);
        ctx->current_construct = NULL;
    }
    if (ctx->expected_terminator) {
        lle_pool_free(ctx->expected_terminator);
        ctx->expected_terminator = NULL;
    }

    ctx->construct_start_line = 0;
    ctx->construct_start_offset = 0;
    ctx->nesting_level = 0;
    ctx->construct_complete = true;
    ctx->needs_continuation = false;
    ctx->cache_key = 0;
    ctx->cache_valid = false;

    return LLE_SUCCESS;
}

/* ============================================================================
 * MULTILINE CONTEXT - PARSING
 * ============================================================================
 */

/**
 * @brief Helper to determine current construct name from parser state
 * @param state The continuation state to inspect
 * @return Construct name string, or NULL if no active construct
 */
static const char *get_construct_name(const continuation_state_t *state) {
    if (!state) {
        return NULL;
    }

    // Check quote states first (highest priority)
    if (state->in_single_quote) {
        return "single quote";
    }
    if (state->in_double_quote) {
        return "double quote";
    }
    if (state->in_backtick) {
        return "backtick";
    }

    // Check here document
    if (state->in_here_doc) {
        return "here document";
    }

    // Check control structures
    if (state->in_function_definition) {
        return "function definition";
    }
    if (state->in_case_statement) {
        return "case statement";
    }
    if (state->in_if_statement) {
        return "if statement";
    }
    if (state->in_while_loop) {
        return "while loop";
    }
    if (state->in_for_loop) {
        return "for loop";
    }
    if (state->in_until_loop) {
        return "until loop";
    }

    // Check brackets/braces
    if (state->brace_count > 0) {
        return "brace group";
    }
    if (state->paren_count > 0) {
        return "subshell";
    }
    if (state->bracket_count > 0) {
        return "bracket expression";
    }

    // Check command substitution
    if (state->in_command_substitution) {
        return "command substitution";
    }
    if (state->in_arithmetic) {
        return "arithmetic expansion";
    }

    // Check continuation
    if (state->has_continuation) {
        return "line continuation";
    }

    return NULL;
}

/**
 * @brief Helper to calculate nesting level from parser state
 * @param state The continuation state to inspect
 * @return Nesting level (0 for no nesting)
 */
static uint8_t get_nesting_level(const continuation_state_t *state) {
    if (!state) {
        return 0;
    }

    uint8_t level = 0;

    // Count nesting from various constructs
    level += state->paren_count;
    level += state->brace_count;
    level += state->bracket_count;
    level += state->compound_command_depth;

    // Quote states don't nest but contribute to depth
    if (state->in_single_quote || state->in_double_quote ||
        state->in_backtick) {
        level += 1;
    }

    return level;
}

/**
 * @brief Analyze a line of input for multiline state updates
 * @param ctx The multiline context
 * @param line The line text to analyze
 * @param length Length of the line (reserved for future use)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_analyze_line(lle_multiline_context_t *ctx,
                                        const char *line, size_t length) {
    (void)length; // Reserved for length-aware analysis
    if (!ctx || !line) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    continuation_state_t *state = (continuation_state_t *)ctx->core_state;
    if (!state) {
        return LLE_ERROR_INVALID_STATE;
    }

    // Delegate core parsing to shared parser
    continuation_analyze_line(line, state);

    // Extract and cache LLE-specific state
    const char *construct = get_construct_name(state);
    if (construct) {
        // Allocate and copy construct name if changed
        if (!ctx->current_construct ||
            strcmp(ctx->current_construct, construct) != 0) {
            if (ctx->current_construct) {
                lle_pool_free(ctx->current_construct);
            }
            size_t len = strlen(construct) + 1;
            ctx->current_construct = lle_pool_alloc(len);
            if (ctx->current_construct) {
                memcpy(ctx->current_construct, construct, len);
            }
        }
    } else {
        // No construct - clear
        if (ctx->current_construct) {
            lle_pool_free(ctx->current_construct);
            ctx->current_construct = NULL;
        }
    }

    // Update nesting level
    ctx->nesting_level = get_nesting_level(state);

    // Update completion state
    ctx->construct_complete = continuation_is_complete(state);
    ctx->needs_continuation = continuation_needs_continuation(state);

    // Invalidate cache since state changed
    ctx->cache_valid = false;

    return LLE_SUCCESS;
}

/* ============================================================================
 * MULTILINE CONTEXT - STATE QUERIES
 * ============================================================================
 */

/**
 * @brief Check if the multiline context represents a complete command
 * @param ctx The multiline context to check
 * @return true if command is complete, false if continuation needed
 */
bool lle_multiline_is_complete(const lle_multiline_context_t *ctx) {
    if (!ctx || !ctx->core_state) {
        return true; // No context = complete
    }

    return continuation_is_complete(
        (const continuation_state_t *)ctx->core_state);
}

/**
 * @brief Check if the multiline context needs continuation input
 * @param ctx The multiline context to check
 * @return true if continuation is needed, false otherwise
 */
bool lle_multiline_needs_continuation(const lle_multiline_context_t *ctx) {
    if (!ctx || !ctx->core_state) {
        return false; // No context = no continuation
    }

    return continuation_needs_continuation(
        (const continuation_state_t *)ctx->core_state);
}

/**
 * @brief Get the continuation prompt for the current multiline state
 * @param ctx The multiline context
 * @return Prompt string for continuation, or default "> " if no context
 */
const char *lle_multiline_get_prompt(const lle_multiline_context_t *ctx) {
    if (!ctx || !ctx->core_state) {
        return "> "; // Default prompt
    }

    return continuation_get_prompt(
        (const continuation_state_t *)ctx->core_state);
}

/**
 * @brief Get the name of the current construct being parsed
 * @param ctx The multiline context
 * @return Construct name string, or NULL if no active construct
 */
const char *lle_multiline_get_construct(const lle_multiline_context_t *ctx) {
    if (!ctx) {
        return NULL;
    }

    return ctx->current_construct;
}

/* ============================================================================
 * MULTILINE MANAGER - LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Initialize a new multiline manager
 * @param manager Pointer to store the created manager
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_manager_init(lle_multiline_manager_t **manager,
                                        lush_memory_pool_t *memory_pool) {
    if (!manager || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate manager structure
    lle_multiline_manager_t *mgr =
        lle_pool_alloc(sizeof(lle_multiline_manager_t));
    if (!mgr) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Initialize fields
    memset(mgr, 0, sizeof(lle_multiline_manager_t));
    mgr->memory_pool = memory_pool;
    mgr->analysis_count = 0;
    mgr->line_updates = 0;
    mgr->perf_monitor = NULL; // Optional - can be set later

    *manager = mgr;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a multiline manager and free resources
 * @param manager The manager to destroy
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if manager is
 * NULL
 */
lle_result_t lle_multiline_manager_destroy(lle_multiline_manager_t *manager) {
    if (!manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Free manager structure
    lle_pool_free(manager);

    return LLE_SUCCESS;
}

/* ============================================================================
 * MULTILINE MANAGER - BUFFER ANALYSIS
 * ============================================================================
 */

/**
 * @brief Helper to convert continuation_state_t to lle_multiline_state_t
 * @param state The continuation state to convert
 * @return Corresponding LLE multiline state enum value
 */
static lle_multiline_state_t
convert_to_lle_state(const continuation_state_t *state) {
    if (!state) {
        return LLE_MULTILINE_STATE_NONE;
    }

    // Priority order matches input_continuation.c logic
    if (state->in_single_quote) {
        return LLE_MULTILINE_STATE_QUOTE_SINGLE;
    }
    if (state->in_double_quote) {
        return LLE_MULTILINE_STATE_QUOTE_DOUBLE;
    }
    if (state->in_backtick) {
        return LLE_MULTILINE_STATE_QUOTE_BACKTICK;
    }
    if (state->in_here_doc) {
        return LLE_MULTILINE_STATE_HEREDOC;
    }
    /* Control structures - map to generic states since specific ones don't
     * exist */
    if (state->in_if_statement || state->in_case_statement ||
        state->in_while_loop || state->in_for_loop || state->in_until_loop ||
        state->in_function_definition) {
        return LLE_MULTILINE_STATE_BRACE; /* Use brace as generic control
                                             structure */
    }
    if (state->brace_count > 0) {
        return LLE_MULTILINE_STATE_BRACE;
    }
    if (state->paren_count > 0) {
        return LLE_MULTILINE_STATE_PAREN;
    }
    if (state->bracket_count > 0) {
        return LLE_MULTILINE_STATE_BRACKET;
    }
    if (state->has_continuation) {
        return LLE_MULTILINE_STATE_BACKSLASH;
    }

    return LLE_MULTILINE_STATE_NONE;
}

/**
 * @brief Analyze an entire buffer for multiline state
 * @param manager The multiline manager
 * @param buffer The buffer to analyze
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_multiline_manager_analyze_buffer(lle_multiline_manager_t *manager,
                                     lle_buffer_t *buffer) {
    if (!manager || !buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_result_t result = LLE_SUCCESS;

    // Step 1: Initialize or reset multiline context
    if (!buffer->multiline_ctx) {
        result = lle_multiline_context_init(&buffer->multiline_ctx,
                                            manager->memory_pool);
        if (result != LLE_SUCCESS) {
            return result;
        }
    } else {
        result = lle_multiline_context_reset(buffer->multiline_ctx);
        if (result != LLE_SUCCESS) {
            return result;
        }
    }

    // Step 2: Analyze each line in the buffer
    if (buffer->line_count > 0) {
        // Buffer has line structure - analyze each line
        for (size_t line_idx = 0; line_idx < buffer->line_count; line_idx++) {
            lle_line_info_t *line = &buffer->lines[line_idx];

            // Extract line content
            if (line->start_offset + line->length > buffer->length) {
                return LLE_ERROR_BUFFER_OVERFLOW;
            }

            const char *line_content =
                (const char *)(buffer->data + line->start_offset);
            size_t line_length = line->length;

            // Analyze line
            result = lle_multiline_analyze_line(buffer->multiline_ctx,
                                                line_content, line_length);
            if (result != LLE_SUCCESS) {
                return result;
            }

            // Update line multiline state
            continuation_state_t *state =
                (continuation_state_t *)buffer->multiline_ctx->core_state;
            line->ml_state = convert_to_lle_state(state);

            // Update line flags
            if (buffer->multiline_ctx->needs_continuation) {
                line->flags |= LLE_LINE_FLAG_CONTINUATION;
            } else {
                line->flags &= ~LLE_LINE_FLAG_CONTINUATION;
            }

            manager->line_updates++;
        }
    } else if (buffer->length > 0) {
        /* Buffer has text but no line structure - treat entire buffer as one
         * line */
        result = lle_multiline_analyze_line(
            buffer->multiline_ctx, (const char *)buffer->data, buffer->length);
        if (result != LLE_SUCCESS) {
            return result;
        }
        manager->line_updates++;
    }

    // Step 3: Update buffer-wide multiline status
    buffer->multiline_active = !buffer->multiline_ctx->construct_complete;

    // Update statistics
    manager->analysis_count++;

    return LLE_SUCCESS;
}

/**
 * @brief Update multiline state for a specific line in a buffer
 * @param manager The multiline manager
 * @param buffer The buffer containing the line
 * @param line_index Index of the line to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_manager_update_line_state(
    lle_multiline_manager_t *manager, lle_buffer_t *buffer, size_t line_index) {
    if (!manager || !buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (line_index >= buffer->line_count) {
        return LLE_ERROR_INVALID_PARAMETER; /* Use existing error code for out
                                               of bounds */
    }

    /* For single-line update, we need to re-analyze from the beginning
     * because multiline state is cumulative. This is acceptable for
     * typical shell inputs (<100 lines). */
    return lle_multiline_manager_analyze_buffer(manager, buffer);
}
