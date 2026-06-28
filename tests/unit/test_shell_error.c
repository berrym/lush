/**
 * @file test_shell_error.c
 * @brief Unit tests for shell error handling system
 *
 * Tests the unified error management system including:
 * - Error creation and destruction
 * - Source location tracking
 * - Error context chains
 * - Error collectors
 * - Error display formatting
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "shell_error.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Capture shell_error_display(err) output into buf. The display goes to a
/// FILE*, so route it through a tmpfile and read it back.
static void capture_error_display(shell_error_t *err, bool color, char *buf,
                                  size_t cap) {
    buf[0] = '\0';
    char tmpl[] = "/tmp/lush_se_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return;
    }
    unlink(tmpl);
    FILE *fp = fdopen(fd, "w+");
    if (!fp) {
        close(fd);
        return;
    }
    shell_error_display(err, fp, color);
    fflush(fp);
    rewind(fp);
    size_t n = fread(buf, 1, cap - 1, fp);
    buf[n] = '\0';
    fclose(fp);
}

/// Capture shell_error_display_all(collector) output into buf.
static void capture_display_all(shell_error_collector_t *collector, char *buf,
                                size_t cap) {
    buf[0] = '\0';
    char tmpl[] = "/tmp/lush_seall_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return;
    }
    unlink(tmpl);
    FILE *fp = fdopen(fd, "w+");
    if (!fp) {
        close(fd);
        return;
    }
    shell_error_display_all(collector, fp, false);
    fflush(fp);
    rewind(fp);
    size_t n = fread(buf, 1, cap - 1, fp);
    buf[n] = '\0';
    fclose(fp);
}

/// Test framework macros

/* ============================================================================
 * SOURCE LOCATION TESTS
 * ============================================================================
 */

TEST(source_loc_unknown) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;
    ASSERT_NULL(loc.filename, "Unknown location should have NULL filename");
    ASSERT_EQ(loc.line, 0, "Unknown location should have line 0");
    ASSERT_EQ(loc.column, 0, "Unknown location should have column 0");
    ASSERT(!SOURCE_LOC_VALID(loc), "Unknown location should be invalid");
}

TEST(source_loc_valid) {
    source_location_t loc = {.filename = "test.sh",
                             .line = 10,
                             .column = 5,
                             .offset = 100,
                             .length = 15};
    ASSERT(SOURCE_LOC_VALID(loc), "Location with line > 0 should be valid");
    ASSERT_STR_EQ(loc.filename, "test.sh", "Filename should match");
    ASSERT_EQ(loc.line, 10, "Line should match");
    ASSERT_EQ(loc.column, 5, "Column should match");
}

TEST(source_loc_here) {
    source_location_t loc = SOURCE_LOC_HERE();
    ASSERT_NOT_NULL(loc.filename, "HERE location should have filename");
    ASSERT(loc.line > 0, "HERE location should have valid line");
}

/* ============================================================================
 * ERROR CREATION TESTS
 * ============================================================================
 */

TEST(error_create_basic) {
    source_location_t loc = {.filename = "test.sh",
                             .line = 5,
                             .column = 10,
                             .offset = 50,
                             .length = 3};

    shell_error_t *err =
        shell_error_create(SHELL_ERR_UNEXPECTED_TOKEN, SHELL_SEVERITY_ERROR,
                           loc, "unexpected token '%s'", ";");

    ASSERT_NOT_NULL(err, "shell_error_create should return non-NULL");
    ASSERT_EQ(err->code, SHELL_ERR_UNEXPECTED_TOKEN, "Error code should match");
    ASSERT_EQ(err->severity, SHELL_SEVERITY_ERROR, "Severity should match");
    ASSERT_EQ(err->location.line, 5, "Line should match");
    ASSERT_NOT_NULL(err->message, "Message should not be NULL");
    ASSERT(strstr(err->message, ";") != NULL, "Message should contain token");

    shell_error_free(err);
}

