/**
 * @file builtin_widgets.c
 * @brief LLE Built-in Widget Implementations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides core widgets that are automatically registered when the editor
 * is created. These widgets provide essential editing operations that can
 * be bound to keys, triggered by hooks, or invoked programmatically.
 *
 * Specification: docs/lle_specification/07_extensibility_framework_complete.md
 * Section: 4 - Widget System Architecture
 *
 * Widget Categories:
 * - Movement widgets: cursor navigation
 * - Editing widgets: text manipulation
 * - History widgets: command history navigation
 * - Display widgets: screen refresh and clearing
 * - Completion widgets: tab completion operations
 *
 * Naming Convention (ZSH-style):
 * - lowercase-with-hyphens for all widget names
 * - e.g., "forward-char", "backward-word", "kill-line"
 */

#include "display/display_controller.h"
#include "lle/buffer_management.h"
#include "lle/error_handling.h"
#include "lle/keybinding_actions.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_integration.h"
#include "lle/prompt/composer.h"
#include "lle/prompt/prompt_expansion.h"
#include "lle/widget_hooks.h"
#include "lle/widget_system.h"

/* ============================================================================
 * MOVEMENT WIDGETS
 * ============================================================================
 */

/**
 * @brief Move cursor forward one character
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_forward_char(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_forward_char(editor);
}

/**
 * @brief Move cursor backward one character
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_backward_char(lle_editor_t *editor,
                                         void *user_data) {
    (void)user_data;
    return lle_backward_char(editor);
}

/**
 * @brief Move cursor forward one word
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_forward_word(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_forward_word(editor);
}

/**
 * @brief Move cursor backward one word
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_backward_word(lle_editor_t *editor,
                                         void *user_data) {
    (void)user_data;
    return lle_backward_word(editor);
}

/**
 * @brief Move cursor to beginning of line
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_beginning_of_line(lle_editor_t *editor,
                                             void *user_data) {
    (void)user_data;
    return lle_beginning_of_line(editor);
}

/**
 * @brief Move cursor to end of line
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_end_of_line(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_end_of_line(editor);
}

/**
 * @brief Move cursor to beginning of buffer
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_beginning_of_buffer(lle_editor_t *editor,
                                               void *user_data) {
    (void)user_data;
    return lle_beginning_of_buffer(editor);
}

/**
 * @brief Move cursor to end of buffer
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_end_of_buffer(lle_editor_t *editor,
                                         void *user_data) {
    (void)user_data;
    return lle_end_of_buffer(editor);
}

/* ============================================================================
 * EDITING WIDGETS
 * ============================================================================
 */

/**
 * @brief Delete character at cursor (delete-char)
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_delete_char(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_delete_char(editor);
}

/**
 * @brief Delete character before cursor (backward-delete-char)
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_backward_delete_char(lle_editor_t *editor,
                                                void *user_data) {
    (void)user_data;
    return lle_backward_delete_char(editor);
}

/**
 * @brief Kill from cursor to end of line
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_kill_line(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_kill_line(editor);
}

/**
 * @brief Kill from beginning of line to cursor (unix-line-discard)
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_backward_kill_line(lle_editor_t *editor,
                                              void *user_data) {
    (void)user_data;
    return lle_backward_kill_line(editor);
}

/**
 * @brief Kill word forward
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_kill_word(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_kill_word(editor);
}

/**
 * @brief Kill word backward (unix-word-rubout)
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_backward_kill_word(lle_editor_t *editor,
                                              void *user_data) {
    (void)user_data;
    return lle_backward_kill_word(editor);
}

/**
 * @brief Yank (paste) from kill ring
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_yank(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_yank(editor);
}

/**
 * @brief Transpose characters at cursor
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_transpose_chars(lle_editor_t *editor,
                                           void *user_data) {
    (void)user_data;
    return lle_transpose_chars(editor);
}

/**
 * @brief Transpose words at cursor
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_transpose_words(lle_editor_t *editor,
                                           void *user_data) {
    (void)user_data;
    return lle_transpose_words(editor);
}

/* ============================================================================
 * CASE CHANGE WIDGETS
 * ============================================================================
 */

