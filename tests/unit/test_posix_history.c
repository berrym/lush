/**
 * @file test_posix_history.c
 * @brief Unit tests for POSIX history management
 *
 * Tests the posix_history module including history creation, entry management,
 * range parsing, file operations, and utility functions.
 */

#include "posix_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test_framework.h"

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

// ============================================================================
// Test Framework
// ============================================================================






// ============================================================================
// Creation and Destruction Tests
// ============================================================================

TEST(create_default_capacity) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Should create manager with default capacity");
    ASSERT_EQ(mgr->count, 0, "Initial count should be 0");
    ASSERT_EQ(mgr->next_number, 1, "Next number should start at 1");
    ASSERT_FALSE(mgr->wraparound_occurred, "No wraparound initially");
    posix_history_destroy(mgr);
}

TEST(create_custom_capacity) {
    posix_history_manager_t *mgr = posix_history_create(500);
    ASSERT_NOT_NULL(mgr, "Should create manager with custom capacity");
    ASSERT_EQ(mgr->capacity, 500, "Capacity should be 500");
    posix_history_destroy(mgr);
}

TEST(create_minimum_capacity) {
    // Requesting less than minimum should use minimum
    posix_history_manager_t *mgr = posix_history_create(50);
    ASSERT_NOT_NULL(mgr, "Should create manager with min capacity");
    ASSERT_EQ(mgr->capacity, POSIX_HISTORY_MIN_ENTRIES,
              "Should use minimum capacity");
    posix_history_destroy(mgr);
}

TEST(create_maximum_capacity) {
    // Requesting more than maximum should cap at max
    posix_history_manager_t *mgr = posix_history_create(50000);
    ASSERT_NOT_NULL(mgr, "Should create manager with capped capacity");
    ASSERT_EQ(mgr->capacity, POSIX_HISTORY_MAX_ENTRIES,
              "Should cap at maximum capacity");
    posix_history_destroy(mgr);
}

TEST(destroy_null_safe) {
    // Should not crash on NULL
    posix_history_destroy(NULL);
    ASSERT_TRUE(true, "Destroy NULL should not crash");
}

// ============================================================================
// Entry Addition Tests
// ============================================================================

TEST(add_single_entry) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    int num = posix_history_add(mgr, "echo hello");
    ASSERT_EQ(num, 1, "First entry should be number 1");
    ASSERT_EQ(mgr->count, 1, "Count should be 1");

    posix_history_destroy(mgr);
}

TEST(add_multiple_entries) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    ASSERT_EQ(posix_history_add(mgr, "cmd1"), 1, "First should be 1");
    ASSERT_EQ(posix_history_add(mgr, "cmd2"), 2, "Second should be 2");
    ASSERT_EQ(posix_history_add(mgr, "cmd3"), 3, "Third should be 3");
    ASSERT_EQ(mgr->count, 3, "Count should be 3");

    posix_history_destroy(mgr);
}

TEST(add_empty_command_rejected) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    ASSERT_EQ(posix_history_add(mgr, ""), -1, "Empty command should fail");
    ASSERT_EQ(posix_history_add(mgr, "   "), -1, "Whitespace-only should fail");
    ASSERT_EQ(posix_history_add(mgr, "\t\n"), -1,
              "Whitespace-only should fail");
    ASSERT_EQ(mgr->count, 0, "Count should still be 0");

    posix_history_destroy(mgr);
}

TEST(add_null_parameters) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    ASSERT_EQ(posix_history_add(NULL, "cmd"), -1, "Null manager should fail");
    ASSERT_EQ(posix_history_add(mgr, NULL), -1, "Null command should fail");

    posix_history_destroy(mgr);
}

TEST(add_no_duplicates_enabled) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_set_no_duplicates(mgr, true);

    int n1 = posix_history_add(mgr, "ls -la");
    int n2 = posix_history_add(mgr, "ls -la"); // Duplicate
    int n3 = posix_history_add(mgr, "pwd");

    ASSERT_EQ(n1, 1, "First entry");
    ASSERT_EQ(n2, 1, "Duplicate should return same number");
    ASSERT_EQ(n3, 2, "Third distinct entry");
    ASSERT_EQ(mgr->count, 2, "Count should be 2 (no duplicate)");

    posix_history_destroy(mgr);
}

