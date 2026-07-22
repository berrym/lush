/**
 * @file test_alias.c
 * @brief Unit tests for shell alias management
 *
 * Tests the alias subsystem including:
 * - Alias creation and deletion
 * - Alias lookup and expansion
 * - Recursive alias expansion
 * - Alias name validation
 * - Shell operator handling
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "alias.h"
#include "builtins.h"
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

/// Test framework macros

/// Helper to setup and teardown aliases for each test
static void setup_aliases(void) { init_aliases(); }

static void teardown_aliases(void) { free_aliases(); }

/// Capture writes to a file descriptor (stdout or stderr) so output-producing
/// functions can be asserted on. Works at the fd level, so it catches printf,
/// fprintf(stderr), and the pager's direct writes alike.
typedef struct {
    int target_fd;
    int saved_fd;
    FILE *tmp;
} fd_capture_t;

static fd_capture_t capture_begin(int fd) {
    fd_capture_t c = {.target_fd = fd, .saved_fd = -1, .tmp = tmpfile()};
    fflush(NULL);
    c.saved_fd = dup(fd);
    dup2(fileno(c.tmp), fd);
    return c;
}

static void capture_end(fd_capture_t *c, char *buf, size_t size) {
    fflush(NULL);
    dup2(c->saved_fd, c->target_fd);
    close(c->saved_fd);
    rewind(c->tmp);
    size_t n = fread(buf, 1, size - 1, c->tmp);
    buf[n] = '\0';
    fclose(c->tmp);
}

/* ============================================================================
 * INITIALIZATION TESTS
 * ============================================================================
 */

TEST(init_aliases_basic) {
    init_aliases();
    ASSERT_NOT_NULL(aliases, "Alias hash table should be created");
    free_aliases();
}

TEST(default_aliases_interactive_only) {
    /// The default convenience aliases (`ls`, `ll`, `..`, ...) are seeded only
    /// in an interactive session (POSIX makes alias substitution interactive).
    /// This test binary is non-interactive, so init_aliases creates the table
    /// but seeds no defaults -- a non-interactive `-c`/script run keeps
    /// `command -v ls` resolving to the real binary and injects no color into
    /// pipes. Issue #568.
    free_aliases();
    init_aliases();
    ASSERT_NOT_NULL(aliases, "table is still created non-interactively");
    ASSERT_NULL(lookup_alias("ls"), "ls default not seeded non-interactively");
    ASSERT_NULL(lookup_alias("ll"), "ll default not seeded non-interactively");
    ASSERT_NULL(lookup_alias(".."), ".. default not seeded non-interactively");
    free_aliases();
}

TEST(free_aliases_null) {
    /// Calling free before init must be a safe no-op that leaves the table
    /// NULL.
    free_aliases();
    ASSERT_NULL(aliases, "alias table is NULL before init / after free");
}

TEST(init_free_cycle) {
    for (int i = 0; i < 3; i++) {
        init_aliases();
        ASSERT_NOT_NULL(aliases, "init creates the table each cycle");
        set_alias("test", "value");
        ASSERT_STR_EQ(lookup_alias("test"), "value",
                      "alias set takes effect within the cycle");
        free_aliases();
        ASSERT_NULL(aliases, "free clears the table each cycle");
    }
}

/* ============================================================================
 * ALIAS NAME VALIDATION TESTS
 * ============================================================================
 */

TEST(valid_alias_name_simple) {
    ASSERT(valid_alias_name("ls"), "Simple name should be valid");
}

TEST(valid_alias_name_underscore) {
    ASSERT(valid_alias_name("my_alias"), "Underscore should be valid");
}

TEST(valid_alias_name_numbers) {
    ASSERT(valid_alias_name("ls2"), "Numbers in name should be valid");
}

TEST(valid_alias_name_long) {
    ASSERT(valid_alias_name("this_is_a_very_long_alias_name"),
           "Long name should be valid");
}

TEST(valid_alias_name_starts_with_number) {
    /// POSIX bans digit-first names, but bash and zsh both accept them
    /// (zsh uses this for numeric dirstack aliases like `alias 1='cd +1'`).
    /// Lush matches the bash/zsh consensus.
    ASSERT(valid_alias_name("2ls"),
           "Name starting with number should be valid (bash/zsh consensus)");
    ASSERT(valid_alias_name("1"), "Single-digit name should be valid");
}