TEST(error_create_all_severities) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;

    shell_error_t *note =
        shell_error_create(SHELL_OK, SHELL_SEVERITY_NOTE, loc, "note");
    ASSERT_NOT_NULL(note, "Note creation should succeed");
    ASSERT_EQ(note->severity, SHELL_SEVERITY_NOTE, "Severity should be NOTE");
    shell_error_free(note);

    shell_error_t *warn =
        shell_error_create(SHELL_OK, SHELL_SEVERITY_WARNING, loc, "warning");
    ASSERT_NOT_NULL(warn, "Warning creation should succeed");
    ASSERT_EQ(warn->severity, SHELL_SEVERITY_WARNING,
              "Severity should be WARNING");
    shell_error_free(warn);

    shell_error_t *err =
        shell_error_create(SHELL_OK, SHELL_SEVERITY_ERROR, loc, "error");
    ASSERT_NOT_NULL(err, "Error creation should succeed");
    ASSERT_EQ(err->severity, SHELL_SEVERITY_ERROR, "Severity should be ERROR");
    shell_error_free(err);

    shell_error_t *fatal =
        shell_error_create(SHELL_OK, SHELL_SEVERITY_FATAL, loc, "fatal");
    ASSERT_NOT_NULL(fatal, "Fatal creation should succeed");
    ASSERT_EQ(fatal->severity, SHELL_SEVERITY_FATAL,
              "Severity should be FATAL");
    shell_error_free(fatal);
}

TEST(error_create_parse_errors) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;

    /// Test various parse error codes
    shell_error_code_t codes[] = {
        SHELL_ERR_UNEXPECTED_TOKEN, SHELL_ERR_UNEXPECTED_EOF,
        SHELL_ERR_UNCLOSED_QUOTE,   SHELL_ERR_UNCLOSED_SUBST,
        SHELL_ERR_UNCLOSED_CONTROL, SHELL_ERR_INVALID_REDIRECT,
        SHELL_ERR_INVALID_FUNCTION, SHELL_ERR_INVALID_ARRAY,
        SHELL_ERR_RESERVED_WORD,    SHELL_ERR_HEREDOC_DELIMITER};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        shell_error_t *err =
            shell_error_create(codes[i], SHELL_SEVERITY_ERROR, loc, "test");
        ASSERT_NOT_NULL(err, "Error creation should succeed");
        ASSERT_EQ(err->code, codes[i], "Error code should match");
        shell_error_free(err);
    }
}

TEST(error_create_runtime_errors) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;

    shell_error_code_t codes[] = {
        SHELL_ERR_COMMAND_NOT_FOUND, SHELL_ERR_PERMISSION_DENIED,
        SHELL_ERR_FILE_NOT_FOUND,    SHELL_ERR_DIVISION_BY_ZERO,
        SHELL_ERR_READONLY_VAR,      SHELL_ERR_UNBOUND_VARIABLE,
        SHELL_ERR_PIPE_FAILED,       SHELL_ERR_FORK_FAILED};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        shell_error_t *err =
            shell_error_create(codes[i], SHELL_SEVERITY_ERROR, loc, "test");
        ASSERT_NOT_NULL(err, "Error creation should succeed");
        ASSERT_EQ(err->code, codes[i], "Error code should match");
        shell_error_free(err);
    }
}

TEST(error_free_null) {
    /// Should not crash
    shell_error_free(NULL);
}

/* ============================================================================
 * ERROR DECORATION TESTS
 * ============================================================================
 */

TEST(error_set_suggestion) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;
    shell_error_t *err =
        shell_error_create(SHELL_ERR_COMMAND_NOT_FOUND, SHELL_SEVERITY_ERROR,
                           loc, "command not found");

    shell_error_set_suggestion(err, "Did you mean 'ls'?");
    ASSERT_NOT_NULL(err->suggestion, "Suggestion should be set");
    ASSERT(strstr(err->suggestion, "ls") != NULL,
           "Suggestion should contain 'ls'");

    shell_error_free(err);
}

TEST(error_set_detail) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;
    shell_error_t *err = shell_error_create(
        SHELL_ERR_UNCLOSED_QUOTE, SHELL_SEVERITY_ERROR, loc, "unclosed quote");

    shell_error_set_detail(err, "The quote started at line 5");
    ASSERT_NOT_NULL(err->detail, "Detail should be set");
    ASSERT(strstr(err->detail, "line 5") != NULL,
           "Detail should contain 'line 5'");

    shell_error_free(err);
}

TEST(error_set_source_line) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;
    shell_error_t *err = shell_error_create(SHELL_ERR_UNEXPECTED_TOKEN,
                                            SHELL_SEVERITY_ERROR, loc, "test");

    shell_error_set_source_line(err, "echo $foo bar", 5, 9);
    ASSERT_NOT_NULL(err->source_line, "Source line should be set");
    ASSERT_STR_EQ(err->source_line, "echo $foo bar",
                  "Source line should match");
    ASSERT_EQ(err->highlight_start, 5, "Highlight start should match");
    ASSERT_EQ(err->highlight_end, 9, "Highlight end should match");

    shell_error_free(err);
}

/* ============================================================================
 * ERROR CHAINING TESTS
 * ============================================================================
 */