TEST(add_overflow_removes_oldest) {
    posix_history_manager_t *mgr =
        posix_history_create(POSIX_HISTORY_MIN_ENTRIES);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    // Fill to capacity
    for (size_t i = 0; i < mgr->capacity; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "cmd%zu", i);
        posix_history_add(mgr, cmd);
    }

    ASSERT_EQ(mgr->count, mgr->capacity, "Should be at capacity");

    // First entry should be cmd0
    posix_history_entry_t *first = posix_history_get_by_index(mgr, 0);
    ASSERT_NOT_NULL(first, "First entry should exist");
    ASSERT_STR_EQ(first->command, "cmd0", "First should be cmd0");

    // Add one more, cmd0 should be removed
    posix_history_add(mgr, "overflow");
    ASSERT_EQ(mgr->count, mgr->capacity, "Count should still be at capacity");

    first = posix_history_get_by_index(mgr, 0);
    ASSERT_NOT_NULL(first, "First entry should exist");
    ASSERT_STR_EQ(first->command, "cmd1", "First should now be cmd1");

    posix_history_destroy(mgr);
}

// ============================================================================
// Entry Retrieval Tests
// ============================================================================

TEST(get_by_number) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "first");
    posix_history_add(mgr, "second");
    posix_history_add(mgr, "third");

    posix_history_entry_t *e1 = posix_history_get(mgr, 1);
    posix_history_entry_t *e2 = posix_history_get(mgr, 2);
    posix_history_entry_t *e3 = posix_history_get(mgr, 3);

    ASSERT_NOT_NULL(e1, "Entry 1 should exist");
    ASSERT_NOT_NULL(e2, "Entry 2 should exist");
    ASSERT_NOT_NULL(e3, "Entry 3 should exist");

    ASSERT_STR_EQ(e1->command, "first", "Entry 1 command");
    ASSERT_STR_EQ(e2->command, "second", "Entry 2 command");
    ASSERT_STR_EQ(e3->command, "third", "Entry 3 command");

    posix_history_destroy(mgr);
}

TEST(get_by_number_invalid) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "only");

    ASSERT_NULL(posix_history_get(mgr, 0), "Number 0 should not exist");
    ASSERT_NULL(posix_history_get(mgr, -1), "Negative number should not exist");
    ASSERT_NULL(posix_history_get(mgr, 999), "Non-existent should return NULL");
    ASSERT_NULL(posix_history_get(NULL, 1), "Null manager should return NULL");

    posix_history_destroy(mgr);
}

TEST(get_by_index) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "alpha");
    posix_history_add(mgr, "beta");
    posix_history_add(mgr, "gamma");

    posix_history_entry_t *e0 = posix_history_get_by_index(mgr, 0);
    posix_history_entry_t *e1 = posix_history_get_by_index(mgr, 1);
    posix_history_entry_t *e2 = posix_history_get_by_index(mgr, 2);

    ASSERT_NOT_NULL(e0, "Index 0 should exist");
    ASSERT_NOT_NULL(e1, "Index 1 should exist");
    ASSERT_NOT_NULL(e2, "Index 2 should exist");

    ASSERT_STR_EQ(e0->command, "alpha", "Index 0 command");
    ASSERT_STR_EQ(e1->command, "beta", "Index 1 command");
    ASSERT_STR_EQ(e2->command, "gamma", "Index 2 command");

    posix_history_destroy(mgr);
}

TEST(get_by_index_invalid) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "only");

    ASSERT_NULL(posix_history_get_by_index(mgr, 1), "Out of range should fail");
    ASSERT_NULL(posix_history_get_by_index(mgr, 100), "Far out of range");
    ASSERT_NULL(posix_history_get_by_index(NULL, 0), "Null manager");

    posix_history_destroy(mgr);
}

// ============================================================================
// Entry Deletion Tests
// ============================================================================

TEST(delete_entry) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "first");
    posix_history_add(mgr, "second");
    posix_history_add(mgr, "third");

    ASSERT_EQ(mgr->count, 3, "Initial count");

    ASSERT_TRUE(posix_history_delete(mgr, 2), "Delete should succeed");
    ASSERT_EQ(mgr->count, 2, "Count after delete");

    ASSERT_NULL(posix_history_get(mgr, 2), "Entry 2 should not exist");
    ASSERT_NOT_NULL(posix_history_get(mgr, 1), "Entry 1 still exists");
    ASSERT_NOT_NULL(posix_history_get(mgr, 3), "Entry 3 still exists");

    posix_history_destroy(mgr);
}