TEST(valid_alias_name_empty) {
    ASSERT(!valid_alias_name(""), "Empty name should be invalid");
}

TEST(valid_alias_name_with_dash) {
    /// '-' is an accepted alias-name character, so a dashed name is valid.
    ASSERT(valid_alias_name("my-alias"), "dashed name is valid");
}

TEST(valid_alias_name_with_space) {
    /// valid_alias_name validates up to the first whitespace, so "my alias"
    /// is accepted on the strength of its leading word "my".
    ASSERT(valid_alias_name("my alias"),
           "name validated up to whitespace is valid");
}

TEST(valid_alias_name_with_equals) {
    ASSERT(!valid_alias_name("foo=bar"), "Name with equals should be invalid");
}

TEST(valid_alias_name_special_chars) {
    ASSERT(!valid_alias_name("foo$bar"), "Name with $ should be invalid");
    ASSERT(!valid_alias_name("foo!bar"), "Name with ! should be invalid");
    ASSERT(!valid_alias_name("foo@bar"), "Name with @ should be invalid");
}

/* ============================================================================
 * BASIC ALIAS OPERATIONS TESTS
 * ============================================================================
 */

TEST(set_alias_basic) {
    setup_aliases();

    bool result = set_alias("ll", "ls -l");
    ASSERT(result, "set_alias should succeed");

    char *value = lookup_alias("ll");
    ASSERT_NOT_NULL(value, "Alias should be found");
    ASSERT_STR_EQ(value, "ls -l", "Alias value should match");

    teardown_aliases();
}

TEST(set_alias_overwrite) {
    setup_aliases();

    set_alias("ll", "ls -l");
    bool result = set_alias("ll", "ls -la");
    ASSERT(result, "Overwriting alias should succeed");

    char *value = lookup_alias("ll");
    ASSERT_STR_EQ(value, "ls -la", "Alias should be overwritten");

    teardown_aliases();
}

TEST(lookup_alias_nonexistent) {
    setup_aliases();

    char *value = lookup_alias("nonexistent");
    ASSERT_NULL(value, "Nonexistent alias should return NULL");

    teardown_aliases();
}

TEST(unset_alias_basic) {
    setup_aliases();

    set_alias("ll", "ls -l");
    unset_alias("ll");

    char *value = lookup_alias("ll");
    ASSERT_NULL(value, "Unset alias should not be found");

    teardown_aliases();
}

TEST(unset_alias_nonexistent) {
    setup_aliases();

    /// Should not crash
    unset_alias("nonexistent");

    teardown_aliases();
}

TEST(set_multiple_aliases) {
    setup_aliases();

    set_alias("ll", "ls -l");
    set_alias("la", "ls -a");
    set_alias("grep", "grep --color=auto");

    ASSERT_STR_EQ(lookup_alias("ll"), "ls -l", "ll should work");
    ASSERT_STR_EQ(lookup_alias("la"), "ls -a", "la should work");
    ASSERT_STR_EQ(lookup_alias("grep"), "grep --color=auto",
                  "grep should work");

    teardown_aliases();
}

/* ============================================================================
 * PRINT ALIASES TESTS
 * ============================================================================
 */

TEST(print_aliases_empty) {
    setup_aliases();

    /// init_aliases seeds example aliases; clear them to exercise the
    /// genuinely-empty print path. (A non-empty buffer here also flags any
    /// newly-seeded default that this list does not account for.)
    const char *seeded[] = {"..", "...", "l", "la", "ll", "ls"};
    for (size_t i = 0; i < sizeof(seeded) / sizeof(seeded[0]); i++) {
        unset_alias(seeded[i]);
    }

    char buf[256];
    fd_capture_t cap = capture_begin(STDOUT_FILENO);
    print_aliases();
    capture_end(&cap, buf, sizeof(buf));

    /// With no aliases defined, no alias lines are printed.
    ASSERT(strstr(buf, "alias ") == NULL, "empty table prints no alias lines");

    teardown_aliases();
}