/**
 * @brief Capitalize word at cursor
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_capitalize_word(lle_editor_t *editor,
                                           void *user_data) {
    (void)user_data;
    return lle_capitalize_word(editor);
}

/**
 * @brief Convert word to uppercase
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_upcase_word(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_upcase_word(editor);
}

/**
 * @brief Convert word to lowercase
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_downcase_word(lle_editor_t *editor,
                                         void *user_data) {
    (void)user_data;
    return lle_downcase_word(editor);
}

/* ============================================================================
 * HISTORY WIDGETS
 * ============================================================================
 */

/**
 * @brief Navigate to previous history entry
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_previous_history(lle_editor_t *editor,
                                            void *user_data) {
    (void)user_data;
    return lle_history_previous(editor);
}

/**
 * @brief Navigate to next history entry
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_next_history(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_history_next(editor);
}

/* Note: beginning-of-history and end-of-history widgets not yet implemented
 * These would require lle_beginning_of_history() and lle_end_of_history()
 * functions in keybinding_actions.c - planned for future implementation */

/* ============================================================================
 * COMPLETION WIDGETS
 * ============================================================================
 */

/**
 * @brief Trigger tab completion
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_complete(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_complete(editor);
}

/* ============================================================================
 * LINE CONTROL WIDGETS
 * ============================================================================
 */

/**
 * @brief Clear screen and redraw
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_clear_screen(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_clear_screen(editor);
}

/* ============================================================================
 * MULTILINE WIDGETS
 * ============================================================================
 */

/**
 * @brief Smart up arrow - line navigation or history
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_smart_up(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_smart_up_arrow(editor);
}

/**
 * @brief Smart down arrow - line navigation or history
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_smart_down(lle_editor_t *editor, void *user_data) {
    (void)user_data;
    return lle_smart_down_arrow(editor);
}

/* ============================================================================
 * TRANSIENT PROMPT WIDGET (Spec 25 Section 12)
 * ============================================================================
 */

/**
 * @brief Apply transient prompt - simplify prompt in scrollback
 *
 * This widget is triggered by the LINE_ACCEPTED hook, which fires after
 * the user presses Enter but before the cursor moves to the output area.
 * At this point:
 * - Cursor is at end of command line
 * - Screen buffer still has valid prompt metrics
 * - Display controller can re-render with transient prompt
 *
 * The transient prompt replaces the fancy multi-line prompt with a minimal
 * version (e.g., ">" ) in the terminal scrollback, reducing visual clutter.
 *
 * This implementation uses dc_apply_transient_prompt() which goes through
 * the screen buffer system, respecting the LLE architecture.
 *
 * @param editor The LLE editor instance
 * @param user_data User data (unused)
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t widget_transient_prompt(lle_editor_t *editor,
                                            void *user_data) {
    (void)user_data;

    /* Get shell integration for composer access */
    if (!g_lle_integration || !g_lle_integration->prompt_composer) {
        return LLE_SUCCESS; /* Graceful degradation */
    }

    lle_prompt_composer_t *composer = g_lle_integration->prompt_composer;

    /* Check if transient prompts are enabled */
    if (!composer->config.enable_transient) {
        return LLE_SUCCESS;
    }

    /* Get active theme */
    const lle_theme_t *theme = lle_theme_registry_get_active(composer->themes);
    if (!theme || !theme->layout.enable_transient ||
        theme->layout.transient_format[0] == '\0') {
        return LLE_SUCCESS;
    }

    /* Render transient format through Spec 28 two-pass expansion engine.
     * This routes the transient format string through lle_prompt_expand()
     * which handles bash \X, zsh %X, and LLE ${segment} syntax. */
    lle_template_render_ctx_t render_ctx =
        lle_composer_create_render_ctx(composer);

    lle_prompt_expand_ctx_t expand_ctx;
    memset(&expand_ctx, 0, sizeof(expand_ctx));
    expand_ctx.template_ctx = &render_ctx;
    expand_ctx.last_exit_status = composer->context.last_exit_code;
    expand_ctx.job_count = composer->context.background_job_count;

    char transient_output[LLE_TRANSIENT_OUTPUT_MAX];
    lle_result_t result = lle_prompt_expand(
        theme->layout.transient_format, transient_output,
        sizeof(transient_output), &expand_ctx);

    if (result != LLE_SUCCESS) {
        return LLE_SUCCESS; /* Graceful degradation on render failure */
    }

    /* Get command text from editor buffer */
    const char *command_text = NULL;
    if (editor && editor->buffer && editor->buffer->data) {
        command_text = editor->buffer->data;
    }

    /* Apply transient prompt through display controller (screen buffer) */
    dc_apply_transient_prompt(transient_output, command_text);

    return LLE_SUCCESS;
}

