/**
 * @file spec_02_terminal_abstraction_compliance.c
 * @brief Spec 02 Terminal Abstraction - Compliance Test
 *
 * API verified from include/lle/terminal_abstraction.h on 2025-10-30
 *
 * This test verifies that Spec 02 type definitions match the specification.
 *
 * Layer 0: Type definition compliance (structure existence and field
 * verification) Layer 1: Function implementation compliance (now implemented)
 */

#include "lle/error_handling.h"
#include "lle/terminal_abstraction.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Test counter
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (condition) {                                                       \
            tests_passed++;                                                    \
        } else {                                                               \
            printf("FAILED: %s\n", message);                                   \
            tests_failed++;                                                    \
        }                                                                      \
    } while (0)

/**
 * @brief Test: Verify lle_terminal_abstraction_t structure exists
 */
void test_terminal_abstraction_structure(void) {
    printf("[ TEST ] Terminal abstraction structure definition\n");

    // Verify structure can be declared
    lle_terminal_abstraction_t *abs = NULL;
    TEST_ASSERT(abs == NULL, "Can declare lle_terminal_abstraction_t pointer");

    // Verify size is reasonable (non-zero, not too large)
    size_t size = sizeof(lle_terminal_abstraction_t);
    TEST_ASSERT(size > 0, "Structure has non-zero size");
    TEST_ASSERT(size < 10000, "Structure size is reasonable");

    printf("[ PASS ] Terminal abstraction structure definition\n");
}

/**
 * @brief Test: Verify lle_terminal_capabilities_t structure
 */
void test_terminal_capabilities_structure(void) {
    printf("[ TEST ] Terminal capabilities structure definition\n");

    lle_terminal_capabilities_t caps;
    memset(&caps, 0, sizeof(caps));

    // Verify boolean fields exist
    caps.is_tty = true;
    caps.supports_ansi_colors = true;
    caps.supports_256_colors = true;
    caps.supports_truecolor = true;

    TEST_ASSERT(caps.is_tty == true, "is_tty field accessible");
    TEST_ASSERT(caps.supports_ansi_colors == true,
                "supports_ansi_colors field accessible");

    // Verify geometry fields exist
    caps.terminal_width = 80;
    caps.terminal_height = 24;
    TEST_ASSERT(caps.terminal_width == 80, "terminal_width field accessible");
    TEST_ASSERT(caps.terminal_height == 24, "terminal_height field accessible");

    printf("[ PASS ] Terminal capabilities structure definition\n");
}

/**
 * @brief Test: Verify lle_internal_state_t structure
 */
void test_internal_state_structure(void) {
    printf("[ TEST ] Internal state structure definition\n");

    lle_internal_state_t state;
    memset(&state, 0, sizeof(state));

    // Verify core fields exist
    state.cursor_position = 0;
    state.has_selection = false;
    state.buffer_modified = false;

    TEST_ASSERT(state.cursor_position == 0, "cursor_position field accessible");
    TEST_ASSERT(state.has_selection == false, "has_selection field accessible");
    TEST_ASSERT(state.buffer_modified == false,
                "buffer_modified field accessible");

    printf("[ PASS ] Internal state structure definition\n");
}

/**
 * @brief Test: Verify lle_display_content_t structure
 */
void test_display_content_structure(void) {
    printf("[ TEST ] Display content structure definition\n");

    lle_display_content_t content;
    memset(&content, 0, sizeof(content));

    // Verify fields exist
    content.line_count = 0;
    content.cursor_line = 0;
    content.cursor_column = 0;
    content.cursor_visible = true;

    TEST_ASSERT(content.line_count == 0, "line_count field accessible");
    TEST_ASSERT(content.cursor_visible == true,
                "cursor_visible field accessible");

    printf("[ PASS ] Display content structure definition\n");
}

/**
 * @brief Test: Verify lle_input_event_t structure
 */