TEST(print_aliases_with_content) {
    setup_aliases();

    set_alias("ll", "ls -l");
    set_alias("la", "ls -a");

    char buf[512];
    fd_capture_t cap = capture_begin(STDOUT_FILENO);
    print_aliases();
    capture_end(&cap, buf, sizeof(buf));

    /// Each alias prints in `alias name='value'` form (order unspecified).
    ASSERT(strstr(buf, "alias ll='ls -l'") != NULL, "ll alias printed");
    ASSERT(strstr(buf, "alias la='ls -a'") != NULL, "la alias printed");

    teardown_aliases();
}

/* ============================================================================
 * RECURSIVE EXPANSION TESTS
 * ============================================================================
 */

TEST(expand_aliases_recursive_simple) {
    setup_aliases();

    /// Use a unique command name that won't have existing aliases
    set_alias("mytest", "mycommand --option");
    char *expanded = expand_aliases_recursive("mytest", 10);
    ASSERT_NOT_NULL(expanded, "Expansion should succeed");
    ASSERT_STR_EQ(expanded, "mycommand --option",
                  "Simple expansion should work");
    free(expanded);

    teardown_aliases();
}

TEST(expand_aliases_recursive_chain) {
    setup_aliases();

    set_alias("l", "ls");
    set_alias("ll", "l -l");
    char *expanded = expand_aliases_recursive("ll", 10);
    ASSERT_NOT_NULL(expanded, "Chain expansion should succeed");
    /// Should expand ll -> l -l -> ls -l
    ASSERT(strstr(expanded, "ls") != NULL, "Chain should expand to ls");
    free(expanded);

    teardown_aliases();
}

TEST(expand_aliases_recursive_nonexistent) {
    setup_aliases();

    /// A name with no alias expands to nothing.
    char *expanded = expand_aliases_recursive("notanalias", 10);
    ASSERT(expanded == NULL, "non-alias name should not expand");
    free(expanded);

    teardown_aliases();
}

TEST(expand_aliases_recursive_depth_limit) {
    setup_aliases();

    /// Create a deep chain
    set_alias("a", "b");
    set_alias("b", "c");
    set_alias("c", "d");
    set_alias("d", "e");
    set_alias("e", "f");

    /// Depth 2 expands a->b->c and stops; it must not run the chain to "f".
    char *expanded = expand_aliases_recursive("a", 2);
    ASSERT_NOT_NULL(expanded, "depth-2 expansion should produce a result");
    ASSERT_STR_EQ(expanded, "c", "depth 2 expands a->b->c then stops");
    free(expanded);

    teardown_aliases();
}

TEST(expand_aliases_recursive_circular) {
    setup_aliases();

    /// Create circular aliases
    set_alias("a", "b");
    set_alias("b", "a");

    /// Circular a<->b must terminate at the depth bound rather than loop
    /// forever or expand without limit.
    char *expanded = expand_aliases_recursive("a", 10);
    ASSERT_NOT_NULL(expanded,
                    "circular expansion must terminate with a result");
    ASSERT(strlen(expanded) <= 1,
           "circular expansion stays bounded (no runaway growth)");
    free(expanded);

    teardown_aliases();
}

/* ============================================================================
 * FIRST WORD EXPANSION TESTS
 * ============================================================================
 */

TEST(expand_first_word_alias_basic) {
    setup_aliases();

    set_alias("ll", "ls -l");
    char *expanded = expand_first_word_alias("ll /home");
    ASSERT_NOT_NULL(expanded, "First word expansion should succeed");
    ASSERT(strstr(expanded, "ls -l") != NULL, "Should expand first word");
    ASSERT(strstr(expanded, "/home") != NULL, "Should preserve arguments");
    free(expanded);

    teardown_aliases();
}

TEST(expand_first_word_alias_no_alias) {
    setup_aliases();

    char *expanded = expand_first_word_alias("ls /home");
    ASSERT_NOT_NULL(expanded, "Non-alias should return copy");
    ASSERT(strstr(expanded, "ls") != NULL, "Should preserve command");
    free(expanded);

    teardown_aliases();
}

