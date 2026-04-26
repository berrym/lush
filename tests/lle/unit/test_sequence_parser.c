/*
 * test_sequence_parser.c - Unit Tests for Terminal Sequence Parser
 *
 * Tests state machine-based parsing of terminal escape sequences including:
 * - CSI sequences (Control Sequence Introducer)
 * - OSC sequences (Operating System Command)
 * - DCS sequences (Device Control String)
 * - Control characters
 * - Timeout handling
 * - Error recovery
 *
 * Spec 06: Input Parsing - Phase 3 Tests
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

/* Mock terminal capabilities and memory pool */
static int mock_terminal_dummy = 42;
static int mock_pool_dummy = 43;
static lle_terminal_capabilities_t *mock_terminal =
    (lle_terminal_capabilities_t *)&mock_terminal_dummy;
static lle_memory_pool_t *mock_pool = (lle_memory_pool_t *)&mock_pool_dummy;

/*
 * Test: Initialize and destroy sequence parser
 */
void test_init_destroy(void) {
    TEST("init and destroy");

    lle_sequence_parser_t *parser = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");
    ASSERT(parser != NULL, "Parser should not be NULL");

    result = lle_sequence_parser_destroy(parser);
    ASSERT(result == LLE_SUCCESS, "Destroy should succeed");

    TEST_END;
}

/*
 * Test: Initialize with invalid parameters
 */
void test_init_invalid_params(void) {
    TEST("init with invalid parameters");

    lle_sequence_parser_t *parser = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(NULL, mock_terminal, mock_pool);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
           "Init with NULL parser should fail");

    result = lle_sequence_parser_init(&parser, NULL, mock_pool);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
           "Init with NULL terminal should fail");

    result = lle_sequence_parser_init(&parser, mock_terminal, NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
           "Init with NULL pool should fail");

    TEST_END;
}

/*
 * Test: Parse simple control character
 */