TEST(delete_nonexistent) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "only");

    ASSERT_FALSE(posix_history_delete(mgr, 999), "Delete non-existent fails");
    ASSERT_FALSE(posix_history_delete(mgr, 0), "Delete number 0 fails");
    ASSERT_FALSE(posix_history_delete(NULL, 1),
                 "Delete with null manager fails");

    posix_history_destroy(mgr);
}

TEST(clear_history) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "cmd1");
    posix_history_add(mgr, "cmd2");
    posix_history_add(mgr, "cmd3");

    ASSERT_EQ(mgr->count, 3, "Before clear");

    ASSERT_TRUE(posix_history_clear(mgr), "Clear should succeed");
    ASSERT_EQ(mgr->count, 0, "After clear count should be 0");
    ASSERT_EQ(mgr->next_number, 1, "Next number should reset to 1");

    posix_history_destroy(mgr);
}

TEST(clear_null_manager) {
    ASSERT_FALSE(posix_history_clear(NULL), "Clear null manager fails");
}

// ============================================================================
// Range and Number Resolution Tests
// ============================================================================

TEST(resolve_number_positive) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "cmd1");
    posix_history_add(mgr, "cmd2");
    posix_history_add(mgr, "cmd3");

    ASSERT_EQ(posix_history_resolve_number(mgr, "1"), 1, "Resolve 1");
    ASSERT_EQ(posix_history_resolve_number(mgr, "2"), 2, "Resolve 2");
    ASSERT_EQ(posix_history_resolve_number(mgr, "3"), 3, "Resolve 3");
    ASSERT_EQ(posix_history_resolve_number(mgr, "999"), -1, "Non-existent");

    posix_history_destroy(mgr);
}

TEST(resolve_number_negative_offset) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "first");
    posix_history_add(mgr, "second");
    posix_history_add(mgr, "third");

    ASSERT_EQ(posix_history_resolve_number(mgr, "-1"), 3, "Last entry");
    ASSERT_EQ(posix_history_resolve_number(mgr, "-2"), 2, "Second to last");
    ASSERT_EQ(posix_history_resolve_number(mgr, "-3"), 1, "Third to last");
    ASSERT_EQ(posix_history_resolve_number(mgr, "-4"), -1, "Beyond range");

    posix_history_destroy(mgr);
}

TEST(resolve_number_string_prefix) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "echo hello");
    posix_history_add(mgr, "ls -la");
    posix_history_add(mgr, "echo world");

    // Should find most recent match
    ASSERT_EQ(posix_history_resolve_number(mgr, "echo"), 3, "Most recent echo");
    ASSERT_EQ(posix_history_resolve_number(mgr, "ls"), 2, "ls command");
    ASSERT_EQ(posix_history_resolve_number(mgr, "notfound"), -1, "Not found");

    posix_history_destroy(mgr);
}

TEST(parse_range_no_args) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "cmd1");
    posix_history_add(mgr, "cmd2");

    posix_history_range_t range = {0};
    ASSERT_TRUE(posix_history_parse_range(mgr, NULL, NULL, &range), "Parse ok");
    ASSERT_TRUE(range.valid, "Range valid");
    ASSERT_EQ(range.first, 2, "First defaults to last entry");
    ASSERT_EQ(range.last, 2, "Last defaults to first");

    posix_history_destroy(mgr);
}

TEST(parse_range_first_only) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "cmd1");
    posix_history_add(mgr, "cmd2");
    posix_history_add(mgr, "cmd3");

    posix_history_range_t range = {0};
    ASSERT_TRUE(posix_history_parse_range(mgr, "1", NULL, &range), "Parse ok");
    ASSERT_TRUE(range.valid, "Range valid");
    ASSERT_EQ(range.first, 1, "First is 1");
    ASSERT_EQ(range.last, 1, "Last defaults to first");

    posix_history_destroy(mgr);
}

TEST(parse_range_both_specified) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "cmd1");
    posix_history_add(mgr, "cmd2");
    posix_history_add(mgr, "cmd3");

    posix_history_range_t range = {0};
    ASSERT_TRUE(posix_history_parse_range(mgr, "1", "3", &range), "Parse ok");
    ASSERT_TRUE(range.valid, "Range valid");
    ASSERT_EQ(range.first, 1, "First is 1");
    ASSERT_EQ(range.last, 3, "Last is 3");

    posix_history_destroy(mgr);
}

