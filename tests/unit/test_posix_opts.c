/**
 * @file test_posix_opts.c
 * @brief Unit tests for POSIX shell options management
 *
 * Tests the POSIX options module including:
 * - Option initialization and defaults
 * - Option query functions
 * - Option setting/unsetting
 * - is_posix_option_set() function
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lush.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* External declarations for posix_opts functions */
extern shell_options_t shell_opts;
extern void init_posix_options(void);
extern bool is_posix_option_set(char option);
extern bool should_exit_on_error(void);
extern bool should_trace_execution(void);
extern bool is_syntax_check_mode(void);
extern bool should_error_unset_vars(void);
extern bool is_verbose_mode(void);
extern bool is_globbing_disabled(void);
extern bool should_auto_export(void);
extern bool is_noclobber_enabled(void);
extern bool is_ignoreeof_enabled(void);
extern bool is_nolog_enabled(void);
extern bool is_emacs_mode_enabled(void);
extern bool is_vi_mode_enabled(void);
extern bool is_posix_mode_enabled(void);
extern bool is_pipefail_enabled(void);
extern bool is_histexpand_enabled(void);
extern bool is_history_enabled(void);
extern bool is_interactive_comments_enabled(void);
extern void print_command_trace(const char *command);

/* Test framework macros */
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                         \
    do {                                                                       \
        printf("  Running: %s...\n", #name);                                   \
        test_##name();                                                         \
        printf("    PASSED\n");                                                \
    } while (0)

#define ASSERT(condition, message)                                             \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("    FAILED: %s\n", message);                               \
            printf("      at %s:%d\n", __FILE__, __LINE__);                    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(condition, message) ASSERT((condition), message)
#define ASSERT_FALSE(condition, message) ASSERT(!(condition), message)

#define ASSERT_EQ(actual, expected, message)                                   \
    do {                                                                       \
        if ((actual) != (expected)) {                                          \
            printf("    FAILED: %s\n", message);                               \
            printf("      Expected: %ld, Got: %ld\n", (long)(expected),        \
                   (long)(actual));                                            \
            printf("      at %s:%d\n", __FILE__, __LINE__);                    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/* ============================================================================
 * INITIALIZATION TESTS
 * ============================================================================
 */

TEST(init_posix_options_defaults) {
    init_posix_options();

    /* Check default values after initialization */
    ASSERT_FALSE(shell_opts.command_mode,
                 "command_mode should default to false");
    ASSERT_FALSE(shell_opts.stdin_mode, "stdin_mode should default to false");
    ASSERT_FALSE(shell_opts.interactive, "interactive should default to false");
    ASSERT_FALSE(shell_opts.login_shell, "login_shell should default to false");
    ASSERT_FALSE(shell_opts.exit_on_error,
                 "exit_on_error should default to false");
    ASSERT_FALSE(shell_opts.trace_execution,
                 "trace_execution should default to false");
    ASSERT_FALSE(shell_opts.syntax_check,
                 "syntax_check should default to false");
    ASSERT_FALSE(shell_opts.unset_error, "unset_error should default to false");
    ASSERT_FALSE(shell_opts.verbose, "verbose should default to false");
    ASSERT_FALSE(shell_opts.no_globbing, "no_globbing should default to false");
    ASSERT_TRUE(shell_opts.hash_commands,
                "hash_commands should default to true");
    ASSERT_FALSE(shell_opts.job_control, "job_control should default to false");
    ASSERT_FALSE(shell_opts.allexport, "allexport should default to false");
    ASSERT_FALSE(shell_opts.noclobber, "noclobber should default to false");
    ASSERT_FALSE(shell_opts.onecmd, "onecmd should default to false");
    ASSERT_FALSE(shell_opts.notify, "notify should default to false");
    ASSERT_FALSE(shell_opts.ignoreeof, "ignoreeof should default to false");
    ASSERT_FALSE(shell_opts.nolog, "nolog should default to false");
    ASSERT_TRUE(shell_opts.emacs_mode, "emacs_mode should default to true");
    ASSERT_FALSE(shell_opts.vi_mode, "vi_mode should default to false");
    ASSERT_FALSE(shell_opts.posix_mode, "posix_mode should default to false");
    ASSERT_FALSE(shell_opts.pipefail_mode,
                 "pipefail_mode should default to false");
    ASSERT_TRUE(shell_opts.histexpand_mode,
                "histexpand_mode should default to true");
    ASSERT_TRUE(shell_opts.history_mode, "history_mode should default to true");
    ASSERT_TRUE(shell_opts.interactive_comments_mode,
                "interactive_comments_mode should default to true");
    ASSERT_FALSE(shell_opts.physical_mode,
                 "physical_mode should default to false");
    ASSERT_FALSE(shell_opts.privileged_mode,
                 "privileged_mode should default to false");
}

/* ============================================================================
 * OPTION QUERY FUNCTION TESTS
 * ============================================================================
 */

TEST(should_exit_on_error_query) {
    init_posix_options();

    shell_opts.exit_on_error = false;
    ASSERT_FALSE(should_exit_on_error(), "should return false when disabled");

    shell_opts.exit_on_error = true;
    ASSERT_TRUE(should_exit_on_error(), "should return true when enabled");

    shell_opts.exit_on_error = false; /* Reset */
}

TEST(should_trace_execution_query) {
    init_posix_options();

    shell_opts.trace_execution = false;
    ASSERT_FALSE(should_trace_execution(), "should return false when disabled");

    shell_opts.trace_execution = true;
    ASSERT_TRUE(should_trace_execution(), "should return true when enabled");

    shell_opts.trace_execution = false; /* Reset */
}

TEST(is_syntax_check_mode_query) {
    init_posix_options();

    shell_opts.syntax_check = false;
    ASSERT_FALSE(is_syntax_check_mode(), "should return false when disabled");

    shell_opts.syntax_check = true;
    ASSERT_TRUE(is_syntax_check_mode(), "should return true when enabled");

    shell_opts.syntax_check = false; /* Reset */
}

TEST(should_error_unset_vars_query) {
    init_posix_options();

    shell_opts.unset_error = false;
    ASSERT_FALSE(should_error_unset_vars(),
                 "should return false when disabled");

    shell_opts.unset_error = true;
    ASSERT_TRUE(should_error_unset_vars(), "should return true when enabled");

    shell_opts.unset_error = false; /* Reset */
}

TEST(is_verbose_mode_query) {
    init_posix_options();

    shell_opts.verbose = false;
    ASSERT_FALSE(is_verbose_mode(), "should return false when disabled");

    shell_opts.verbose = true;
    ASSERT_TRUE(is_verbose_mode(), "should return true when enabled");

    shell_opts.verbose = false; /* Reset */
}

TEST(is_globbing_disabled_query) {
    init_posix_options();

    shell_opts.no_globbing = false;
    ASSERT_FALSE(is_globbing_disabled(), "should return false when disabled");

    shell_opts.no_globbing = true;
    ASSERT_TRUE(is_globbing_disabled(), "should return true when enabled");

    shell_opts.no_globbing = false; /* Reset */
}

TEST(should_auto_export_query) {
    init_posix_options();

    shell_opts.allexport = false;
    ASSERT_FALSE(should_auto_export(), "should return false when disabled");

    shell_opts.allexport = true;
    ASSERT_TRUE(should_auto_export(), "should return true when enabled");

    shell_opts.allexport = false; /* Reset */
}

TEST(is_noclobber_enabled_query) {
    init_posix_options();

    shell_opts.noclobber = false;
    ASSERT_FALSE(is_noclobber_enabled(), "should return false when disabled");

    shell_opts.noclobber = true;
    ASSERT_TRUE(is_noclobber_enabled(), "should return true when enabled");

    shell_opts.noclobber = false; /* Reset */
}

TEST(is_ignoreeof_enabled_query) {
    init_posix_options();

    shell_opts.ignoreeof = false;
    ASSERT_FALSE(is_ignoreeof_enabled(), "should return false when disabled");

    shell_opts.ignoreeof = true;
    ASSERT_TRUE(is_ignoreeof_enabled(), "should return true when enabled");

    shell_opts.ignoreeof = false; /* Reset */
}

TEST(is_nolog_enabled_query) {
    init_posix_options();

    shell_opts.nolog = false;
    ASSERT_FALSE(is_nolog_enabled(), "should return false when disabled");

    shell_opts.nolog = true;
    ASSERT_TRUE(is_nolog_enabled(), "should return true when enabled");

    shell_opts.nolog = false; /* Reset */
}

TEST(is_emacs_mode_enabled_query) {
    init_posix_options();

    /* emacs_mode defaults to true */
    ASSERT_TRUE(is_emacs_mode_enabled(), "should return true by default");

    shell_opts.emacs_mode = false;
    ASSERT_FALSE(is_emacs_mode_enabled(), "should return false when disabled");

    shell_opts.emacs_mode = true; /* Reset */
}

TEST(is_vi_mode_enabled_query) {
    init_posix_options();

    shell_opts.vi_mode = false;
    ASSERT_FALSE(is_vi_mode_enabled(), "should return false when disabled");

    shell_opts.vi_mode = true;
    ASSERT_TRUE(is_vi_mode_enabled(), "should return true when enabled");

    shell_opts.vi_mode = false; /* Reset */
}

TEST(is_posix_mode_enabled_query) {
    init_posix_options();

    shell_opts.posix_mode = false;
    ASSERT_FALSE(is_posix_mode_enabled(), "should return false when disabled");

    shell_opts.posix_mode = true;
    ASSERT_TRUE(is_posix_mode_enabled(), "should return true when enabled");

    shell_opts.posix_mode = false; /* Reset */
}

TEST(is_pipefail_enabled_query) {
    init_posix_options();

    shell_opts.pipefail_mode = false;
    ASSERT_FALSE(is_pipefail_enabled(), "should return false when disabled");

    shell_opts.pipefail_mode = true;
    ASSERT_TRUE(is_pipefail_enabled(), "should return true when enabled");

    shell_opts.pipefail_mode = false; /* Reset */
}

TEST(is_histexpand_enabled_query) {
    init_posix_options();

    /* histexpand_mode defaults to true */
    ASSERT_TRUE(is_histexpand_enabled(), "should return true by default");

    shell_opts.histexpand_mode = false;
    ASSERT_FALSE(is_histexpand_enabled(), "should return false when disabled");

    shell_opts.histexpand_mode = true; /* Reset */
}

TEST(is_history_enabled_query) {
    init_posix_options();

    /* history_mode defaults to true */
    ASSERT_TRUE(is_history_enabled(), "should return true by default");

    shell_opts.history_mode = false;
    ASSERT_FALSE(is_history_enabled(), "should return false when disabled");

    shell_opts.history_mode = true; /* Reset */
}

TEST(is_interactive_comments_enabled_query) {
    init_posix_options();

    /* interactive_comments_mode defaults to true */
    ASSERT_TRUE(is_interactive_comments_enabled(),
                "should return true by default");

    shell_opts.interactive_comments_mode = false;
    ASSERT_FALSE(is_interactive_comments_enabled(),
                 "should return false when disabled");

    shell_opts.interactive_comments_mode = true; /* Reset */
}

/* ============================================================================
 * IS_POSIX_OPTION_SET TESTS
 * ============================================================================
 */

TEST(is_posix_option_set_c) {
    init_posix_options();

    shell_opts.command_mode = false;
    ASSERT_FALSE(is_posix_option_set('c'),
                 "-c should return false when disabled");

    shell_opts.command_mode = true;
    ASSERT_TRUE(is_posix_option_set('c'), "-c should return true when enabled");

    shell_opts.command_mode = false;
}

TEST(is_posix_option_set_s) {
    init_posix_options();

    shell_opts.stdin_mode = false;
    ASSERT_FALSE(is_posix_option_set('s'),
                 "-s should return false when disabled");

    shell_opts.stdin_mode = true;
    ASSERT_TRUE(is_posix_option_set('s'), "-s should return true when enabled");

    shell_opts.stdin_mode = false;
}

TEST(is_posix_option_set_i) {
    init_posix_options();

    shell_opts.interactive = false;
    ASSERT_FALSE(is_posix_option_set('i'),
                 "-i should return false when disabled");

    shell_opts.interactive = true;
    ASSERT_TRUE(is_posix_option_set('i'), "-i should return true when enabled");

    shell_opts.interactive = false;
}

TEST(is_posix_option_set_l) {
    init_posix_options();

    shell_opts.login_shell = false;
    ASSERT_FALSE(is_posix_option_set('l'),
                 "-l should return false when disabled");

    shell_opts.login_shell = true;
    ASSERT_TRUE(is_posix_option_set('l'), "-l should return true when enabled");

    shell_opts.login_shell = false;
}

TEST(is_posix_option_set_e) {
    init_posix_options();

    shell_opts.exit_on_error = false;
    ASSERT_FALSE(is_posix_option_set('e'),
                 "-e should return false when disabled");

    shell_opts.exit_on_error = true;
    ASSERT_TRUE(is_posix_option_set('e'), "-e should return true when enabled");

    shell_opts.exit_on_error = false;
}

TEST(is_posix_option_set_x) {
    init_posix_options();

    shell_opts.trace_execution = false;
    ASSERT_FALSE(is_posix_option_set('x'),
                 "-x should return false when disabled");

    shell_opts.trace_execution = true;
    ASSERT_TRUE(is_posix_option_set('x'), "-x should return true when enabled");

    shell_opts.trace_execution = false;
}

TEST(is_posix_option_set_n) {
    init_posix_options();

    shell_opts.syntax_check = false;
    ASSERT_FALSE(is_posix_option_set('n'),
                 "-n should return false when disabled");

    shell_opts.syntax_check = true;
    ASSERT_TRUE(is_posix_option_set('n'), "-n should return true when enabled");

    shell_opts.syntax_check = false;
}

TEST(is_posix_option_set_u) {
    init_posix_options();

    shell_opts.unset_error = false;
    ASSERT_FALSE(is_posix_option_set('u'),
                 "-u should return false when disabled");

    shell_opts.unset_error = true;
    ASSERT_TRUE(is_posix_option_set('u'), "-u should return true when enabled");

    shell_opts.unset_error = false;
}

TEST(is_posix_option_set_v) {
    init_posix_options();

    shell_opts.verbose = false;
    ASSERT_FALSE(is_posix_option_set('v'),
                 "-v should return false when disabled");

    shell_opts.verbose = true;
    ASSERT_TRUE(is_posix_option_set('v'), "-v should return true when enabled");

    shell_opts.verbose = false;
}

TEST(is_posix_option_set_f) {
    init_posix_options();

    shell_opts.no_globbing = false;
    ASSERT_FALSE(is_posix_option_set('f'),
                 "-f should return false when disabled");

    shell_opts.no_globbing = true;
    ASSERT_TRUE(is_posix_option_set('f'), "-f should return true when enabled");

    shell_opts.no_globbing = false;
}

TEST(is_posix_option_set_h) {
    init_posix_options();

    /* hash_commands defaults to true */
    ASSERT_TRUE(is_posix_option_set('h'), "-h should return true by default");

    shell_opts.hash_commands = false;
    ASSERT_FALSE(is_posix_option_set('h'),
                 "-h should return false when disabled");

    shell_opts.hash_commands = true;
}

TEST(is_posix_option_set_m) {
    init_posix_options();

    shell_opts.job_control = false;
    ASSERT_FALSE(is_posix_option_set('m'),
                 "-m should return false when disabled");

    shell_opts.job_control = true;
    ASSERT_TRUE(is_posix_option_set('m'), "-m should return true when enabled");

    shell_opts.job_control = false;
}

TEST(is_posix_option_set_a) {
    init_posix_options();

    shell_opts.allexport = false;
    ASSERT_FALSE(is_posix_option_set('a'),
                 "-a should return false when disabled");

    shell_opts.allexport = true;
    ASSERT_TRUE(is_posix_option_set('a'), "-a should return true when enabled");

    shell_opts.allexport = false;
}

TEST(is_posix_option_set_C) {
    init_posix_options();

    shell_opts.noclobber = false;
    ASSERT_FALSE(is_posix_option_set('C'),
                 "-C should return false when disabled");

    shell_opts.noclobber = true;
    ASSERT_TRUE(is_posix_option_set('C'), "-C should return true when enabled");

    shell_opts.noclobber = false;
}

TEST(is_posix_option_set_t) {
    init_posix_options();

    shell_opts.onecmd = false;
    ASSERT_FALSE(is_posix_option_set('t'),
                 "-t should return false when disabled");

    shell_opts.onecmd = true;
    ASSERT_TRUE(is_posix_option_set('t'), "-t should return true when enabled");

    shell_opts.onecmd = false;
}

TEST(is_posix_option_set_b) {
    init_posix_options();

    shell_opts.notify = false;
    ASSERT_FALSE(is_posix_option_set('b'),
                 "-b should return false when disabled");

    shell_opts.notify = true;
    ASSERT_TRUE(is_posix_option_set('b'), "-b should return true when enabled");

    shell_opts.notify = false;
}

TEST(is_posix_option_set_invalid) {
    init_posix_options();

    /* Invalid/unknown options should return false */
    ASSERT_FALSE(is_posix_option_set('z'),
                 "unknown option should return false");
    ASSERT_FALSE(is_posix_option_set('?'),
                 "invalid option should return false");
    ASSERT_FALSE(is_posix_option_set('\0'), "null option should return false");
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Running POSIX Options tests...\n");

    /* Initialization tests */
    printf("\n=== Initialization Tests ===\n");
    RUN_TEST(init_posix_options_defaults);

    /* Option query function tests */
    printf("\n=== Option Query Function Tests ===\n");
    RUN_TEST(should_exit_on_error_query);
    RUN_TEST(should_trace_execution_query);
    RUN_TEST(is_syntax_check_mode_query);
    RUN_TEST(should_error_unset_vars_query);
    RUN_TEST(is_verbose_mode_query);
    RUN_TEST(is_globbing_disabled_query);
    RUN_TEST(should_auto_export_query);
    RUN_TEST(is_noclobber_enabled_query);
    RUN_TEST(is_ignoreeof_enabled_query);
    RUN_TEST(is_nolog_enabled_query);
    RUN_TEST(is_emacs_mode_enabled_query);
    RUN_TEST(is_vi_mode_enabled_query);
    RUN_TEST(is_posix_mode_enabled_query);
    RUN_TEST(is_pipefail_enabled_query);
    RUN_TEST(is_histexpand_enabled_query);
    RUN_TEST(is_history_enabled_query);
    RUN_TEST(is_interactive_comments_enabled_query);

    /* is_posix_option_set tests */
    printf("\n=== is_posix_option_set Tests ===\n");
    RUN_TEST(is_posix_option_set_c);
    RUN_TEST(is_posix_option_set_s);
    RUN_TEST(is_posix_option_set_i);
    RUN_TEST(is_posix_option_set_l);
    RUN_TEST(is_posix_option_set_e);
    RUN_TEST(is_posix_option_set_x);
    RUN_TEST(is_posix_option_set_n);
    RUN_TEST(is_posix_option_set_u);
    RUN_TEST(is_posix_option_set_v);
    RUN_TEST(is_posix_option_set_f);
    RUN_TEST(is_posix_option_set_h);
    RUN_TEST(is_posix_option_set_m);
    RUN_TEST(is_posix_option_set_a);
    RUN_TEST(is_posix_option_set_C);
    RUN_TEST(is_posix_option_set_t);
    RUN_TEST(is_posix_option_set_b);
    RUN_TEST(is_posix_option_set_invalid);

    printf("\n=== All POSIX Options tests passed! ===\n");
    return 0;
}
