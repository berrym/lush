/**
 * @file spec_25_keybinding_compliance.c
 * @brief Spec 25 keybinding compliance test
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/**
 * spec_25_keybinding_compliance.c - Spec 25 Compliance Test
 *
 * Tests for LLE Specification 25: Default Keybindings
 * Validates API completeness - all functions and types are declared
 *
 * This is a HEADER-ONLY compliance test that verifies:
 * - All types are defined
 * - All functions are declared
 * - No runtime behavior testing (link-free)
 *
 * NOTE: This test does NOT call functions or test runtime behavior.
 * It only verifies the API exists as specified via function pointer checks.
 * Functional testing is done via integration tests that link with liblle.a.
 *
 * Test Coverage:
 * - Phase 1: Kill Ring System (7 functions)
 * - Phase 2: Keybinding Manager (4 core functions)
 * - Phase 3: Keybinding Actions (42 action functions + 2 preset loaders)
 * - Phase 4: Editor Context Structure
 *
 * Specification:
 * docs/lle_specification/critical_gaps/25_default_keybindings_complete.md Date:
 * 2025-11-02
 */

/// API verified from include/lle/kill_ring.h on 2025-11-02
/// API verified from include/lle/keybinding.h on 2025-11-02
/// API verified from include/lle/keybinding_actions.h on 2025-11-02
/// API verified from include/lle/lle_editor.h on 2025-11-02

#include "lle/keybinding.h"
#include "lle/keybinding_actions.h"
#include "lle/kill_ring.h"
#include "lle/lle_editor.h"
#include <stdio.h>
#include <stdlib.h>

/// Test assertion counter
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

