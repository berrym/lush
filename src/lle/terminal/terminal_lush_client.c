/**
 * @file terminal_lush_client.c
 * @brief Lush Display Layer Integration (Spec 02 Subsystem 4)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * CRITICAL DESIGN PRINCIPLE:
 * LLE NEVER directly controls terminal or sends escape sequences.
 * ALL display operations go through Lush display system.
 *
 * Key Responsibilities:
 * - Register LLE as Lush display layer client
 * - Convert LLE display content to Lush layer format
 * - Submit display updates through Lush display API
 * - Handle Lush display system errors
 *
 * Spec 02: Terminal Abstraction - Subsystem 4
 */

#include "lle/terminal_abstraction.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * LUSH DISPLAY CLIENT OPERATIONS
 * ============================================================================
 */

/**
 * @brief Initialize Lush display client
 *
 * @param client Output pointer for created client
 * @param display_context Lush display context
 * @param capabilities Terminal capabilities reference
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_lush_display_client_init(lle_lush_display_client_t **client,
                             lush_display_context_t *display_context,
                             lle_terminal_capabilities_t *capabilities) {
    if (!client || !display_context || !capabilities) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_lush_display_client_t *c = calloc(1, sizeof(lle_lush_display_client_t));
    if (!c) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    c->display_context = display_context;
    c->capabilities = capabilities;

    // Configure LLE layer for Lush
    c->layer_config.layer_name = "lle_editing";
    c->layer_config.layer_priority = LUSH_LAYER_PRIORITY_EDITING;
    c->layer_config.supports_transparency = false;
    c->layer_config.requires_full_refresh = true;
    c->layer_config.color_capabilities = capabilities->detected_color_depth;

    // Initialize submission tracking
    c->last_submission_time = 0;
    c->submission_count = 0;

    /* Note: Actual Lush layer registration will happen when Lush
     * display system API is available. For now, store configuration. */
    c->lle_display_layer = NULL;

    *client = c;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy Lush display client
 *
 * @param client Client to destroy
 */
void lle_lush_display_client_destroy(lle_lush_display_client_t *client) {
    if (!client) {
        return;
    }

    /* Note: When Lush display system is implemented, we would unregister
     * the LLE layer here. For now, just cleanup. */

    free(client);
}

/**
 * @brief Convert LLE display content to Lush layer format
 *
 * This function translates LLE's internal display representation to
 * the format expected by Lush display system. It handles line-by-line
 * conversion, cursor position mapping, and attribute translation based
 * on terminal capabilities.
 *
 * @param client Display client instance with capability information
 * @param content LLE display content to convert
 * @param lush_content Output pointer for Lush format content (allocated)
 * @return LLE_SUCCESS on success, LLE_ERROR_OUT_OF_MEMORY on allocation failure
 */
static lle_result_t LLE_MAYBE_UNUSED convert_to_lush_format(
    lle_lush_display_client_t *client, lle_display_content_t *content,
    lush_layer_content_t **lush_content) {
    if (!client || !content || !lush_content) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Note: This is a placeholder implementation. When Lush display system
     * is implemented, this function will perform the actual conversion:
     *
     * 1. Allocate lush_layer_content_t structure
     * 2. Convert each LLE display line to Lush display line format
     * 3. Include cursor position information
     * 4. Apply color/attribute information based on capabilities
     * 5. Return converted content
     *
     * For now, return success to allow compilation.
     */

    *lush_content = NULL;
    return LLE_SUCCESS;
}

