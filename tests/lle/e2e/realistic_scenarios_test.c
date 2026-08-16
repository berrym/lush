/**
 * @file realistic_scenarios_test.c
 * @brief Comprehensive end-to-end scenario tests for LLE Spec 03
 *
 * Tests realistic editing workflows that combine all subsystems:
 * - Buffer management
 * - UTF-8 index
 * - Cursor manager
 * - Change tracker (undo/redo)
 * - Buffer validator
 *
 * Scenarios simulate real-world shell command editing patterns.
 */

#include "../../../include/lle/buffer_management.h"
#include "../../../include/lle/error_handling.h"
#include "../../../include/lle/memory_management.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/// External global from test_memory_mock.c
extern lush_memory_pool_t *global_memory_pool;

/// Test macros
#define TEST(name)                                                             \
    static void name(void);                                                    \
    static void name##_wrapper(void) {                                         \
        printf("[ TEST ] %s\n", #name);                                        \
        name();                                                                \
        printf("[ PASS ] %s\n\n", #name);                                      \
    }                                                                          \
    static void name(void)

#define ASSERT_SUCCESS(result, msg)                                            \
    do {                                                                       \
        if ((result) != LLE_SUCCESS) {                                         \
            printf("[ FAIL ] %s: %s (code %d)\n", msg, #result, result);       \
            assert(0);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(actual, expected, msg)                                       \
    do {                                                                       \
        if ((actual) != (expected)) {                                          \
            printf("[ FAIL ] %s: expected %zu, got %zu\n", msg,                \
                   (size_t)(expected), (size_t)(actual));                      \
            assert(0);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_STR_EQ(actual, expected, msg)                                   \
    do {                                                                       \
        if (strncmp(actual, expected, strlen(expected)) != 0) {                \
            printf("[ FAIL ] %s: expected '%s', got '%.*s'\n", msg, expected,  \
                   (int)strlen(expected), actual);                             \
            assert(0);                                                         \
        }                                                                      \
    } while (0)

/* ============================================================================
 * SCENARIO 1: BASIC COMMAND EDITING
 * ============================================================================
 */

TEST(scenario_basic_command_editing) {
    printf("  Scenario: User types 'ls -la', realizes they want 'ls -lah', "
           "edits to add 'h'\n");

    lle_buffer_t *buffer = NULL;
    lle_change_tracker_t *tracker = NULL;
    lle_result_t result;

    /// Initialize
    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Create buffer");

    result = lle_change_tracker_init(&tracker, global_memory_pool, 1000);
    ASSERT_SUCCESS(result, "Create tracker");

    buffer->change_tracking_enabled = true;

    /// User types: "ls -la"
    lle_change_sequence_t *seq1 = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "type command", &seq1);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq1;

    result = lle_buffer_insert_text(buffer, 0, "ls -la", 6);
    ASSERT_SUCCESS(result, "Insert 'ls -la'");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");

    /// Verify state
    ASSERT_EQ(buffer->length, 6, "Buffer length is 6");
    ASSERT_STR_EQ((char *)buffer->data, "ls -la", "Buffer content");

    /// User realizes they want "-lah", moves cursor to end and adds 'h'
    lle_change_sequence_t *seq2 = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "add h", &seq2);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq2;

    result = lle_buffer_insert_text(buffer, 6, "h", 1);
    ASSERT_SUCCESS(result, "Insert 'h'");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");

    /// Verify final state
    ASSERT_EQ(buffer->length, 7, "Buffer length is 7");
    ASSERT_STR_EQ((char *)buffer->data, "ls -lah", "Final buffer content");

    /// User can undo the 'h' addition
    result = lle_change_tracker_undo(tracker, buffer);
    ASSERT_SUCCESS(result, "Undo add h");
    ASSERT_EQ(buffer->length, 6, "Buffer length back to 6");
    ASSERT_STR_EQ((char *)buffer->data, "ls -la", "Buffer after undo");

    /// And redo it
    result = lle_change_tracker_redo(tracker, buffer);
    ASSERT_SUCCESS(result, "Redo add h");
    ASSERT_EQ(buffer->length, 7, "Buffer length is 7 again");
    ASSERT_STR_EQ((char *)buffer->data, "ls -lah", "Buffer after redo");

    /// Cleanup
    lle_change_tracker_destroy(tracker);
    lle_buffer_destroy(buffer);
}

/* ============================================================================
 * SCENARIO 2: TYPO CORRECTION WITH UTF-8
 * ============================================================================
 */

TEST(scenario_typo_correction_with_utf8) {
    printf(
        "  Scenario: User types command with emoji, makes typo, corrects it\n");

    lle_buffer_t *buffer = NULL;
    lle_change_tracker_t *tracker = NULL;
    lle_cursor_manager_t *cursor_mgr = NULL;
    lle_result_t result;

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Create buffer");

    result = lle_change_tracker_init(&tracker, global_memory_pool, 1000);
    ASSERT_SUCCESS(result, "Create tracker");

    result = lle_cursor_manager_init(&cursor_mgr, buffer);
    ASSERT_SUCCESS(result, "Create cursor manager");

    buffer->change_tracking_enabled = true;

    /// User types: "echo 'Hello U+1F30D Wrold'" (typo: Wrold)
    lle_change_sequence_t *seq = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "type with typo", &seq);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 0,
                                    "echo 'Hello \xf0\x9f\x8c\x8d Wrold'", 23);
    ASSERT_SUCCESS(result, "Insert text with UTF-8");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");

    /// Verify UTF-8 counts
    ASSERT_EQ(buffer->codepoint_count, 20,
              "Codepoint count (emoji is 1 codepoint)");

    /// User notices typo, deletes "Wrold" and types "World"
    result = lle_change_tracker_begin_sequence(tracker, "fix typo", &seq);
    ASSERT_SUCCESS(result, "Begin fix sequence");
    buffer->current_sequence = seq;

    /// Delete "Wrold" (5 bytes at position 17) - emoji is 4 bytes
    result = lle_buffer_delete_text(buffer, 17, 5);
    ASSERT_SUCCESS(result, "Delete 'Wrold'");

    /// Insert "World"
    result = lle_buffer_insert_text(buffer, 17, "World", 5);
    ASSERT_SUCCESS(result, "Insert 'World'");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete fix");

    /// Verify corrected text
    ASSERT_STR_EQ((char *)buffer->data, "echo 'Hello \xf0\x9f\x8c\x8d World'",
                  "Corrected text");

    /// Undo and redo work correctly
    result = lle_change_tracker_undo(tracker, buffer);
    ASSERT_SUCCESS(result, "Undo correction");
    ASSERT_STR_EQ((char *)buffer->data, "echo 'Hello \xf0\x9f\x8c\x8d Wrold'",
                  "Back to typo");

    result = lle_change_tracker_redo(tracker, buffer);
    ASSERT_SUCCESS(result, "Redo correction");
    ASSERT_STR_EQ((char *)buffer->data, "echo 'Hello \xf0\x9f\x8c\x8d World'",
                  "Corrected again");

    /// Cleanup
    lle_cursor_manager_destroy(cursor_mgr);
    lle_change_tracker_destroy(tracker);
    lle_buffer_destroy(buffer);
}

/* ============================================================================
 * SCENARIO 3: COMPLEX COMMAND CONSTRUCTION
 * ============================================================================
 */

TEST(scenario_complex_command_construction) {
    printf("  Scenario: Build complex pipeline command incrementally\n");

    lle_buffer_t *buffer = NULL;
    lle_change_tracker_t *tracker = NULL;
    lle_buffer_validator_t *validator = NULL;
    lle_result_t result;

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Create buffer");

    result = lle_change_tracker_init(&tracker, global_memory_pool, 1000);
    ASSERT_SUCCESS(result, "Create tracker");

    result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Create validator");

    buffer->change_tracking_enabled = true;

    /// Step 1: Start with basic grep
    lle_change_sequence_t *seq = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "initial grep", &seq);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 0, "grep error", 10);
    ASSERT_SUCCESS(result, "Insert grep");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");

    /// Validate buffer
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid after step 1");

    /// Step 2: Add filename
    result = lle_change_tracker_begin_sequence(tracker, "add filename", &seq);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 10, " app.log", 8);
    ASSERT_SUCCESS(result, "Add filename");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");

    ASSERT_STR_EQ((char *)buffer->data, "grep error app.log",
                  "After adding filename");

    /// Step 3: Add pipe to sort
    result = lle_change_tracker_begin_sequence(tracker, "add pipe", &seq);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 18, " | sort", 7);
    ASSERT_SUCCESS(result, "Add pipe");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");

    /// Step 4: Add unique filter
    result = lle_change_tracker_begin_sequence(tracker, "add uniq", &seq);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 25, " | uniq -c", 10);
    ASSERT_SUCCESS(result, "Add uniq");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");

    /// Verify final complex command
    ASSERT_STR_EQ((char *)buffer->data, "grep error app.log | sort | uniq -c",
                  "Final complex command");
    ASSERT_EQ(buffer->length, 35, "Final buffer length");

    /// Validate final buffer
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid after complete construction");

    /// User can undo steps
    result = lle_change_tracker_undo(tracker, buffer); /// Remove uniq
    ASSERT_SUCCESS(result, "Undo step 4");
    ASSERT_STR_EQ((char *)buffer->data, "grep error app.log | sort",
                  "After undo 1");

    result = lle_change_tracker_undo(tracker, buffer); /// Remove sort
    ASSERT_SUCCESS(result, "Undo step 3");
    ASSERT_STR_EQ((char *)buffer->data, "grep error app.log", "After undo 2");

    result = lle_change_tracker_undo(tracker, buffer); /// Remove filename
    ASSERT_SUCCESS(result, "Undo step 2");
    ASSERT_STR_EQ((char *)buffer->data, "grep error", "After undo 3");

    /// Redo all steps
    result = lle_change_tracker_redo(tracker, buffer);
    ASSERT_SUCCESS(result, "Redo step 2");

    result = lle_change_tracker_redo(tracker, buffer);
    ASSERT_SUCCESS(result, "Redo step 3");

    result = lle_change_tracker_redo(tracker, buffer);
    ASSERT_SUCCESS(result, "Redo step 4");

    ASSERT_STR_EQ((char *)buffer->data, "grep error app.log | sort | uniq -c",
                  "Back to final state");

    /// Cleanup
    lle_buffer_validator_destroy(validator);
    lle_change_tracker_destroy(tracker);
    lle_buffer_destroy(buffer);
}

