/**
 * @file test_lle_pager_present.c
 * @brief Unit tests for lle_pager_present non-pager fallback paths
 *
 * The interactive pager path requires a pty and key injection that
 * unit tests cannot reasonably provide. These tests exercise the
 * decision branches that resolve to a direct write before any pager
 * machinery activates: NULL/empty input, non-tty stdout, and the
 * trailing-newline guarantee. The interactive path is covered by
 * the existing pager_input / pager_layer / pager_render tests for
 * their respective subsystems; this file gates the public-entry
 * contract.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/lle_pager.h"
#include "test_framework.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * stdout-capture helper
 * ============================================================================
 *
 * Redirects STDOUT_FILENO to a pipe, runs `fn(content)`, then reads
 * the pipe back into a heap buffer. The pipe write end is closed
 * before reading so read() returns EOF after the data. Returns the
 * captured bytes (caller frees) and stores the byte count in *out_len.
 */
static char *capture_stdout(int (*fn)(struct executor *, const char *),
                            const char *content, size_t *out_len) {
    int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0) {
        return NULL;
    }
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        close(saved_stdout);
        return NULL;
    }
    if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        close(saved_stdout);
        return NULL;
    }
    close(pipefd[1]);

    fn(NULL, content);

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    size_t cap = 4096;
    char *buf = malloc(cap);
    size_t len = 0;
    if (!buf) {
        close(pipefd[0]);
        return NULL;
    }
    for (;;) {
        if (len + 512 >= cap) {
            cap *= 2;
            char *bigger = realloc(buf, cap);
            if (!bigger) {
                free(buf);
                close(pipefd[0]);
                return NULL;
            }
            buf = bigger;
        }
        ssize_t n = read(pipefd[0], buf + len, cap - len - 1);
        if (n <= 0) {
            break;
        }
        len += (size_t)n;
    }
    close(pipefd[0]);
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

/* ============================================================================
 * Empty-input branches
 * ============================================================================
 */

TEST(null_content_is_noop_success) {
    size_t len = 999;
    char *out = capture_stdout(lle_pager_present, NULL, &len);
    ASSERT(out != NULL, "capture helper allocated");
    ASSERT_EQ(len, 0, "NULL content writes nothing");
    free(out);
}

TEST(empty_content_is_noop_success) {
    size_t len = 999;
    char *out = capture_stdout(lle_pager_present, "", &len);
    ASSERT(out != NULL, "capture helper allocated");
    ASSERT_EQ(len, 0, "empty string writes nothing");
    free(out);
}

/* ============================================================================
 * Non-tty direct-write branch (stdout is a pipe in this test)
 * ============================================================================
 */

TEST(short_content_writes_verbatim_with_newline) {
    size_t len = 0;
    char *out = capture_stdout(lle_pager_present, "hello", &len);
    ASSERT(out != NULL, "capture helper allocated");
    ASSERT_STR_EQ(out, "hello\n",
                  "short content writes content + trailing newline");
    free(out);
}

TEST(content_with_existing_newline_not_doubled) {
    size_t len = 0;
    char *out = capture_stdout(lle_pager_present, "hello\n", &len);
    ASSERT(out != NULL, "capture helper allocated");
    ASSERT_STR_EQ(out, "hello\n", "trailing newline is not duplicated");
    free(out);
}

TEST(multi_line_content_preserved_verbatim) {
    size_t len = 0;
    char *out =
        capture_stdout(lle_pager_present, "line1\nline2\nline3\n", &len);
    ASSERT(out != NULL, "capture helper allocated");
    ASSERT_STR_EQ(out, "line1\nline2\nline3\n",
                  "multi-line content writes through verbatim");
    free(out);
}

TEST(content_without_newline_gets_one_appended) {
    size_t len = 0;
    char *out = capture_stdout(lle_pager_present, "no-newline-at-end", &len);
    ASSERT(out != NULL, "capture helper allocated");
    ASSERT_STR_EQ(out, "no-newline-at-end\n",
                  "missing trailing newline is added");
    free(out);
}

TEST(very_long_content_writes_in_full) {
    /// 50 lines * 32 bytes ~ 1.6 KiB; larger than any single write() and
    /// larger than typical screen height so the non-tty path is the
    /// only branch that handles it.
    char buf[2048];
    size_t pos = 0;
    for (int i = 0; i < 50; i++) {
        int n =
            snprintf(buf + pos, sizeof(buf) - pos, "line-%02d-padding\n", i);
        if (n <= 0) {
            break;
        }
        pos += (size_t)n;
    }
    size_t len = 0;
    char *out = capture_stdout(lle_pager_present, buf, &len);
    ASSERT(out != NULL, "capture helper allocated");
    ASSERT_EQ(len, pos, "all bytes written");
    ASSERT(memcmp(out, buf, pos) == 0, "content bytes match input");
    free(out);
}

/* ============================================================================
 * Driver
 * ============================================================================
 */

int main(void) {
    printf("Running lle_pager_present tests...\n\n");

    printf("Empty-input branches:\n");
    RUN_TEST(null_content_is_noop_success);
    RUN_TEST(empty_content_is_noop_success);

    printf("\nNon-tty direct write:\n");
    RUN_TEST(short_content_writes_verbatim_with_newline);
    RUN_TEST(content_with_existing_newline_not_doubled);
    RUN_TEST(multi_line_content_preserved_verbatim);
    RUN_TEST(content_without_newline_gets_one_appended);
    RUN_TEST(very_long_content_writes_in_full);

    return TEST_RESULT();
}