TEST(expand_first_word_alias_only_first) {
    setup_aliases();

    set_alias("ll", "ls -l");
    set_alias("home", "/home");

    char *expanded = expand_first_word_alias("ll home");
    ASSERT_NOT_NULL(expanded, "Expansion should succeed");
    /// Should expand ll but not home
    ASSERT(strstr(expanded, "ls -l") != NULL, "Should expand first word");
    ASSERT(strstr(expanded, "home") != NULL, "Should not expand second word");
    free(expanded);

    teardown_aliases();
}

/* ============================================================================
 * SHELL OPERATOR HANDLING TESTS
 * ============================================================================
 */

TEST(contains_shell_operators_pipe) {
    ASSERT(contains_shell_operators("ls | grep foo"),
           "Pipe should be detected");
}

TEST(contains_shell_operators_redirect_out) {
    ASSERT(contains_shell_operators("ls > file"),
           "Redirect out should be detected");
}

TEST(contains_shell_operators_redirect_in) {
    ASSERT(contains_shell_operators("cat < file"),
           "Redirect in should be detected");
}

TEST(contains_shell_operators_append) {
    ASSERT(contains_shell_operators("ls >> file"), "Append should be detected");
}

TEST(contains_shell_operators_semicolon) {
    ASSERT(contains_shell_operators("ls; pwd"), "Semicolon should be detected");
}

TEST(contains_shell_operators_ampersand) {
    ASSERT(contains_shell_operators("cmd &"), "Ampersand should be detected");
}

TEST(contains_shell_operators_and) {
    ASSERT(contains_shell_operators("cmd1 && cmd2"), "AND should be detected");
}

TEST(contains_shell_operators_or) {
    ASSERT(contains_shell_operators("cmd1 || cmd2"), "OR should be detected");
}

TEST(contains_shell_operators_none) {
    ASSERT(!contains_shell_operators("ls -la /home"),
           "Simple command should not detect operators");
}

/* Note: is_special_alias_char is actually an alias for valid_alias_name_char,
 * checking if char is valid in alias names, not shell operators */
TEST(is_special_alias_char_valid_chars) {
    /// Alphanumeric chars are valid
    ASSERT(is_special_alias_char('a'), "Letter should be valid");
    ASSERT(is_special_alias_char('A'), "Uppercase should be valid");
    ASSERT(is_special_alias_char('0'), "Digit should be valid");
    ASSERT(is_special_alias_char('_'), "Underscore should be valid");
    ASSERT(is_special_alias_char('-'), "Dash should be valid");
    ASSERT(is_special_alias_char('.'), "Dot should be valid");
}

TEST(is_special_alias_char_invalid_chars) {
    /// Shell operators are NOT valid alias name chars
    ASSERT(!is_special_alias_char('|'), "Pipe should not be valid alias char");
    ASSERT(!is_special_alias_char('>'), "> should not be valid alias char");
    ASSERT(!is_special_alias_char('<'), "< should not be valid alias char");
    ASSERT(!is_special_alias_char(';'), "; should not be valid alias char");
    ASSERT(!is_special_alias_char(' '), "Space should not be valid alias char");
}

/* ============================================================================
 * EXPAND WITH SHELL OPERATORS TESTS
 * ============================================================================
 */

TEST(expand_alias_with_shell_operators_simple) {
    setup_aliases();

    set_alias("ll", "ls -l");
    char *expanded = expand_alias_with_shell_operators("ll /home");
    ASSERT_NOT_NULL(expanded, "Expansion should succeed");
    ASSERT(strstr(expanded, "ls -l") != NULL, "Should expand alias");
    free(expanded);

    teardown_aliases();
}

TEST(expand_alias_with_shell_operators_pipe) {
    setup_aliases();

    set_alias("ll", "ls -l");
    char *expanded = expand_alias_with_shell_operators("ll | grep foo");
    ASSERT_NOT_NULL(expanded, "Expansion with pipe should succeed");
    ASSERT_STR_EQ(expanded, "ls -l | grep foo",
                  "first word expands; pipe and the rest are preserved");
    free(expanded);

    teardown_aliases();
}