TEST(error_set_cause) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;

    shell_error_t *cause = shell_error_create(
        SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, loc, "file not found");
    shell_error_t *err =
        shell_error_create(SHELL_ERR_COMMAND_NOT_FOUND, SHELL_SEVERITY_ERROR,
                           loc, "command failed");

    shell_error_set_cause(err, cause);
    ASSERT(err->cause == cause, "Cause should be linked");

    /// Free should handle chain
    shell_error_free(err);
}

TEST(error_push_context) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;
    shell_error_t *err = shell_error_create(SHELL_ERR_UNEXPECTED_TOKEN,
                                            SHELL_SEVERITY_ERROR, loc, "test");

    shell_error_push_context(err, "while parsing function '%s'", "myfunc");
    ASSERT_EQ(err->context_depth, 1, "Context depth should be 1");
    ASSERT_NOT_NULL(err->context_stack[0], "Context should be set");
    ASSERT(strstr(err->context_stack[0], "myfunc") != NULL,
           "Context should contain function name");

    shell_error_push_context(err, "in file '%s'", "script.sh");
    ASSERT_EQ(err->context_depth, 2, "Context depth should be 2");

    shell_error_free(err);
}

TEST(error_context_max_depth) {
    source_location_t loc = SOURCE_LOC_UNKNOWN;
    shell_error_t *err = shell_error_create(SHELL_ERR_UNEXPECTED_TOKEN,
                                            SHELL_SEVERITY_ERROR, loc, "test");

    /// Push more than max contexts
    for (int i = 0; i < SHELL_ERROR_CONTEXT_MAX + 5; i++) {
        shell_error_push_context(err, "context %d", i);
    }

    /// Should cap at max
    ASSERT(err->context_depth <= SHELL_ERROR_CONTEXT_MAX,
           "Context depth should be capped");

    shell_error_free(err);
}

/* ============================================================================
 * ERROR COLLECTOR TESTS
 * ============================================================================
 */

TEST(collector_new) {
    const char *source = "echo hello\necho world\n";
    shell_error_collector_t *collector =
        shell_error_collector_new(source, strlen(source), "test.sh", 0);

    ASSERT_NOT_NULL(collector, "Collector creation should succeed");
    ASSERT_EQ(collector->count, 0, "Initial count should be 0");
    ASSERT_NULL(collector->first, "First should be NULL");
    ASSERT(!collector->has_fatal, "Should not have fatal initially");

    shell_error_collector_free(collector);
}

TEST(collector_add_error) {
    shell_error_collector_t *collector =
        shell_error_collector_new(NULL, 0, "test.sh", 0);
    source_location_t loc = SOURCE_LOC_UNKNOWN;

    shell_error_t *err1 = shell_error_create(
        SHELL_ERR_UNEXPECTED_TOKEN, SHELL_SEVERITY_ERROR, loc, "error 1");
    shell_error_collector_add(collector, err1);
    ASSERT_EQ(collector->count, 1, "Count should be 1");

    shell_error_t *err2 = shell_error_create(
        SHELL_ERR_UNEXPECTED_EOF, SHELL_SEVERITY_ERROR, loc, "error 2");
    shell_error_collector_add(collector, err2);
    ASSERT_EQ(collector->count, 2, "Count should be 2");

    shell_error_collector_free(collector);
}

TEST(collector_add_warning) {
    shell_error_collector_t *collector =
        shell_error_collector_new(NULL, 0, "test.sh", 0);
    source_location_t loc = SOURCE_LOC_UNKNOWN;

    shell_error_t *warn =
        shell_error_create(SHELL_OK, SHELL_SEVERITY_WARNING, loc, "warning");
    shell_error_collector_add(collector, warn);

    /// Warnings go to warning_count, not count
    ASSERT_EQ(collector->warning_count, 1, "Warning count should be 1");
    ASSERT_EQ(collector->count, 0,
              "Error count should be 0 (only warnings added)");
    ASSERT(!shell_error_collector_has_errors(collector),
           "Should not have errors (only warnings)");

    shell_error_collector_free(collector);
}

TEST(collector_add_fatal) {
    shell_error_collector_t *collector =
        shell_error_collector_new(NULL, 0, "test.sh", 0);
    source_location_t loc = SOURCE_LOC_UNKNOWN;

    shell_error_t *fatal = shell_error_create(
        SHELL_ERR_ASSERTION, SHELL_SEVERITY_FATAL, loc, "fatal error");
    shell_error_collector_add(collector, fatal);

    ASSERT(collector->has_fatal, "Should have fatal error");
    ASSERT(shell_error_collector_has_errors(collector), "Should have errors");

    shell_error_collector_free(collector);
}

