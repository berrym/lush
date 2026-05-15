/*
 * test_input_utf8_processor.c - Unit Tests for Input UTF-8 Processor
 *
 * Tests streaming UTF-8 decoding, grapheme boundary detection,
 * partial sequence handling, and error recovery.
 *
 * Spec 06: Input Parsing - Phase 2 Tests
 */

#include "../../../include/lle/error_handling.h"
#include "../../../include/lle/input_parsing.h"
#include "test_framework.h"
#include <stdio.h>
#include <string.h>

#undef TEST
#define TEST(name_str) ((void)0)
#define TEST_END ((void)0)
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* Mock memory pool (same as test_input_stream.c) */
static int mock_pool_dummy = 43;
static lle_memory_pool_t *mock_pool = (lle_memory_pool_t *)&mock_pool_dummy;

/*
 * Test: Initialize and destroy UTF-8 processor
 */
void test_init_destroy(void) {
    TEST("init and destroy");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");
    ASSERT(processor != NULL, "Processor should not be NULL");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Initialize with invalid parameters
 */
void test_init_invalid_params(void) {
    TEST("init with invalid parameters");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;

    result = lle_input_utf8_processor_init(NULL, mock_pool);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
           "Init with NULL processor should fail");

    result = lle_input_utf8_processor_init(&processor, NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
           "Init with NULL pool should fail");

    TEST_END;
}

/*
 * Test: Process single ASCII byte
 */
