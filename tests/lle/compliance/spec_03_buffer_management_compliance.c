/**
 * @file spec_03_buffer_management_compliance.c
 * @brief Spec 03 Buffer Management - Compliance Verification Tests
 *
 * Verifies that the Buffer Management implementation is 100% compliant
 * with specification 03_buffer_management_complete.md
 *
 * ZERO-TOLERANCE ENFORCEMENT:
 * These tests verify spec compliance. Any failure indicates a violation
 * and the code MUST be corrected before commit is allowed.
 *
 * Implementation Phase: PHASE 1 - Core Buffer Structure
 *
 * Tests verify:
 * - All buffer constants match specification
 * - All enumerations are defined correctly
 * - All structure types are defined
 * - All flags and limits are correct
 *
 * Spec Reference: docs/lle_specification/03_buffer_management_complete.md
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/// Include only the buffer management header
#include "lle/buffer_management.h"

/// Test tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/// Simple assertion macros
#define ASSERT_EQ(expected, actual, message)                                   \
    do {                                                                       \
        tests_run++;                                                           \
        if ((expected) != (actual)) {                                          \
            printf("  FAIL: %s\n", message);                                   \
            printf("        Expected: %d, Got: %d\n", (int)(expected),         \
                   (int)(actual));                                             \
            tests_failed++;                                                    \
            return false;                                                      \
        } else {                                                               \
            tests_passed++;                                                    \
        }                                                                      \
    } while (0)

#define ASSERT_EQ_ULL(expected, actual, message)                               \
    do {                                                                       \
        tests_run++;                                                           \
        if ((expected) != (actual)) {                                          \
            printf("  FAIL: %s\n", message);                                   \
            printf("        Expected: %llu, Got: %llu\n",                      \
                   (unsigned long long)(expected),                             \
                   (unsigned long long)(actual));                              \
            tests_failed++;                                                    \
            return false;                                                      \
        } else {                                                               \
            tests_passed++;                                                    \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(condition, message)                                        \
    do {                                                                       \
        tests_run++;                                                           \
        if (!(condition)) {                                                    \
            printf("  FAIL: %s\n", message);                                   \
            tests_failed++;                                                    \
            return false;                                                      \
        } else {                                                               \
            tests_passed++;                                                    \
        }                                                                      \
    } while (0)

/* ============================================================================
 * Buffer Constants Compliance Tests
 * ========================================================================== */

/**
 * Test: Verify buffer size and capacity constants
 */
static bool test_buffer_size_constants(void) {
    printf("  Testing buffer size and capacity constants...\n");

    ASSERT_EQ(256, LLE_BUFFER_NAME_MAX, "LLE_BUFFER_NAME_MAX must be 256");

    ASSERT_EQ(4096, LLE_BUFFER_DEFAULT_CAPACITY,
              "LLE_BUFFER_DEFAULT_CAPACITY must be 4096");

    ASSERT_EQ(256, LLE_BUFFER_MIN_CAPACITY,
              "LLE_BUFFER_MIN_CAPACITY must be 256");

    ASSERT_EQ((1024 * 1024), LLE_BUFFER_MAX_CAPACITY,
              "LLE_BUFFER_MAX_CAPACITY must be 1MB (1048576)");

    ASSERT_EQ(2, LLE_BUFFER_GROWTH_FACTOR,
              "LLE_BUFFER_GROWTH_FACTOR must be 2");

    return true;
}

/**
 * Test: Verify memory alignment constant
 */
static bool test_memory_alignment_constant(void) {
    printf("  Testing memory alignment constant...\n");

    ASSERT_EQ(16, LLE_BUFFER_MEMORY_ALIGNMENT,
              "LLE_BUFFER_MEMORY_ALIGNMENT must be 16");

    return true;
}

/**
 * Test: Verify line structure limits
 */
static bool test_line_structure_constants(void) {
    printf("  Testing line structure limit constants...\n");

    ASSERT_EQ(10000, LLE_BUFFER_MAX_LINES,
              "LLE_BUFFER_MAX_LINES must be 10000");

    ASSERT_EQ(100, LLE_BUFFER_DEFAULT_LINE_CAPACITY,
              "LLE_BUFFER_DEFAULT_LINE_CAPACITY must be 100");

    return true;
}

/**
 * Test: Verify UTF-8 index configuration
 */
static bool test_utf8_index_constants(void) {
    printf("  Testing UTF-8 index configuration constants...\n");

    ASSERT_EQ(64, LLE_UTF8_INDEX_GRANULARITY,
              "LLE_UTF8_INDEX_GRANULARITY must be 64");

    ASSERT_EQ(1024, LLE_UTF8_INDEX_MAX_ENTRIES,
              "LLE_UTF8_INDEX_MAX_ENTRIES must be 1024");

    return true;
}

/**
 * Test: Verify change tracking limits
 */