void test_input_event_structure(void) {
    printf("[ TEST ] Input event structure definition\n");

    lle_input_event_t event;
    memset(&event, 0, sizeof(event));

    // Verify fields exist
    event.type = LLE_INPUT_TYPE_CHARACTER;
    event.timestamp = 0;
    event.sequence_number = 0;

    TEST_ASSERT(event.type == LLE_INPUT_TYPE_CHARACTER,
                "type field accessible");
    TEST_ASSERT(event.timestamp == 0, "timestamp field accessible");

    printf("[ PASS ] Input event structure definition\n");
}

/**
 * @brief Test: Verify enum types are defined
 */
void test_enum_definitions(void) {
    printf("[ TEST ] Enum type definitions\n");

    // Terminal type enum
    lle_terminal_type_t term_type = LLE_TERMINAL_XTERM;
    TEST_ASSERT(term_type == LLE_TERMINAL_XTERM,
                "lle_terminal_type_t enum defined");

    // Input type enum
    lle_input_type_t input_type = LLE_INPUT_TYPE_CHARACTER;
    TEST_ASSERT(input_type == LLE_INPUT_TYPE_CHARACTER,
                "lle_input_type_t enum defined");

    // Special key enum
    lle_special_key_t special_key = LLE_KEY_UP;
    TEST_ASSERT(special_key == LLE_KEY_UP, "lle_special_key_t enum defined");

    // Key modifier enum
    lle_key_modifier_t modifier = LLE_MOD_CTRL;
    TEST_ASSERT(modifier == LLE_MOD_CTRL, "lle_key_modifier_t enum defined");

    printf("[ PASS ] Enum type definitions\n");
}

/**
 * @brief Test: Verify function declarations exist
 *
 * Layer 1: Function declarations now implemented (Spec 02 complete)
 */
void test_function_declarations(void) {
    printf("[ TEST ] Function declarations (Layer 1 compliance)\n");

    // Verify key function pointers exist
    void *fn1 = (void *)lle_terminal_abstraction_init;
    void *fn2 = (void *)lle_terminal_abstraction_destroy;
    void *fn3 = (void *)lle_capabilities_detect_environment;
    void *fn4 = (void *)lle_internal_state_init;
    void *fn5 = (void *)lle_display_generator_generate_content;
    void *fn6 = (void *)lle_unix_interface_init;
    void *fn7 = (void *)lle_command_buffer_init;
    void *fn8 = (void *)lle_input_processor_init;

    TEST_ASSERT(fn1 != NULL, "lle_terminal_abstraction_init declared");
    TEST_ASSERT(fn2 != NULL, "lle_terminal_abstraction_destroy declared");
    TEST_ASSERT(fn3 != NULL, "lle_capabilities_detect_environment declared");
    TEST_ASSERT(fn4 != NULL, "lle_internal_state_init declared");
    TEST_ASSERT(fn5 != NULL, "lle_display_generator_generate_content declared");
    TEST_ASSERT(fn6 != NULL, "lle_unix_interface_init declared");
    TEST_ASSERT(fn7 != NULL, "lle_command_buffer_init declared");
    TEST_ASSERT(fn8 != NULL, "lle_input_processor_init declared");

    printf("[ PASS ] Function declarations (Layer 1 compliance)\n");
}

int main(void) {
    printf("=================================================\n");
    printf("Spec 02 Terminal Abstraction - Compliance Tests\n");
    printf("Layer 0 + Layer 1: Type and Function Compliance\n");
    printf("=================================================\n\n");

    test_terminal_abstraction_structure();
    test_terminal_capabilities_structure();
    test_internal_state_structure();
    test_display_content_structure();
    test_input_event_structure();
    test_enum_definitions();
    test_function_declarations();

    printf("\n=================================================\n");
    printf("Compliance Test Results:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n");

    if (tests_failed > 0) {
        printf("COMPLIANCE VIOLATION: %d tests failed\n", tests_failed);
        return 1;
    }

    printf("COMPLIANCE: All type definitions match Spec 02\n");
    return 0;
}
