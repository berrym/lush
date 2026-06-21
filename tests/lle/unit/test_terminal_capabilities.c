/**
 * @file test_terminal_capabilities.c
 * @brief Unit tests for terminal capabilities
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/*
 * test_terminal_capabilities.c - Unit tests for terminal capability detection
 *
 * Tests Spec 02: Terminal Capability Detection
 *
 * Test Categories:
 * 1. Terminal type detection
 * 2. Color capability detection
 * 3. Advanced feature detection
 * 4. Geometry detection
 * 5. Performance characteristics
 */

#include "lle/terminal_abstraction.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// Detect capabilities as if running under the given TERM/COLORTERM, then
/// restore the real environment before returning. Because detection reads
/// these variables, this makes the type- and colorterm-derived capabilities
/// deterministic regardless of the terminal the test actually runs in.
/// Restoring before return means a later assertion failure cannot leave the
/// process environment polluted for subsequent tests.
static lle_terminal_capabilities_t *detect_as(const char *term,
                                              const char *colorterm) {
    char *saved_term = getenv("TERM");
    saved_term = saved_term ? strdup(saved_term) : NULL;
    char *saved_ct = getenv("COLORTERM");
    saved_ct = saved_ct ? strdup(saved_ct) : NULL;

    if (term) {
        setenv("TERM", term, 1);
    } else {
        unsetenv("TERM");
    }
    if (colorterm) {
        setenv("COLORTERM", colorterm, 1);
    } else {
        unsetenv("COLORTERM");
    }

    lle_terminal_capabilities_t *caps = NULL;
    lle_capabilities_detect_environment(&caps, NULL);

    if (saved_term) {
        setenv("TERM", saved_term, 1);
        free(saved_term);
    } else {
        unsetenv("TERM");
    }
    if (saved_ct) {
        setenv("COLORTERM", saved_ct, 1);
        free(saved_ct);
    } else {
        unsetenv("COLORTERM");
    }
    return caps;
}

/* ============================================================================
 * TERMINAL TYPE DETECTION TESTS
 * ============================================================================
 */

TEST(capability_detection_basic) {
    /// A recognized TERM classifies to the matching terminal type; an
    /// unrecognized TERM does not classify as that type. This pins the
    /// detection to a deterministic input rather than the >= UNKNOWN range
    /// check, which an unsigned enum always satisfies.
    lle_terminal_capabilities_t *xterm = detect_as("xterm-256color", NULL);
    ASSERT(xterm != NULL);
    int xterm_type = xterm->terminal_type_enum;
    int has_strings =
        xterm->terminal_type != NULL && xterm->terminal_program != NULL;
    lle_capabilities_destroy(xterm);

    lle_terminal_capabilities_t *dumb = detect_as("dumb", NULL);
    ASSERT(dumb != NULL);
    int dumb_type = dumb->terminal_type_enum;
    lle_capabilities_destroy(dumb);

    ASSERT(has_strings);
    ASSERT(xterm_type == LLE_TERMINAL_XTERM);
    ASSERT(dumb_type != LLE_TERMINAL_XTERM);
}

TEST(terminal_type_strings) {
    /// The terminal_type string echoes the detected TERM verbatim, so a known
    /// TERM yields exactly that string. terminal_program is derived from the
    /// host's TERM_PROGRAM (not controlled here), so only assert it is a
    /// populated, non-empty string.
    lle_terminal_capabilities_t *caps = detect_as("xterm-256color", NULL);
    ASSERT(caps != NULL);

    ASSERT(strcmp(caps->terminal_type, "xterm-256color") == 0);
    ASSERT(caps->terminal_program != NULL);
    ASSERT(strlen(caps->terminal_program) > 0);

    lle_capabilities_destroy(caps);
}

TEST(tty_detection) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    /// is_tty should match actual TTY status
    int stdin_is_tty = isatty(STDIN_FILENO);
    int stdout_is_tty = isatty(STDOUT_FILENO);
    bool expected_tty = (stdin_is_tty && stdout_is_tty);

    ASSERT(caps->is_tty == expected_tty);

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * COLOR CAPABILITY TESTS
 * ============================================================================
 */

