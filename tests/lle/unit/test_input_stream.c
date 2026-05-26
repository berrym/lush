/**
 * Unit Tests for LLE Input Stream Management
 *
 * Tests the input stream buffering and flow control implementation.
 * Spec 06 Phase 1: Input Stream Management
 */

#include "lle/error_handling.h"
#include "lle/input_parsing.h"
#include "lle/memory_management.h"
#include "test_framework.h"

#undef TEST
#define TEST(name_str) ((void)0)
#define TEST_END ((void)0)
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)
#include <stdio.h>
#include <string.h>

/// Mock terminal system and memory pool (opaque pointers for testing)
/// We use dummy non-NULL pointers since the functions check for NULL
static int mock_terminal_dummy = 42;
static int mock_pool_dummy = 43;
static lle_terminal_system_t *mock_terminal =
    (lle_terminal_system_t *)&mock_terminal_dummy;
static lle_memory_pool_t *mock_pool = (lle_memory_pool_t *)&mock_pool_dummy;

/* ============================================================================
 * Test: Initialization and Destruction
 * ============================================================================
 */

void test_init_destroy(void) {
    TEST("init and destroy");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;

    /// Test successful initialization
    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");
    ASSERT(stream != NULL, "Stream should not be NULL");
    ASSERT(stream->buffer != NULL, "Buffer should be allocated");
    ASSERT(stream->buffer_size == LLE_INPUT_BUFFER_SIZE,
           "Buffer size should match");
    ASSERT(stream->buffer_used == 0, "Buffer should be empty initially");
    ASSERT(stream->buffer_pos == 0, "Buffer position should be 0");
    ASSERT(stream->blocking_mode == false, "Should default to non-blocking");

    /// Test destruction
    result = lle_input_stream_destroy(stream);
    ASSERT(result == LLE_SUCCESS, "Destroy should succeed");

    TEST_END;
}

void test_init_invalid_params(void) {
    TEST("init with invalid parameters");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;

    /// NULL stream pointer
    result = lle_input_stream_init(NULL, mock_terminal, mock_pool);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER, "Should reject NULL stream");

    /// NULL terminal
    result = lle_input_stream_init(&stream, NULL, mock_pool);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
           "Should reject NULL terminal");

    /// NULL memory pool
    result = lle_input_stream_init(&stream, mock_terminal, NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER, "Should reject NULL pool");

    TEST_END;
}

/* ============================================================================
 * Test: Buffer Data (Testing/Simulation)
 * ============================================================================
 */