TEST(parse_range_invalid) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "cmd1");

    posix_history_range_t range = {0};
    ASSERT_FALSE(posix_history_parse_range(mgr, "999", NULL, &range),
                 "Invalid first");
    ASSERT_FALSE(posix_history_parse_range(NULL, "1", NULL, &range),
                 "Null manager");
    ASSERT_FALSE(posix_history_parse_range(mgr, "1", NULL, NULL), "Null range");

    posix_history_destroy(mgr);
}

TEST(get_valid_range) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "cmd1");
    posix_history_add(mgr, "cmd2");
    posix_history_add(mgr, "cmd3");

    int min_num, max_num;
    ASSERT_TRUE(posix_history_get_valid_range(mgr, &min_num, &max_num),
                "Get range");
    ASSERT_EQ(min_num, 1, "Min should be 1");
    ASSERT_EQ(max_num, 3, "Max should be 3");

    posix_history_destroy(mgr);
}

TEST(get_valid_range_empty) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    int min_num, max_num;
    ASSERT_FALSE(posix_history_get_valid_range(mgr, &min_num, &max_num),
                 "Empty history");

    posix_history_destroy(mgr);
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST(set_filename) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    ASSERT_TRUE(posix_history_set_filename(mgr, "/tmp/test_history"),
                "Set filename");
    ASSERT_NOT_NULL(mgr->filename, "Filename set");
    ASSERT_STR_EQ(mgr->filename, "/tmp/test_history", "Filename value");

    ASSERT_TRUE(posix_history_set_filename(mgr, NULL), "Clear filename");
    ASSERT_NULL(mgr->filename, "Filename cleared");

    posix_history_destroy(mgr);
}

TEST(set_no_duplicates) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    ASSERT_FALSE(mgr->no_duplicates, "Initially false");

    posix_history_set_no_duplicates(mgr, true);
    ASSERT_TRUE(mgr->no_duplicates, "Now true");

    posix_history_set_no_duplicates(mgr, false);
    ASSERT_FALSE(mgr->no_duplicates, "Back to false");

    // Should not crash with NULL
    posix_history_set_no_duplicates(NULL, true);

    posix_history_destroy(mgr);
}

// ============================================================================
// Statistics and Validation Tests
// ============================================================================

TEST(get_stats) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "cmd1");
    posix_history_add(mgr, "cmd2");
    posix_history_add(mgr, "cmd3");

    size_t total;
    int current_num, wraparound;

    ASSERT_TRUE(posix_history_get_stats(mgr, &total, &current_num, &wraparound),
                "Get stats");
    ASSERT_EQ(total, 3, "Total entries");
    ASSERT_EQ(current_num, 3, "Current number");
    ASSERT_EQ(wraparound, 0, "No wraparound");

    posix_history_destroy(mgr);
}

TEST(get_stats_invalid) {
    size_t total;
    int current_num, wraparound;

    ASSERT_FALSE(
        posix_history_get_stats(NULL, &total, &current_num, &wraparound),
        "Null manager");

    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    ASSERT_FALSE(posix_history_get_stats(mgr, NULL, &current_num, &wraparound),
                 "Null total");
    ASSERT_FALSE(posix_history_get_stats(mgr, &total, NULL, &wraparound),
                 "Null current_num");
    ASSERT_FALSE(posix_history_get_stats(mgr, &total, &current_num, NULL),
                 "Null wraparound");

    posix_history_destroy(mgr);
}

TEST(validate_good_state) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "cmd1");
    posix_history_add(mgr, "cmd2");

    ASSERT_TRUE(posix_history_validate(mgr), "Valid state");

    posix_history_destroy(mgr);
}

TEST(validate_null) {
    ASSERT_FALSE(posix_history_validate(NULL), "Null manager invalid");
}

// ============================================================================
// File Operations Tests
// ============================================================================