TEST(color_depth_valid) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    /// Color depth should be 0, 4, 8, or 24
    ASSERT(caps->detected_color_depth == 0 || caps->detected_color_depth == 4 ||
           caps->detected_color_depth == 8 || caps->detected_color_depth == 24);

    /// Color support flags should be consistent with depth
    if (caps->detected_color_depth >= 4) {
        ASSERT(caps->supports_ansi_colors == true);
    }

    if (caps->detected_color_depth >= 8) {
        ASSERT(caps->supports_256_colors == true);
    }

    if (caps->detected_color_depth == 24) {
        ASSERT(caps->supports_truecolor == true);
    }

    lle_capabilities_destroy(caps);
}

TEST(color_flags_consistency) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    /// If truecolor supported, 256 colors must also be supported
    if (caps->supports_truecolor) {
        ASSERT(caps->supports_256_colors == true);
        ASSERT(caps->supports_ansi_colors == true);
    }

    /// If 256 colors supported, ANSI colors must be supported
    if (caps->supports_256_colors) {
        ASSERT(caps->supports_ansi_colors == true);
    }

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * TEXT ATTRIBUTE TESTS
 * ============================================================================
 */

TEST(text_attributes_detected) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    /// Modern terminals should support at least bold and underline
    /// Note: This might fail in very minimal environments, but that's OK
    /// We're just testing that detection ran, not enforcing support

    /// The text-attribute flags are queried from the terminfo database, so
    /// their exact values vary by platform and cannot be pinned to a literal.
    /// What is invariant is that detection is deterministic: two detections of
    /// the same environment agree on every text-attribute flag.
    lle_terminal_capabilities_t *again = NULL;
    ASSERT(lle_capabilities_detect_environment(&again, NULL) == LLE_SUCCESS);
    ASSERT(again != NULL);
    ASSERT(caps->supports_bold == again->supports_bold);
    ASSERT(caps->supports_italic == again->supports_italic);
    ASSERT(caps->supports_underline == again->supports_underline);
    ASSERT(caps->supports_strikethrough == again->supports_strikethrough);
    ASSERT(caps->supports_reverse == again->supports_reverse);
    ASSERT(caps->supports_dim == again->supports_dim);

    lle_capabilities_destroy(again);
    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * ADVANCED FEATURE TESTS
 * ============================================================================
 */

TEST(advanced_features_detected) {
    /// These feature flags are derived from the terminal type, so an
    /// xterm-class terminal deterministically reports mouse reporting,
    /// bracketed paste, focus events, and Unicode support.
    lle_terminal_capabilities_t *caps =
        detect_as("xterm-256color", "truecolor");
    ASSERT(caps != NULL);
    bool mouse = caps->supports_mouse_reporting;
    bool paste = caps->supports_bracketed_paste;
    bool focus = caps->supports_focus_events;
    bool unicode = caps->supports_unicode;
    lle_capabilities_destroy(caps);

    ASSERT(mouse == true);
    ASSERT(paste == true);
    ASSERT(focus == true);
    ASSERT(unicode == true);
}

TEST(feature_correlation) {
    /// A modern terminal (Kitty) correlates to the full feature set:
    /// Unicode, mouse reporting, and bracketed paste are all present. Driving
    /// the terminal type makes this run rather than being skipped because the
    /// host terminal happened not to be modern.
    lle_terminal_capabilities_t *caps = detect_as("kitty", NULL);
    ASSERT(caps != NULL);
    bool unicode = caps->supports_unicode;
    bool mouse = caps->supports_mouse_reporting;
    bool paste = caps->supports_bracketed_paste;
    lle_capabilities_destroy(caps);

    ASSERT(unicode == true);
    ASSERT(mouse == true);
    ASSERT(paste == true);
}

/* ============================================================================
 * GEOMETRY DETECTION TESTS
 * ============================================================================
 */

TEST(geometry_detection) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    /// Width and height should be reasonable
    ASSERT(caps->terminal_width >= 20); /// Minimum enforced
    ASSERT(caps->terminal_height >= 5); /// Minimum enforced

    /// Should not be absurdly large
    ASSERT(caps->terminal_width < 10000);
    ASSERT(caps->terminal_height < 10000);

    lle_capabilities_destroy(caps);
}

TEST(geometry_update) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    /// Update with specific dimensions
    result = lle_capabilities_update_geometry(caps, 100, 40);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps->terminal_width == 100);
    ASSERT(caps->terminal_height == 40);

    /// Update with zeros (should re-detect)
    result = lle_capabilities_update_geometry(caps, 0, 0);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps->terminal_width >= 20);
    ASSERT(caps->terminal_height >= 5);

    /// Update with too-small values (should enforce minimums)
    result = lle_capabilities_update_geometry(caps, 10, 2);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps->terminal_width == 80);  /// Enforced minimum
    ASSERT(caps->terminal_height == 24); /// Enforced minimum

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * PERFORMANCE CHARACTERISTICS TESTS
 * ============================================================================
 */

