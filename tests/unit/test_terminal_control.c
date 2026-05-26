/**
 * @file test_terminal_control.c
 * @brief Unit tests for terminal control layer
 *
 * Tests the terminal control layer's ANSI sequence generation, capability
 * detection, color management, cursor control, and error handling.
 *
 * The terminal_control layer is tightly integrated with base_terminal for
 * actual terminal I/O. These tests focus on functions that can be tested
 * in isolation: color utilities, error strings, sequence generation helpers,
 * and parameter validation.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display/base_terminal.h"
#include "display/terminal_control.h"
#include "test_framework.h"

/* Local 1-arg / 2-arg ASSERT_* helpers (no message) bridge to the
 * framework's variants by synthesizing a stringified-expression
 * message. Replaces the historical exit(1) failure path with
 * longjmp-based isolation per RUN_TEST. */
#undef ASSERT
#undef ASSERT_EQ
#undef ASSERT_STR_EQ
#undef ASSERT_NULL
#undef ASSERT_NOT_NULL
#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_MSG(#cond);                                              \
        }                                                                      \
    } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b), #a " == " #b)
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0, "strings equal")
#define ASSERT_NULL(p) ASSERT_TRUE((p) == NULL, #p " is NULL")
#define ASSERT_NOT_NULL(p) ASSERT_TRUE((p) != NULL, #p " is non-NULL")

/// Test framework macros

/* ============================================================
 * ERROR STRING TESTS
 * ============================================================ */

static int test_error_string_success(void) {
    const char *msg = terminal_control_error_string(TERMINAL_CONTROL_SUCCESS);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_invalid_param(void) {
    const char *msg =
        terminal_control_error_string(TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_memory_allocation(void) {
    const char *msg =
        terminal_control_error_string(TERMINAL_CONTROL_ERROR_MEMORY_ALLOCATION);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_capability_detection(void) {
    const char *msg = terminal_control_error_string(
        TERMINAL_CONTROL_ERROR_CAPABILITY_DETECTION);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_sequence_too_long(void) {
    const char *msg =
        terminal_control_error_string(TERMINAL_CONTROL_ERROR_SEQUENCE_TOO_LONG);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_unsupported_operation(void) {
    const char *msg = terminal_control_error_string(
        TERMINAL_CONTROL_ERROR_UNSUPPORTED_OPERATION);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_color_out_of_range(void) {
    const char *msg = terminal_control_error_string(
        TERMINAL_CONTROL_ERROR_COLOR_OUT_OF_RANGE);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_position_out_of_range(void) {
    const char *msg = terminal_control_error_string(
        TERMINAL_CONTROL_ERROR_POSITION_OUT_OF_RANGE);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_terminal_not_ready(void) {
    const char *msg = terminal_control_error_string(
        TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_unknown(void) {
    const char *msg =
        terminal_control_error_string((terminal_control_error_t)9999);
    ASSERT_NOT_NULL(msg);
    /// Should return some string for unknown errors
    return 1;
}

static int test_error_strings_are_different(void) {
    const char *success =
        terminal_control_error_string(TERMINAL_CONTROL_SUCCESS);
    const char *invalid =
        terminal_control_error_string(TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    const char *memory =
        terminal_control_error_string(TERMINAL_CONTROL_ERROR_MEMORY_ALLOCATION);

    ASSERT(strcmp(success, invalid) != 0);
    ASSERT(strcmp(success, memory) != 0);
    ASSERT(strcmp(invalid, memory) != 0);

    return 1;
}

/* ============================================================
 * COLOR UTILITY TESTS
 * ============================================================ */

static int test_color_default(void) {
    terminal_color_t color = terminal_control_color_default();
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_DEFAULT);
    return 1;
}

static int test_color_from_basic_black(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_BLACK);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_BLACK);
    return 1;
}

static int test_color_from_basic_red(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_RED);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_RED);
    return 1;
}

static int test_color_from_basic_green(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_GREEN);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_GREEN);
    return 1;
}

static int test_color_from_basic_yellow(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_YELLOW);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_YELLOW);
    return 1;
}

static int test_color_from_basic_blue(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_BLUE);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_BLUE);
    return 1;
}

static int test_color_from_basic_magenta(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_MAGENTA);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_MAGENTA);
    return 1;
}

