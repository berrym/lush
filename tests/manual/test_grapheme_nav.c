/**
 * Test grapheme navigation with complex emoji
 * Compile: gcc -o test_grapheme_nav test_grapheme_nav.c -I../../include
 * -L../../build/src/lle -llle -L../../build/src/pool -llush_pool
 */

#include "lle/buffer_management.h"
#include <stdio.h>
#include <string.h>

extern lush_memory_pool_t *global_memory_pool;

int main(void) {
    printf("=== Grapheme Navigation Test ===\n\n");

    /// Initialize global memory pool
    if (lle_pool_init(1024 * 1024) != LLE_SUCCESS) {
        fprintf(stderr, "Failed to initialize memory pool\n");
        return 1;
    }
    global_memory_pool = lle_pool_get_global();

    /// Test cases
    struct {
        const char *name;
        const char *text;
        size_t expected_graphemes;
    } tests[] = {
        {"Family emoji", "👨‍👩‍👧‍👦", 1},
        {  "Flag emoji",                        "🇺🇸", 1},
        {   "Skin tone",                      "👋🏽", 1},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        printf("Test: %s\n", tests[i].name);
        printf("Text: %s\n", tests[i].text);

        /// Create buffer
        lle_buffer_t *buffer = NULL;
        lle_result_t result = lle_buffer_create(&buffer, global_memory_pool, 0);
        if (result != LLE_SUCCESS) {
            printf("  ERROR: Failed to create buffer\n\n");
            continue;
        }

        /// Insert text at position 0
        size_t text_len = strlen(tests[i].text);
        result = lle_buffer_insert_text(buffer, 0, tests[i].text, text_len);
        if (result != LLE_SUCCESS) {
            printf("  ERROR: Failed to insert text\n\n");
            lle_buffer_destroy(buffer);
            continue;
        }

        printf("  Buffer length: %zu bytes\n", buffer->length);
        printf("  Buffer codepoint_count: %zu\n", buffer->codepoint_count);
        printf("  Buffer grapheme_count: %zu (expected %zu)\n",
               buffer->grapheme_count, tests[i].expected_graphemes);
        printf("  Cursor byte_offset: %zu\n", buffer->cursor.byte_offset);
        printf("  Cursor codepoint_index: %zu\n",
               buffer->cursor.codepoint_index);
        printf("  Cursor grapheme_index: %zu\n", buffer->cursor.grapheme_index);

        /// Create cursor manager
        lle_cursor_manager_t *cursor_mgr = NULL;
        result = lle_cursor_manager_init(&cursor_mgr, buffer);
        if (result != LLE_SUCCESS) {
            printf("  ERROR: Failed to create cursor manager\n\n");
            lle_buffer_destroy(buffer);
            continue;
        }

        /// Try to sync cursor
        result = lle_cursor_manager_move_to_byte_offset(
            cursor_mgr, buffer->cursor.byte_offset);
        if (result != LLE_SUCCESS) {
            printf("  ERROR: Failed to sync cursor\n\n");
        } else {
            printf("  After sync:\n");
            printf("    Cursor byte_offset: %zu\n", buffer->cursor.byte_offset);
            printf("    Cursor codepoint_index: %zu\n",
                   buffer->cursor.codepoint_index);
            printf("    Cursor grapheme_index: %zu\n",
                   buffer->cursor.grapheme_index);
        }

        /// Try moving back by 1 grapheme
        printf("  Moving back by 1 grapheme...\n");
        result = lle_cursor_manager_move_by_graphemes(cursor_mgr, -1);
        if (result != LLE_SUCCESS) {
            printf("    ERROR: Failed to move back\n");
        } else {
            printf("    After move back:\n");
            printf("      Cursor byte_offset: %zu\n",
                   buffer->cursor.byte_offset);
            printf("      Cursor grapheme_index: %zu\n",
                   buffer->cursor.grapheme_index);
        }

        printf("  %s\n\n",
               (buffer->grapheme_count == tests[i].expected_graphemes)
                   ? "PASS"
                   : "FAIL");

        lle_cursor_manager_destroy(cursor_mgr);
        lle_buffer_destroy(buffer);
    }

    lle_pool_cleanup();
    return 0;
}