void test_buffer_data(void) {
    TEST("buffer data");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *test_data = "Hello, World!";
    size_t test_len = strlen(test_data);

    /// Initialize stream
    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Buffer some data
    result = lle_input_stream_buffer_data(stream, test_data, test_len);
    ASSERT(result == LLE_SUCCESS, "Buffer data should succeed");
    ASSERT(stream->buffer_used == test_len,
           "Buffer used should match data length");
    ASSERT(memcmp(stream->buffer, test_data, test_len) == 0,
           "Data should match");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

void test_buffer_data_multiple(void) {
    TEST("buffer data multiple times");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *data1 = "First ";
    const char *data2 = "Second ";
    const char *data3 = "Third";

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Buffer multiple chunks
    result = lle_input_stream_buffer_data(stream, data1, strlen(data1));
    ASSERT(result == LLE_SUCCESS, "First buffer should succeed");

    result = lle_input_stream_buffer_data(stream, data2, strlen(data2));
    ASSERT(result == LLE_SUCCESS, "Second buffer should succeed");

    result = lle_input_stream_buffer_data(stream, data3, strlen(data3));
    ASSERT(result == LLE_SUCCESS, "Third buffer should succeed");

    /// Verify total data
    size_t expected_len = strlen(data1) + strlen(data2) + strlen(data3);
    ASSERT(stream->buffer_used == expected_len,
           "Total buffer used should match");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

/* ============================================================================
 * Test: Get Buffered Data
 * ============================================================================
 */

void test_get_buffered(void) {
    TEST("get buffered data");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *test_data = "Test Data";
    const char *buffered_data = NULL;
    size_t buffered_len = 0;

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Buffer some data
    result = lle_input_stream_buffer_data(stream, test_data, strlen(test_data));
    ASSERT(result == LLE_SUCCESS, "Buffer should succeed");

    /// Get buffered data
    result =
        lle_input_stream_get_buffered(stream, &buffered_data, &buffered_len);
    ASSERT(result == LLE_SUCCESS, "Get buffered should succeed");
    ASSERT(buffered_data != NULL, "Buffered data should not be NULL");
    ASSERT(buffered_len == strlen(test_data), "Buffered length should match");
    ASSERT(memcmp(buffered_data, test_data, buffered_len) == 0,
           "Data should match");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

void test_get_buffered_empty(void) {
    TEST("get buffered data when empty");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *buffered_data = NULL;
    size_t buffered_len = 0;

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Get buffered data when empty
    result =
        lle_input_stream_get_buffered(stream, &buffered_data, &buffered_len);
    ASSERT(result == LLE_SUCCESS,
           "Get buffered should succeed even when empty");
    ASSERT(buffered_data == NULL, "Buffered data should be NULL when empty");
    ASSERT(buffered_len == 0, "Buffered length should be 0 when empty");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

/* ============================================================================
 * Test: Consume Data
 * ============================================================================
 */

void test_consume(void) {
    TEST("consume buffered data");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *test_data = "0123456789";
    const char *buffered_data = NULL;
    size_t buffered_len = 0;

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Buffer data
    result = lle_input_stream_buffer_data(stream, test_data, strlen(test_data));
    ASSERT(result == LLE_SUCCESS, "Buffer should succeed");

    /// Consume 5 bytes
    result = lle_input_stream_consume(stream, 5);
    ASSERT(result == LLE_SUCCESS, "Consume should succeed");
    ASSERT(stream->buffer_pos == 5, "Buffer position should be 5");

    /// Get remaining data
    result =
        lle_input_stream_get_buffered(stream, &buffered_data, &buffered_len);
    ASSERT(result == LLE_SUCCESS, "Get buffered should succeed");
    ASSERT(buffered_len == 5, "Should have 5 bytes remaining");
    ASSERT(memcmp(buffered_data, "56789", 5) == 0,
           "Remaining data should match");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

void test_consume_invalid(void) {
    TEST("consume with invalid parameters");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *test_data = "Test";

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Buffer some data
    result = lle_input_stream_buffer_data(stream, test_data, strlen(test_data));
    ASSERT(result == LLE_SUCCESS, "Buffer should succeed");

    /// Try to consume more than available
    result = lle_input_stream_consume(stream, 100);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
           "Should reject consuming too much");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

/* ============================================================================
 * Test: Peek
 * ============================================================================
 */

void test_peek(void) {
    TEST("peek at data");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *test_data = "ABCDEF";
    char byte;

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Buffer data
    result = lle_input_stream_buffer_data(stream, test_data, strlen(test_data));
    ASSERT(result == LLE_SUCCESS, "Buffer should succeed");

    /// Peek at different offsets
    result = lle_input_stream_peek(stream, 0, &byte);
    ASSERT(result == LLE_SUCCESS, "Peek should succeed");
    ASSERT(byte == 'A', "First byte should be 'A'");

    result = lle_input_stream_peek(stream, 2, &byte);
    ASSERT(result == LLE_SUCCESS, "Peek should succeed");
    ASSERT(byte == 'C', "Third byte should be 'C'");

    result = lle_input_stream_peek(stream, 5, &byte);
    ASSERT(result == LLE_SUCCESS, "Peek should succeed");
    ASSERT(byte == 'F', "Sixth byte should be 'F'");

    /// Verify buffer position unchanged
    ASSERT(stream->buffer_pos == 0,
           "Buffer position should not change after peek");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

void test_peek_out_of_bounds(void) {
    TEST("peek out of bounds");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *test_data = "ABC";
    char byte;

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Buffer data
    result = lle_input_stream_buffer_data(stream, test_data, strlen(test_data));
    ASSERT(result == LLE_SUCCESS, "Buffer should succeed");

    /// Try to peek beyond available data
    result = lle_input_stream_peek(stream, 10, &byte);
    ASSERT(result == LLE_ERROR_BUFFER_UNDERFLOW,
           "Should fail for out of bounds peek");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

/* ============================================================================
 * Test: Statistics
 * ============================================================================
 */

void test_statistics(void) {
    TEST("get statistics");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *data1 = "First";
    const char *data2 = "Second";
    uint64_t bytes_read = 0;
    uint64_t read_operations = 0;
    uint64_t buffer_overflows = 0;

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Buffer data twice
    lle_input_stream_buffer_data(stream, data1, strlen(data1));
    lle_input_stream_buffer_data(stream, data2, strlen(data2));

    /// Get statistics
    result = lle_input_stream_get_statistics(
        stream, &bytes_read, &read_operations, &buffer_overflows);
    ASSERT(result == LLE_SUCCESS, "Get statistics should succeed");
    ASSERT(bytes_read == strlen(data1) + strlen(data2),
           "Bytes read should match");
    ASSERT(buffer_overflows == 0, "Should have no overflows");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

/* ============================================================================
 * Test: Reset
 * ============================================================================
 */

void test_reset(void) {
    TEST("reset stream");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *test_data = "Test Data";
    const char *buffered_data = NULL;
    size_t buffered_len = 0;

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Buffer data
    result = lle_input_stream_buffer_data(stream, test_data, strlen(test_data));
    ASSERT(result == LLE_SUCCESS, "Buffer should succeed");

    /// Reset
    result = lle_input_stream_reset(stream);
    ASSERT(result == LLE_SUCCESS, "Reset should succeed");

    /// Verify everything is cleared
    ASSERT(stream->buffer_used == 0, "Buffer should be empty");
    ASSERT(stream->buffer_pos == 0, "Buffer position should be 0");
    ASSERT(stream->bytes_read == 0, "Bytes read should be 0");

    /// Verify no data available
    result =
        lle_input_stream_get_buffered(stream, &buffered_data, &buffered_len);
    ASSERT(result == LLE_SUCCESS, "Get buffered should succeed");
    ASSERT(buffered_len == 0, "Should have no data after reset");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

/* ============================================================================
 * Test: Available Bytes
 * ============================================================================
 */

void test_get_available(void) {
    TEST("get available bytes");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    const char *test_data = "0123456789";
    size_t available = 0;

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Initially empty
    result = lle_input_stream_get_available(stream, &available);
    ASSERT(result == LLE_SUCCESS, "Get available should succeed");
    ASSERT(available == 0, "Should have 0 bytes initially");

    /// Buffer data
    result = lle_input_stream_buffer_data(stream, test_data, strlen(test_data));
    ASSERT(result == LLE_SUCCESS, "Buffer should succeed");

    /// Check available
    result = lle_input_stream_get_available(stream, &available);
    ASSERT(result == LLE_SUCCESS, "Get available should succeed");
    ASSERT(available == strlen(test_data), "Should have all bytes available");

    /// Consume some
    result = lle_input_stream_consume(stream, 3);
    ASSERT(result == LLE_SUCCESS, "Consume should succeed");

    /// Check available again
    result = lle_input_stream_get_available(stream, &available);
    ASSERT(result == LLE_SUCCESS, "Get available should succeed");
    ASSERT(available == strlen(test_data) - 3,
           "Should have reduced available bytes");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

/* ============================================================================
 * Test: Buffer Overflow Handling
 * ============================================================================
 */

void test_buffer_overflow(void) {
    TEST("buffer overflow handling");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    char large_data[LLE_INPUT_BUFFER_SIZE + 100];
    memset(large_data, 'X', sizeof(large_data));

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Try to buffer more than buffer size
    result =
        lle_input_stream_buffer_data(stream, large_data, sizeof(large_data));
    ASSERT(result == LLE_ERROR_BUFFER_OVERFLOW,
           "Should detect buffer overflow");

    /// Verify overflow was counted
    uint64_t overflows = 0;
    lle_input_stream_get_statistics(stream, NULL, NULL, &overflows);
    ASSERT(overflows > 0, "Overflow count should be incremented");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

/* ============================================================================
 * Test: Buffer Compaction
 * ============================================================================
 */

void test_buffer_compaction(void) {
    TEST("buffer compaction");

    lle_input_stream_t *stream = NULL;
    lle_result_t result;
    char data[100];
    memset(data, 'A', sizeof(data));

    result = lle_input_stream_init(&stream, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /// Fill buffer with enough data
    size_t half_size = LLE_INPUT_BUFFER_SIZE / 2;
    size_t initial_data = half_size + 200;

    /// Buffer data in chunks to fill more than half
    for (size_t i = 0; i < initial_data / sizeof(data); i++) {
        result = lle_input_stream_buffer_data(stream, data, sizeof(data));
        ASSERT(result == LLE_SUCCESS, "Buffer should succeed");
    }

    /// Consume more than half (triggers compaction)
    size_t consume_amount = half_size + 100;
    result = lle_input_stream_consume(stream, consume_amount);
    ASSERT(result == LLE_SUCCESS, "Consume should succeed");

    /// Verify compaction occurred
    ASSERT(stream->buffer_pos == 0,
           "Buffer position should be 0 after compaction");
    ASSERT(stream->buffer_used < consume_amount, "Buffer should be compacted");

    /// Cleanup
    lle_input_stream_destroy(stream);

    TEST_END;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================
 */

int main(void) {
    printf("\n");
    printf("==================================================================="
           "==========\n");
    printf("LLE Input Stream Unit Tests\n");
    printf("==================================================================="
           "==========\n");
    printf("\n");

    /// Lifecycle tests
    printf("Lifecycle Tests:\n");
    RUN_TEST(init_destroy);
    RUN_TEST(init_invalid_params);
    printf("\n");

    /// Buffer management tests
    printf("Buffer Management Tests:\n");
    RUN_TEST(buffer_data);
    RUN_TEST(buffer_data_multiple);
    RUN_TEST(get_buffered);
    RUN_TEST(get_buffered_empty);
    printf("\n");

    /// Consume tests
    printf("Consume Tests:\n");
    RUN_TEST(consume);
    RUN_TEST(consume_invalid);
    printf("\n");

    /// Peek tests
    printf("Peek Tests:\n");
    RUN_TEST(peek);
    RUN_TEST(peek_out_of_bounds);
    printf("\n");

    /// Utility tests
    printf("Utility Tests:\n");
    RUN_TEST(statistics);
    RUN_TEST(reset);
    RUN_TEST(get_available);
    printf("\n");

    /// Edge case tests
    printf("Edge Case Tests:\n");
    RUN_TEST(buffer_overflow);
    RUN_TEST(buffer_compaction);
    printf("\n");

    return TEST_RESULT();
}