TEST(expand_alias_with_shell_operators_in_value) {
    setup_aliases();

    set_alias("lsgrep", "ls | grep");
    char *expanded = expand_alias_with_shell_operators("lsgrep foo");
    ASSERT_NOT_NULL(expanded, "Alias with operators should expand");
    ASSERT_STR_EQ(expanded, "ls | grep foo",
                  "operators inside the alias value are preserved");
    free(expanded);

    teardown_aliases();
}

/* ============================================================================
 * USAGE FUNCTIONS TESTS
 * ============================================================================
 */

TEST(alias_usage) {
    /// Usage text goes to stderr; capture it and assert the synopsis line.
    char buf[512];
    fd_capture_t cap = capture_begin(STDERR_FILENO);
    alias_usage();
    capture_end(&cap, buf, sizeof(buf));

    ASSERT(strstr(buf, "usage: alias") != NULL,
           "alias usage prints its synopsis to stderr");
}

TEST(unalias_usage) {
    char buf[512];
    fd_capture_t cap = capture_begin(STDERR_FILENO);
    unalias_usage();
    capture_end(&cap, buf, sizeof(buf));

    ASSERT(strstr(buf, "usage: unalias") != NULL,
           "unalias usage prints its synopsis to stderr");
}

/* ============================================================================
 * EDGE CASES AND STRESS TESTS
 * ============================================================================
 */

TEST(alias_with_quotes) {
    setup_aliases();

    set_alias("say", "echo 'hello world'");
    char *value = lookup_alias("say");
    ASSERT_STR_EQ(value, "echo 'hello world'",
                  "Quoted value should be preserved");

    teardown_aliases();
}

TEST(alias_with_variables) {
    setup_aliases();

    set_alias("home", "cd $HOME");
    char *value = lookup_alias("home");
    ASSERT_STR_EQ(value, "cd $HOME", "Variable should be preserved");

    teardown_aliases();
}

TEST(alias_empty_value) {
    setup_aliases();

    set_alias("empty", "");
    char *value = lookup_alias("empty");
    ASSERT_NOT_NULL(value, "Empty alias should exist");
    ASSERT_STR_EQ(value, "", "Empty value should be empty");

    teardown_aliases();
}

TEST(bin_alias_double_dash_ends_options) {
    setup_aliases();

    /// `alias -- -=cd -` should create an alias named "-" with value "cd -".
    /// Without `--` handling, the `--` itself would be treated as a missing
    /// alias-name lookup and the assignment would still apply, but the lookup
    /// would set exit_status=1. With `--` handling, neither happens.
    char argv0[] = "alias";
    char argv1[] = "--";
    char argv2[] = "-=cd -";
    char *argv[] = {argv0, argv1, argv2};
    int rc = bin_alias(3, argv);
    ASSERT_EQ(rc, 0, "alias -- -=cd - should return 0");
    ASSERT_STR_EQ(lookup_alias("-"), "cd -", "alias `-` should be set");

    teardown_aliases();
}

TEST(bin_alias_dash_g_set_and_lookup_global) {
    setup_aliases();

    /// `alias -g NAME=value` stores in the global table, not the regular one.
    /// Regression target: if the two tables ever get merged or the routing
    /// breaks, regular lookup would surface globals (or vice versa).
    char argv0[] = "alias";
    char argv1[] = "-g";
    char argv2[] = "GLOB=hello world";
    char *argv[] = {argv0, argv1, argv2};
    int rc = bin_alias(3, argv);
    ASSERT_EQ(rc, 0, "alias -g GLOB=hello world should return 0");
    ASSERT_STR_EQ(lookup_global_alias("GLOB"), "hello world",
                  "global alias GLOB should be in the global table");
    ASSERT_NULL(lookup_alias("GLOB"),
                "global alias must NOT appear in the regular table");

    teardown_aliases();
}

TEST(bin_alias_dash_g_lookup_undefined_returns_error) {
    setup_aliases();

    /// `alias -g NAME` for a name not defined globally must return non-zero
    /// so scripts can detect missing globals via $?. Silent success would
    /// mask typos.
    char argv0[] = "alias";
    char argv1[] = "-g";
    char argv2[] = "DOES_NOT_EXIST_GLOBALLY";
    char *argv[] = {argv0, argv1, argv2};
    int rc = bin_alias(3, argv);
    ASSERT_NE(rc, 0,
              "alias -g NAME should return non-zero when NAME is not "
              "a defined global alias");

    teardown_aliases();
}