static int test_color_from_basic_cyan(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_CYAN);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_CYAN);
    return 1;
}

static int test_color_from_basic_white(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_WHITE);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_WHITE);
    return 1;
}

static int test_color_from_basic_bright_black(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_BRIGHT_BLACK);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_BRIGHT_BLACK);
    return 1;
}

static int test_color_from_basic_bright_white(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_BRIGHT_WHITE);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, TERMINAL_COLOR_BRIGHT_WHITE);
    return 1;
}

static int test_color_from_basic_max_value(void) {
    /// Basic colors are 0-15
    terminal_color_t color = terminal_control_color_from_basic(15);
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_BASIC);
    ASSERT_EQ(color.value.basic, 15);
    return 1;
}

/* ============================================================
 * CREATE/DESTROY NULL TESTS
 * ============================================================ */

static int test_create_null_base_terminal(void) {
    terminal_control_t *tc = terminal_control_create(NULL);
    /* Should handle NULL gracefully - either return NULL or create with NULL
     * base */
    /// Implementation may vary
    if (tc) {
        terminal_control_destroy(tc);
    }
    return 1;
}

static int test_destroy_null_safe(void) {
    /// Should not crash
    terminal_control_destroy(NULL);
    return 1;
}

/* ============================================================
 * INIT NULL/INVALID TESTS
 * ============================================================ */

