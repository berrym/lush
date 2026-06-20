/**
 * @file spec_25_keybinding_compliance.c
 * @brief Spec 25 default-keybindings behavioral compliance test
 *
 * Verifies the editing actions and preset loaders defined by Spec 25 by
 * running each against a real editor and asserting the resulting buffer,
 * cursor, or binding -- not merely that the function symbols exist.
 *
 * The kill ring and keybinding-manager APIs are exercised behaviorally by
 * test_kill_ring.c and test_keybinding.c; the per-character/word movement
 * actions by test_utf8_movement.c. This file covers what those do not: the
 * line-movement, deletion, kill, case-change, transpose, insertion, and
 * preset-loading behavior, plus the NULL-editor contract shared by the
 * integration-level actions (history, completion, shell control) whose full
 * behavior requires a live terminal loop.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/buffer_management.h"
#include "lle/keybinding.h"
#include "lle/keybinding_actions.h"
#include "lle/lle_editor.h"
#include "lush_memory_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int assertions_passed = 0;

#define COMPLIANCE_ASSERT(condition, message)                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "COMPLIANCE VIOLATION: %s\n", message);            \
            fprintf(stderr, "   at %s:%d\n", __FILE__, __LINE__);              \
            exit(1);                                                           \
        }                                                                      \
        assertions_passed++;                                                   \
    } while (0)

/// Build an editor holding @p content with the cursor reset to byte 0.
static lle_editor_t *editor_with(const char *content) {
    lle_editor_t *editor = NULL;
    if (lle_editor_create(&editor, NULL) != LLE_SUCCESS || !editor) {
        return NULL;
    }
    if (content && *content) {
        if (lle_buffer_insert_text(editor->buffer, 0, content,
                                   strlen(content)) != LLE_SUCCESS) {
            lle_editor_destroy(editor);
            return NULL;
        }
    }
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, 0);
    return editor;
}

/// Move the cursor to an absolute byte offset.
static void seek(lle_editor_t *editor, size_t offset) {
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, offset);
}

/// Current cursor byte offset.
static size_t cursor(lle_editor_t *editor) {
    lle_cursor_position_t pos;
    lle_cursor_manager_get_position(editor->cursor_manager, &pos);
    return pos.byte_offset;
}

/// True when the buffer text equals @p expected.
static bool text_is(lle_editor_t *editor, const char *expected) {
    size_t len = editor->buffer->length;
    return strlen(expected) == len &&
           (len == 0 || memcmp(editor->buffer->data, expected, len) == 0);
}

int main(void) {
    printf("Spec 25 Default Keybindings Behavioral Compliance\n");
    printf("==================================================\n\n");

    lush_pool_config_t pool_config = lush_pool_get_default_config();
    if (lush_pool_init(&pool_config) != LUSH_POOL_SUCCESS) {
        fprintf(stderr, "FATAL: failed to initialize memory pool\n");
        return 1;
    }

    lle_editor_t *ed;

    /// =====================================================================
    /// Line movement: beginning-of-line / end-of-line
    /// =====================================================================
    ed = editor_with("hello world");
    COMPLIANCE_ASSERT(ed != NULL, "editor creation");
    seek(ed, 5);
    lle_beginning_of_line(ed);
    COMPLIANCE_ASSERT(cursor(ed) == 0, "beginning-of-line moves cursor to 0");
    lle_end_of_line(ed);
    COMPLIANCE_ASSERT(cursor(ed) == 11, "end-of-line moves cursor to EOL");
    lle_editor_destroy(ed);

    /// =====================================================================
    /// Deletion: delete-char / backward-delete-char
    /// =====================================================================
    ed = editor_with("hello");
    seek(ed, 0);
    lle_delete_char(ed);
    COMPLIANCE_ASSERT(text_is(ed, "ello"),
                      "delete-char removes the char at the cursor");
    lle_editor_destroy(ed);

    ed = editor_with("hello");
    seek(ed, 5);
    lle_backward_delete_char(ed);
    COMPLIANCE_ASSERT(
        text_is(ed, "hell"),
        "backward-delete-char removes the char before the cursor");
    lle_editor_destroy(ed);

    /// =====================================================================
    /// Kill: kill-line / kill-word / backward-kill-word
    /// =====================================================================
    ed = editor_with("hello world");
    seek(ed, 5);
    lle_kill_line(ed);
    COMPLIANCE_ASSERT(text_is(ed, "hello"), "kill-line removes through EOL");
    lle_editor_destroy(ed);

    ed = editor_with("hello world");
    seek(ed, 0);
    lle_kill_word(ed);
    COMPLIANCE_ASSERT(text_is(ed, " world"),
                      "kill-word removes the word after the cursor");
    lle_editor_destroy(ed);

    ed = editor_with("hello world");
    seek(ed, 11);
    lle_backward_kill_word(ed);
    COMPLIANCE_ASSERT(text_is(ed, "hello "),
                      "backward-kill-word removes the word before the cursor");
    lle_editor_destroy(ed);

    /// =====================================================================
    /// Case change: upcase / downcase / capitalize word
    /// =====================================================================
    ed = editor_with("hello");
    seek(ed, 0);
    lle_upcase_word(ed);
    COMPLIANCE_ASSERT(text_is(ed, "HELLO"), "upcase-word uppercases the word");
    lle_editor_destroy(ed);

    ed = editor_with("HELLO");
    seek(ed, 0);
    lle_downcase_word(ed);
    COMPLIANCE_ASSERT(text_is(ed, "hello"),
                      "downcase-word lowercases the word");
    lle_editor_destroy(ed);

    ed = editor_with("hello");
    seek(ed, 0);
    lle_capitalize_word(ed);
    COMPLIANCE_ASSERT(text_is(ed, "Hello"),
                      "capitalize-word capitalizes the first letter");
    lle_editor_destroy(ed);

    /// =====================================================================
    /// Transpose and insertion
    /// =====================================================================
    ed = editor_with("abc");
    seek(ed, 1);
    lle_transpose_chars(ed);
    COMPLIANCE_ASSERT(text_is(ed, "bac"),
                      "transpose-chars swaps the two chars around the cursor");
    lle_editor_destroy(ed);

    ed = editor_with("hi");
    seek(ed, 2);
    lle_self_insert(ed, '!');
    COMPLIANCE_ASSERT(text_is(ed, "hi!"),
                      "self-insert inserts the codepoint at the cursor");
    lle_editor_destroy(ed);

    ed = editor_with("foo bar");
    seek(ed, 7);
    lle_unix_word_rubout(ed);
    COMPLIANCE_ASSERT(
        text_is(ed, "foo "),
        "unix-word-rubout kills the whitespace-delimited word back");
    lle_editor_destroy(ed);

    /// =====================================================================
    /// Preset loaders install the documented bindings
    /// =====================================================================
    ed = editor_with("");
    lle_keybinding_manager_t *mgr = NULL;
    COMPLIANCE_ASSERT(lle_keybinding_manager_create(&mgr, NULL) == LLE_SUCCESS,
                      "keybinding manager creation");
    ed->keybinding_manager = mgr;

    COMPLIANCE_ASSERT(lle_keybinding_load_emacs_preset(ed) == LLE_SUCCESS,
                      "emacs preset loads");
    lle_keybinding_action_t *action = NULL;
    COMPLIANCE_ASSERT(lle_keybinding_manager_lookup(mgr, "C-a", &action) ==
                              LLE_SUCCESS &&
                          action != NULL,
                      "emacs preset binds C-a");
    COMPLIANCE_ASSERT(strcmp(action->name, "beginning-of-line") == 0,
                      "C-a is bound to beginning-of-line");
    COMPLIANCE_ASSERT(lle_keybinding_manager_lookup(mgr, "C-e", &action) ==
                              LLE_SUCCESS &&
                          action != NULL,
                      "emacs preset binds C-e");
    COMPLIANCE_ASSERT(strcmp(action->name, "end-of-line") == 0,
                      "C-e is bound to end-of-line");

    COMPLIANCE_ASSERT(lle_keybinding_load_vi_preset(ed) == LLE_SUCCESS,
                      "vi preset loads");

    ed->keybinding_manager = NULL;
    lle_keybinding_manager_destroy(mgr);
    lle_editor_destroy(ed);

    /// =====================================================================
    /// Editor lifecycle: reset clears the buffer
    /// =====================================================================
    ed = editor_with("scratch");
    COMPLIANCE_ASSERT(!text_is(ed, ""), "editor holds its initial content");
    COMPLIANCE_ASSERT(lle_editor_reset(ed) == LLE_SUCCESS, "editor reset");
    COMPLIANCE_ASSERT(text_is(ed, ""), "reset clears the buffer");
    lle_editor_destroy(ed);

    /// =====================================================================
    /// Integration-level actions (history, completion, shell control) need a
    /// live terminal loop; their behavior is exercised by the integration and
    /// e2e suites. Here verify the NULL-editor contract they all share, which
    /// guards against a regression that would crash or wrongly succeed.
    /// =====================================================================
    COMPLIANCE_ASSERT(lle_history_previous(NULL) == LLE_ERROR_INVALID_PARAMETER,
                      "history-previous rejects a NULL editor");
    COMPLIANCE_ASSERT(lle_history_next(NULL) == LLE_ERROR_INVALID_PARAMETER,
                      "history-next rejects a NULL editor");
    COMPLIANCE_ASSERT(lle_complete(NULL) == LLE_ERROR_INVALID_PARAMETER,
                      "complete rejects a NULL editor");
    COMPLIANCE_ASSERT(lle_accept_line(NULL) == LLE_ERROR_INVALID_PARAMETER,
                      "accept-line rejects a NULL editor");
    COMPLIANCE_ASSERT(lle_abort_line(NULL) == LLE_ERROR_INVALID_PARAMETER,
                      "abort-line rejects a NULL editor");
    COMPLIANCE_ASSERT(lle_interrupt(NULL) == LLE_ERROR_INVALID_PARAMETER,
                      "interrupt rejects a NULL editor");
    COMPLIANCE_ASSERT(lle_clear_screen(NULL) == LLE_ERROR_INVALID_PARAMETER,
                      "clear-screen rejects a NULL editor");

    printf("\nSpec 25 behavioral compliance: %d assertions passed\n",
           assertions_passed);
    return 0;
}