TEST(bin_alias_invalid_flag_returns_error) {
    setup_aliases();

    /// Unknown `-x` flags must error rather than be silently accepted; an
    /// accepted-but-ignored flag would mask typos and ship unintended
    /// behavior.
    char argv0[] = "alias";
    char argv1[] = "-xyz";
    char argv2[] = "name=value";
    char *argv[] = {argv0, argv1, argv2};
    int rc = bin_alias(3, argv);
    ASSERT_NE(rc, 0, "alias -xyz should return non-zero (unknown flag)");
    ASSERT_NULL(lookup_alias("name"),
                "no alias should be created when a flag is rejected");

    teardown_aliases();
}

TEST(bin_alias_dash_g_then_double_dash) {
    setup_aliases();

    /// `alias -g -- NAME=value` must work: -g sets global-mode, -- ends
    /// option parsing, then NAME=value is an assignment. Regression target:
    /// the option-parsing loop must consume flags in any order and not lose
    /// the global-mode bit when -- appears after -g.
    char argv0[] = "alias";
    char argv1[] = "-g";
    char argv2[] = "--";
    char argv3[] = "AFTER_DD=afterval";
    char *argv[] = {argv0, argv1, argv2, argv3};
    int rc = bin_alias(4, argv);
    ASSERT_EQ(rc, 0, "alias -g -- AFTER_DD=afterval should return 0");
    ASSERT_STR_EQ(lookup_global_alias("AFTER_DD"), "afterval",
                  "global alias must be set after -g -- terminator");
    ASSERT_NULL(lookup_alias("AFTER_DD"),
                "alias must NOT land in the regular table");

    teardown_aliases();
}

TEST(set_global_alias_rejects_null_inputs) {
    setup_aliases();

    /// NULL-safe API contract: set_global_alias must reject NULL key/value
    /// without crashing. Tested directly because the bin_alias path is
    /// already guarded by argv-validity checks earlier.
    ASSERT_FALSE(set_global_alias(NULL, "value"),
                 "set_global_alias(NULL, value) should return false");
    ASSERT_FALSE(set_global_alias("name", NULL),
                 "set_global_alias(name, NULL) should return false");
    ASSERT_FALSE(set_global_alias(NULL, NULL),
                 "set_global_alias(NULL, NULL) should return false");

    teardown_aliases();
}

TEST(lookup_global_alias_uninitialized_returns_null) {
    /// Pre-init / post-free: lookup_global_alias must return NULL rather
    /// than crash. Lifecycle invariant: callers can probe before the alias
    /// subsystem is initialized.
    free_aliases(); /// ensure post-free state
    ASSERT_NULL(lookup_global_alias("anything"),
                "lookup_global_alias on uninitialized table returns NULL");
    ASSERT_NULL(lookup_global_alias(NULL),
                "lookup_global_alias(NULL) returns NULL");
}

TEST(bin_alias_digit_name) {
    setup_aliases();

    /// Zsh-style numeric dirstack aliases.
    char argv0[] = "alias";
    char argv1[] = "1=cd +1";
    char *argv[] = {argv0, argv1};
    int rc = bin_alias(2, argv);
    ASSERT_EQ(rc, 0, "alias 1=cd +1 should return 0");
    ASSERT_STR_EQ(lookup_alias("1"), "cd +1", "alias `1` should be set");

    teardown_aliases();
}

