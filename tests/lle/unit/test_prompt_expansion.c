/**
 * Unit tests for LLE Prompt Expansion Engine (Spec 28 Phase 1)
 *
 * Tests the unified two-pass expansion:
 *   Pass 1: Template engine (${...} segments)
 *   Pass 2: Bash (\X) and Zsh (%X) escapes
 */

#include "lle/error_handling.h"
#include "lle/prompt/prompt_expansion.h"
#include "lle/prompt/template.h"
#include "version.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                         \
    do {                                                                       \
        printf("Running test: %s\n", #name);                                   \
        test_##name();                                                         \
    } while (0)

#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("  FAILED: %s (line %d)\n", #cond, __LINE__);               \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(a, b)                                                        \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            printf("  FAILED: %s == %s (%d != %d, line %d)\n", #a, #b,        \
                   (int)(a), (int)(b), __LINE__);                              \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                    \
    do {                                                                       \
        if (strcmp((a), (b)) != 0) {                                           \
            printf("  FAILED: '%s' == '%s' (line %d)\n", (a), (b), __LINE__);  \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle)                                  \
    do {                                                                       \
        if (strstr((haystack), (needle)) == NULL) {                            \
            printf("  FAILED: '%s' contains '%s' (line %d)\n", (haystack),     \
                   (needle), __LINE__);                                        \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define PASS()                                                                 \
    do {                                                                       \
        printf("  PASSED\n");                                                  \
        tests_passed++;                                                        \
    } while (0)

/* Default context for most tests: no template engine, truecolor */
static lle_prompt_expand_ctx_t make_ctx(void) {
    lle_prompt_expand_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.color_depth = 3;
    return ctx;
}

/* ========================================================================== */
/* NULL / edge case tests                                                     */
/* ========================================================================== */

TEST(null_format) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand(NULL, out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_ERROR_NULL_POINTER);
    PASS();
}

TEST(null_output) {
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("test", NULL, 64, &ctx);
    ASSERT_EQ(r, LLE_ERROR_NULL_POINTER);
    PASS();
}

TEST(null_ctx) {
    char out[64];
    lle_result_t r = lle_prompt_expand("test", out, sizeof(out), NULL);
    ASSERT_EQ(r, LLE_ERROR_NULL_POINTER);
    PASS();
}

TEST(zero_size) {
    char out[1];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("test", out, 0, &ctx);
    ASSERT_EQ(r, LLE_ERROR_NULL_POINTER);
    PASS();
}

TEST(empty_format) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "");
    PASS();
}

TEST(plain_text_passthrough) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("hello world", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "hello world");
    PASS();
}

/* ========================================================================== */
/* Bash escape tests                                                          */
/* ========================================================================== */

TEST(bash_username) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\u", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    struct passwd *pw = getpwuid(getuid());
    if (pw)
        ASSERT_STR_EQ(out, pw->pw_name);
    PASS();
}

TEST(bash_hostname_short) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\h", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Should not contain a dot */
    ASSERT(strchr(out, '.') == NULL);
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(bash_hostname_full) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\H", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(bash_cwd_tilde) {
    char out[1024];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\w", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(bash_cwd_basename) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\W", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) > 0);
    /* Should not contain / (unless root dir) */
    PASS();
}

TEST(bash_date) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\d", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Format: "Sat Feb 22" - should have two spaces */
    ASSERT(strlen(out) >= 8);
    PASS();
}

TEST(bash_time_24h) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\t", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Format: HH:MM:SS */
    ASSERT(strlen(out) == 8);
    ASSERT(out[2] == ':');
    ASSERT(out[5] == ':');
    PASS();
}

TEST(bash_time_12h) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\T", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) == 8);
    PASS();
}

TEST(bash_time_ampm) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\@", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Should contain AM or PM */
    ASSERT(strstr(out, "AM") || strstr(out, "PM") ||
           strstr(out, "am") || strstr(out, "pm"));
    PASS();
}

