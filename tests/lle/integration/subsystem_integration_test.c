/**
 * @file subsystem_integration_test.c
 * @brief LLE Buffer Management Subsystem Integration Tests
 *
 * Tests the integration and interaction between multiple LLE subsystems:
 * - Buffer operations (insert/delete/replace)
 * - UTF-8 index (position mapping)
 * - Cursor manager (cursor positioning)
 * - Buffer validator (integrity checking)
 * - Change tracker (undo/redo)
 *
 * These tests verify that subsystems work correctly together and maintain
 * consistency across operations.
 */

#include "lle/buffer_management.h"
#include "lle/error_handling.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// External memory pool from mock
extern lush_memory_pool_t *global_memory_pool;

/// Test counter
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/// Current test name
static const char *current_test = NULL;

/// Test macros
#define TEST(name)                                                             \
    do {                                                                       \
        current_test = name;                                                   \
        tests_run++;                                                           \
        printf("[ TEST ] %s\n", name);                                         \
    } while (0)

#define PASS()                                                                 \
    do {                                                                       \
        tests_passed++;                                                        \
        printf("[ PASS ] %s\n", current_test);                                 \
        return;                                                                \
    } while (0)

#define FAIL(msg)                                                              \
    do {                                                                       \
        tests_failed++;                                                        \
        printf("[ FAIL ] %s: %s\n", current_test, msg);                        \
        return;                                                                \
    } while (0)