/* ============================================================================
 * SCENARIO 4: CURSOR NAVIGATION DURING EDITING
 * ============================================================================
 */

TEST(scenario_cursor_navigation_editing) {
    printf("  Scenario: Navigate and edit in middle of command\n");

    lle_buffer_t *buffer = NULL;
    lle_cursor_manager_t *cursor_mgr = NULL;
    lle_change_tracker_t *tracker = NULL;
    lle_result_t result;

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Create buffer");

    result = lle_cursor_manager_init(&cursor_mgr, buffer);
    ASSERT_SUCCESS(result, "Create cursor manager");

    result = lle_change_tracker_init(&tracker, global_memory_pool, 1000);
    ASSERT_SUCCESS(result, "Create tracker");

    buffer->change_tracking_enabled = true;

    /// Initial command: "find . -name test.txt"
    lle_change_sequence_t *seq = NULL;
    result =
        lle_change_tracker_begin_sequence(tracker, "initial command", &seq);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 0, "find . -name test.txt", 21);
    ASSERT_SUCCESS(result, "Insert command");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");

    /// User wants to change "test.txt" to "*.txt"
    /// Navigate to position 13 (start of "test.txt")
    result = lle_cursor_manager_move_to_byte_offset(cursor_mgr, 13);
    ASSERT_SUCCESS(result, "Move cursor to position 13");

    /// The cursor actually lands at byte 13 on the single command line, not
    /// merely that the move call reported success.
    lle_cursor_position_t pos;
    result = lle_cursor_manager_get_position(cursor_mgr, &pos);
    ASSERT_SUCCESS(result, "Read cursor position");
    ASSERT_EQ(pos.byte_offset, 13, "cursor sits at byte 13");
    ASSERT_EQ(pos.line_number, 0, "cursor stays on the first line");

    /// Delete "test" (4 bytes)
    result = lle_change_tracker_begin_sequence(tracker, "delete test", &seq);
    ASSERT_SUCCESS(result, "Begin delete");
    buffer->current_sequence = seq;

    result = lle_buffer_delete_text(buffer, 13, 4);
    ASSERT_SUCCESS(result, "Delete 'test'");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete delete");

    /// Insert "*"
    result =
        lle_change_tracker_begin_sequence(tracker, "insert asterisk", &seq);
    ASSERT_SUCCESS(result, "Begin insert");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 13, "*", 1);
    ASSERT_SUCCESS(result, "Insert '*'");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete insert");

    /// Verify result
    ASSERT_STR_EQ((char *)buffer->data, "find . -name *.txt",
                  "Modified command");
    ASSERT_EQ(buffer->length, 18, "Modified buffer length");

    /// Undo changes
    result = lle_change_tracker_undo(tracker, buffer); /* Undo insert * */
    ASSERT_SUCCESS(result, "Undo insert");

    result = lle_change_tracker_undo(tracker, buffer); /// Undo delete test
    ASSERT_SUCCESS(result, "Undo delete");

    ASSERT_STR_EQ((char *)buffer->data, "find . -name test.txt",
                  "Back to original");

    /// Cleanup
    lle_change_tracker_destroy(tracker);
    lle_cursor_manager_destroy(cursor_mgr);
    lle_buffer_destroy(buffer);
}