TEST(save_and_load) {
    const char *testfile = "/tmp/test_posix_history_save_load.txt";

    // Create and populate manager
    posix_history_manager_t *mgr1 = posix_history_create(0);
    ASSERT_NOT_NULL(mgr1, "Manager 1 creation");

    posix_history_add(mgr1, "echo hello");
    posix_history_add(mgr1, "ls -la");
    posix_history_add(mgr1, "pwd");

    // Save to file
    int saved = posix_history_save(mgr1, testfile, false);
    ASSERT_EQ(saved, 3, "Should save 3 entries");

    posix_history_destroy(mgr1);

    // Create new manager and load
    posix_history_manager_t *mgr2 = posix_history_create(0);
    ASSERT_NOT_NULL(mgr2, "Manager 2 creation");

    int loaded = posix_history_load(mgr2, testfile, false);
    ASSERT_EQ(loaded, 3, "Should load 3 entries");
    ASSERT_EQ(mgr2->count, 3, "Count should be 3");

    posix_history_entry_t *e1 = posix_history_get_by_index(mgr2, 0);
    posix_history_entry_t *e2 = posix_history_get_by_index(mgr2, 1);
    posix_history_entry_t *e3 = posix_history_get_by_index(mgr2, 2);

    ASSERT_NOT_NULL(e1, "Entry 1");
    ASSERT_NOT_NULL(e2, "Entry 2");
    ASSERT_NOT_NULL(e3, "Entry 3");

    ASSERT_STR_EQ(e1->command, "echo hello", "Command 1");
    ASSERT_STR_EQ(e2->command, "ls -la", "Command 2");
    ASSERT_STR_EQ(e3->command, "pwd", "Command 3");

    posix_history_destroy(mgr2);

    // Clean up
    unlink(testfile);
}

TEST(load_nonexistent_file) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    int loaded =
        posix_history_load(mgr, "/tmp/nonexistent_history_file_12345", false);
    ASSERT_EQ(loaded, 0, "Non-existent file returns 0 (not error)");

    posix_history_destroy(mgr);
}

TEST(save_no_filename) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "test");

    int saved = posix_history_save(mgr, NULL, false);
    ASSERT_EQ(saved, -1, "No filename should fail");

    posix_history_destroy(mgr);
}

TEST(load_with_append) {
    const char *testfile = "/tmp/test_posix_history_append.txt";

    // Create and populate manager
    posix_history_manager_t *mgr1 = posix_history_create(0);
    ASSERT_NOT_NULL(mgr1, "Manager 1 creation");

    posix_history_add(mgr1, "cmd1");
    posix_history_add(mgr1, "cmd2");
    posix_history_save(mgr1, testfile, false);
    posix_history_destroy(mgr1);

    // Create manager with existing entries and load with append
    posix_history_manager_t *mgr2 = posix_history_create(0);
    ASSERT_NOT_NULL(mgr2, "Manager 2 creation");

    posix_history_add(mgr2, "existing");
    ASSERT_EQ(mgr2->count, 1, "Initial count");

    int loaded = posix_history_load(mgr2, testfile, true);
    ASSERT_EQ(loaded, 2, "Should load 2 entries");
    ASSERT_EQ(mgr2->count, 3, "Total count with append");

    posix_history_destroy(mgr2);

    // Clean up
    unlink(testfile);
}

// ============================================================================
// Editor Integration Tests
// ============================================================================

TEST(get_default_editor) {
    char *editor = posix_history_get_default_editor();
    ASSERT_NOT_NULL(editor, "Should return an editor");
    // Should be either from environment or default "vi"
    ASSERT(strlen(editor) > 0, "Editor should have content");
    free(editor);
}

TEST(create_temp_file) {
    char *filename = NULL;

    ASSERT_TRUE(posix_history_create_temp_file("test content\n", &filename),
                "Create temp file");
    ASSERT_NOT_NULL(filename, "Filename returned");

    // Verify file exists and has content
    char *content = posix_history_read_file_content(filename);
    ASSERT_NOT_NULL(content, "Content read");
    ASSERT_STR_EQ(content, "test content\n", "Content matches");

    free(content);
    unlink(filename);
    free(filename);
}

TEST(create_temp_file_invalid) {
    char *filename = NULL;

    ASSERT_FALSE(posix_history_create_temp_file(NULL, &filename),
                 "Null content fails");
    ASSERT_FALSE(posix_history_create_temp_file("test", NULL),
                 "Null filename fails");
}

TEST(read_file_content) {
    const char *testfile = "/tmp/test_read_content.txt";

    // Write test file
    FILE *fp = fopen(testfile, "w");
    ASSERT_NOT_NULL(fp, "Create test file");
    fprintf(fp, "line1\nline2\nline3\n");
    fclose(fp);

    // Read content
    char *content = posix_history_read_file_content(testfile);
    ASSERT_NOT_NULL(content, "Read content");
    ASSERT_STR_EQ(content, "line1\nline2\nline3\n", "Content matches");

    free(content);
    unlink(testfile);
}