void test_process_ascii_byte(void) {
    TEST("process single ASCII byte");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;
    uint32_t codepoint = 0;
    bool is_boundary = false;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    result = lle_input_utf8_processor_process_byte(processor, 'A', &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(codepoint == 'A', "Codepoint should be 'A'");
    ASSERT(is_boundary == true, "ASCII should be boundary");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Process multi-byte UTF-8 sequence (2 bytes)
 */
void test_process_two_byte_utf8(void) {
    TEST("process two-byte UTF-8 sequence");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;
    uint32_t codepoint = 0;
    bool is_boundary = false;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // U+00E9 (é) = C3 A9
    result = lle_input_utf8_processor_process_byte(processor, 0xC3, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "First byte should succeed");
    ASSERT(codepoint == 0, "No codepoint yet");

    result = lle_input_utf8_processor_process_byte(processor, 0xA9, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "Second byte should succeed");
    ASSERT(codepoint == 0x00E9, "Codepoint should be U+00E9");
    ASSERT(is_boundary == true, "Should be boundary");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Process multi-byte UTF-8 sequence (3 bytes)
 */
void test_process_three_byte_utf8(void) {
    TEST("process three-byte UTF-8 sequence");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;
    uint32_t codepoint = 0;
    bool is_boundary = false;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // U+2603 (☃) = E2 98 83
    result = lle_input_utf8_processor_process_byte(processor, 0xE2, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "First byte should succeed");
    ASSERT(codepoint == 0, "No codepoint yet");

    result = lle_input_utf8_processor_process_byte(processor, 0x98, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "Second byte should succeed");
    ASSERT(codepoint == 0, "No codepoint yet");

    result = lle_input_utf8_processor_process_byte(processor, 0x83, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "Third byte should succeed");
    ASSERT(codepoint == 0x2603, "Codepoint should be U+2603");
    ASSERT(is_boundary == true, "Should be boundary");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Process multi-byte UTF-8 sequence (4 bytes - emoji)
 */
void test_process_four_byte_utf8(void) {
    TEST("process four-byte UTF-8 sequence (emoji)");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;
    uint32_t codepoint = 0;
    bool is_boundary = false;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // U+1F600 (😀) = F0 9F 98 80
    result = lle_input_utf8_processor_process_byte(processor, 0xF0, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "First byte should succeed");

    result = lle_input_utf8_processor_process_byte(processor, 0x9F, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "Second byte should succeed");

    result = lle_input_utf8_processor_process_byte(processor, 0x98, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "Third byte should succeed");

    result = lle_input_utf8_processor_process_byte(processor, 0x80, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "Fourth byte should succeed");
    ASSERT(codepoint == 0x1F600, "Codepoint should be U+1F600");
    ASSERT(is_boundary == true, "Should be boundary");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Invalid UTF-8 start byte
 */
void test_invalid_start_byte(void) {
    TEST("invalid UTF-8 start byte");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;
    uint32_t codepoint = 0;
    bool is_boundary = false;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // 0xFF is invalid UTF-8 start byte
    result = lle_input_utf8_processor_process_byte(processor, 0xFF, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_ERROR_INVALID_ENCODING, "Invalid byte should fail");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Invalid continuation byte
 */
void test_invalid_continuation_byte(void) {
    TEST("invalid continuation byte");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;
    uint32_t codepoint = 0;
    bool is_boundary = false;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Start two-byte sequence
    result = lle_input_utf8_processor_process_byte(processor, 0xC3, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "First byte should succeed");

    // Send invalid continuation (should be 10xxxxxx, sending 11xxxxxx)
    result = lle_input_utf8_processor_process_byte(processor, 0xC0, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_ERROR_INVALID_ENCODING,
           "Invalid continuation should fail");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Process buffer with multiple codepoints
 */
void test_process_buffer(void) {
    TEST("process buffer with multiple codepoints");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // "Hé☃😀" = 48 C3A9 E29883 F09F9880
    const char *text = "H\xC3\xA9\xE2\x98\x83\xF0\x9F\x98\x80";
    size_t text_len = strlen(text);

    lle_codepoint_info_t codepoints[10];
    size_t decoded_count = 0;
    size_t bytes_consumed = 0;

    result = lle_input_utf8_processor_process_buffer(
        processor, text, text_len, codepoints, 10, &decoded_count,
        &bytes_consumed);

    ASSERT(result == LLE_SUCCESS, "Process buffer should succeed");
    ASSERT(decoded_count == 4, "Should decode 4 codepoints");
    ASSERT(bytes_consumed == text_len, "Should consume all bytes");

    ASSERT(codepoints[0].codepoint == 'H', "First codepoint should be 'H'");
    ASSERT(codepoints[1].codepoint == 0x00E9,
           "Second codepoint should be U+00E9");
    ASSERT(codepoints[2].codepoint == 0x2603,
           "Third codepoint should be U+2603");
    ASSERT(codepoints[3].codepoint == 0x1F600,
           "Fourth codepoint should be U+1F600");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Partial sequence detection
 */
void test_partial_sequence(void) {
    TEST("partial sequence detection");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;
    uint32_t codepoint = 0;
    bool is_boundary = false;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Start three-byte sequence
    result = lle_input_utf8_processor_process_byte(processor, 0xE2, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "First byte should succeed");

    bool has_partial = lle_input_utf8_processor_has_partial(processor);
    ASSERT(has_partial == true, "Should have partial sequence");

    size_t needed = lle_input_utf8_processor_bytes_needed(processor);
    ASSERT(needed == 2, "Should need 2 more bytes");

    // Add second byte
    result = lle_input_utf8_processor_process_byte(processor, 0x98, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "Second byte should succeed");

    needed = lle_input_utf8_processor_bytes_needed(processor);
    ASSERT(needed == 1, "Should need 1 more byte");

    // Complete sequence
    result = lle_input_utf8_processor_process_byte(processor, 0x83, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "Third byte should succeed");

    has_partial = lle_input_utf8_processor_has_partial(processor);
    ASSERT(has_partial == false, "Should not have partial sequence");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Reset processor
 */
void test_reset(void) {
    TEST("reset processor");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;
    uint32_t codepoint = 0;
    bool is_boundary = false;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Start partial sequence
    result = lle_input_utf8_processor_process_byte(processor, 0xE2, &codepoint,
                                                   &is_boundary);
    ASSERT(result == LLE_SUCCESS, "First byte should succeed");

    bool has_partial = lle_input_utf8_processor_has_partial(processor);
    ASSERT(has_partial == true, "Should have partial sequence");

    // Reset
    result = lle_input_utf8_processor_reset(processor);
    ASSERT(result == LLE_SUCCESS, "Reset should succeed");

    has_partial = lle_input_utf8_processor_has_partial(processor);
    ASSERT(has_partial == false,
           "Should not have partial sequence after reset");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Get statistics
 */
void test_get_statistics(void) {
    TEST("get statistics");

    lle_utf8_processor_t *processor = NULL;
    lle_result_t result;

    result = lle_input_utf8_processor_init(&processor, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Process some text
    const char *text = "Hé☃😀";
    lle_codepoint_info_t codepoints[10];
    size_t decoded_count = 0;
    size_t bytes_consumed = 0;

    result = lle_input_utf8_processor_process_buffer(
        processor, text, strlen(text), codepoints, 10, &decoded_count,
        &bytes_consumed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");

    // Get statistics
    lle_utf8_processor_stats_t stats;
    result = lle_input_utf8_processor_get_stats(processor, &stats);
    ASSERT(result == LLE_SUCCESS, "Get stats should succeed");
    ASSERT(stats.codepoints_processed == 4,
           "Should have processed 4 codepoints");
    ASSERT(stats.grapheme_clusters_detected == 4,
           "Should have detected 4 graphemes");
    ASSERT(stats.invalid_sequences_handled == 0,
           "Should have no invalid sequences");

    lle_input_utf8_processor_destroy(processor);

    TEST_END;
}

/*
 * Test: Convenience wrapper - validate string
 */
void test_validate_string(void) {
    TEST("validate string wrapper");

    bool valid = lle_input_utf8_validate_string("Hello", 5);
    ASSERT(valid == true, "ASCII should be valid");

    valid = lle_input_utf8_validate_string("Hé☃😀", 11);
    ASSERT(valid == true, "UTF-8 should be valid");

    // Invalid UTF-8
    const char invalid[] = {0xFF, 0x00};
    valid = lle_input_utf8_validate_string(invalid, 1);
    ASSERT(valid == false, "Invalid UTF-8 should fail");

    TEST_END;
}

/*
 * Test: Convenience wrapper - count codepoints
 */
void test_count_codepoints(void) {
    TEST("count codepoints wrapper");

    size_t count = lle_input_utf8_count_codepoints("Hello", 5);
    ASSERT(count == 5, "ASCII should have 5 codepoints");

    // "Hé☃😀" = H(1) + é(2) + ☃(3) + 😀(4) = 10 bytes
    const char *text = "H\xC3\xA9\xE2\x98\x83\xF0\x9F\x98\x80";
    count = lle_input_utf8_count_codepoints(text, 10);
    ASSERT(count == 4, "UTF-8 should have 4 codepoints");

    TEST_END;
}

/*
 * Test: Convenience wrapper - count graphemes
 */
void test_count_graphemes(void) {
    TEST("count graphemes wrapper");

    size_t count = lle_input_utf8_count_graphemes("Hello", 5);
    ASSERT(count == 5, "ASCII should have 5 graphemes");

    // "Hé☃😀" = H(1) + é(2) + ☃(3) + 😀(4) = 10 bytes
    const char *text = "H\xC3\xA9\xE2\x98\x83\xF0\x9F\x98\x80";
    count = lle_input_utf8_count_graphemes(text, 10);
    ASSERT(count == 4, "Simple UTF-8 should have 4 graphemes");

    TEST_END;
}

/*
 * Test: Convenience wrapper - get display width
 */
void test_get_display_width(void) {
    TEST("get display width wrapper");

    size_t width = lle_input_utf8_get_display_width("Hello", 5);
    ASSERT(width == 5, "ASCII should have width 5");

    width = lle_input_utf8_get_display_width("Hé", 3);
    ASSERT(width == 2, "Hé should have width 2");

    // Emoji has width 2
    width = lle_input_utf8_get_display_width("😀", 4);
    ASSERT(width == 2, "Emoji should have width 2");

    TEST_END;
}

/*
 * Main test runner
 */
int main(void) {
    printf("\n=== LLE Input UTF-8 Processor Unit Tests ===\n\n");

    RUN_TEST(init_destroy);
    RUN_TEST(init_invalid_params);
    RUN_TEST(process_ascii_byte);
    RUN_TEST(process_two_byte_utf8);
    RUN_TEST(process_three_byte_utf8);
    RUN_TEST(process_four_byte_utf8);
    RUN_TEST(invalid_start_byte);
    RUN_TEST(invalid_continuation_byte);
    RUN_TEST(process_buffer);
    RUN_TEST(partial_sequence);
    RUN_TEST(reset);
    RUN_TEST(get_statistics);
    RUN_TEST(validate_string);
    RUN_TEST(count_codepoints);
    RUN_TEST(count_graphemes);
    RUN_TEST(get_display_width);

    return TEST_RESULT();
}