TEST(collector_full) {
    shell_error_collector_t *collector =
        shell_error_collector_new(NULL, 0, "test.sh", 3);
    source_location_t loc = SOURCE_LOC_UNKNOWN;

    ASSERT(!shell_error_collector_full(collector),
           "Should not be full initially");

    for (int i = 0; i < 3; i++) {
        shell_error_t *err =
            shell_error_create(SHELL_ERR_UNEXPECTED_TOKEN, SHELL_SEVERITY_ERROR,
                               loc, "error %d", i);
        shell_error_collector_add(collector, err);
    }

    ASSERT(shell_error_collector_full(collector),
           "Should be full after max errors");

    shell_error_collector_free(collector);
}

TEST(collector_get_line) {
    const char *source = "line one\nline two\nline three\n";
    shell_error_collector_t *collector =
        shell_error_collector_new(source, strlen(source), "test.sh", 0);

    char *line1 = shell_error_collector_get_line(collector, 1);
    ASSERT_NOT_NULL(line1, "Line 1 should be returned");
    ASSERT_STR_EQ(line1, "line one", "Line 1 should match");
    free(line1);

    char *line2 = shell_error_collector_get_line(collector, 2);
    ASSERT_NOT_NULL(line2, "Line 2 should be returned");
    ASSERT_STR_EQ(line2, "line two", "Line 2 should match");
    free(line2);

    char *line3 = shell_error_collector_get_line(collector, 3);
    ASSERT_NOT_NULL(line3, "Line 3 should be returned");
    ASSERT_STR_EQ(line3, "line three", "Line 3 should match");
    free(line3);

    shell_error_collector_free(collector);
}

TEST(collector_free_null) {
    /// Should not crash
    shell_error_collector_free(NULL);
}

/* ============================================================================
 * ERROR CODE STRING TESTS
 * ============================================================================
 */

TEST(error_code_str_parse) {
    /// The code string is "E" followed by the four-digit numeric code.
    const char *str = shell_error_code_str(SHELL_ERR_UNEXPECTED_TOKEN);
    ASSERT_NOT_NULL(str, "Error code string should not be NULL");
    ASSERT_STR_EQ(str, "E1001", "unexpected-token code is E1001");
}

TEST(error_code_str_runtime) {
    const char *str = shell_error_code_str(SHELL_ERR_COMMAND_NOT_FOUND);
    ASSERT_NOT_NULL(str, "Error code string should not be NULL");
    ASSERT_STR_EQ(str, "E1101", "command-not-found code is E1101");
}

TEST(error_category_parse) {
    const char *cat = shell_error_category(SHELL_ERR_UNEXPECTED_TOKEN);
    ASSERT_NOT_NULL(cat, "Category should not be NULL");
    ASSERT_STR_EQ(cat, "parse error", "a parse code is in the parse category");
}

TEST(error_category_runtime) {
    const char *cat = shell_error_category(SHELL_ERR_COMMAND_NOT_FOUND);
    ASSERT_NOT_NULL(cat, "Category should not be NULL");
    ASSERT_STR_EQ(cat, "runtime error",
                  "a runtime code is in the runtime category");
}

TEST(error_severity_str) {
    ASSERT_STR_EQ(shell_error_severity_str(SHELL_SEVERITY_NOTE), "note",
                  "NOTE severity string");
    ASSERT_STR_EQ(shell_error_severity_str(SHELL_SEVERITY_WARNING), "warning",
                  "WARNING severity string");
    ASSERT_STR_EQ(shell_error_severity_str(SHELL_SEVERITY_ERROR), "error",
                  "ERROR severity string");
    ASSERT_STR_EQ(shell_error_severity_str(SHELL_SEVERITY_FATAL), "fatal",
                  "FATAL severity string");
}

/* ============================================================================
 * ERROR DISPLAY TESTS
 * ============================================================================
 */

TEST(error_display_basic) {
    source_location_t loc = {.filename = "test.sh",
                             .line = 5,
                             .column = 10,
                             .offset = 50,
                             .length = 3};

    shell_error_t *err =
        shell_error_create(SHELL_ERR_UNEXPECTED_TOKEN, SHELL_SEVERITY_ERROR,
                           loc, "unexpected token");
    shell_error_set_source_line(err, "echo hello world", 5, 10);

    char out[1024];
    capture_error_display(err, false, out, sizeof(out));
    ASSERT(strstr(out, "error[E1001]") != NULL, "renders the code");
    ASSERT(strstr(out, "unexpected token") != NULL, "renders the message");
    ASSERT(strstr(out, "test.sh:5:10") != NULL, "renders the location");
    ASSERT(strstr(out, "echo hello world") != NULL, "renders the source line");
    ASSERT(strstr(out, "^~~") != NULL, "renders the caret under the span");

    shell_error_free(err);
}

