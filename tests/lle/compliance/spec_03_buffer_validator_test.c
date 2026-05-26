/**
 * @file spec_03_buffer_validator_test.c
 * @brief Spec 03 buffer validator compliance test
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/**
 * Spec 03 Buffer Validator Compliance Tests
 *
 * Verifies that buffer validator implementation complies with:
 * - Spec 03 Section 8: Buffer Validation and Integrity
 * - UTF-8 validation
 * - Line structure validation
 * - Cursor position validation
 * - Bounds checking
 */

#include "../../../include/lle/buffer_management.h"
#include "../../../include/lle/error_handling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    do {                                                                       \
        printf("  Testing: %s ... ", name);                                    \
        fflush(stdout);                                                        \
        tests_run++;                                                           \
    } while (0)

#define PASS()                                                                 \
    do {                                                                       \
        printf("PASS\n");                                                      \
        tests_passed++;                                                        \
    } while (0)

#define FAIL(msg)                                                              \
    do {                                                                       \
        printf("FAIL: %s\n", msg);                                             \
        tests_failed++;                                                        \
    } while (0)

#define ASSERT_EQ(a, b, msg)                                                   \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            printf("\n    Expected: %u, Got: %u - %s\n", (unsigned)(b),        \
                   (unsigned)(a), msg);                                        \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_SUCCESS(result, msg)                                            \
    do {                                                                       \
        if ((result) != LLE_SUCCESS) {                                         \
            printf("\n    Error code: %d - %s\n", (result), msg);              \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_FAILURE(result, msg)                                            \
    do {                                                                       \
        if ((result) == LLE_SUCCESS) {                                         \
            printf("\n    Expected failure but got success - %s\n", msg);      \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

/// Helper to create a minimal valid buffer for testing
static lle_buffer_t *create_test_buffer(const char *content) {
    lle_buffer_t *buffer = calloc(1, sizeof(lle_buffer_t));
    if (!buffer)
        return NULL;

    size_t content_len = content ? strlen(content) : 0;
    buffer->capacity = content_len + 1 > LLE_BUFFER_MIN_CAPACITY
                           ? content_len + 1
                           : LLE_BUFFER_MIN_CAPACITY;
    buffer->data = calloc(buffer->capacity, 1);
    if (!buffer->data) {
        free(buffer);
        return NULL;
    }

    if (content) {
        strcpy(buffer->data, content);
        buffer->length = content_len;
        buffer->used = content_len + 1;
    }

    buffer->line_count = 0;
    buffer->line_capacity = 0;
    buffer->lines = NULL;
    buffer->cursor.byte_offset = 0;
    buffer->cursor.line_number = 0;
    buffer->cursor.position_valid = false;
    buffer->modification_count = 0;

    return buffer;
}

static void free_test_buffer(lle_buffer_t *buffer) {
    if (buffer) {
        free(buffer->data);
        free(buffer->lines);
        free(buffer);
    }
}

/// Test: Buffer validator structure fields
static void test_buffer_validator_structure() {
    TEST("Buffer validator structure has all required fields");

    lle_buffer_validator_t val;
    memset(&val, 0, sizeof(val));

    /// Verify all fields exist and can be set
    val.utf8_validation_enabled = true;
    val.line_structure_validation = true;
    val.cursor_validation_enabled = true;
    val.bounds_checking_enabled = true;
    val.validation_count = 0;
    val.validation_failures = 0;
    val.corruption_detections = 0;
    val.bounds_violations = 0;
    val.utf8_processor = NULL;
    val.last_validation_result = LLE_SUCCESS;
    val.last_validation_time = 0;

    ASSERT_TRUE(sizeof(val.utf8_validation_enabled) > 0,
                "utf8_validation_enabled exists");
    ASSERT_TRUE(sizeof(val.validation_count) > 0, "validation_count exists");
    ASSERT_TRUE(sizeof(val.last_validation_result) > 0,
                "last_validation_result exists");

    PASS();
}

/// Test: Buffer validator initialization
static void test_buffer_validator_init() {
    TEST("Buffer validator initialization");

    lle_buffer_validator_t *validator = NULL;
    lle_result_t result = lle_buffer_validator_init(&validator);

    ASSERT_SUCCESS(result, "Validator initialization succeeds");
    ASSERT_TRUE(validator != NULL, "Validator is allocated");
    ASSERT_TRUE(validator->utf8_validation_enabled,
                "UTF-8 validation enabled by default");
    ASSERT_TRUE(validator->line_structure_validation,
                "Line validation enabled by default");
    ASSERT_TRUE(validator->cursor_validation_enabled,
                "Cursor validation enabled by default");
    ASSERT_TRUE(validator->bounds_checking_enabled,
                "Bounds checking enabled by default");
    ASSERT_EQ(validator->validation_count, 0,
              "Validation count initially zero");
    ASSERT_EQ(validator->validation_failures, 0,
              "Validation failures initially zero");

    lle_buffer_validator_destroy(validator);
    PASS();
}

/// Test: Validate valid buffer
static void test_validate_valid_buffer() {
    TEST("Validate valid buffer");

    lle_buffer_validator_t *validator = NULL;
    lle_result_t result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator initialization succeeds");

    lle_buffer_t *buffer = create_test_buffer("Hello, World!");
    ASSERT_TRUE(buffer != NULL, "Test buffer created");

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Valid buffer passes validation");
    ASSERT_EQ(validator->validation_count, 1, "Validation count incremented");
    ASSERT_EQ(validator->validation_failures, 0, "No validation failures");

    free_test_buffer(buffer);
    lle_buffer_validator_destroy(validator);
    PASS();
}

/// Test: Detect buffer overflow
static void test_detect_buffer_overflow() {
    TEST("Detect buffer overflow (length > capacity)");

    lle_buffer_validator_t *validator = NULL;
    lle_result_t result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator initialization succeeds");

    lle_buffer_t *buffer = create_test_buffer("Hello");
    ASSERT_TRUE(buffer != NULL, "Test buffer created");

    /// Corrupt buffer: set length > capacity
    buffer->length = buffer->capacity + 100;

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_FAILURE(result, "Overflow detected");
    ASSERT_TRUE(validator->validation_failures > 0,
                "Validation failure recorded");
    ASSERT_TRUE(validator->bounds_violations > 0, "Bounds violation recorded");

    free_test_buffer(buffer);
    lle_buffer_validator_destroy(validator);
    PASS();
}

/// Test: Detect invalid UTF-8
static void test_detect_invalid_utf8() {
    TEST("Detect invalid UTF-8 encoding");

    lle_buffer_validator_t *validator = NULL;
    lle_result_t result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator initialization succeeds");

    lle_buffer_t *buffer = create_test_buffer("");
    ASSERT_TRUE(buffer != NULL, "Test buffer created");

    /// Insert invalid UTF-8 sequence
    buffer->data[0] = (char)0xFF; /// Invalid UTF-8 start byte
    buffer->data[1] = (char)0xFF;
    buffer->data[2] = '\0';
    buffer->length = 2;

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_FAILURE(result, "Invalid UTF-8 detected");
    ASSERT_TRUE(validator->validation_failures > 0,
                "Validation failure recorded");

    free_test_buffer(buffer);
    lle_buffer_validator_destroy(validator);
    PASS();
}

/// Test: Detect cursor out of bounds
static void test_detect_cursor_out_of_bounds() {
    TEST("Detect cursor position out of bounds");

    lle_buffer_validator_t *validator = NULL;
    lle_result_t result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator initialization succeeds");

    lle_buffer_t *buffer = create_test_buffer("Hello");
    ASSERT_TRUE(buffer != NULL, "Test buffer created");

    /// Set cursor beyond buffer end
    buffer->cursor.byte_offset = buffer->length + 100;
    buffer->cursor.position_valid = true;

    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_FAILURE(result, "Cursor out of bounds detected");
    ASSERT_TRUE(validator->validation_failures > 0,
                "Validation failure recorded");
    ASSERT_TRUE(validator->bounds_violations > 0, "Bounds violation recorded");

    free_test_buffer(buffer);
    lle_buffer_validator_destroy(validator);
    PASS();
}

/// Test: Validate bounds checking
static void test_validate_bounds() {
    TEST("Buffer bounds validation");

    lle_buffer_validator_t *validator = NULL;
    lle_result_t result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator initialization succeeds");

    lle_buffer_t *buffer = create_test_buffer("Test");
    ASSERT_TRUE(buffer != NULL, "Test buffer created");

    result = lle_buffer_validate_bounds(buffer, validator);
    ASSERT_SUCCESS(result, "Bounds validation succeeds for valid buffer");

    /// Test used > capacity
    buffer->used = buffer->capacity + 1;
    result = lle_buffer_validate_bounds(buffer, validator);
    ASSERT_FAILURE(result, "Detects used > capacity");

    free_test_buffer(buffer);
    lle_buffer_validator_destroy(validator);
    PASS();
}

/// Test: Selective validation (disable features)
static void test_selective_validation() {
    TEST("Selective validation (disable UTF-8 check)");

    lle_buffer_validator_t *validator = NULL;
    lle_result_t result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator initialization succeeds");

    /// Disable UTF-8 validation
    validator->utf8_validation_enabled = false;

    lle_buffer_t *buffer = create_test_buffer("");
    ASSERT_TRUE(buffer != NULL, "Test buffer created");

    /// Insert invalid UTF-8 (should pass because validation disabled)
    buffer->data[0] = (char)0xFF;
    buffer->data[1] = '\0';
    buffer->length = 1;

    result = lle_buffer_validate_utf8(buffer, validator);
    ASSERT_SUCCESS(result, "UTF-8 validation skipped when disabled");

    free_test_buffer(buffer);
    lle_buffer_validator_destroy(validator);
    PASS();
}

/// Test: Validation statistics
static void test_validation_statistics() {
    TEST("Validation statistics tracking");

    lle_buffer_validator_t *validator = NULL;
    lle_result_t result = lle_buffer_validator_init(&validator);
    ASSERT_SUCCESS(result, "Validator initialization succeeds");

    lle_buffer_t *buffer = create_test_buffer("Hello");
    ASSERT_TRUE(buffer != NULL, "Test buffer created");

    /// First validation (should succeed)
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "First validation succeeds");
    ASSERT_EQ(validator->validation_count, 1, "Validation count is 1");

    /// Second validation (should succeed)
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_SUCCESS(result, "Second validation succeeds");
    ASSERT_EQ(validator->validation_count, 2, "Validation count is 2");

    /// Corrupt buffer and validate (should fail)
    buffer->length = buffer->capacity + 1;
    result = lle_buffer_validate_complete(buffer, validator);
    ASSERT_FAILURE(result, "Corrupted buffer validation fails");
    ASSERT_EQ(validator->validation_count, 3, "Validation count is 3");
    ASSERT_EQ(validator->validation_failures, 1, "Validation failures is 1");

    free_test_buffer(buffer);
    lle_buffer_validator_destroy(validator);
    PASS();
}

/// Test: Error handling
static void test_error_handling() {
    TEST("Buffer validator error handling");

    lle_result_t result;

    /// NULL pointer to init
    result = lle_buffer_validator_init(NULL);
    ASSERT_TRUE(result == LLE_ERROR_INVALID_PARAMETER,
                "Init rejects NULL pointer");

    /// NULL pointer to destroy
    result = lle_buffer_validator_destroy(NULL);
    ASSERT_TRUE(result == LLE_ERROR_INVALID_PARAMETER,
                "Destroy rejects NULL pointer");

    /// NULL buffer to validate
    lle_buffer_validator_t *validator = NULL;
    lle_buffer_validator_init(&validator);
    result = lle_buffer_validate_complete(NULL, validator);
    ASSERT_TRUE(result == LLE_ERROR_INVALID_PARAMETER,
                "Validate rejects NULL buffer");

    lle_buffer_validator_destroy(validator);
    PASS();
}

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("Spec 03: Buffer Validator Compliance Tests\n");
    printf("=================================================\n\n");

    /// Structure Tests
    printf("Buffer Validator Structure Tests:\n");
    test_buffer_validator_structure();
    test_buffer_validator_init();

    /// Validation Tests
    printf("\nBuffer Validation Tests:\n");
    test_validate_valid_buffer();
    test_validate_bounds();
    test_selective_validation();

    /// Detection Tests
    printf("\nCorruption Detection Tests:\n");
    test_detect_buffer_overflow();
    test_detect_invalid_utf8();
    test_detect_cursor_out_of_bounds();

    /// Management Tests
    printf("\nValidator Management Tests:\n");
    test_validation_statistics();
    test_error_handling();

    /// Summary
    printf("\n");
    printf("=================================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n\n");

    return (tests_failed == 0) ? 0 : 1;
}