TEST(performance_characteristics) {
    /// Drive a known modern terminal (Alacritty) so the performance profile
    /// is deterministic: estimated latency is within the sane 1-100ms band,
    /// is in the low-latency tier (<= 10ms), and fast updates are supported.
    lle_terminal_capabilities_t *caps = detect_as("alacritty", NULL);
    ASSERT(caps != NULL);
    int latency = caps->estimated_latency_ms;
    bool fast = caps->supports_fast_updates;
    lle_capabilities_destroy(caps);

    ASSERT(latency >= 1);
    ASSERT(latency <= 100);
    ASSERT(latency <= 10);
    ASSERT(fast == true);
}

TEST(optimization_flags) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    /// Optimization flags should be set
    /// At minimum, unicode awareness should be set for modern terminals
    if (caps->supports_unicode) {
        ASSERT((caps->optimizations & LLE_OPT_UNICODE_AWARE) != 0);
    }

    /// Fast terminals should have incremental draw enabled
    if (caps->supports_fast_updates) {
        ASSERT((caps->optimizations & LLE_OPT_INCREMENTAL_DRAW) != 0);
    }

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

TEST(null_parameter_handling) {
    /// NULL caps pointer should return error
    lle_result_t result = lle_capabilities_detect_environment(NULL, NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    /// NULL destroy should not crash
    lle_capabilities_destroy(NULL);

    /// NULL update should return error
    result = lle_capabilities_update_geometry(NULL, 80, 24);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);
}

TEST(multiple_detections) {
    /// Should be able to detect multiple times
    lle_terminal_capabilities_t *caps1 = NULL;
    lle_terminal_capabilities_t *caps2 = NULL;

    lle_result_t result1 = lle_capabilities_detect_environment(&caps1, NULL);
    lle_result_t result2 = lle_capabilities_detect_environment(&caps2, NULL);

    ASSERT(result1 == LLE_SUCCESS);
    ASSERT(result2 == LLE_SUCCESS);
    ASSERT(caps1 != NULL);
    ASSERT(caps2 != NULL);

    /// Results should be consistent
    ASSERT(caps1->terminal_type_enum == caps2->terminal_type_enum);
    ASSERT(caps1->terminal_width == caps2->terminal_width);
    ASSERT(caps1->terminal_height == caps2->terminal_height);

    lle_capabilities_destroy(caps1);
    lle_capabilities_destroy(caps2);
}

/* ============================================================================
 * MEMORY MANAGEMENT TESTS
 * ============================================================================
 */

TEST(memory_cleanup) {
    /// Test that destroy properly frees memory
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);
    ASSERT(caps->terminal_type != NULL);
    ASSERT(caps->terminal_program != NULL);

    /// Should not leak memory when destroyed
    lle_capabilities_destroy(caps);
    /// If valgrind is run, this will be verified
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Running Terminal Capability Detection Tests\n");
    printf("============================================\n\n");

    printf("Terminal Type Detection Tests:\n");
    RUN_TEST(capability_detection_basic);
    RUN_TEST(terminal_type_strings);
    RUN_TEST(tty_detection);

    printf("\nColor Capability Tests:\n");
    RUN_TEST(color_depth_valid);
    RUN_TEST(color_flags_consistency);

    printf("\nText Attribute Tests:\n");
    RUN_TEST(text_attributes_detected);

    printf("\nAdvanced Feature Tests:\n");
    RUN_TEST(advanced_features_detected);
    RUN_TEST(feature_correlation);

    printf("\nGeometry Detection Tests:\n");
    RUN_TEST(geometry_detection);
    RUN_TEST(geometry_update);

    printf("\nPerformance Characteristics Tests:\n");
    RUN_TEST(performance_characteristics);
    RUN_TEST(optimization_flags);

    printf("\nError Handling Tests:\n");
    RUN_TEST(null_parameter_handling);
    RUN_TEST(multiple_detections);

    printf("\nMemory Management Tests:\n");
    RUN_TEST(memory_cleanup);

    return TEST_RESULT();
}