void test_control_character(void) {
    TEST("parse control character");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Test Ctrl+C (0x03)
    const char data[] = {0x03};
    result = lle_sequence_parser_process_data(parser, data, 1, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(parsed != NULL, "Should produce parsed input");
    ASSERT(parsed->type == LLE_PARSED_INPUT_TYPE_KEY, "Should be key input");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Parse simple CSI sequence (cursor movement)
 */
void test_csi_simple(void) {
    TEST("parse simple CSI sequence");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // ESC[A - Cursor up
    const char data[] = "\x1B[A";
    result = lle_sequence_parser_process_data(parser, data, 3, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(parsed != NULL, "Should produce parsed input");
    ASSERT(parsed->type == LLE_PARSED_INPUT_TYPE_SEQUENCE,
           "Should be sequence input");

    // Check parser state
    lle_parser_state_t state = lle_sequence_parser_get_state(parser);
    ASSERT(state == LLE_PARSER_STATE_NORMAL, "Should return to normal state");

    lle_sequence_type_t seq_type = lle_sequence_parser_get_type(parser);
    ASSERT(seq_type == LLE_SEQ_TYPE_UNKNOWN, "Type should be reset");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Parse CSI sequence with parameters
 */
void test_csi_with_parameters(void) {
    TEST("parse CSI with parameters");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // ESC[1;5H - Move cursor to row 1, col 5
    const char data[] = "\x1B[1;5H";
    result = lle_sequence_parser_process_data(parser, data, 7, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(parsed != NULL, "Should produce parsed input");

    // Get parameters
    const uint32_t *params;
    uint8_t param_count;
    result = lle_sequence_parser_get_csi_params(parser, &params, &param_count);
    ASSERT(result == LLE_SUCCESS, "Get params should succeed");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Parse CSI sequence with multiple parameters
 */
void test_csi_multiple_parameters(void) {
    TEST("parse CSI with multiple parameters");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // ESC[38;5;196m - Set foreground color to RGB index 196
    const char data[] = "\x1B[38;5;196m";
    result = lle_sequence_parser_process_data(parser, data, 12, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(parsed != NULL, "Should produce parsed input");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Parse OSC sequence (Operating System Command)
 */
void test_osc_sequence(void) {
    TEST("parse OSC sequence");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // ESC]0;Window Title\x07 - Set window title (BEL terminated)
    const char data[] = "\x1B]0;Window Title\x07";
    result = lle_sequence_parser_process_data(parser, data, 20, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(parsed != NULL, "Should produce parsed input");
    ASSERT(parsed->type == LLE_PARSED_INPUT_TYPE_SEQUENCE,
           "Should be sequence input");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Parse OSC sequence with ST terminator
 */
void test_osc_st_terminator(void) {
    TEST("parse OSC with ST terminator");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // ESC]0;Title\ESC\ - Set window title (ST terminated)
    const char data[] = "\x1B]0;Title\x1B\\";
    result = lle_sequence_parser_process_data(parser, data, 12, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(parsed != NULL, "Should produce parsed input");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Parse DCS sequence (Device Control String)
 */
void test_dcs_sequence(void) {
    TEST("parse DCS sequence");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // ESC P...ESC\ - DCS sequence
    const char data[] = "\x1BP0;1;2\x1B\\";
    result = lle_sequence_parser_process_data(parser, data, 10, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(parsed != NULL, "Should produce parsed input");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Parse SS3 sequence (function key)
 */
void test_ss3_sequence(void) {
    TEST("parse SS3 sequence");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // ESC O P - F1 key (SS3 format)
    const char data[] = "\x1BOP";
    result = lle_sequence_parser_process_data(parser, data, 3, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(parsed != NULL, "Should produce parsed input");
    ASSERT(parsed->type == LLE_PARSED_INPUT_TYPE_KEY, "Should be key input");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Reset parser state
 */
void test_reset_state(void) {
    TEST("reset parser state");

    lle_sequence_parser_t *parser = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Start a sequence but don't complete it
    const char data[] = "\x1B[1";
    lle_parsed_input_t *parsed = NULL;
    result = lle_sequence_parser_process_data(parser, data, 3, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");

    // Parser should be in CSI state
    lle_parser_state_t state = lle_sequence_parser_get_state(parser);
    ASSERT(state == LLE_PARSER_STATE_CSI, "Should be in CSI state");

    // Reset
    result = lle_sequence_parser_reset_state(parser);
    ASSERT(result == LLE_SUCCESS, "Reset should succeed");

    // Should be back to normal
    state = lle_sequence_parser_get_state(parser);
    ASSERT(state == LLE_PARSER_STATE_NORMAL, "Should be in normal state");

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Get buffered sequence data
 */
void test_get_buffer(void) {
    TEST("get buffered sequence data");

    lle_sequence_parser_t *parser = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Start a sequence
    const char data[] = "\x1B[1;5";
    lle_parsed_input_t *parsed = NULL;
    result = lle_sequence_parser_process_data(parser, data, 5, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");

    // Get buffer
    const char *buffer;
    size_t buffer_len;
    result = lle_sequence_parser_get_buffer(parser, &buffer, &buffer_len);
    ASSERT(result == LLE_SUCCESS, "Get buffer should succeed");
    ASSERT(buffer_len == 5, "Buffer length should match");
    ASSERT(memcmp(buffer, data, 5) == 0, "Buffer content should match");

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Get error statistics
 */
void test_get_statistics(void) {
    TEST("get error statistics");

    lle_sequence_parser_t *parser = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Get initial stats
    uint32_t malformed, timeout;
    result = lle_sequence_parser_get_stats(parser, &malformed, &timeout);
    ASSERT(result == LLE_SUCCESS, "Get stats should succeed");
    ASSERT(malformed == 0, "Initial malformed count should be 0");
    ASSERT(timeout == 0, "Initial timeout count should be 0");

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Parse multiple sequences in one buffer
 */
void test_multiple_sequences(void) {
    TEST("parse multiple sequences");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // ESC[A followed by ESC[B
    const char data[] = "\x1B[A\x1B[B";
    result = lle_sequence_parser_process_data(parser, data, 6, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");

    // First sequence should be parsed
    ASSERT(parsed != NULL, "Should produce parsed input");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Handle incomplete sequence
 */
void test_incomplete_sequence(void) {
    TEST("handle incomplete sequence");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Incomplete CSI - just ESC[
    const char data[] = "\x1B[";
    result = lle_sequence_parser_process_data(parser, data, 2, &parsed);
    ASSERT(result == LLE_SUCCESS, "Process should succeed");
    ASSERT(parsed == NULL, "Should not produce output yet");

    // Parser should be waiting for more data
    lle_parser_state_t state = lle_sequence_parser_get_state(parser);
    ASSERT(state == LLE_PARSER_STATE_CSI, "Should be in CSI state");

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Test: Complete incomplete sequence with additional data
 */
void test_complete_incomplete_sequence(void) {
    TEST("complete incomplete sequence");

    lle_sequence_parser_t *parser = NULL;
    lle_parsed_input_t *parsed = NULL;
    lle_result_t result;

    result = lle_sequence_parser_init(&parser, mock_terminal, mock_pool);
    ASSERT(result == LLE_SUCCESS, "Init should succeed");

    // Send first part: ESC[1
    const char data1[] = "\x1B[1";
    result = lle_sequence_parser_process_data(parser, data1, 3, &parsed);
    ASSERT(result == LLE_SUCCESS, "First process should succeed");
    ASSERT(parsed == NULL, "Should not produce output yet");

    // Send second part: ;5H
    const char data2[] = ";5H";
    result = lle_sequence_parser_process_data(parser, data2, 3, &parsed);
    ASSERT(result == LLE_SUCCESS, "Second process should succeed");
    ASSERT(parsed != NULL, "Should now produce parsed input");

    if (parsed) {
        lle_pool_free(parsed);
    }

    lle_sequence_parser_destroy(parser);

    TEST_END;
}

/*
 * Main test runner
 */
int main(void) {
    printf("\n=== LLE Sequence Parser Unit Tests ===\n\n");

    RUN_TEST(init_destroy);
    RUN_TEST(init_invalid_params);
    RUN_TEST(control_character);
    RUN_TEST(csi_simple);
    RUN_TEST(csi_with_parameters);
    RUN_TEST(csi_multiple_parameters);
    RUN_TEST(osc_sequence);
    RUN_TEST(osc_st_terminator);
    RUN_TEST(dcs_sequence);
    RUN_TEST(ss3_sequence);
    RUN_TEST(reset_state);
    RUN_TEST(get_buffer);
    RUN_TEST(get_statistics);
    RUN_TEST(multiple_sequences);
    RUN_TEST(incomplete_sequence);
    RUN_TEST(complete_incomplete_sequence);

    return TEST_RESULT();
}