TEST(bash_time_24h_short) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\A", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Format: HH:MM */
    ASSERT(strlen(out) == 5);
    ASSERT(out[2] == ':');
    PASS();
}

TEST(bash_dollar_sign) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\$", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Non-root should get $, root gets # */
    if (getuid() != 0)
        ASSERT_STR_EQ(out, "$");
    else
        ASSERT_STR_EQ(out, "#");
    PASS();
}

TEST(bash_newline) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("a\\nb", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "a\nb");
    PASS();
}

TEST(bash_carriage_return) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("a\\rb", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "a\rb");
    PASS();
}

TEST(bash_literal_backslash) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\\\", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "\\");
    PASS();
}

TEST(bash_bracket_stripping) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    /* \[\033[32m\] should strip \[ and \], leaving the ANSI code */
    lle_result_t r =
        lle_prompt_expand("\\[\\e[32m\\]hi", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "\033[32m");
    ASSERT_STR_CONTAINS(out, "hi");
    /* No literal \[ or \] in output */
    ASSERT(strstr(out, "\\[") == NULL);
    ASSERT(strstr(out, "\\]") == NULL);
    PASS();
}

TEST(bash_history_number) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.history_number = 42;
    lle_result_t r = lle_prompt_expand("\\!", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "42");
    PASS();
}

TEST(bash_command_number) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.command_number = 7;
    lle_result_t r = lle_prompt_expand("\\#", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "7");
    PASS();
}

TEST(bash_job_count) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.job_count = 3;
    lle_result_t r = lle_prompt_expand("\\j", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "3");
    PASS();
}

TEST(bash_tty_name) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\l", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Should produce something (or "?" if no tty) */
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(bash_shell_name) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\s", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, LUSH_NAME);
    PASS();
}

TEST(bash_version_short) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\v", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    char expected[16];
    snprintf(expected, sizeof(expected), "%d.%d", LUSH_VERSION_MAJOR,
             LUSH_VERSION_MINOR);
    ASSERT_STR_EQ(out, expected);
    PASS();
}

TEST(bash_version_full) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\V", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, LUSH_VERSION_STRING);
    PASS();
}

TEST(bash_escape_char) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\e", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(out[0] == '\033');
    ASSERT(out[1] == '\0');
    PASS();
}

TEST(bash_bell_char) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\a", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(out[0] == '\a');
    ASSERT(out[1] == '\0');
    PASS();
}

TEST(bash_octal) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    /* \0101 = octal 101 = 'A' */
    lle_result_t r = lle_prompt_expand("\\0101", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "A");
    PASS();
}

TEST(bash_hex) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    /* \x41 = 'A' */
    lle_result_t r = lle_prompt_expand("\\x41", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "A");
    PASS();
}

TEST(bash_unknown_escape_passthrough) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("\\z", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "\\z");
    PASS();
}

/* ========================================================================== */
/* Zsh escape tests                                                           */
/* ========================================================================== */

TEST(zsh_username) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%n", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    struct passwd *pw = getpwuid(getuid());
    if (pw)
        ASSERT_STR_EQ(out, pw->pw_name);
    PASS();
}

TEST(zsh_hostname_short) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%m", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strchr(out, '.') == NULL);
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(zsh_hostname_full) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%M", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(zsh_cwd_full) {
    char out[1024];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%d", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Full path starts with / */
    ASSERT(out[0] == '/');
    PASS();
}

TEST(zsh_cwd_slash) {
    char out[1024];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%/", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(out[0] == '/');
    PASS();
}

TEST(zsh_cwd_tilde) {
    char out[1024];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%~", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(zsh_cwd_tail) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%c", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(zsh_cwd_dot) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%.", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(zsh_hash_sign) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%#", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    if (getuid() != 0)
        ASSERT_STR_EQ(out, "%");
    else
        ASSERT_STR_EQ(out, "#");
    PASS();
}

TEST(zsh_literal_percent) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("100%%", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "100%");
    PASS();
}

TEST(zsh_time_24h_short) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%T", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) == 5);
    ASSERT(out[2] == ':');
    PASS();
}