#define ASSERT_SUCCESS(result, msg)                                            \
    do {                                                                       \
        if ((result) != LLE_SUCCESS) {                                         \
            FAIL(msg);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(a, b, msg)                                                   \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            printf("  Expected: %zu, Got: %zu\n", (size_t)(b), (size_t)(a));   \
            FAIL(msg);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            FAIL(msg);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(cond, msg)                                                \
    do {                                                                       \
        if (cond) {                                                            \
            FAIL(msg);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_STR_EQ(a, b, msg)                                               \
    do {                                                                       \
        if (strcmp((a), (b)) != 0) {                                           \
            printf("  Expected: '%s', Got: '%s'\n", (b), (a));                 \
            FAIL(msg);                                                         \
        }                                                                      \
    } while (0)

/* ============================================================================
 * INTEGRATION TEST 1: Buffer Operations + UTF-8 Index
 * ============================================================================
 * Verify that buffer operations correctly update UTF-8 index
 */

static void test_insert_updates_utf8_index() {
    TEST("Insert text updates UTF-8 index correctly");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Insert ASCII text
    const char *text1 = "Hello";
    result = lle_buffer_insert_text(buffer, 0, text1, strlen(text1));
    ASSERT_SUCCESS(result, "ASCII text insertion succeeds");

    /// Verify byte count matches codepoint count for ASCII
    ASSERT_EQ(buffer->length, 5, "Buffer length correct");
    ASSERT_EQ(buffer->codepoint_count, 5, "Codepoint count correct for ASCII");

    /// Insert UTF-8 text with multibyte characters
    const char *text2 = "世界"; /// 2 Chinese characters, 6 bytes
    result = lle_buffer_insert_text(buffer, 5, text2, strlen(text2));
    ASSERT_SUCCESS(result, "UTF-8 text insertion succeeds");

    /// Verify counts: 5 ASCII + 6 UTF-8 bytes = 11 bytes, but 7 codepoints
    ASSERT_EQ(buffer->length, 11, "Buffer length includes UTF-8 bytes");
    ASSERT_EQ(buffer->codepoint_count, 7, "Codepoint count correct with UTF-8");

    /// UTF-8 index is lazily built - after modifications it's invalidated.
    /// The codepoint_count is updated incrementally (verified above),
    /// but the full index structure needs rebuilding on next access.
    ASSERT_FALSE(buffer->utf8_index_valid,
                 "UTF-8 index invalidated after modification");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_delete_updates_utf8_index() {
    TEST("Delete text updates UTF-8 index correctly");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Insert mixed ASCII and UTF-8
    const char *text = "Hello世界World";
    result = lle_buffer_insert_text(buffer, 0, text, strlen(text));
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    size_t initial_length = buffer->length;
    size_t initial_codepoints = buffer->codepoint_count;

    /// Delete the UTF-8 characters (6 bytes, 2 codepoints)
    result = lle_buffer_delete_text(buffer, 5, 6);
    ASSERT_SUCCESS(result, "UTF-8 deletion succeeds");

    /// Verify counts updated
    ASSERT_EQ(buffer->length, initial_length - 6,
              "Buffer length decreased by 6 bytes");
    ASSERT_EQ(buffer->codepoint_count, initial_codepoints - 2,
              "Codepoint count decreased by 2");

    /// Verify content
    ASSERT_STR_EQ(buffer->data, "HelloWorld", "Content correct after deletion");

    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * INTEGRATION TEST 2: Buffer Operations + Cursor Manager
 * ============================================================================
 * Verify that buffer operations correctly update cursor positions
 */

static void test_insert_adjusts_cursor() {
    TEST("Insert text adjusts cursor position correctly");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Initialize cursor manager
    lle_cursor_manager_t *cursor_mgr = NULL;
    result = lle_cursor_manager_init(&cursor_mgr, buffer);
    ASSERT_SUCCESS(result, "Cursor manager init succeeds");

    /// Insert initial text
    const char *text1 = "Hello";
    result = lle_buffer_insert_text(buffer, 0, text1, strlen(text1));
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    /// Move cursor to end
    result = lle_cursor_manager_move_to_byte_offset(cursor_mgr, 5);
    ASSERT_SUCCESS(result, "Cursor move succeeds");
    ASSERT_EQ(buffer->cursor.byte_offset, 5, "Cursor at end");

    /// Insert text before cursor
    const char *text2 = "XXX";
    result = lle_buffer_insert_text(buffer, 2, text2, strlen(text2));
    ASSERT_SUCCESS(result, "Insertion before cursor succeeds");

    /// Buffer operations update buffer->cursor directly
    /// Cursor should have moved forward by 3 bytes
    ASSERT_EQ(buffer->cursor.byte_offset, 8, "Cursor adjusted after insertion");

    /// Verify content
    ASSERT_STR_EQ(buffer->data, "HeXXXllo", "Content correct");

    lle_cursor_manager_destroy(cursor_mgr);
    lle_buffer_destroy(buffer);
    PASS();
}

static void test_cursor_movement_with_utf8() {
    TEST("Cursor movement handles UTF-8 correctly");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Insert UTF-8 text
    const char *text = "a世b界c"; /* a + 世(3 bytes) + b + 界(3 bytes) + c = 9
                                     bytes, 5 codepoints */
    result = lle_buffer_insert_text(buffer, 0, text, strlen(text));
    ASSERT_SUCCESS(result, "UTF-8 text insertion succeeds");

    /// Initialize cursor manager
    lle_cursor_manager_t *cursor_mgr = NULL;
    result = lle_cursor_manager_init(&cursor_mgr, buffer);
    ASSERT_SUCCESS(result, "Cursor manager init succeeds");

    /// Start at beginning
    result = lle_cursor_manager_move_to_byte_offset(cursor_mgr, 0);
    ASSERT_SUCCESS(result, "Move to start succeeds");
    ASSERT_EQ(buffer->cursor.codepoint_index, 0, "At codepoint 0");

    /// Move forward by 1 codepoint (should skip 1 byte for 'a')
    result = lle_cursor_manager_move_by_codepoints(cursor_mgr, 1);
    ASSERT_SUCCESS(result, "Move by 1 codepoint succeeds");
    ASSERT_EQ(buffer->cursor.byte_offset, 1, "At byte 1");
    ASSERT_EQ(buffer->cursor.codepoint_index, 1, "At codepoint 1");

    /// Move forward by 1 more codepoint (should skip 3 bytes for '世')
    result = lle_cursor_manager_move_by_codepoints(cursor_mgr, 1);
    ASSERT_SUCCESS(result, "Move by 1 codepoint succeeds");
    ASSERT_EQ(buffer->cursor.byte_offset, 4, "At byte 4");
    ASSERT_EQ(buffer->cursor.codepoint_index, 2, "At codepoint 2");

    lle_cursor_manager_destroy(cursor_mgr);
    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * INTEGRATION TEST 3: Buffer Operations + Validator
 * ============================================================================
 * Verify that buffer operations maintain buffer validity
 */

static void test_operations_maintain_validity() {
    TEST("Buffer operations maintain validity");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Initialize validator
    lle_buffer_validator_t *validator = NULL;
    result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator init succeeds");

    /// Validate empty buffer
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Empty buffer is valid");

    /// Insert text and validate
    const char *text1 = "Hello World";
    result = lle_buffer_insert_text(buffer, 0, text1, strlen(text1));
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid after insertion");

    /// Delete text and validate
    result = lle_buffer_delete_text(buffer, 5, 6);
    ASSERT_SUCCESS(result, "Text deletion succeeds");

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid after deletion");

    /// Replace text and validate
    const char *text2 = "XXX";
    result = lle_buffer_replace_text(buffer, 0, 5, text2, strlen(text2));
    ASSERT_SUCCESS(result, "Text replacement succeeds");

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid after replacement");

    lle_buffer_validator_destroy(validator);
    lle_buffer_destroy(buffer);
    PASS();
}

static void test_validator_detects_corruption() {
    TEST("Validator detects buffer corruption");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Initialize validator
    lle_buffer_validator_t *validator = NULL;
    result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator init succeeds");

    /// Insert valid UTF-8
    const char *text = "Hello世界";
    result = lle_buffer_insert_text(buffer, 0, text, strlen(text));
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    /// Manually corrupt the UTF-8 by writing invalid byte sequence
    /// This simulates a bug or memory corruption
    buffer->data[5] = 0xFF; /// Invalid UTF-8 byte
    buffer->data[6] = 0xFF; /// Invalid UTF-8 byte

    /// Validator should detect the corruption
    result = lle_buffer_validate_utf8(buffer, validator);
    ASSERT_TRUE(result != LLE_SUCCESS, "Validator detects UTF-8 corruption");
    ASSERT_TRUE(validator->corruption_detections > 0,
                "Corruption counter incremented");

    lle_buffer_validator_destroy(validator);
    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * INTEGRATION TEST 4: Buffer Operations + Change Tracker (Undo/Redo)
 * ============================================================================
 * Verify that buffer operations are correctly tracked and can be undone/redone
 */

static void test_undo_single_insert() {
    TEST("Undo single insert operation");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Initialize change tracker
    lle_change_tracker_t *tracker = NULL;
    result = lle_change_tracker_init(&tracker, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Change tracker init succeeds");

    /// Start change tracking sequence
    lle_change_sequence_t *sequence = NULL;
    result =
        lle_change_tracker_begin_sequence(tracker, "insert text", &sequence);
    ASSERT_SUCCESS(result, "Begin sequence succeeds");

    /// Attach sequence to buffer to enable tracking
    buffer->current_sequence = sequence;
    buffer->change_tracking_enabled = true;

    /// Insert text
    const char *text = "Hello";
    result = lle_buffer_insert_text(buffer, 0, text, strlen(text));
    ASSERT_SUCCESS(result, "Text insertion succeeds");
    ASSERT_STR_EQ(buffer->data, "Hello", "Content correct");

    /// Complete the sequence
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence succeeds");

    /// Verify can undo
    ASSERT_TRUE(lle_change_tracker_can_undo(tracker), "Undo available");

    /// Undo the insertion
    result = lle_change_tracker_undo(tracker, buffer);
    ASSERT_SUCCESS(result, "Undo succeeds");

    /// Buffer should be empty
    ASSERT_EQ(buffer->length, 0, "Buffer empty after undo");

    /// Verify can redo
    ASSERT_TRUE(lle_change_tracker_can_redo(tracker), "Redo available");

    /// Redo the insertion
    result = lle_change_tracker_redo(tracker, buffer);
    ASSERT_SUCCESS(result, "Redo succeeds");

    /// Content should be restored
    ASSERT_STR_EQ(buffer->data, "Hello", "Content restored after redo");

    lle_change_tracker_destroy(tracker);
    lle_buffer_destroy(buffer);
    PASS();
}

static void test_undo_complex_sequence() {
    TEST("Undo complex operation sequence");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Initialize change tracker
    lle_change_tracker_t *tracker = NULL;
    result = lle_change_tracker_init(&tracker, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Change tracker init succeeds");

    /// Attach change tracker to buffer
    buffer->change_tracking_enabled = true;

    /// Perform sequence of operations, each in its own sequence
    lle_change_sequence_t *seq1 = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "insert hello", &seq1);
    ASSERT_SUCCESS(result, "Begin sequence 1");
    buffer->current_sequence = seq1;
    result = lle_buffer_insert_text(buffer, 0, "Hello", 5);
    ASSERT_SUCCESS(result, "Insert 1 succeeds");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence 1");

    lle_change_sequence_t *seq2 = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "insert world", &seq2);
    ASSERT_SUCCESS(result, "Begin sequence 2");
    buffer->current_sequence = seq2;
    result = lle_buffer_insert_text(buffer, 5, " World", 6);
    ASSERT_SUCCESS(result, "Insert 2 succeeds");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence 2");

    lle_change_sequence_t *seq3 = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "delete world", &seq3);
    ASSERT_SUCCESS(result, "Begin sequence 3");
    buffer->current_sequence = seq3;
    result = lle_buffer_delete_text(buffer, 5, 6);
    ASSERT_SUCCESS(result, "Delete succeeds");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence 3");

    ASSERT_STR_EQ(buffer->data, "Hello", "Content after operations");

    /// Undo delete
    result = lle_change_tracker_undo(tracker, buffer);
    ASSERT_SUCCESS(result, "Undo delete succeeds");
    ASSERT_STR_EQ(buffer->data, "Hello World", "Content restored");

    /// Undo second insert
    result = lle_change_tracker_undo(tracker, buffer);
    ASSERT_SUCCESS(result, "Undo insert 2 succeeds");
    ASSERT_STR_EQ(buffer->data, "Hello", "Content after undo 2");

    /// Undo first insert
    result = lle_change_tracker_undo(tracker, buffer);
    ASSERT_SUCCESS(result, "Undo insert 1 succeeds");
    ASSERT_EQ(buffer->length, 0, "Buffer empty after undo all");

    lle_change_tracker_destroy(tracker);
    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * INTEGRATION TEST 5: End-to-End Multi-Subsystem Tests
 * ============================================================================
 * Complex scenarios combining all subsystems
 */

static void test_e2e_text_editing_session() {
    TEST("End-to-end text editing session with all subsystems");

    /// Create buffer
    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Initialize all subsystems
    lle_cursor_manager_t *cursor_mgr = NULL;
    result = lle_cursor_manager_init(&cursor_mgr, buffer);
    ASSERT_SUCCESS(result, "Cursor manager init succeeds");

    lle_buffer_validator_t *validator = NULL;
    result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator init succeeds");

    lle_change_tracker_t *tracker = NULL;
    result = lle_change_tracker_init(&tracker, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Change tracker init succeeds");

    /// Enable change tracking
    buffer->change_tracking_enabled = true;

    /// Simulate typing "Hello World" with change tracking
    lle_change_sequence_t *seq_h = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "type H", &seq_h);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq_h;
    result = lle_buffer_insert_text(buffer, 0, "H", 1);
    ASSERT_SUCCESS(result, "Insert 'H'");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");
    result = lle_cursor_manager_move_to_byte_offset(cursor_mgr, 1);
    ASSERT_SUCCESS(result, "Move cursor");

    lle_change_sequence_t *seq_ello = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "type ello", &seq_ello);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq_ello;
    result = lle_buffer_insert_text(buffer, 1, "ello", 4);
    ASSERT_SUCCESS(result, "Insert 'ello'");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");
    result = lle_cursor_manager_move_to_byte_offset(cursor_mgr, 5);
    ASSERT_SUCCESS(result, "Move cursor");

    lle_change_sequence_t *seq_world = NULL;
    result =
        lle_change_tracker_begin_sequence(tracker, "type  World", &seq_world);
    ASSERT_SUCCESS(result, "Begin sequence");
    buffer->current_sequence = seq_world;
    result = lle_buffer_insert_text(buffer, 5, " World", 6);
    ASSERT_SUCCESS(result, "Insert ' World'");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence");
    result = lle_cursor_manager_move_to_byte_offset(cursor_mgr, 11);
    ASSERT_SUCCESS(result, "Move cursor");

    /// Validate after typing
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid after typing");
    ASSERT_STR_EQ(buffer->data, "Hello World", "Content correct");
    ASSERT_EQ(cursor_mgr->position.byte_offset, 11, "Cursor at end");

    /// User realizes they want "Hello Universe" instead
    /// Delete "World" with change tracking
    lle_change_sequence_t *seq_del = NULL;
    result =
        lle_change_tracker_begin_sequence(tracker, "delete World", &seq_del);
    ASSERT_SUCCESS(result, "Begin delete sequence");
    buffer->current_sequence = seq_del;
    result = lle_buffer_delete_text(buffer, 6, 5);
    ASSERT_SUCCESS(result, "Delete 'World'");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete delete sequence");

    /// Insert "Universe" with change tracking
    lle_change_sequence_t *seq_ins = NULL;
    result =
        lle_change_tracker_begin_sequence(tracker, "insert Universe", &seq_ins);
    ASSERT_SUCCESS(result, "Begin insert sequence");
    buffer->current_sequence = seq_ins;
    result = lle_buffer_insert_text(buffer, 6, "Universe", 8);
    ASSERT_SUCCESS(result, "Insert 'Universe'");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete insert sequence");

    /// Validate
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid after edit");
    ASSERT_STR_EQ(buffer->data, "Hello Universe", "Content updated");

    /// User changes mind, undo to get "Hello World" back
    result =
        lle_change_tracker_undo(tracker, buffer); /// Undo insert "Universe"
    ASSERT_SUCCESS(result, "Undo insert");
    result = lle_change_tracker_undo(tracker, buffer); /// Undo delete "World"
    ASSERT_SUCCESS(result, "Undo delete");

    /// Validate after undo
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid after undo");
    ASSERT_STR_EQ(buffer->data, "Hello World", "Original content restored");

    /// Cleanup
    lle_change_tracker_destroy(tracker);
    lle_buffer_validator_destroy(validator);
    lle_cursor_manager_destroy(cursor_mgr);
    lle_buffer_destroy(buffer);
    PASS();
}

static void test_e2e_utf8_editing_with_all_subsystems() {
    TEST("End-to-end UTF-8 editing with all subsystems");

    /// Create buffer
    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Initialize all subsystems
    lle_cursor_manager_t *cursor_mgr = NULL;
    result = lle_cursor_manager_init(&cursor_mgr, buffer);
    ASSERT_SUCCESS(result, "Cursor manager init succeeds");

    lle_buffer_validator_t *validator = NULL;
    result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator init succeeds");

    lle_change_tracker_t *tracker = NULL;
    result = lle_change_tracker_init(&tracker, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Change tracker init succeeds");

    /// Enable change tracking
    buffer->change_tracking_enabled = true;

    /// Insert mixed ASCII and UTF-8 content with change tracking
    lle_change_sequence_t *seq1 = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "insert Hello", &seq1);
    ASSERT_SUCCESS(result, "Begin sequence 1");
    buffer->current_sequence = seq1;
    result = lle_buffer_insert_text(buffer, 0, "Hello ", 6);
    ASSERT_SUCCESS(result, "Insert ASCII");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence 1");

    lle_change_sequence_t *seq2 = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "insert 世界", &seq2);
    ASSERT_SUCCESS(result, "Begin sequence 2");
    buffer->current_sequence = seq2;
    result = lle_buffer_insert_text(buffer, 6, "世界",
                                    6); /// 2 Chinese chars, 6 bytes
    ASSERT_SUCCESS(result, "Insert UTF-8");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence 2");

    lle_change_sequence_t *seq3 = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "insert World", &seq3);
    ASSERT_SUCCESS(result, "Begin sequence 3");
    buffer->current_sequence = seq3;
    result = lle_buffer_insert_text(buffer, 12, " World", 6);
    ASSERT_SUCCESS(result, "Insert ASCII");
    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence 3");

    /// Verify UTF-8 handling
    ASSERT_EQ(buffer->length, 18, "Total bytes correct");
    ASSERT_EQ(buffer->codepoint_count, 14, "Total codepoints correct");
    /// UTF-8 index is lazily built and invalidated after modifications
    ASSERT_FALSE(buffer->utf8_index_valid,
                 "UTF-8 index invalidated after modifications");

    /// Move cursor by codepoints (not bytes)
    result = lle_cursor_manager_move_to_byte_offset(cursor_mgr, 0);
    ASSERT_SUCCESS(result, "Move to start");

    result = lle_cursor_manager_move_by_codepoints(cursor_mgr, 7);
    ASSERT_SUCCESS(result, "Move by 7 codepoints");

    /// Should be after "Hello 世" (6 ASCII + 1 Chinese = 9 bytes)
    ASSERT_EQ(buffer->cursor.byte_offset, 9, "Cursor at correct byte offset");
    ASSERT_EQ(buffer->cursor.codepoint_index, 7, "Cursor at codepoint 7");

    /// Validate everything
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid with UTF-8");

    /// Test undo with UTF-8
    result = lle_change_tracker_undo(tracker, buffer); /// Undo " World"
    ASSERT_SUCCESS(result, "Undo succeeds");
    ASSERT_EQ(buffer->length, 12, "Length after undo");

    result = lle_change_tracker_undo(tracker, buffer); /// Undo "世界"
    ASSERT_SUCCESS(result, "Undo UTF-8 succeeds");
    ASSERT_EQ(buffer->length, 6, "Length after UTF-8 undo");
    ASSERT_STR_EQ(buffer->data, "Hello ", "Content after UTF-8 undo");

    /// Redo
    result = lle_change_tracker_redo(tracker, buffer);
    ASSERT_SUCCESS(result, "Redo UTF-8 succeeds");
    ASSERT_EQ(buffer->codepoint_count, 8, "Codepoint count restored");

    /// Validate after redo
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Buffer valid after redo");

    /// Cleanup
    lle_change_tracker_destroy(tracker);
    lle_buffer_validator_destroy(validator);
    lle_cursor_manager_destroy(cursor_mgr);
    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("=================================================\n");
    printf("LLE Subsystem Integration Tests\n");
    printf("=================================================\n\n");

    /// Buffer Operations + UTF-8 Index
    printf("--- Buffer Operations + UTF-8 Index ---\n");
    test_insert_updates_utf8_index();
    test_delete_updates_utf8_index();
    printf("\n");

    /// Buffer Operations + Cursor Manager
    printf("--- Buffer Operations + Cursor Manager ---\n");
    test_insert_adjusts_cursor();
    test_cursor_movement_with_utf8();
    printf("\n");

    /// Buffer Operations + Validator
    printf("--- Buffer Operations + Validator ---\n");
    test_operations_maintain_validity();
    test_validator_detects_corruption();
    printf("\n");

    /// Buffer Operations + Change Tracker
    printf("--- Buffer Operations + Change Tracker ---\n");
    test_undo_single_insert();
    test_undo_complex_sequence();
    printf("\n");

    /// End-to-End Multi-Subsystem Tests
    printf("--- End-to-End Multi-Subsystem Tests ---\n");
    test_e2e_text_editing_session();
    test_e2e_utf8_editing_with_all_subsystems();
    printf("\n");

    /// Summary
    printf("=================================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n");

    return tests_failed > 0 ? 1 : 0;
}