TEST(read_file_content_null) {
    ASSERT_NULL(posix_history_read_file_content(NULL), "Null filename");
    ASSERT_NULL(posix_history_read_file_content("/nonexistent/file"),
                "Non-existent file");
}

// ============================================================================
// Error and Debug Tests
// ============================================================================

TEST(get_last_error_after_failure) {
    // Cause an error
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_delete(mgr, 999); // This should set an error

    const char *err = posix_history_get_last_error();
    ASSERT_NOT_NULL(err, "Error message set");
    ASSERT(strlen(err) > 0, "Error message has content");

    posix_history_destroy(mgr);
}

TEST(set_debug_mode) {
    // Just verify it doesn't crash
    posix_history_set_debug(true);
    posix_history_set_debug(false);
    ASSERT_TRUE(true, "Debug mode toggle works");
}

// ============================================================================
// Entry Metadata Tests
// ============================================================================

TEST(entry_has_timestamp) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "timestamped");

    posix_history_entry_t *entry = posix_history_get(mgr, 1);
    ASSERT_NOT_NULL(entry, "Entry exists");
    ASSERT(entry->timestamp > 0, "Timestamp should be set");

    posix_history_destroy(mgr);
}

TEST(entry_has_length) {
    posix_history_manager_t *mgr = posix_history_create(0);
    ASSERT_NOT_NULL(mgr, "Manager creation");

    posix_history_add(mgr, "hello");

    posix_history_entry_t *entry = posix_history_get(mgr, 1);
    ASSERT_NOT_NULL(entry, "Entry exists");
    ASSERT_EQ(entry->length, 5, "Length should be 5");

    posix_history_destroy(mgr);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("Running POSIX history tests...\n\n");

    printf("Creation and Destruction:\n");
    RUN_TEST(create_default_capacity);
    RUN_TEST(create_custom_capacity);
    RUN_TEST(create_minimum_capacity);
    RUN_TEST(create_maximum_capacity);
    RUN_TEST(destroy_null_safe);

    printf("\nEntry Addition:\n");
    RUN_TEST(add_single_entry);
    RUN_TEST(add_multiple_entries);
    RUN_TEST(add_empty_command_rejected);
    RUN_TEST(add_null_parameters);
    RUN_TEST(add_no_duplicates_enabled);
    RUN_TEST(add_overflow_removes_oldest);

    printf("\nEntry Retrieval:\n");
    RUN_TEST(get_by_number);
    RUN_TEST(get_by_number_invalid);
    RUN_TEST(get_by_index);
    RUN_TEST(get_by_index_invalid);

    printf("\nEntry Deletion:\n");
    RUN_TEST(delete_entry);
    RUN_TEST(delete_nonexistent);
    RUN_TEST(clear_history);
    RUN_TEST(clear_null_manager);

    printf("\nRange and Number Resolution:\n");
    RUN_TEST(resolve_number_positive);
    RUN_TEST(resolve_number_negative_offset);
    RUN_TEST(resolve_number_string_prefix);
    RUN_TEST(parse_range_no_args);
    RUN_TEST(parse_range_first_only);
    RUN_TEST(parse_range_both_specified);
    RUN_TEST(parse_range_invalid);
    RUN_TEST(get_valid_range);
    RUN_TEST(get_valid_range_empty);

    printf("\nConfiguration:\n");
    RUN_TEST(set_filename);
    RUN_TEST(set_no_duplicates);

    printf("\nStatistics and Validation:\n");
    RUN_TEST(get_stats);
    RUN_TEST(get_stats_invalid);
    RUN_TEST(validate_good_state);
    RUN_TEST(validate_null);

    printf("\nFile Operations:\n");
    RUN_TEST(save_and_load);
    RUN_TEST(load_nonexistent_file);
    RUN_TEST(save_no_filename);
    RUN_TEST(load_with_append);

    printf("\nEditor Integration:\n");
    RUN_TEST(get_default_editor);
    RUN_TEST(create_temp_file);
    RUN_TEST(create_temp_file_invalid);
    RUN_TEST(read_file_content);
    RUN_TEST(read_file_content_null);

    printf("\nError and Debug:\n");
    RUN_TEST(get_last_error_after_failure);
    RUN_TEST(set_debug_mode);

    printf("\nEntry Metadata:\n");
    RUN_TEST(entry_has_timestamp);
    RUN_TEST(entry_has_length);

    return TEST_RESULT();
}