static int test_init_null_control(void) {
    terminal_control_error_t result = terminal_control_init(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

/* ============================================================
 * CLEANUP NULL/INVALID TESTS
 * ============================================================ */

static int test_cleanup_null_control(void) {
    terminal_control_error_t result = terminal_control_cleanup(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

/* ============================================================
 * CAPABILITY DETECTION NULL TESTS
 * ============================================================ */

static int test_detect_capabilities_null_control(void) {
    terminal_control_error_t result =
        terminal_control_detect_capabilities(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_get_capabilities_null_control(void) {
    terminal_capabilities_t caps = terminal_control_get_capabilities(NULL);
    /// Should return empty/zeroed capabilities
    ASSERT_EQ(caps.terminal_width, 0);
    ASSERT_EQ(caps.terminal_height, 0);
    return 1;
}

static int test_has_capability_null_control(void) {
    bool result = terminal_control_has_capability(NULL, TERMINAL_CAP_COLOR_8);
    ASSERT(!result);
    return 1;
}

static int test_update_size_null_control(void) {
    terminal_control_error_t result = terminal_control_update_size(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

/* ============================================================
 * CURSOR CONTROL NULL TESTS
 * ============================================================ */

static int test_move_cursor_null_control(void) {
    terminal_control_error_t result = terminal_control_move_cursor(NULL, 1, 1);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_move_cursor_relative_null_control(void) {
    terminal_control_error_t result =
        terminal_control_move_cursor_relative(NULL, 1, 1);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_get_cursor_position_null_control(void) {
    terminal_position_t pos = terminal_control_get_cursor_position(NULL);
    /// Should return invalid position (-1, -1)
    ASSERT_EQ(pos.row, -1);
    ASSERT_EQ(pos.column, -1);
    return 1;
}

static int test_set_cursor_visible_null_control(void) {
    terminal_control_error_t result =
        terminal_control_set_cursor_visible(NULL, true);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_save_cursor_null_control(void) {
    terminal_control_error_t result = terminal_control_save_cursor(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_restore_cursor_null_control(void) {
    terminal_control_error_t result = terminal_control_restore_cursor(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

/* ============================================================
 * SCREEN CONTROL NULL TESTS
 * ============================================================ */

static int test_clear_screen_null_control(void) {
    terminal_control_error_t result = terminal_control_clear_screen(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_clear_to_end_of_line_null_control(void) {
    terminal_control_error_t result =
        terminal_control_clear_to_end_of_line(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_clear_to_beginning_of_line_null_control(void) {
    terminal_control_error_t result =
        terminal_control_clear_to_beginning_of_line(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_clear_line_null_control(void) {
    terminal_control_error_t result = terminal_control_clear_line(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_clear_to_end_of_screen_null_control(void) {
    terminal_control_error_t result =
        terminal_control_clear_to_end_of_screen(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

/* ============================================================
 * COLOR AND STYLE NULL TESTS
 * ============================================================ */

static int test_set_foreground_color_null_control(void) {
    terminal_color_t color = terminal_control_color_default();
    terminal_control_error_t result =
        terminal_control_set_foreground_color(NULL, color);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_set_background_color_null_control(void) {
    terminal_color_t color = terminal_control_color_default();
    terminal_control_error_t result =
        terminal_control_set_background_color(NULL, color);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_set_style_null_control(void) {
    terminal_control_error_t result =
        terminal_control_set_style(NULL, TERMINAL_STYLE_BOLD);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

static int test_reset_formatting_null_control(void) {
    terminal_control_error_t result = terminal_control_reset_formatting(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

/* ============================================================
 * SEQUENCE GENERATION NULL TESTS
 * ============================================================ */

static int test_generate_cursor_sequence_null_control(void) {
    char buffer[64];
    ssize_t result = terminal_control_generate_cursor_sequence(
        NULL, 1, 1, buffer, sizeof(buffer));
    ASSERT(result < 0);
    return 1;
}

static int test_generate_cursor_sequence_null_buffer(void) {
    /* Can't test without a valid control, but test that NULL buffer is handled
     */
    ssize_t result =
        terminal_control_generate_cursor_sequence(NULL, 1, 1, NULL, 64);
    ASSERT(result < 0);
    return 1;
}

static int test_generate_color_sequence_null_control(void) {
    char buffer[64];
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_RED);
    ssize_t result = terminal_control_generate_color_sequence(
        NULL, color, false, buffer, sizeof(buffer));
    ASSERT(result < 0);
    return 1;
}

static int test_generate_style_sequence_null_control(void) {
    char buffer[64];
    ssize_t result = terminal_control_generate_style_sequence(
        NULL, TERMINAL_STYLE_BOLD, buffer, sizeof(buffer));
    ASSERT(result < 0);
    return 1;
}

/* ============================================================
 * COLOR VALIDATION NULL TESTS
 * ============================================================ */

static int test_validate_color_null_control(void) {
    terminal_color_t color =
        terminal_control_color_from_basic(TERMINAL_COLOR_RED);
    bool result = terminal_control_validate_color(NULL, color);
    ASSERT(!result);
    return 1;
}

static int test_color_from_rgb_null_control(void) {
    terminal_color_t color = terminal_control_color_from_rgb(NULL, 128, 0, 128);
    /// Should still return a color, but may default to basic type
    /// The function should handle NULL gracefully
    (void)color;
    return 1;
}

/* ============================================================
 * PERFORMANCE/METRICS NULL TESTS
 * ============================================================ */

static int test_get_performance_metrics_null_control(void) {
    uint64_t seqs;
    double hit_rate;
    uint64_t avg_time;
    terminal_control_error_t result = terminal_control_get_performance_metrics(
        NULL, &seqs, &hit_rate, &avg_time);
    ASSERT(result != TERMINAL_CONTROL_SUCCESS);
    return 1;
}

static int test_clear_metrics_null_control(void) {
    terminal_control_error_t result = terminal_control_clear_metrics(NULL);
    ASSERT(result != TERMINAL_CONTROL_SUCCESS);
    return 1;
}

static int test_set_caching_enabled_null_control(void) {
    terminal_control_error_t result =
        terminal_control_set_caching_enabled(NULL, true);
    ASSERT(result != TERMINAL_CONTROL_SUCCESS);
    return 1;
}

/* ============================================================
 * LAST ERROR NULL TEST
 * ============================================================ */

static int test_get_last_error_null_control(void) {
    terminal_control_error_t result = terminal_control_get_last_error(NULL);
    ASSERT_EQ(result, TERMINAL_CONTROL_ERROR_INVALID_PARAM);
    return 1;
}

/* ============================================================
 * VERSION TESTS
 * ============================================================ */

static int test_get_version_not_null(void) {
    int major, minor, patch;
    terminal_control_get_version(&major, &minor, &patch);

    ASSERT(major >= 0);
    ASSERT(minor >= 0);
    ASSERT(patch >= 0);
    return 1;
}

static int test_get_version_null_params(void) {
    /// Should not crash with NULL params
    terminal_control_get_version(NULL, NULL, NULL);
    return 1;
}

static int test_get_version_partial_null_params(void) {
    int major;
    terminal_control_get_version(&major, NULL, NULL);
    ASSERT(major >= 0);

    int minor;
    terminal_control_get_version(NULL, &minor, NULL);
    ASSERT(minor >= 0);

    int patch;
    terminal_control_get_version(NULL, NULL, &patch);
    ASSERT(patch >= 0);

    return 1;
}

/* ============================================================
 * CAPABILITY FLAG TESTS
 * ============================================================ */

static int test_capability_flags_distinct(void) {
    /// Verify capability flags are distinct bit flags
    ASSERT(TERMINAL_CAP_NONE == 0);
    ASSERT(TERMINAL_CAP_COLOR_8 != TERMINAL_CAP_COLOR_16);
    ASSERT(TERMINAL_CAP_COLOR_16 != TERMINAL_CAP_COLOR_256);
    ASSERT(TERMINAL_CAP_COLOR_256 != TERMINAL_CAP_COLOR_TRUECOLOR);
    ASSERT(TERMINAL_CAP_CURSOR_POSITIONING != TERMINAL_CAP_CURSOR_VISIBILITY);
    ASSERT(TERMINAL_CAP_UNICODE != TERMINAL_CAP_BOLD);
    ASSERT(TERMINAL_CAP_ITALIC != TERMINAL_CAP_UNDERLINE);
    return 1;
}

static int test_capability_flags_are_powers_of_two(void) {
    /// Each capability should be a power of 2 for bitwise operations
    terminal_capability_flags_t caps[] = {TERMINAL_CAP_COLOR_8,
                                          TERMINAL_CAP_COLOR_16,
                                          TERMINAL_CAP_COLOR_256,
                                          TERMINAL_CAP_COLOR_TRUECOLOR,
                                          TERMINAL_CAP_CURSOR_POSITIONING,
                                          TERMINAL_CAP_CURSOR_VISIBILITY,
                                          TERMINAL_CAP_UNICODE,
                                          TERMINAL_CAP_BOLD,
                                          TERMINAL_CAP_ITALIC,
                                          TERMINAL_CAP_UNDERLINE,
                                          TERMINAL_CAP_REVERSE,
                                          TERMINAL_CAP_STRIKETHROUGH,
                                          TERMINAL_CAP_ALTERNATE_SCREEN,
                                          TERMINAL_CAP_MOUSE_REPORTING,
                                          TERMINAL_CAP_BRACKETED_PASTE,
                                          TERMINAL_CAP_WINDOW_TITLE};

    for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); i++) {
        /// A power of 2 has exactly one bit set
        terminal_capability_flags_t cap = caps[i];
        ASSERT((cap & (cap - 1)) == 0);
    }
    return 1;
}

/* ============================================================
 * STYLE FLAG TESTS
 * ============================================================ */

static int test_style_flags_distinct(void) {
    ASSERT(TERMINAL_STYLE_NONE == 0);
    ASSERT(TERMINAL_STYLE_BOLD != TERMINAL_STYLE_ITALIC);
    ASSERT(TERMINAL_STYLE_ITALIC != TERMINAL_STYLE_UNDERLINE);
    ASSERT(TERMINAL_STYLE_UNDERLINE != TERMINAL_STYLE_REVERSE);
    ASSERT(TERMINAL_STYLE_REVERSE != TERMINAL_STYLE_STRIKETHROUGH);
    return 1;
}

static int test_style_flags_combinable(void) {
    /// Style flags should be combinable
    terminal_style_flags_t combined =
        TERMINAL_STYLE_BOLD | TERMINAL_STYLE_UNDERLINE;

    ASSERT(combined & TERMINAL_STYLE_BOLD);
    ASSERT(combined & TERMINAL_STYLE_UNDERLINE);
    ASSERT(!(combined & TERMINAL_STYLE_ITALIC));
    return 1;
}

/* ============================================================
 * COLOR TYPE TESTS
 * ============================================================ */

static int test_color_type_default_is_zero(void) {
    terminal_color_t color = {0};
    ASSERT_EQ(color.type, TERMINAL_COLOR_TYPE_DEFAULT);
    return 1;
}

static int test_color_types_distinct(void) {
    ASSERT(TERMINAL_COLOR_TYPE_DEFAULT != TERMINAL_COLOR_TYPE_BASIC);
    ASSERT(TERMINAL_COLOR_TYPE_BASIC != TERMINAL_COLOR_TYPE_256);
    ASSERT(TERMINAL_COLOR_TYPE_256 != TERMINAL_COLOR_TYPE_RGB);
    return 1;
}

/* ============================================================
 * COLOR CONSTANT TESTS
 * ============================================================ */

static int test_basic_color_constants(void) {
    ASSERT_EQ(TERMINAL_COLOR_BLACK, 0);
    ASSERT_EQ(TERMINAL_COLOR_RED, 1);
    ASSERT_EQ(TERMINAL_COLOR_GREEN, 2);
    ASSERT_EQ(TERMINAL_COLOR_YELLOW, 3);
    ASSERT_EQ(TERMINAL_COLOR_BLUE, 4);
    ASSERT_EQ(TERMINAL_COLOR_MAGENTA, 5);
    ASSERT_EQ(TERMINAL_COLOR_CYAN, 6);
    ASSERT_EQ(TERMINAL_COLOR_WHITE, 7);
    return 1;
}

static int test_bright_color_constants(void) {
    ASSERT_EQ(TERMINAL_COLOR_BRIGHT_BLACK, 8);
    ASSERT_EQ(TERMINAL_COLOR_BRIGHT_RED, 9);
    ASSERT_EQ(TERMINAL_COLOR_BRIGHT_GREEN, 10);
    ASSERT_EQ(TERMINAL_COLOR_BRIGHT_YELLOW, 11);
    ASSERT_EQ(TERMINAL_COLOR_BRIGHT_BLUE, 12);
    ASSERT_EQ(TERMINAL_COLOR_BRIGHT_MAGENTA, 13);
    ASSERT_EQ(TERMINAL_COLOR_BRIGHT_CYAN, 14);
    ASSERT_EQ(TERMINAL_COLOR_BRIGHT_WHITE, 15);
    return 1;
}

/* ============================================================
 * CONSTANT DEFINITION TESTS
 * ============================================================ */

static int test_version_constants_positive(void) {
    ASSERT(TERMINAL_CONTROL_VERSION_MAJOR >= 0);
    ASSERT(TERMINAL_CONTROL_VERSION_MINOR >= 0);
    ASSERT(TERMINAL_CONTROL_VERSION_PATCH >= 0);
    return 1;
}

static int test_max_sequence_length_reasonable(void) {
    ASSERT(TERMINAL_CONTROL_MAX_SEQUENCE_LENGTH > 0);
    ASSERT(TERMINAL_CONTROL_MAX_SEQUENCE_LENGTH >=
           32); /// At least 32 for safety
    return 1;
}

static int test_max_terminal_name_length_reasonable(void) {
    ASSERT(TERMINAL_CONTROL_MAX_TERMINAL_NAME_LENGTH > 0);
    ASSERT(TERMINAL_CONTROL_MAX_TERMINAL_NAME_LENGTH >= 16); /// At least 16
    return 1;
}

static int test_cache_size_reasonable(void) {
    ASSERT(TERMINAL_CONTROL_CAPABILITY_CACHE_SIZE > 0);
    return 1;
}

/* ============================================================
 * ERROR CODE TESTS
 * ============================================================ */

static int test_error_codes_distinct(void) {
    ASSERT_EQ((int)TERMINAL_CONTROL_SUCCESS, 0);
    ASSERT(TERMINAL_CONTROL_ERROR_INVALID_PARAM != TERMINAL_CONTROL_SUCCESS);
    ASSERT(TERMINAL_CONTROL_ERROR_MEMORY_ALLOCATION !=
           TERMINAL_CONTROL_SUCCESS);
    ASSERT(TERMINAL_CONTROL_ERROR_CAPABILITY_DETECTION !=
           TERMINAL_CONTROL_SUCCESS);
    ASSERT(TERMINAL_CONTROL_ERROR_SEQUENCE_TOO_LONG !=
           TERMINAL_CONTROL_SUCCESS);
    ASSERT(TERMINAL_CONTROL_ERROR_UNSUPPORTED_OPERATION !=
           TERMINAL_CONTROL_SUCCESS);
    ASSERT(TERMINAL_CONTROL_ERROR_COLOR_OUT_OF_RANGE !=
           TERMINAL_CONTROL_SUCCESS);
    ASSERT(TERMINAL_CONTROL_ERROR_POSITION_OUT_OF_RANGE !=
           TERMINAL_CONTROL_SUCCESS);
    ASSERT(TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY !=
           TERMINAL_CONTROL_SUCCESS);
    return 1;
}

/* ============================================================
 * STRUCTURE SIZE TESTS
 * ============================================================ */

static int test_terminal_color_struct_size(void) {
    /// Verify struct isn't unexpectedly large
    ASSERT(sizeof(terminal_color_t) <= 16);
    return 1;
}

static int test_terminal_position_struct_size(void) {
    /// Position struct should be compact
    ASSERT(sizeof(terminal_position_t) <= 16);
    return 1;
}

static int test_terminal_capabilities_has_required_fields(void) {
    terminal_capabilities_t caps = {0};

    /// Verify fields exist and are accessible
    caps.terminal_width = 80;
    caps.terminal_height = 24;
    caps.flags = TERMINAL_CAP_COLOR_8;
    caps.max_colors = 8;
    caps.cursor_positioning_support = true;
    caps.unicode_support = true;
    caps.mouse_support = false;
    caps.sequence_caching_enabled = true;
    caps.cache_hit_count = 0;
    caps.cache_miss_count = 0;

    ASSERT_EQ(caps.terminal_width, 80);
    ASSERT_EQ(caps.terminal_height, 24);
    ASSERT(caps.cursor_positioning_support);

    return 1;
}

/* ============================================================
 * MAIN TEST RUNNER
 * ============================================================ */

int main(void) {
    printf("Running terminal control tests...\n\n");

    printf("=== Error String Tests ===\n");
    RUN_TEST(error_string_success);
    RUN_TEST(error_string_invalid_param);
    RUN_TEST(error_string_memory_allocation);
    RUN_TEST(error_string_capability_detection);
    RUN_TEST(error_string_sequence_too_long);
    RUN_TEST(error_string_unsupported_operation);
    RUN_TEST(error_string_color_out_of_range);
    RUN_TEST(error_string_position_out_of_range);
    RUN_TEST(error_string_terminal_not_ready);
    RUN_TEST(error_string_unknown);
    RUN_TEST(error_strings_are_different);

    printf("\n=== Color Utility Tests ===\n");
    RUN_TEST(color_default);
    RUN_TEST(color_from_basic_black);
    RUN_TEST(color_from_basic_red);
    RUN_TEST(color_from_basic_green);
    RUN_TEST(color_from_basic_yellow);
    RUN_TEST(color_from_basic_blue);
    RUN_TEST(color_from_basic_magenta);
    RUN_TEST(color_from_basic_cyan);
    RUN_TEST(color_from_basic_white);
    RUN_TEST(color_from_basic_bright_black);
    RUN_TEST(color_from_basic_bright_white);
    RUN_TEST(color_from_basic_max_value);

    printf("\n=== Create/Destroy Null Tests ===\n");
    RUN_TEST(create_null_base_terminal);
    RUN_TEST(destroy_null_safe);

    printf("\n=== Init Null/Invalid Tests ===\n");
    RUN_TEST(init_null_control);

    printf("\n=== Cleanup Null/Invalid Tests ===\n");
    RUN_TEST(cleanup_null_control);

    printf("\n=== Capability Detection Null Tests ===\n");
    RUN_TEST(detect_capabilities_null_control);
    RUN_TEST(get_capabilities_null_control);
    RUN_TEST(has_capability_null_control);
    RUN_TEST(update_size_null_control);

    printf("\n=== Cursor Control Null Tests ===\n");
    RUN_TEST(move_cursor_null_control);
    RUN_TEST(move_cursor_relative_null_control);
    RUN_TEST(get_cursor_position_null_control);
    RUN_TEST(set_cursor_visible_null_control);
    RUN_TEST(save_cursor_null_control);
    RUN_TEST(restore_cursor_null_control);

    printf("\n=== Screen Control Null Tests ===\n");
    RUN_TEST(clear_screen_null_control);
    RUN_TEST(clear_to_end_of_line_null_control);
    RUN_TEST(clear_to_beginning_of_line_null_control);
    RUN_TEST(clear_line_null_control);
    RUN_TEST(clear_to_end_of_screen_null_control);

    printf("\n=== Color and Style Null Tests ===\n");
    RUN_TEST(set_foreground_color_null_control);
    RUN_TEST(set_background_color_null_control);
    RUN_TEST(set_style_null_control);
    RUN_TEST(reset_formatting_null_control);

    printf("\n=== Sequence Generation Null Tests ===\n");
    RUN_TEST(generate_cursor_sequence_null_control);
    RUN_TEST(generate_cursor_sequence_null_buffer);
    RUN_TEST(generate_color_sequence_null_control);
    RUN_TEST(generate_style_sequence_null_control);

    printf("\n=== Color Validation Null Tests ===\n");
    RUN_TEST(validate_color_null_control);
    RUN_TEST(color_from_rgb_null_control);

    printf("\n=== Performance/Metrics Null Tests ===\n");
    RUN_TEST(get_performance_metrics_null_control);
    RUN_TEST(clear_metrics_null_control);
    RUN_TEST(set_caching_enabled_null_control);

    printf("\n=== Last Error Null Test ===\n");
    RUN_TEST(get_last_error_null_control);

    printf("\n=== Version Tests ===\n");
    RUN_TEST(get_version_not_null);
    RUN_TEST(get_version_null_params);
    RUN_TEST(get_version_partial_null_params);

    printf("\n=== Capability Flag Tests ===\n");
    RUN_TEST(capability_flags_distinct);
    RUN_TEST(capability_flags_are_powers_of_two);

    printf("\n=== Style Flag Tests ===\n");
    RUN_TEST(style_flags_distinct);
    RUN_TEST(style_flags_combinable);

    printf("\n=== Color Type Tests ===\n");
    RUN_TEST(color_type_default_is_zero);
    RUN_TEST(color_types_distinct);

    printf("\n=== Color Constant Tests ===\n");
    RUN_TEST(basic_color_constants);
    RUN_TEST(bright_color_constants);

    printf("\n=== Constant Definition Tests ===\n");
    RUN_TEST(version_constants_positive);
    RUN_TEST(max_sequence_length_reasonable);
    RUN_TEST(max_terminal_name_length_reasonable);
    RUN_TEST(cache_size_reasonable);

    printf("\n=== Error Code Tests ===\n");
    RUN_TEST(error_codes_distinct);

    printf("\n=== Structure Size Tests ===\n");
    RUN_TEST(terminal_color_struct_size);
    RUN_TEST(terminal_position_struct_size);
    RUN_TEST(terminal_capabilities_has_required_fields);

    printf("\n========================================\n");
    printf("========================================\n");

    return TEST_RESULT();
}