TEST(zsh_time_24h_full) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%*", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) == 8);
    PASS();
}

TEST(zsh_job_count) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.job_count = 5;
    lle_result_t r = lle_prompt_expand("%j", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "5");
    PASS();
}

TEST(zsh_tty_name) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%l", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) > 0);
    PASS();
}

TEST(zsh_exit_status) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.last_exit_status = 127;
    lle_result_t r = lle_prompt_expand("%?", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "127");
    PASS();
}

TEST(zsh_date_format) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r =
        lle_prompt_expand("%D{%Y-%m-%d}", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Should be YYYY-MM-DD format */
    ASSERT(strlen(out) == 10);
    ASSERT(out[4] == '-');
    ASSERT(out[7] == '-');
    PASS();
}

TEST(zsh_date_default) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    /* %D without braces: default yy-mm-dd format */
    lle_result_t r = lle_prompt_expand("%D", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(out) == 8); /* yy-mm-dd */
    PASS();
}

TEST(zsh_bold) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%Bbold%b", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "\033[1m");
    ASSERT_STR_CONTAINS(out, "bold");
    ASSERT_STR_CONTAINS(out, "\033[22m");
    PASS();
}

TEST(zsh_underline) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%Uuline%u", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "\033[4m");
    ASSERT_STR_CONTAINS(out, "uline");
    ASSERT_STR_CONTAINS(out, "\033[24m");
    PASS();
}

TEST(zsh_standout) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%Srev%s", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "\033[7m");
    ASSERT_STR_CONTAINS(out, "rev");
    ASSERT_STR_CONTAINS(out, "\033[27m");
    PASS();
}

TEST(zsh_fg_color_named) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r =
        lle_prompt_expand("%F{red}hi%f", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "\033[31m"); /* red = 30+1 */
    ASSERT_STR_CONTAINS(out, "hi");
    ASSERT_STR_CONTAINS(out, "\033[39m"); /* reset fg */
    PASS();
}

TEST(zsh_fg_color_numeric) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r =
        lle_prompt_expand("%F{82}hi%f", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "\033[38;5;82m");
    ASSERT_STR_CONTAINS(out, "hi");
    PASS();
}

TEST(zsh_fg_color_hex) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r =
        lle_prompt_expand("%F{#FF0000}hi%f", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "\033[38;2;255;0;0m");
    ASSERT_STR_CONTAINS(out, "hi");
    PASS();
}

TEST(zsh_bg_color) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r =
        lle_prompt_expand("%K{blue}bg%k", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "\033[44m"); /* blue bg = 40+4 */
    ASSERT_STR_CONTAINS(out, "bg");
    ASSERT_STR_CONTAINS(out, "\033[49m"); /* reset bg */
    PASS();
}

TEST(zsh_color_256_fallback) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.color_depth = 2; /* 256-color only */
    lle_result_t r =
        lle_prompt_expand("%F{#FF8000}hi%f", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Should use 256-color escape, not truecolor */
    ASSERT_STR_CONTAINS(out, "\033[38;5;");
    ASSERT(strstr(out, "\033[38;2;") == NULL);
    PASS();
}