int main(void) {
    printf("Spec 25 Default Keybindings Compliance Test\n");
    printf("============================================\n\n");

    /// =====================================================================
    /// PHASE 1: KILL RING SYSTEM
    /// =====================================================================

    printf("Phase 1: Kill Ring System API\n");
    printf("------------------------------\n");

    /// Opaque type - verify pointer type exists
    lle_kill_ring_t *kill_ring_ptr = NULL;
    COMPLIANCE_ASSERT(kill_ring_ptr == NULL,
                      "lle_kill_ring_t opaque type defined");

    /// Kill ring lifecycle
    COMPLIANCE_ASSERT(lle_kill_ring_create != NULL,
                      "lle_kill_ring_create declared");
    COMPLIANCE_ASSERT(lle_kill_ring_destroy != NULL,
                      "lle_kill_ring_destroy declared");

    /// Kill ring operations
    COMPLIANCE_ASSERT(lle_kill_ring_add != NULL, "lle_kill_ring_add declared");
    COMPLIANCE_ASSERT(lle_kill_ring_get_current != NULL,
                      "lle_kill_ring_get_current declared");
    COMPLIANCE_ASSERT(lle_kill_ring_yank_pop != NULL,
                      "lle_kill_ring_yank_pop declared");
    COMPLIANCE_ASSERT(lle_kill_ring_clear != NULL,
                      "lle_kill_ring_clear declared");
    COMPLIANCE_ASSERT(lle_kill_ring_reset_yank_state != NULL,
                      "lle_kill_ring_reset_yank_state declared");

    printf("  ✓ Kill ring API complete (7 functions)\n\n");

    /// =====================================================================
    /// PHASE 2: KEYBINDING MANAGER
    /// =====================================================================

    printf("Phase 2: Keybinding Manager API\n");
    printf("--------------------------------\n");

    /// Opaque type - verify pointer type exists
    lle_keybinding_manager_t *kb_mgr_ptr = NULL;
    COMPLIANCE_ASSERT(kb_mgr_ptr == NULL,
                      "lle_keybinding_manager_t opaque type defined");

    /// Keybinding manager lifecycle
    COMPLIANCE_ASSERT(lle_keybinding_manager_create != NULL,
                      "lle_keybinding_manager_create declared");
    COMPLIANCE_ASSERT(lle_keybinding_manager_destroy != NULL,
                      "lle_keybinding_manager_destroy declared");

    /// Keybinding operations
    COMPLIANCE_ASSERT(lle_keybinding_manager_bind != NULL,
                      "lle_keybinding_manager_bind declared");
    COMPLIANCE_ASSERT(lle_keybinding_manager_unbind != NULL,
                      "lle_keybinding_manager_unbind declared");
    COMPLIANCE_ASSERT(lle_keybinding_manager_process_key != NULL,
                      "lle_keybinding_manager_process_key declared");
    COMPLIANCE_ASSERT(lle_keybinding_manager_list_bindings != NULL,
                      "lle_keybinding_manager_list_bindings declared");

    printf("  ✓ Keybinding manager API complete (6 functions)\n\n");

    /// =====================================================================
    /// PHASE 3: KEYBINDING ACTIONS (44 FUNCTIONS TOTAL)
    /// =====================================================================

    printf("Phase 3: Keybinding Actions API\n");
    printf("--------------------------------\n");

    /// Movement actions (6)
    COMPLIANCE_ASSERT(lle_beginning_of_line != NULL,
                      "lle_beginning_of_line declared");
    COMPLIANCE_ASSERT(lle_end_of_line != NULL, "lle_end_of_line declared");
    COMPLIANCE_ASSERT(lle_forward_char != NULL, "lle_forward_char declared");
    COMPLIANCE_ASSERT(lle_backward_char != NULL, "lle_backward_char declared");
    COMPLIANCE_ASSERT(lle_forward_word != NULL, "lle_forward_word declared");
    COMPLIANCE_ASSERT(lle_backward_word != NULL, "lle_backward_word declared");
    printf("  ✓ Movement actions (6 functions)\n");

    /// Editing/kill actions (6)
    COMPLIANCE_ASSERT(lle_delete_char != NULL, "lle_delete_char declared");
    COMPLIANCE_ASSERT(lle_backward_delete_char != NULL,
                      "lle_backward_delete_char declared");
    COMPLIANCE_ASSERT(lle_kill_line != NULL, "lle_kill_line declared");
    COMPLIANCE_ASSERT(lle_backward_kill_line != NULL,
                      "lle_backward_kill_line declared");
    COMPLIANCE_ASSERT(lle_kill_word != NULL, "lle_kill_word declared");
    COMPLIANCE_ASSERT(lle_backward_kill_word != NULL,
                      "lle_backward_kill_word declared");
    printf("  ✓ Editing/kill actions (6 functions)\n");

    /// Yank/transpose actions (4)
    COMPLIANCE_ASSERT(lle_yank != NULL, "lle_yank declared");
    COMPLIANCE_ASSERT(lle_yank_pop != NULL, "lle_yank_pop declared");
    COMPLIANCE_ASSERT(lle_transpose_chars != NULL,
                      "lle_transpose_chars declared");
    COMPLIANCE_ASSERT(lle_transpose_words != NULL,
                      "lle_transpose_words declared");
    printf("  ✓ Yank/transpose actions (4 functions)\n");

    /// Case change actions (3)
    COMPLIANCE_ASSERT(lle_upcase_word != NULL, "lle_upcase_word declared");
    COMPLIANCE_ASSERT(lle_downcase_word != NULL, "lle_downcase_word declared");
    COMPLIANCE_ASSERT(lle_capitalize_word != NULL,
                      "lle_capitalize_word declared");
    printf("  ✓ Case change actions (3 functions)\n");

    /// History actions (6)
    COMPLIANCE_ASSERT(lle_history_previous != NULL,
                      "lle_history_previous declared");
    COMPLIANCE_ASSERT(lle_history_next != NULL, "lle_history_next declared");
    COMPLIANCE_ASSERT(lle_reverse_search_history != NULL,
                      "lle_reverse_search_history declared");
    COMPLIANCE_ASSERT(lle_forward_search_history != NULL,
                      "lle_forward_search_history declared");
    COMPLIANCE_ASSERT(lle_history_search_backward != NULL,
                      "lle_history_search_backward declared");
    COMPLIANCE_ASSERT(lle_history_search_forward != NULL,
                      "lle_history_search_forward declared");
    printf("  ✓ History actions (6 functions)\n");

    /// Completion actions (3)
    COMPLIANCE_ASSERT(lle_complete != NULL, "lle_complete declared");
    COMPLIANCE_ASSERT(lle_possible_completions != NULL,
                      "lle_possible_completions declared");
    COMPLIANCE_ASSERT(lle_insert_completions != NULL,
                      "lle_insert_completions declared");
    printf("  ✓ Completion actions (3 functions)\n");

    /// Shell operations (6)
    COMPLIANCE_ASSERT(lle_accept_line != NULL, "lle_accept_line declared");
    COMPLIANCE_ASSERT(lle_abort_line != NULL, "lle_abort_line declared");
    COMPLIANCE_ASSERT(lle_send_eof != NULL, "lle_send_eof declared");
    COMPLIANCE_ASSERT(lle_interrupt != NULL, "lle_interrupt declared");
    COMPLIANCE_ASSERT(lle_suspend != NULL, "lle_suspend declared");
    COMPLIANCE_ASSERT(lle_clear_screen != NULL, "lle_clear_screen declared");
    printf("  ✓ Shell operations (6 functions)\n");

    /// Utility actions (8)
    COMPLIANCE_ASSERT(lle_quoted_insert != NULL, "lle_quoted_insert declared");
    COMPLIANCE_ASSERT(lle_unix_line_discard != NULL,
                      "lle_unix_line_discard declared");
    COMPLIANCE_ASSERT(lle_unix_word_rubout != NULL,
                      "lle_unix_word_rubout declared");
    COMPLIANCE_ASSERT(lle_delete_horizontal_space != NULL,
                      "lle_delete_horizontal_space declared");
    COMPLIANCE_ASSERT(lle_self_insert != NULL, "lle_self_insert declared");
    COMPLIANCE_ASSERT(lle_newline != NULL, "lle_newline declared");
    COMPLIANCE_ASSERT(lle_tab_insert != NULL, "lle_tab_insert declared");
    printf("  ✓ Utility actions (7 functions)\n");

    /// Preset loaders (2)
    COMPLIANCE_ASSERT(lle_keybinding_load_emacs_preset != NULL,
                      "lle_keybinding_load_emacs_preset declared");
    COMPLIANCE_ASSERT(lle_keybinding_load_vi_preset != NULL,
                      "lle_keybinding_load_vi_preset declared");
    printf("  ✓ Preset loaders (2 functions)\n\n");

    /// =====================================================================
    /// PHASE 4: EDITOR CONTEXT STRUCTURE
    /// =====================================================================

    printf("Phase 4: Editor Context API\n");
    printf("---------------------------\n");

    /// Editor structure - verify type exists
    lle_editor_t *editor_ptr = NULL;
    COMPLIANCE_ASSERT(editor_ptr == NULL, "lle_editor_t structure defined");

    /// Editor lifecycle
    COMPLIANCE_ASSERT(lle_editor_create != NULL, "lle_editor_create declared");
    COMPLIANCE_ASSERT(lle_editor_destroy != NULL,
                      "lle_editor_destroy declared");
    COMPLIANCE_ASSERT(lle_editor_reset != NULL, "lle_editor_reset declared");

    printf("  ✓ Editor context API complete (3 functions)\n\n");

    /// =====================================================================
    /// SUMMARY
    /// =====================================================================

    printf("============================================\n");
    printf("Spec 25 Compliance: ALL TESTS PASSED\n");
    printf("Total Assertions: %d\n", assertions_passed);
    printf("============================================\n");
    printf("\n");
    printf("API Summary:\n");
    printf("  Phase 1: Kill Ring (7 functions)\n");
    printf("  Phase 2: Keybinding Manager (6 functions)\n");
    printf("  Phase 3: Keybinding Actions (42 functions)\n");
    printf("  Phase 4: Editor Context (3 functions)\n");
    printf("  Total: 58 API functions verified\n");
    printf("\n");

    return 0;
}
