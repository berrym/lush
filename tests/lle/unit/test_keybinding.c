/**
 * test_keybinding.c - Unit tests for keybinding engine
 *
 * Tests GNU Readline compatible keybinding system.
 *
 * Date: 2025-11-02
 */

#include "lle/keybinding.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

// Mock editor structure for testing
struct lle_editor {
    int dummy; /**< Placeholder */
};

// Test action functions
static int g_action_called = 0;

static lle_result_t test_action(lle_editor_t *editor) {
    (void)editor;
    g_action_called++;
    return LLE_SUCCESS;
}

LLE_MAYBE_UNUSED
static lle_result_t test_action_error(lle_editor_t *editor) {
    (void)editor;
    g_action_called++;
    return LLE_ERROR_FATAL_INTERNAL;
}

/* ============================================================================
 * LIFECYCLE TESTS
 * ============================================================================
 */

TEST(create_destroy) {
    lle_keybinding_manager_t *manager = NULL;
    lle_result_t result;

    // Create manager
    result = lle_keybinding_manager_create(&manager, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");
    ASSERT(manager != NULL, "Manager is NULL");

    // Verify initial state
    lle_keymap_mode_t mode;
    result = lle_keybinding_manager_get_mode(manager, &mode);
    ASSERT(result == LLE_SUCCESS, "Get mode failed");
    ASSERT(mode == LLE_KEYMAP_EMACS, "Default mode not EMACS");

    size_t count;
    result = lle_keybinding_manager_get_count(manager, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 0, "Initial count not 0");

    // Destroy
    result = lle_keybinding_manager_destroy(manager);
    ASSERT(result == LLE_SUCCESS, "Destroy failed");
}

TEST(null_pointer_checks) {
    lle_result_t result;

    // Create with NULL pointer
    result = lle_keybinding_manager_create(NULL, NULL);
    ASSERT(result == LLE_ERROR_NULL_POINTER, "Create accepted NULL");

    // Destroy with NULL
    result = lle_keybinding_manager_destroy(NULL);
    ASSERT(result == LLE_ERROR_NULL_POINTER, "Destroy accepted NULL");
}

/* ============================================================================
 * KEY SEQUENCE PARSING TESTS
 * ============================================================================
 */

TEST(parse_simple_key) {
    lle_key_event_t key;
    lle_result_t result;

    // Parse 'a'
    result = lle_key_sequence_parse("a", &key);
    ASSERT(result == LLE_SUCCESS, "Parse 'a' failed");
    ASSERT(key.codepoint == 'a', "Codepoint incorrect");
    ASSERT(!key.ctrl, "Ctrl set incorrectly");
    ASSERT(!key.alt, "Alt set incorrectly");
    ASSERT(!key.is_special, "Is special set incorrectly");
}

TEST(parse_ctrl_key) {
    lle_key_event_t key;
    lle_result_t result;

    // Parse 'C-a'
    result = lle_key_sequence_parse("C-a", &key);
    ASSERT(result == LLE_SUCCESS, "Parse 'C-a' failed");
    ASSERT(key.codepoint == 'A', "Ctrl-a codepoint incorrect");
    ASSERT(key.ctrl, "Ctrl not set");
    ASSERT(!key.alt, "Alt set incorrectly");
}

TEST(parse_meta_key) {
    lle_key_event_t key;
    lle_result_t result;

    // Parse 'M-f'
    result = lle_key_sequence_parse("M-f", &key);
    ASSERT(result == LLE_SUCCESS, "Parse 'M-f' failed");
    ASSERT(key.codepoint == 'f', "Meta-f codepoint incorrect");
    ASSERT(!key.ctrl, "Ctrl set incorrectly");
    ASSERT(key.alt, "Alt not set");
}

TEST(parse_ctrl_meta_key) {
    lle_key_event_t key;
    lle_result_t result;

    // Parse 'C-M-x'
    result = lle_key_sequence_parse("C-M-x", &key);
    ASSERT(result == LLE_SUCCESS, "Parse 'C-M-x' failed");
    ASSERT(key.ctrl, "Ctrl not set");
    ASSERT(key.alt, "Alt not set");
}

TEST(parse_special_keys) {
    lle_key_event_t key;
    lle_result_t result;

    // Parse RET
    result = lle_key_sequence_parse("RET", &key);
    ASSERT(result == LLE_SUCCESS, "Parse RET failed");
    ASSERT(key.is_special, "RET not marked as special");
    ASSERT(key.special_key == LLE_KEY_ENTER, "RET key code incorrect");

    // Parse TAB
    result = lle_key_sequence_parse("TAB", &key);
    ASSERT(result == LLE_SUCCESS, "Parse TAB failed");
    ASSERT(key.is_special, "TAB not marked as special");
    ASSERT(key.special_key == LLE_KEY_TAB, "TAB key code incorrect");

    // Parse UP
    result = lle_key_sequence_parse("UP", &key);
    ASSERT(result == LLE_SUCCESS, "Parse UP failed");
    ASSERT(key.is_special, "UP not marked as special");
    ASSERT(key.special_key == LLE_KEY_UP, "UP key code incorrect");
}

TEST(parse_function_keys) {
    lle_key_event_t key;
    lle_result_t result;

    // Parse F1
    result = lle_key_sequence_parse("F1", &key);
    ASSERT(result == LLE_SUCCESS, "Parse F1 failed");
    ASSERT(key.is_special, "F1 not marked as special");
    ASSERT(key.special_key == LLE_KEY_F1, "F1 key code incorrect");

    // Parse F12
    result = lle_key_sequence_parse("F12", &key);
    ASSERT(result == LLE_SUCCESS, "Parse F12 failed");
    ASSERT(key.is_special, "F12 not marked as special");
    ASSERT(key.special_key == LLE_KEY_F12, "F12 key code incorrect");
}

TEST(key_event_to_string) {
    lle_key_event_t key;
    char buffer[64];
    lle_result_t result;

    // C-a
    memset(&key, 0, sizeof(key));
    key.ctrl = true;
    key.codepoint = 'A';
    result = lle_key_event_to_string(&key, buffer, sizeof(buffer));
    ASSERT(result == LLE_SUCCESS, "Key to string failed");
    ASSERT(strcmp(buffer, "C-a") == 0, "C-a string incorrect");

    // M-f
    memset(&key, 0, sizeof(key));
    key.alt = true;
    key.codepoint = 'f';
    result = lle_key_event_to_string(&key, buffer, sizeof(buffer));
    ASSERT(result == LLE_SUCCESS, "Key to string failed");
    ASSERT(strcmp(buffer, "M-f") == 0, "M-f string incorrect");
}

/* ============================================================================
 * KEYBINDING OPERATIONS TESTS
 * ============================================================================
 */

TEST(bind_and_lookup) {
    lle_keybinding_manager_t *manager = NULL;
    lle_result_t result;

    result = lle_keybinding_manager_create(&manager, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    // Bind C-a to test_action
    result = lle_keybinding_manager_bind(manager, "C-a", test_action,
                                         "beginning-of-line");
    ASSERT(result == LLE_SUCCESS, "Bind failed");

    // Lookup
    lle_keybinding_action_t *action = NULL;
    result = lle_keybinding_manager_lookup(manager, "C-a", &action);
    ASSERT(result == LLE_SUCCESS, "Lookup failed");
    ASSERT(action != NULL && action->type == LLE_ACTION_TYPE_SIMPLE &&
               action->func.simple == test_action,
           "Action pointer incorrect");

    // Verify count
    size_t count;
    result = lle_keybinding_manager_get_count(manager, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 1, "Count not 1 after bind");

    lle_keybinding_manager_destroy(manager);
}

TEST(bind_multiple_keys) {
    lle_keybinding_manager_t *manager = NULL;
    lle_result_t result;

    result = lle_keybinding_manager_create(&manager, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    // Bind multiple keys
    result = lle_keybinding_manager_bind(manager, "C-a", test_action,
                                         "beginning-of-line");
    ASSERT(result == LLE_SUCCESS, "Bind C-a failed");

    result =
        lle_keybinding_manager_bind(manager, "C-e", test_action, "end-of-line");
    ASSERT(result == LLE_SUCCESS, "Bind C-e failed");

    result = lle_keybinding_manager_bind(manager, "M-f", test_action,
                                         "forward-word");
    ASSERT(result == LLE_SUCCESS, "Bind M-f failed");

    // Verify count
    size_t count;
    result = lle_keybinding_manager_get_count(manager, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 3, "Count not 3 after multiple binds");

    lle_keybinding_manager_destroy(manager);
}

TEST(unbind_key) {
    lle_keybinding_manager_t *manager = NULL;
    lle_result_t result;

    result = lle_keybinding_manager_create(&manager, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    // Bind and unbind
    result = lle_keybinding_manager_bind(manager, "C-a", test_action, "test");
    ASSERT(result == LLE_SUCCESS, "Bind failed");

    result = lle_keybinding_manager_unbind(manager, "C-a");
    ASSERT(result == LLE_SUCCESS, "Unbind failed");

    // Verify removed
    lle_keybinding_action_t *action = NULL;
    result = lle_keybinding_manager_lookup(manager, "C-a", &action);
    ASSERT(result == LLE_ERROR_NOT_FOUND, "Lookup should fail after unbind");

    size_t count;
    result = lle_keybinding_manager_get_count(manager, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 0, "Count not 0 after unbind");

    lle_keybinding_manager_destroy(manager);
}

TEST(lookup_nonexistent_key) {
    lle_keybinding_manager_t *manager = NULL;
    lle_result_t result;

    result = lle_keybinding_manager_create(&manager, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    // Lookup non-existent key
    lle_keybinding_action_t *action = NULL;
    result = lle_keybinding_manager_lookup(manager, "C-z", &action);
    ASSERT(result == LLE_ERROR_NOT_FOUND, "Lookup should return NOT_FOUND");

    lle_keybinding_manager_destroy(manager);
}

/* ============================================================================
 * KEY PROCESSING TESTS
 * ============================================================================
 */

TEST(process_key_executes_action) {
    lle_keybinding_manager_t *manager = NULL;
    lle_result_t result;
    lle_editor_t editor = {0};

    result = lle_keybinding_manager_create(&manager, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    // Bind action
    result = lle_keybinding_manager_bind(manager, "C-a", test_action, "test");
    ASSERT(result == LLE_SUCCESS, "Bind failed");

    // Create key event
    lle_key_event_t key;
    result = lle_key_sequence_parse("C-a", &key);
    ASSERT(result == LLE_SUCCESS, "Parse failed");

    // Process key
    g_action_called = 0;
    result = lle_keybinding_manager_process_key(manager, &editor, &key);
    ASSERT(result == LLE_SUCCESS, "Process key failed");
    ASSERT(g_action_called == 1, "Action not called");

    lle_keybinding_manager_destroy(manager);
}

TEST(process_unbound_key) {
    lle_keybinding_manager_t *manager = NULL;
    lle_result_t result;
    lle_editor_t editor = {0};

    result = lle_keybinding_manager_create(&manager, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    // Create unbound key event
    lle_key_event_t key;
    result = lle_key_sequence_parse("C-z", &key);
    ASSERT(result == LLE_SUCCESS, "Parse failed");

    // Process should fail
    g_action_called = 0;
    result = lle_keybinding_manager_process_key(manager, &editor, &key);
    ASSERT(result == LLE_ERROR_NOT_FOUND, "Process should return NOT_FOUND");
    ASSERT(g_action_called == 0, "Action should not be called");

    lle_keybinding_manager_destroy(manager);
}

/* ============================================================================
 * MODE MANAGEMENT TESTS
 * ============================================================================
 */

TEST(mode_switching) {
    lle_keybinding_manager_t *manager = NULL;
    lle_result_t result;

    result = lle_keybinding_manager_create(&manager, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    // Default should be EMACS
    lle_keymap_mode_t mode;
    result = lle_keybinding_manager_get_mode(manager, &mode);
    ASSERT(result == LLE_SUCCESS, "Get mode failed");
    ASSERT(mode == LLE_KEYMAP_EMACS, "Default mode not EMACS");

    // Switch to VI
    result = lle_keybinding_manager_set_mode(manager, LLE_KEYMAP_VI_INSERT);
    ASSERT(result == LLE_SUCCESS, "Set mode failed");

    result = lle_keybinding_manager_get_mode(manager, &mode);
    ASSERT(result == LLE_SUCCESS, "Get mode failed");
    ASSERT(mode == LLE_KEYMAP_VI_INSERT, "Mode not VI_INSERT");

    lle_keybinding_manager_destroy(manager);
}

/* ============================================================================
 * PERFORMANCE TESTS
 * ============================================================================
 */

TEST(performance_tracking) {
    lle_keybinding_manager_t *manager = NULL;
    lle_result_t result;
    lle_editor_t editor = {0};

    result = lle_keybinding_manager_create(&manager, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    // Bind action
    result = lle_keybinding_manager_bind(manager, "C-a", test_action, "test");
    ASSERT(result == LLE_SUCCESS, "Bind failed");

    // Process key multiple times
    lle_key_event_t key;
    result = lle_key_sequence_parse("C-a", &key);
    ASSERT(result == LLE_SUCCESS, "Parse failed");

    for (int i = 0; i < 100; i++) {
        result = lle_keybinding_manager_process_key(manager, &editor, &key);
        ASSERT(result == LLE_SUCCESS, "Process key failed");
    }

    // Check stats
    uint64_t avg_time, max_time;
    result = lle_keybinding_manager_get_stats(manager, &avg_time, &max_time);
    ASSERT(result == LLE_SUCCESS, "Get stats failed");

    // Verify performance requirement (<50us)
    ASSERT(avg_time < LLE_KEYBINDING_LOOKUP_MAX_US,
           "Average lookup time exceeds 50us requirement");

    // Reset stats
    result = lle_keybinding_manager_reset_stats(manager);
    ASSERT(result == LLE_SUCCESS, "Reset stats failed");

    result = lle_keybinding_manager_get_stats(manager, &avg_time, &max_time);
    ASSERT(result == LLE_SUCCESS, "Get stats failed");
    ASSERT(avg_time == 0, "Stats not reset");
    ASSERT(max_time == 0, "Stats not reset");

    lle_keybinding_manager_destroy(manager);
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Keybinding Engine Unit Tests\n");
    printf("=============================\n\n");

    printf("Lifecycle Tests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(null_pointer_checks);

    printf("\nKey Sequence Parsing Tests:\n");
    RUN_TEST(parse_simple_key);
    RUN_TEST(parse_ctrl_key);
    RUN_TEST(parse_meta_key);
    RUN_TEST(parse_ctrl_meta_key);
    RUN_TEST(parse_special_keys);
    RUN_TEST(parse_function_keys);
    RUN_TEST(key_event_to_string);

    printf("\nKeybinding Operations Tests:\n");
    RUN_TEST(bind_and_lookup);
    RUN_TEST(bind_multiple_keys);
    RUN_TEST(unbind_key);
    RUN_TEST(lookup_nonexistent_key);

    printf("\nKey Processing Tests:\n");
    RUN_TEST(process_key_executes_action);
    RUN_TEST(process_unbound_key);

    printf("\nMode Management Tests:\n");
    RUN_TEST(mode_switching);

    printf("\nPerformance Tests:\n");
    RUN_TEST(performance_tracking);

    return TEST_RESULT();
}