TEST(error_display_all) {
    shell_error_collector_t *collector =
        shell_error_collector_new("echo hello\necho world\n", 22, "test.sh", 0);
    source_location_t loc = SOURCE_LOC_UNKNOWN;

    shell_error_t *err1 = shell_error_create(
        SHELL_ERR_UNEXPECTED_TOKEN, SHELL_SEVERITY_ERROR, loc, "error 1");
    shell_error_collector_add(collector, err1);

    shell_error_t *err2 = shell_error_create(
        SHELL_ERR_UNEXPECTED_EOF, SHELL_SEVERITY_WARNING, loc, "warning");
    shell_error_collector_add(collector, err2);

    char out[2048];
    capture_display_all(collector, out, sizeof(out));
    ASSERT(strstr(out, "error 1") != NULL, "renders the first error");
    ASSERT(strstr(out, "warning") != NULL, "renders the second error");

    shell_error_collector_free(collector);
}

/* ============================================================================
 * INTERNAL ERROR MACRO TESTS
 * ============================================================================
 */

TEST(internal_error_macro) {
    shell_error_t *err =
        SHELL_ERROR_INTERNAL(SHELL_ERR_ASSERTION, "internal failure");
    ASSERT_NOT_NULL(err, "Internal error macro should work");
    ASSERT_EQ(err->code, SHELL_ERR_ASSERTION, "Code should match");
    ASSERT_EQ(err->severity, SHELL_SEVERITY_FATAL, "Should be fatal");
    ASSERT(err->location.line > 0, "Should have line info");
    shell_error_free(err);
}

/* ============================================================================
 * ONE-SHOT EMIT TESTS
 * ============================================================================
 */

TEST(error_emit_renders_to_stderr) {
    /// shell_error_emit is the one-shot create + render-to-stderr + free.
    /// Capture stderr around it and assert it rendered the severity, code, and
    /// the formatted message (proving create + display happened; the free is
    /// covered by the suite's leak check).
    fflush(stderr);
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0, "pipe should succeed");
    int saved = dup(STDERR_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                     SOURCE_LOC_UNKNOWN, "bad value %d", 42);

    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    char out[512];
    ssize_t n = read(pipefd[0], out, sizeof(out) - 1);
    close(pipefd[0]);
    out[n > 0 ? (size_t)n : 0] = '\0';

    ASSERT(strstr(out, "warning[E1204]") != NULL,
           "emit renders the severity and the error code "
           "(E1204 = SHELL_ERR_INVALID_ARGUMENT)");
    ASSERT(strstr(out, "bad value 42") != NULL,
           "emit renders the formatted message");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running shell_error.c tests...\n\n");

    printf("Source Location Tests:\n");
    RUN_TEST(source_loc_unknown);
    RUN_TEST(source_loc_valid);
    RUN_TEST(source_loc_here);

    printf("\nError Creation Tests:\n");
    RUN_TEST(error_create_basic);
    RUN_TEST(error_create_all_severities);
    RUN_TEST(error_create_parse_errors);
    RUN_TEST(error_create_runtime_errors);
    RUN_TEST(error_free_null);

    printf("\nError Decoration Tests:\n");
    RUN_TEST(error_set_suggestion);
    RUN_TEST(error_set_detail);
    RUN_TEST(error_set_source_line);

    printf("\nError Chaining Tests:\n");
    RUN_TEST(error_set_cause);
    RUN_TEST(error_push_context);
    RUN_TEST(error_context_max_depth);

    printf("\nError Collector Tests:\n");
    RUN_TEST(collector_new);
    RUN_TEST(collector_add_error);
    RUN_TEST(collector_add_warning);
    RUN_TEST(collector_add_fatal);
    RUN_TEST(collector_full);
    RUN_TEST(collector_get_line);
    RUN_TEST(collector_free_null);

    printf("\nError Code String Tests:\n");
    RUN_TEST(error_code_str_parse);
    RUN_TEST(error_code_str_runtime);
    RUN_TEST(error_category_parse);
    RUN_TEST(error_category_runtime);
    RUN_TEST(error_severity_str);

    printf("\nError Display Tests:\n");
    RUN_TEST(error_display_basic);
    RUN_TEST(error_display_all);

    printf("\nInternal Error Macro Tests:\n");
    RUN_TEST(internal_error_macro);

    printf("\nOne-shot Emit Tests:\n");
    RUN_TEST(error_emit_renders_to_stderr);

    return TEST_RESULT();
}