static bool test_change_tracking_constants(void) {
    printf("  Testing change tracking limit constants...\n");

    ASSERT_EQ(1000, LLE_BUFFER_MAX_UNDO_LEVELS,
              "LLE_BUFFER_MAX_UNDO_LEVELS must be 1000");

    ASSERT_EQ(1000, LLE_BUFFER_MAX_REDO_LEVELS,
              "LLE_BUFFER_MAX_REDO_LEVELS must be 1000");

    ASSERT_EQ(100, LLE_BUFFER_CHANGE_SEQUENCE_LIMIT,
              "LLE_BUFFER_CHANGE_SEQUENCE_LIMIT must be 100");

    return true;
}

/**
 * Test: Verify cache configuration constants
 */
static bool test_cache_configuration_constants(void) {
    printf("  Testing cache configuration constants...\n");

    ASSERT_EQ(256, LLE_BUFFER_CACHE_SIZE, "LLE_BUFFER_CACHE_SIZE must be 256");

    ASSERT_EQ(1000, LLE_BUFFER_CACHE_LINE_TTL,
              "LLE_BUFFER_CACHE_LINE_TTL must be 1000");

    return true;
}

/**
 * Test: Verify performance target constants
 */
static bool test_performance_target_constants(void) {
    printf("  Testing performance target constants...\n");

    ASSERT_EQ_ULL(500000ULL, LLE_BUFFER_PERF_INSERT_MAX_NS,
                  "LLE_BUFFER_PERF_INSERT_MAX_NS must be 500000ns (0.5ms)");

    ASSERT_EQ_ULL(500000ULL, LLE_BUFFER_PERF_DELETE_MAX_NS,
                  "LLE_BUFFER_PERF_DELETE_MAX_NS must be 500000ns (0.5ms)");

    ASSERT_EQ_ULL(100000ULL, LLE_BUFFER_PERF_UTF8_CALC_MAX_NS,
                  "LLE_BUFFER_PERF_UTF8_CALC_MAX_NS must be 100000ns (0.1ms)");

    return true;
}

/**
 * Test: Verify buffer flag bit values
 */
static bool test_buffer_flags(void) {
    printf("  Testing buffer flag bit values...\n");

    ASSERT_EQ(0x0001, LLE_BUFFER_FLAG_READONLY,
              "LLE_BUFFER_FLAG_READONLY must be 0x0001");

    ASSERT_EQ(0x0002, LLE_BUFFER_FLAG_MODIFIED,
              "LLE_BUFFER_FLAG_MODIFIED must be 0x0002");

    ASSERT_EQ(0x0004, LLE_BUFFER_FLAG_MULTILINE,
              "LLE_BUFFER_FLAG_MULTILINE must be 0x0004");

    ASSERT_EQ(0x0008, LLE_BUFFER_FLAG_UTF8_DIRTY,
              "LLE_BUFFER_FLAG_UTF8_DIRTY must be 0x0008");

    ASSERT_EQ(0x0010, LLE_BUFFER_FLAG_LINE_DIRTY,
              "LLE_BUFFER_FLAG_LINE_DIRTY must be 0x0010");

    ASSERT_EQ(0x0020, LLE_BUFFER_FLAG_CACHE_DIRTY,
              "LLE_BUFFER_FLAG_CACHE_DIRTY must be 0x0020");

    ASSERT_EQ(0x0040, LLE_BUFFER_FLAG_VALIDATION_FAILED,
              "LLE_BUFFER_FLAG_VALIDATION_FAILED must be 0x0040");

    return true;
}

/**
 * Test: Verify line flag bit values
 */
static bool test_line_flags(void) {
    printf("  Testing line flag bit values...\n");

    ASSERT_EQ(0x01, LLE_LINE_FLAG_CONTINUATION,
              "LLE_LINE_FLAG_CONTINUATION must be 0x01");

    ASSERT_EQ(0x02, LLE_LINE_FLAG_NEEDS_REVALIDATION,
              "LLE_LINE_FLAG_NEEDS_REVALIDATION must be 0x02");

    ASSERT_EQ(0x04, LLE_LINE_FLAG_CACHED, "LLE_LINE_FLAG_CACHED must be 0x04");

    return true;
}

/**
 * Test: Verify cache flag bit values
 */
static bool test_cache_flags(void) {
    printf("  Testing cache flag bit values...\n");

    ASSERT_EQ(0x01, LLE_CACHE_LINE_STRUCTURE,
              "LLE_CACHE_LINE_STRUCTURE must be 0x01");

    ASSERT_EQ(0x02, LLE_CACHE_RENDER, "LLE_CACHE_RENDER must be 0x02");

    ASSERT_EQ(0x04, LLE_CACHE_LAYOUT, "LLE_CACHE_LAYOUT must be 0x04");

    return true;
}

/* ============================================================================
 * Enumeration Compliance Tests
 * ========================================================================== */

/**
 * Test: Verify change type enumeration
 */
static bool test_change_type_enum(void) {
    printf("  Testing change type enumeration...\n");

    ASSERT_EQ(1, LLE_CHANGE_TYPE_INSERT, "LLE_CHANGE_TYPE_INSERT must equal 1");

    /// Verify all change types exist and are accessible
    int change_types[] = {
        LLE_CHANGE_TYPE_INSERT,    LLE_CHANGE_TYPE_DELETE,
        LLE_CHANGE_TYPE_REPLACE,   LLE_CHANGE_TYPE_CURSOR_MOVE,
        LLE_CHANGE_TYPE_SELECTION, LLE_CHANGE_TYPE_COMPOSITE};

    ASSERT_TRUE(sizeof(change_types) > 0,
                "All change type enum values must be defined");

    return true;
}

