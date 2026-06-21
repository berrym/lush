/**
 * @file multiline_manager_test.c
 * @brief Functional tests for LLE multiline manager
 *
 * Tests the multiline manager's ability to wrap input_continuation.c
 * and provide LLE-specific multiline state tracking.
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
        printf("[ PASS ] %s\n", #name);                                        \
    }                                                                          \
    static void name(void)

#define ASSERT_SUCCESS(result, msg)                                            \
    do {                                                                       \
        if ((result) != LLE_SUCCESS) {                                         \
            printf("[ FAIL ] %s: %s (code %d)\n", msg, #result, result);       \
            assert(0);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("[ FAIL ] %s: %s\n", msg, #cond);                           \
            assert(0);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(cond, msg)                                                \
    do {                                                                       \
        if (cond) {                                                            \
            printf("[ FAIL ] %s: %s should be false\n", msg, #cond);           \
            assert(0);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_STR_EQ(s1, s2, msg)                                             \
    do {                                                                       \
        if (!s1 || !s2 || strcmp(s1, s2) != 0) {                               \
            printf("[ FAIL ] %s: expected '%s', got '%s'\n", msg, s2, s1);     \
            assert(0);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_NULL(ptr, msg)                                                  \
    do {                                                                       \
        if (ptr != NULL) {                                                     \
            printf("[ FAIL ] %s: expected NULL, got %p\n", msg, ptr);          \
            assert(0);                                                         \
        }                                                                      \
    } while (0)

#define ASSERT_NOT_NULL(ptr, msg)                                              \
    do {                                                                       \
        if (ptr == NULL) {                                                     \
            printf("[ FAIL ] %s: expected non-NULL\n", msg);                   \
            assert(0);                                                         \
        }                                                                      \
    } while (0)

/* ============================================================================
 * MULTILINE CONTEXT TESTS
 * ============================================================================
 */

TEST(test_multiline_context_lifecycle) {
    lle_multiline_context_t *ctx = NULL;
    lle_result_t result;

    /// Test init
    result = lle_multiline_context_init(&ctx, global_memory_pool);
    ASSERT_SUCCESS(result, "Init context");
    ASSERT_NOT_NULL(ctx, "Context created");
    ASSERT_NOT_NULL(ctx->core_state, "Core state allocated");

    /// Verify initial state
    ASSERT_TRUE(lle_multiline_is_complete(ctx), "Initially complete");
    ASSERT_FALSE(lle_multiline_needs_continuation(ctx), "No continuation");
    ASSERT_NULL(lle_multiline_get_construct(ctx), "No construct");

    /// Test destroy
    result = lle_multiline_context_destroy(ctx);
    ASSERT_SUCCESS(result, "Destroy context");
}

TEST(test_multiline_context_reset) {

    lle_multiline_context_t *ctx = NULL;
    lle_result_t result;

    result = lle_multiline_context_init(&ctx, global_memory_pool);
    ASSERT_SUCCESS(result, "Init context");

    /// Analyze a line that starts a quote
    result = lle_multiline_analyze_line(ctx, "echo '", 6);
    ASSERT_SUCCESS(result, "Analyze line");
    ASSERT_FALSE(lle_multiline_is_complete(ctx), "Incomplete after quote");

    /// Reset should clear state
    result = lle_multiline_context_reset(ctx);
    ASSERT_SUCCESS(result, "Reset context");
    ASSERT_TRUE(lle_multiline_is_complete(ctx), "Complete after reset");

    lle_multiline_context_destroy(ctx);
}

TEST(test_multiline_single_quote) {

    lle_multiline_context_t *ctx = NULL;
    lle_result_t result;

    result = lle_multiline_context_init(&ctx, global_memory_pool);
    ASSERT_SUCCESS(result, "Init context");

    /// Start single quote
    result = lle_multiline_analyze_line(ctx, "echo 'hello", 11);
    ASSERT_SUCCESS(result, "Analyze line");
    ASSERT_FALSE(lle_multiline_is_complete(ctx), "Incomplete");
    ASSERT_TRUE(lle_multiline_needs_continuation(ctx), "Needs continuation");
    ASSERT_STR_EQ(lle_multiline_get_construct(ctx), "single quote",
                  "Construct name");

    /// Complete quote
    lle_multiline_context_reset(ctx);
    result = lle_multiline_analyze_line(ctx, "echo 'hello'", 12);
    ASSERT_SUCCESS(result, "Analyze complete line");
    ASSERT_TRUE(lle_multiline_is_complete(ctx), "Complete");
    ASSERT_FALSE(lle_multiline_needs_continuation(ctx), "No continuation");

    lle_multiline_context_destroy(ctx);
}