/**
 * @brief Submit display content to Lush display system
 *
 * CRITICAL: This is the ONLY way LLE updates the terminal display.
 * LLE renders through the Lush display system - NEVER sends escape sequences
 * directly.
 *
 * Architecture: LLE display_content -> Lush display_controller -> Terminal
 * output Following the proven pattern from Fish, Zsh, and other modern line
 * editors.
 *
 * @param client Display client instance
 * @param content Display content to submit
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_lush_display_client_submit_content(lle_lush_display_client_t *client,
                                       lle_display_content_t *content) {
    if (!client || !content) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    uint64_t submission_start = lle_get_current_time_microseconds();

    /* For now, use simple direct rendering until display_controller integration
     * is complete.
     *
     * The proper architecture (to be implemented):
     * 1. Convert LLE display_content to display_controller format
     * 2. Call display_controller_display(controller, prompt, command, output,
     * size)
     * 3. Write the composed output to stdout
     *
     * Current temporary implementation writes directly to terminal.
     * This matches what GNU readline does (rl_redisplay writes to stdout).
     */

    // DEBUG: Log what we're about to render
    fprintf(stderr, "[DISPLAY] Rendering %zu lines, cursor at (%zu, %zu)\n",
            content->line_count, content->cursor_line, content->cursor_column);
    for (size_t i = 0; i < content->line_count; i++) {
        fprintf(stderr, "[DISPLAY] Line %zu: len=%zu, content='%.*s'\n", i,
                content->lines[i].length, (int)content->lines[i].length,
                content->lines[i].content ? content->lines[i].content
                                          : "(null)");
    }

    /* All write() calls below target stdout for terminal display
     * (mirroring what GNU readline's rl_redisplay does). Return values
     * are intentionally discarded via the (void)! suppression idiom --
     * short writes on a TTY are rare and there is no useful recovery
     * path inside the render function. The fflush(stdout) at the end
     * of this function provides the cross-platform commit point. */

    // Clear line and move to start: \r\033[K
    (void)!write(STDOUT_FILENO, "\r\033[K", 4);

    // Write each line of content
    for (size_t i = 0; i < content->line_count; i++) {
        if (i > 0) {
            (void)!write(STDOUT_FILENO, "\r\n", 2); // CR+LF for new line
        }
        if (content->lines[i].content && content->lines[i].length > 0) {
            (void)!write(STDOUT_FILENO, content->lines[i].content,
                         content->lines[i].length);
        }
    }

    /* Position cursor at correct location using escape sequences
     * This follows the same pattern as readline's rl_redisplay()
     */
    if (content->line_count > 0) {
        // If we're not on the last line, move cursor up
        size_t current_line = content->line_count - 1;
        if (content->cursor_line < current_line) {
            size_t lines_up = current_line - content->cursor_line;
            char move_up[32];
            int len = snprintf(move_up, sizeof(move_up), "\033[%zuA", lines_up);
            if (len > 0 && (size_t)len < sizeof(move_up)) {
                (void)!write(STDOUT_FILENO, move_up, (size_t)len);
            }
        }

        // Move to correct column
        (void)!write(STDOUT_FILENO, "\r", 1);
        if (content->cursor_column > 0) {
            char move_right[32];
            int len = snprintf(move_right, sizeof(move_right), "\033[%zuC",
                               content->cursor_column);
            if (len > 0 && (size_t)len < sizeof(move_right)) {
                (void)!write(STDOUT_FILENO, move_right, (size_t)len);
            }
        }
    }

    // Flush to ensure immediate display
    fflush(stdout);

    // Update submission tracking
    client->submission_count++;
    client->last_submission_time = lle_get_current_time_microseconds();

    // Calculate submission latency for performance monitoring
    uint64_t submission_latency =
        client->last_submission_time - submission_start;
    (void)submission_latency; // Will be used for perf monitoring

    return LLE_SUCCESS;
}

/**
 * @brief Convert Lush error codes to LLE error codes
 *
 * @param lush_error Lush error code
 * @return Corresponding LLE error code
 */
lle_result_t lle_convert_lush_error(lush_result_t lush_error) {
    /* Note: When Lush display system is implemented, this will map
     * Lush error codes to LLE error codes. For now, simple mapping:
     */

    if (lush_error == LUSH_SUCCESS) {
        return LLE_SUCCESS;
    }

    // Default: treat as generic error
    return LLE_ERROR_DISPLAY_SUBMISSION;
}