TEST(many_aliases) {
    setup_aliases();

    /// Add many aliases
    char name[32];
    char value[64];
    for (int i = 0; i < 100; i++) {
        snprintf(name, sizeof(name), "alias%d", i);
        snprintf(value, sizeof(value), "command%d --option", i);
        set_alias(name, value);
    }

    /// Verify some
    ASSERT_STR_EQ(lookup_alias("alias0"), "command0 --option", "First alias");
    ASSERT_STR_EQ(lookup_alias("alias50"), "command50 --option",
                  "Middle alias");
    ASSERT_STR_EQ(lookup_alias("alias99"), "command99 --option", "Last alias");

    teardown_aliases();
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running alias.c tests...\n\n");

    printf("Initialization Tests:\n");
    RUN_TEST(init_aliases_basic);
    RUN_TEST(default_aliases_interactive_only);
    RUN_TEST(free_aliases_null);
    RUN_TEST(init_free_cycle);

    printf("\nAlias Name Validation Tests:\n");
    RUN_TEST(valid_alias_name_simple);
    RUN_TEST(valid_alias_name_underscore);
    RUN_TEST(valid_alias_name_numbers);
    RUN_TEST(valid_alias_name_long);
    RUN_TEST(valid_alias_name_starts_with_number);
    RUN_TEST(valid_alias_name_empty);
    RUN_TEST(valid_alias_name_with_dash);
    RUN_TEST(valid_alias_name_with_space);
    RUN_TEST(valid_alias_name_with_equals);
    RUN_TEST(valid_alias_name_special_chars);

    printf("\nBasic Alias Operations Tests:\n");
    RUN_TEST(set_alias_basic);
    RUN_TEST(set_alias_overwrite);
    RUN_TEST(lookup_alias_nonexistent);
    RUN_TEST(unset_alias_basic);
    RUN_TEST(unset_alias_nonexistent);
    RUN_TEST(set_multiple_aliases);

    printf("\nPrint Aliases Tests:\n");
    RUN_TEST(print_aliases_empty);
    RUN_TEST(print_aliases_with_content);

    printf("\nRecursive Expansion Tests:\n");
    RUN_TEST(expand_aliases_recursive_simple);
    RUN_TEST(expand_aliases_recursive_chain);
    RUN_TEST(expand_aliases_recursive_nonexistent);
    RUN_TEST(expand_aliases_recursive_depth_limit);
    RUN_TEST(expand_aliases_recursive_circular);

    printf("\nFirst Word Expansion Tests:\n");
    RUN_TEST(expand_first_word_alias_basic);
    RUN_TEST(expand_first_word_alias_no_alias);
    RUN_TEST(expand_first_word_alias_only_first);

    printf("\nShell Operator Handling Tests:\n");
    RUN_TEST(contains_shell_operators_pipe);
    RUN_TEST(contains_shell_operators_redirect_out);
    RUN_TEST(contains_shell_operators_redirect_in);
    RUN_TEST(contains_shell_operators_append);
    RUN_TEST(contains_shell_operators_semicolon);
    RUN_TEST(contains_shell_operators_ampersand);
    RUN_TEST(contains_shell_operators_and);
    RUN_TEST(contains_shell_operators_or);
    RUN_TEST(contains_shell_operators_none);
    RUN_TEST(is_special_alias_char_valid_chars);
    RUN_TEST(is_special_alias_char_invalid_chars);

    printf("\nExpand With Shell Operators Tests:\n");
    RUN_TEST(expand_alias_with_shell_operators_simple);
    RUN_TEST(expand_alias_with_shell_operators_pipe);
    RUN_TEST(expand_alias_with_shell_operators_in_value);

    printf("\nUsage Functions Tests:\n");
    RUN_TEST(alias_usage);
    RUN_TEST(unalias_usage);

    printf("\nEdge Cases and Stress Tests:\n");
    RUN_TEST(alias_with_quotes);
    RUN_TEST(alias_with_variables);
    RUN_TEST(alias_empty_value);
    RUN_TEST(bin_alias_double_dash_ends_options);
    RUN_TEST(bin_alias_digit_name);
    RUN_TEST(many_aliases);

    printf("\nGlobal Alias (-g) Tests:\n");
    RUN_TEST(bin_alias_dash_g_set_and_lookup_global);
    RUN_TEST(bin_alias_dash_g_lookup_undefined_returns_error);
    RUN_TEST(bin_alias_invalid_flag_returns_error);
    RUN_TEST(bin_alias_dash_g_then_double_dash);
    RUN_TEST(set_global_alias_rejects_null_inputs);
    RUN_TEST(lookup_global_alias_uninitialized_returns_null);

    return TEST_RESULT();
}