TEST(test_multiline_double_quote) {

    lle_multiline_context_t *ctx = NULL;
    lle_result_t result;

    result = lle_multiline_context_init(&ctx, global_memory_pool);
    ASSERT_SUCCESS(result, "Init context");

    /// Start double quote
    result = lle_multiline_analyze_line(ctx, "echo \"hello", 11);
    ASSERT_SUCCESS(result, "Analyze line");
    ASSERT_FALSE(lle_multiline_is_complete(ctx), "Incomplete");
    ASSERT_STR_EQ(lle_multiline_get_construct(ctx), "double quote",
                  "Construct name");

    lle_multiline_context_destroy(ctx);
}

TEST(test_multiline_if_statement) {

    lle_multiline_context_t *ctx = NULL;
    lle_result_t result;

    result = lle_multiline_context_init(&ctx, global_memory_pool);
    ASSERT_SUCCESS(result, "Init context");

    /// Start if statement
    result = lle_multiline_analyze_line(ctx, "if true; then", 13);
    ASSERT_SUCCESS(result, "Analyze if line");
    ASSERT_FALSE(lle_multiline_is_complete(ctx), "Incomplete");
    ASSERT_STR_EQ(lle_multiline_get_construct(ctx), "if statement",
                  "Construct name");

    /// Add body
    result = lle_multiline_analyze_line(ctx, "    echo hello", 14);
    ASSERT_SUCCESS(result, "Analyze body");
    ASSERT_FALSE(lle_multiline_is_complete(ctx), "Still incomplete");

    /// Complete with fi
    result = lle_multiline_analyze_line(ctx, "fi", 2);
    ASSERT_SUCCESS(result, "Analyze fi");
    ASSERT_TRUE(lle_multiline_is_complete(ctx), "Complete after fi");

    lle_multiline_context_destroy(ctx);
}

TEST(test_multiline_backslash_continuation) {

    lle_multiline_context_t *ctx = NULL;
    lle_result_t result;

    result = lle_multiline_context_init(&ctx, global_memory_pool);
    ASSERT_SUCCESS(result, "Init context");

    /// Line with backslash continuation
    result = lle_multiline_analyze_line(ctx, "echo hello \\", 12);
    ASSERT_SUCCESS(result, "Analyze line with backslash");
    ASSERT_FALSE(lle_multiline_is_complete(ctx), "Incomplete");
    ASSERT_TRUE(lle_multiline_needs_continuation(ctx), "Needs continuation");

    lle_multiline_context_destroy(ctx);
}

TEST(test_multiline_prompt) {

    lle_multiline_context_t *ctx = NULL;
    lle_result_t result;
    const char *prompt;

    result = lle_multiline_context_init(&ctx, global_memory_pool);
    ASSERT_SUCCESS(result, "Init context");

    /// A complete line uses the default continuation prompt.
    prompt = lle_multiline_get_prompt(ctx);
    ASSERT_STR_EQ(prompt, "> ", "complete line uses the default prompt");

    /// An unterminated single quote switches to the quote-context prompt.
    result = lle_multiline_analyze_line(ctx, "echo '", 6);
    ASSERT_SUCCESS(result, "Analyze quote");
    prompt = lle_multiline_get_prompt(ctx);
    ASSERT_STR_EQ(prompt, "quote> ", "open quote uses the quote prompt");

    lle_multiline_context_destroy(ctx);
}

/* ============================================================================
 * MULTILINE MANAGER TESTS
 * ============================================================================
 */