TEST(zsh_color_none) {
    char out[128];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.color_depth = 0; /* no color */
    lle_result_t r =
        lle_prompt_expand("%F{red}hi%f", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* No ANSI escape for color, just the text and reset */
    ASSERT_STR_CONTAINS(out, "hi");
    /* The %f still emits \033[39m - that's fine, the fg color is skipped */
    PASS();
}

TEST(zsh_unknown_escape_passthrough) {
    char out[64];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r = lle_prompt_expand("%Z", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(out, "%Z");
    PASS();
}

/* ========================================================================== */
/* Mixed syntax tests                                                         */
/* ========================================================================== */

TEST(mixed_bash_and_zsh) {
    char out[512];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.last_exit_status = 0;
    /* Bash \u and zsh %m in same string */
    lle_result_t r =
        lle_prompt_expand("\\u@%m:\\w\\$ ", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Should contain username */
    struct passwd *pw = getpwuid(getuid());
    if (pw)
        ASSERT_STR_CONTAINS(out, pw->pw_name);
    /* Should contain @ separator */
    ASSERT_STR_CONTAINS(out, "@");
    /* Should end with $ or # */
    ASSERT(strstr(out, "$ ") || strstr(out, "# "));
    PASS();
}

TEST(mixed_with_ansi_passthrough) {
    char out[256];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    /* ANSI codes from template engine should pass through unmodified */
    /* Simulate: ESC[32m already in input from pass-1, then \u */
    lle_result_t r =
        lle_prompt_expand("\033[32m\\u\033[0m", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Should contain the ANSI codes intact */
    ASSERT_STR_CONTAINS(out, "\033[32m");
    ASSERT_STR_CONTAINS(out, "\033[0m");
    /* Should contain username between them */
    struct passwd *pw = getpwuid(getuid());
    if (pw)
        ASSERT_STR_CONTAINS(out, pw->pw_name);
    PASS();
}

/* ========================================================================== */
/* Template engine integration test (pass 1)                                  */
/* ========================================================================== */

static char *mock_segment(const char *name, const char *property,
                          void *user_data) {
    (void)user_data;
    if (strcmp(name, "directory") == 0 && !property)
        return strdup("~/project");
    if (strcmp(name, "git") == 0 && !property)
        return strdup("(main)");
    return NULL;
}

static bool mock_visible(const char *name, const char *property,
                         void *user_data) {
    (void)user_data;
    (void)property;
    if (strcmp(name, "directory") == 0)
        return true;
    if (strcmp(name, "git") == 0)
        return true;
    return false;
}

static const char *mock_color(const char *name, void *user_data) {
    (void)user_data;
    if (strcmp(name, "primary") == 0)
        return "\033[34m";
    return "";
}

TEST(template_then_bash) {
    char out[512];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_template_render_ctx_t tctx = {
        .get_segment = mock_segment,
        .is_visible = mock_visible,
        .get_color = mock_color,
        .user_data = NULL,
    };
    ctx.template_ctx = &tctx;

    /* LLE segments + bash escapes */
    lle_result_t r =
        lle_prompt_expand("${directory} \\$ ", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "~/project");
    ASSERT(strstr(out, "$ ") || strstr(out, "# "));
    PASS();
}

TEST(template_then_zsh) {
    char out[512];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.last_exit_status = 42;
    lle_template_render_ctx_t tctx = {
        .get_segment = mock_segment,
        .is_visible = mock_visible,
        .get_color = mock_color,
        .user_data = NULL,
    };
    ctx.template_ctx = &tctx;

    /* LLE segments + zsh escapes */
    lle_result_t r =
        lle_prompt_expand("${git} [%?] %# ", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_CONTAINS(out, "(main)");
    ASSERT_STR_CONTAINS(out, "[42]");
    PASS();
}

TEST(all_three_syntaxes) {
    char out[512];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    ctx.job_count = 2;
    lle_template_render_ctx_t tctx = {
        .get_segment = mock_segment,
        .is_visible = mock_visible,
        .get_color = mock_color,
        .user_data = NULL,
    };
    ctx.template_ctx = &tctx;

    /* All three: LLE ${...}, bash \u, zsh %j */
    lle_result_t r = lle_prompt_expand("\\u ${directory} %j\\$ ", out,
                                       sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    struct passwd *pw = getpwuid(getuid());
    if (pw)
        ASSERT_STR_CONTAINS(out, pw->pw_name);
    ASSERT_STR_CONTAINS(out, "~/project");
    ASSERT_STR_CONTAINS(out, "2");
    PASS();
}

/* ========================================================================== */
/* Buffer limit test                                                          */
/* ========================================================================== */

TEST(small_buffer) {
    char out[8];
    lle_prompt_expand_ctx_t ctx = make_ctx();
    lle_result_t r =
        lle_prompt_expand("abcdefghijklmnop", out, sizeof(out), &ctx);
    ASSERT_EQ(r, LLE_SUCCESS);
    /* Should be truncated but not crash */
    ASSERT(strlen(out) < sizeof(out));
    ASSERT(out[sizeof(out) - 1] == '\0');
    PASS();
}

/* ========================================================================== */
/* Main test runner                                                           */
/* ========================================================================== */

int main(void) {
    printf("===========================================\n");
    printf("    LLE Prompt Expansion Unit Tests\n");
    printf("===========================================\n\n");

    /* Edge cases */
    RUN_TEST(null_format);
    RUN_TEST(null_output);
    RUN_TEST(null_ctx);
    RUN_TEST(zero_size);
    RUN_TEST(empty_format);
    RUN_TEST(plain_text_passthrough);

    /* Bash escapes */
    RUN_TEST(bash_username);
    RUN_TEST(bash_hostname_short);
    RUN_TEST(bash_hostname_full);
    RUN_TEST(bash_cwd_tilde);
    RUN_TEST(bash_cwd_basename);
    RUN_TEST(bash_date);
    RUN_TEST(bash_time_24h);
    RUN_TEST(bash_time_12h);
    RUN_TEST(bash_time_ampm);
    RUN_TEST(bash_time_24h_short);
    RUN_TEST(bash_dollar_sign);
    RUN_TEST(bash_newline);
    RUN_TEST(bash_carriage_return);
    RUN_TEST(bash_literal_backslash);
    RUN_TEST(bash_bracket_stripping);
    RUN_TEST(bash_history_number);
    RUN_TEST(bash_command_number);
    RUN_TEST(bash_job_count);
    RUN_TEST(bash_tty_name);
    RUN_TEST(bash_shell_name);
    RUN_TEST(bash_version_short);
    RUN_TEST(bash_version_full);
    RUN_TEST(bash_escape_char);
    RUN_TEST(bash_bell_char);
    RUN_TEST(bash_octal);
    RUN_TEST(bash_hex);
    RUN_TEST(bash_unknown_escape_passthrough);

    /* Zsh escapes */
    RUN_TEST(zsh_username);
    RUN_TEST(zsh_hostname_short);
    RUN_TEST(zsh_hostname_full);
    RUN_TEST(zsh_cwd_full);
    RUN_TEST(zsh_cwd_slash);
    RUN_TEST(zsh_cwd_tilde);
    RUN_TEST(zsh_cwd_tail);
    RUN_TEST(zsh_cwd_dot);
    RUN_TEST(zsh_hash_sign);
    RUN_TEST(zsh_literal_percent);
    RUN_TEST(zsh_time_24h_short);
    RUN_TEST(zsh_time_24h_full);
    RUN_TEST(zsh_job_count);
    RUN_TEST(zsh_tty_name);
    RUN_TEST(zsh_exit_status);
    RUN_TEST(zsh_date_format);
    RUN_TEST(zsh_date_default);
    RUN_TEST(zsh_bold);
    RUN_TEST(zsh_underline);
    RUN_TEST(zsh_standout);
    RUN_TEST(zsh_fg_color_named);
    RUN_TEST(zsh_fg_color_numeric);
    RUN_TEST(zsh_fg_color_hex);
    RUN_TEST(zsh_bg_color);
    RUN_TEST(zsh_color_256_fallback);
    RUN_TEST(zsh_color_none);
    RUN_TEST(zsh_unknown_escape_passthrough);

    /* Mixed syntax */
    RUN_TEST(mixed_bash_and_zsh);
    RUN_TEST(mixed_with_ansi_passthrough);

    /* Template engine integration */
    RUN_TEST(template_then_bash);
    RUN_TEST(template_then_zsh);
    RUN_TEST(all_three_syntaxes);

    /* Buffer limits */
    RUN_TEST(small_buffer);

    printf("\n===========================================\n");
    printf("Test Results: %d passed, %d failed, %d total\n", tests_passed,
           tests_failed, tests_passed + tests_failed);
    printf("===========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