/* ============================================================================
 * SCENARIO 5: BUFFER VALIDATION THROUGHOUT EDITING
 * ============================================================================
 */

TEST(scenario_continuous_validation) {
    printf("  Scenario: Validate buffer integrity throughout complex editing "
           "session\n");

    lle_buffer_t *buffer = NULL;
    lle_buffer_validator_t *validator = NULL;
    lle_change_tracker_t *tracker = NULL;
    lle_result_t result;

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Create buffer");

    result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Create validator");

    result = lle_change_tracker_init(&tracker, global_memory_pool, 1000);
    ASSERT_SUCCESS(result, "Create tracker");

    buffer->change_tracking_enabled = true;

    /// Series of operations with validation after each
    lle_change_sequence_t *seq = NULL;

    /// Op 1: Insert
    result = lle_change_tracker_begin_sequence(tracker, "op1", &seq);
    ASSERT_SUCCESS(result, "Begin op1");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 0, "cat file.txt", 12);
    ASSERT_SUCCESS(result, "Insert text");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete op1");

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Valid after op1");

    /// Op 2: Append
    result = lle_change_tracker_begin_sequence(tracker, "op2", &seq);
    ASSERT_SUCCESS(result, "Begin op2");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 12, " | grep pattern", 15);
    ASSERT_SUCCESS(result, "Append text");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete op2");

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Valid after op2");

    /// Op 3: Insert in middle
    result = lle_change_tracker_begin_sequence(tracker, "op3", &seq);
    ASSERT_SUCCESS(result, "Begin op3");
    buffer->current_sequence = seq;

    result = lle_buffer_insert_text(buffer, 4, "big_", 4);
    ASSERT_SUCCESS(result, "Insert in middle");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete op3");

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Valid after op3");

    ASSERT_STR_EQ((char *)buffer->data, "cat big_file.txt | grep pattern",
                  "After all ops");

    /// Op 4: Delete
    result = lle_change_tracker_begin_sequence(tracker, "op4", &seq);
    ASSERT_SUCCESS(result, "Begin op4");
    buffer->current_sequence = seq;

    result = lle_buffer_delete_text(buffer, 4, 4); /// Remove "big_"
    ASSERT_SUCCESS(result, "Delete text");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete op4");

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Valid after op4");

    ASSERT_STR_EQ((char *)buffer->data, "cat file.txt | grep pattern",
                  "After delete");

    /// Verify validation statistics
    printf("    Validations performed: %u\n", validator->validation_count);
    printf("    Validation failures: %u\n", validator->validation_failures);
    ASSERT_EQ(validator->validation_failures, 0, "Zero validation failures");

    /// Cleanup
    lle_change_tracker_destroy(tracker);
    lle_buffer_validator_destroy(validator);
    lle_buffer_destroy(buffer);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("=================================================\n");
    printf("LLE Spec 03 - End-to-End Realistic Scenarios\n");
    printf("=================================================\n\n");

    printf("Scenario 1: Basic Command Editing\n");
    printf("--------------------------------------------------\n");
    scenario_basic_command_editing_wrapper();

    printf("Scenario 2: Typo Correction with UTF-8\n");
    printf("--------------------------------------------------\n");
    scenario_typo_correction_with_utf8_wrapper();

    printf("Scenario 3: Complex Command Construction\n");
    printf("--------------------------------------------------\n");
    scenario_complex_command_construction_wrapper();

    printf("Scenario 4: Cursor Navigation During Editing\n");
    printf("--------------------------------------------------\n");
    scenario_cursor_navigation_editing_wrapper();

    printf("Scenario 5: Continuous Buffer Validation\n");
    printf("--------------------------------------------------\n");
    scenario_continuous_validation_wrapper();

    printf("=================================================\n");
    printf("All realistic scenarios passed!\n");
    printf("=================================================\n");

    return 0;
}