TEST(test_multiline_manager_lifecycle) {

    lle_multiline_manager_t *manager = NULL;
    lle_result_t result;

    /// Test init
    result = lle_multiline_manager_init(&manager, global_memory_pool);
    ASSERT_SUCCESS(result, "Init manager");
    ASSERT_NOT_NULL(manager, "Manager created");

    /// Verify initial stats
    ASSERT_TRUE(manager->analysis_count == 0, "Zero analyses");
    ASSERT_TRUE(manager->line_updates == 0, "Zero line updates");

    /// Test destroy
    result = lle_multiline_manager_destroy(manager);
    ASSERT_SUCCESS(result, "Destroy manager");
}

TEST(test_multiline_buffer_analysis_simple) {

    lle_multiline_manager_t *manager = NULL;
    lle_buffer_t *buffer = NULL;
    lle_result_t result;

    result = lle_multiline_manager_init(&manager, global_memory_pool);
    ASSERT_SUCCESS(result, "Init manager");

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Create buffer");

    /// Insert a complete line
    result = lle_buffer_insert_text(buffer, 0, "echo hello", 10);
    ASSERT_SUCCESS(result, "Insert text");

    /// Analyze buffer
    result = lle_multiline_manager_analyze_buffer(manager, buffer);
    ASSERT_SUCCESS(result, "Analyze buffer");

    /// Should be complete
    ASSERT_FALSE(buffer->multiline_active, "Not multiline active");
    ASSERT_TRUE(manager->analysis_count == 1, "One analysis");

    lle_buffer_destroy(buffer);
    lle_multiline_manager_destroy(manager);
}

TEST(test_multiline_buffer_analysis_incomplete_quote) {

    lle_multiline_manager_t *manager = NULL;
    lle_buffer_t *buffer = NULL;
    lle_result_t result;

    result = lle_multiline_manager_init(&manager, global_memory_pool);
    ASSERT_SUCCESS(result, "Init manager");

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Create buffer");

    /// Insert incomplete quote
    result = lle_buffer_insert_text(buffer, 0, "echo '", 6);
    ASSERT_SUCCESS(result, "Insert text");

    /// Analyze buffer
    result = lle_multiline_manager_analyze_buffer(manager, buffer);
    ASSERT_SUCCESS(result, "Analyze buffer");

    /// Should be incomplete
    ASSERT_TRUE(buffer->multiline_active, "Multiline active");

    lle_buffer_destroy(buffer);
    lle_multiline_manager_destroy(manager);
}

TEST(test_multiline_buffer_analysis_multiline_if) {

    lle_multiline_manager_t *manager = NULL;
    lle_buffer_t *buffer = NULL;
    lle_result_t result;

    result = lle_multiline_manager_init(&manager, global_memory_pool);
    ASSERT_SUCCESS(result, "Init manager");

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    ASSERT_SUCCESS(result, "Create buffer");

    /// Insert incomplete if statement (without fi)
    result =
        lle_buffer_insert_text(buffer, 0, "if true; then\necho hello\n", 25);
    ASSERT_SUCCESS(result, "Insert multiline if");

    /// Analyze buffer
    result = lle_multiline_manager_analyze_buffer(manager, buffer);
    ASSERT_SUCCESS(result, "Analyze buffer");

    /// Should be incomplete
    ASSERT_TRUE(buffer->multiline_active, "Multiline active for incomplete if");

    lle_buffer_destroy(buffer);
    lle_multiline_manager_destroy(manager);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("=================================================\n");
    printf("LLE Multiline Manager Functional Tests\n");
    printf("=================================================\n\n");

    printf("--- Multiline Context Tests ---\n");
    test_multiline_context_lifecycle_wrapper();
    test_multiline_context_reset_wrapper();
    test_multiline_single_quote_wrapper();
    test_multiline_double_quote_wrapper();
    test_multiline_if_statement_wrapper();
    test_multiline_backslash_continuation_wrapper();
    test_multiline_prompt_wrapper();

    printf("\n--- Multiline Manager Tests ---\n");
    test_multiline_manager_lifecycle_wrapper();
    test_multiline_buffer_analysis_simple_wrapper();
    test_multiline_buffer_analysis_incomplete_quote_wrapper();
    test_multiline_buffer_analysis_multiline_if_wrapper();

    printf("\n=================================================\n");
    printf("All tests passed!\n");
    printf("=================================================\n");

    return 0;
}