/**
 * Test: Verify line type enumeration
 */
static bool test_line_type_enum(void) {
    printf("  Testing line type enumeration...\n");

    /// Verify all line types exist and are accessible
    int line_types[] = {LLE_LINE_TYPE_COMMAND, LLE_LINE_TYPE_CONTINUATION,
                        LLE_LINE_TYPE_HEREDOC, LLE_LINE_TYPE_QUOTED,
                        LLE_LINE_TYPE_COMMENT};

    ASSERT_TRUE(sizeof(line_types) > 0,
                "All line type enum values must be defined");

    return true;
}

/**
 * Test: Verify multiline state enumeration
 */
static bool test_multiline_state_enum(void) {
    printf("  Testing multiline state enumeration...\n");

    /// Verify all multiline states exist and are accessible
    int multiline_states[] = {
        LLE_MULTILINE_STATE_NONE,         LLE_MULTILINE_STATE_QUOTE_SINGLE,
        LLE_MULTILINE_STATE_QUOTE_DOUBLE, LLE_MULTILINE_STATE_QUOTE_BACKTICK,
        LLE_MULTILINE_STATE_HEREDOC,      LLE_MULTILINE_STATE_PAREN,
        LLE_MULTILINE_STATE_BRACE,        LLE_MULTILINE_STATE_BRACKET,
        LLE_MULTILINE_STATE_PIPE,         LLE_MULTILINE_STATE_BACKSLASH};

    ASSERT_TRUE(sizeof(multiline_states) > 0,
                "All multiline state enum values must be defined");

    return true;
}

/* ============================================================================
 * Structure Definition Tests
 * ========================================================================== */

/**
 * Test: Verify lle_buffer_t structure is defined
 */
static bool test_buffer_structure_defined(void) {
    printf("  Testing lle_buffer_t structure is defined...\n");

    /// This will fail to compile if structure is not defined
    ASSERT_TRUE(sizeof(lle_buffer_t) > 0,
                "lle_buffer_t structure must be defined");

    return true;
}

/**
 * Test: Verify lle_line_info_t structure is defined
 */
static bool test_line_info_structure_defined(void) {
    printf("  Testing lle_line_info_t structure is defined...\n");

    ASSERT_TRUE(sizeof(lle_line_info_t) > 0,
                "lle_line_info_t structure must be defined");

    return true;
}

/**
 * Test: Verify lle_cursor_position_t structure is defined
 */
static bool test_cursor_position_structure_defined(void) {
    printf("  Testing lle_cursor_position_t structure is defined...\n");

    ASSERT_TRUE(sizeof(lle_cursor_position_t) > 0,
                "lle_cursor_position_t structure must be defined");

    return true;
}

/**
 * Test: Verify lle_selection_range_t structure is defined
 */
static bool test_selection_range_structure_defined(void) {
    printf("  Testing lle_selection_range_t structure is defined...\n");

    ASSERT_TRUE(sizeof(lle_selection_range_t) > 0,
                "lle_selection_range_t structure must be defined");

    return true;
}

/* ============================================================================
 * Test Suite Execution
 * ========================================================================== */

/**
 * Run all Spec 03 Phase 1 compliance tests
 * Returns: Number of failed tests (0 = all passed)
 */
int main(void) {
    printf("Running Spec 03 (Buffer Management) Phase 1 Compliance Tests...\n");
    printf(
        "==============================================================\n\n");

    /// Run all compliance tests
    test_buffer_size_constants();
    test_memory_alignment_constant();
    test_line_structure_constants();
    test_utf8_index_constants();
    test_change_tracking_constants();
    test_cache_configuration_constants();
    test_performance_target_constants();
    test_buffer_flags();
    test_line_flags();
    test_cache_flags();
    test_change_type_enum();
    test_line_type_enum();
    test_multiline_state_enum();
    test_buffer_structure_defined();
    test_line_info_structure_defined();
    test_cursor_position_structure_defined();
    test_selection_range_structure_defined();

    /// Print results
    printf("\n");
    printf("==============================================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf(
        "==============================================================\n\n");

    if (tests_failed == 0) {
        printf("RESULT: ALL PHASE 1 COMPLIANCE TESTS PASSED\n\n");
        printf("Buffer Management Phase 1 implementation is 100%% "
               "spec-compliant\n");
        printf("All constants, enums, and types match specification "
               "requirements\n");
        return 0;
    } else {
        printf("RESULT: SPEC COMPLIANCE VIOLATION\n\n");
        printf("Buffer Management Phase 1 implementation DOES NOT match "
               "specification\n");
        printf("This is a ZERO-TOLERANCE violation\n");
        printf("Commit MUST be blocked until violations are corrected\n");
        return 1;
    }
}