/* ============================================================================
 * REGISTRATION
 * ============================================================================
 */

/**
 * @brief Register all built-in widgets
 *
 * Called during editor initialization to register the standard set of
 * widgets that provide basic editing functionality.
 *
 * @param registry Widget registry to register widgets in
 * @return LLE_SUCCESS on success, error code if any registration fails
 */
lle_result_t lle_register_builtin_widgets(lle_widget_registry_t *registry) {
    if (!registry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_result_t result;

    /* Movement widgets */
    result = lle_widget_register(registry, "forward-char", widget_forward_char,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result =
        lle_widget_register(registry, "backward-char", widget_backward_char,
                            LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "forward-word", widget_forward_word,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result =
        lle_widget_register(registry, "backward-word", widget_backward_word,
                            LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result =
        lle_widget_register(registry, "beginning-of-line",
                            widget_beginning_of_line, LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "end-of-line", widget_end_of_line,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "beginning-of-buffer",
                                 widget_beginning_of_buffer, LLE_WIDGET_BUILTIN,
                                 NULL);
    if (result != LLE_SUCCESS)
        return result;

    result =
        lle_widget_register(registry, "end-of-buffer", widget_end_of_buffer,
                            LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    /* Editing widgets */
    result = lle_widget_register(registry, "delete-char", widget_delete_char,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "backward-delete-char",
                                 widget_backward_delete_char,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "kill-line", widget_kill_line,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "backward-kill-line",
                                 widget_backward_kill_line, LLE_WIDGET_BUILTIN,
                                 NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "kill-word", widget_kill_word,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "backward-kill-word",
                                 widget_backward_kill_word, LLE_WIDGET_BUILTIN,
                                 NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "yank", widget_yank,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result =
        lle_widget_register(registry, "transpose-chars", widget_transpose_chars,
                            LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result =
        lle_widget_register(registry, "transpose-words", widget_transpose_words,
                            LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    /* Case change widgets */
    result =
        lle_widget_register(registry, "capitalize-word", widget_capitalize_word,
                            LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "upcase-word", widget_upcase_word,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result =
        lle_widget_register(registry, "downcase-word", widget_downcase_word,
                            LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    /* History widgets */
    result =
        lle_widget_register(registry, "previous-history",
                            widget_previous_history, LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "next-history", widget_next_history,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    /* Note: beginning-of-history and end-of-history widgets pending
     * implementation of underlying action functions */

    /* Completion widgets */
    result = lle_widget_register(registry, "complete", widget_complete,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    /* Line control widgets */
    result = lle_widget_register(registry, "clear-screen", widget_clear_screen,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    /* Multiline/smart navigation widgets */
    result = lle_widget_register(registry, "smart-up", widget_smart_up,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    result = lle_widget_register(registry, "smart-down", widget_smart_down,
                                 LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    /* Transient prompt widget (Spec 25 Section 12) */
    result =
        lle_widget_register(registry, "transient-prompt",
                            widget_transient_prompt, LLE_WIDGET_BUILTIN, NULL);
    if (result != LLE_SUCCESS)
        return result;

    return LLE_SUCCESS;
}

/**
 * @brief Register built-in widget hooks
 *
 * Registers widgets for their associated lifecycle hooks. Called after
 * both widget registry and hooks manager are initialized.
 *
 * @param hooks_manager Widget hooks manager
 * @return LLE_SUCCESS on success, error code if any registration fails
 */
lle_result_t
lle_register_builtin_widget_hooks(lle_widget_hooks_manager_t *hooks_manager) {
    if (!hooks_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_result_t result;

    /* Register transient-prompt widget for LINE_ACCEPTED hook
     * This applies transient prompt when user presses Enter, before
     * cursor moves to output area (Spec 25 Section 12) */
    result = lle_widget_hook_register(hooks_manager, LLE_HOOK_LINE_ACCEPTED,
                                      "transient-prompt");
    if (result != LLE_SUCCESS && result != LLE_ERROR_NOT_FOUND) {
        /* NOT_FOUND is acceptable if shell integration not available */
        return result;
    }

    return LLE_SUCCESS;
}
