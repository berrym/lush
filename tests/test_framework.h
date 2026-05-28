/**
 * @file test_framework.h
 * @brief Shared unit-test framework for lush
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Header-only test framework. Each test binary should:
 *   #include "test_framework.h"
 *   TEST(my_test) { ASSERT_EQ(foo(), 42, "foo should return 42"); }
 *   int main(void) {
 *       printf("=== My Suite ===\n");
 *       RUN_TEST(my_test);
 *       return TEST_RESULT();
 *   }
 *
 * Why setjmp/longjmp:
 *   The historical project pattern of calling exit(1) on the first
 *   failed assertion meant a single bug masked every later test in the
 *   binary, so CI runs reported one bug at a time and developers had
 *   to fix-and-rerun N times to see N bugs. This framework isolates
 *   failures to the TEST that contained them: a failed ASSERT in
 *   TEST X marks X as FAIL and resumes at the next RUN_TEST, so one
 *   run reports every failure across the binary.
 *
 * Argument convention:
 *   Binary assertions use (actual, expected, message). The historical
 *   inconsistency where some macros took (expected, actual) and others
 *   took (actual, expected) is the kind of foundational confusion this
 *   framework removes — pick a convention and stick to it.
 *
 * Why header-only:
 *   Avoids a separate library that every test binary would have to
 *   link. The static counters and jmp_buf live in each test binary's
 *   main translation unit (the one that calls RUN_TEST). Stubs files
 *   that #include this header but never call RUN_TEST get unused
 *   storage, which is harmless.
 */

#ifndef LUSH_TEST_FRAMEWORK_H
#define LUSH_TEST_FRAMEWORK_H

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Internal state — file-scope so each test binary has its own copy.
 * ============================================================================
 */

static int test_framework_run = 0;
static int test_framework_passed = 0;
static int test_framework_failed = 0;
static jmp_buf test_framework_jmpbuf;
static const char *test_framework_current_name = "(none)";

/* ============================================================================
 * Test definition and execution
 * ============================================================================
 */

/** @brief Define a test function */
#define TEST(name) static void test_##name(void)

/**
 * @brief Run a test function with failure isolation
 *
 * On a failed ASSERT, longjmp returns control here, the test is
 * marked failed, and execution proceeds to the next RUN_TEST.
 */
#define RUN_TEST(name)                                                         \
    do {                                                                       \
        test_framework_run++;                                                  \
        test_framework_current_name = #name;                                   \
        printf("  %s ... ", #name);                                            \
        fflush(stdout);                                                        \
        if (setjmp(test_framework_jmpbuf) == 0) {                              \
            test_##name();                                                     \
            test_framework_passed++;                                           \
            printf("PASS\n");                                                  \
        } else {                                                               \
            test_framework_failed++;                                           \
            /* FAIL line was already printed by the failing ASSERT */          \
        }                                                                      \
    } while (0)

/** @brief Print summary and return process exit status */
#define TEST_RESULT()                                                          \
    (printf("\n--- Results: %d passed, %d failed (of %d) ---\n",               \
            test_framework_passed, test_framework_failed, test_framework_run), \
     test_framework_failed > 0 ? 1 : 0)

/* ============================================================================
 * Assertion primitives
 *
 * On failure: print "FAIL\n  at file:line: <expression> [: msg]" and
 * longjmp back to RUN_TEST. The next RUN_TEST resumes execution.
 * ============================================================================
 */

#define TEST_FAIL_FMT(fmt, ...)                                                \
    do {                                                                       \
        printf("FAIL\n  at %s:%d: " fmt "\n", __FILE__, __LINE__,              \
               __VA_ARGS__);                                                   \
        longjmp(test_framework_jmpbuf, 1);                                     \
    } while (0)

#define TEST_FAIL_MSG(msg)                                                     \
    do {                                                                       \
        printf("FAIL\n  at %s:%d: %s\n", __FILE__, __LINE__, (msg));           \
        longjmp(test_framework_jmpbuf, 1);                                     \
    } while (0)

/** @brief Assert a boolean condition (no message variant) */
#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_MSG(#cond);                                              \
        }                                                                      \
    } while (0)

/** @brief Assert true with a descriptive message */
#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_FMT("expected true: %s (%s)", #cond, (msg));             \
        }                                                                      \
    } while (0)

/** @brief Assert false with a descriptive message */
#define ASSERT_FALSE(cond, msg)                                                \
    do {                                                                       \
        if ((cond)) {                                                          \
            TEST_FAIL_FMT("expected false: %s (%s)", #cond, (msg));            \
        }                                                                      \
    } while (0)

/** @brief Assert equality (signed long long printable) */
#define ASSERT_EQ(actual, expected, msg)                                       \
    do {                                                                       \
        long long _a = (long long)(actual);                                    \
        long long _e = (long long)(expected);                                  \
        if (_a != _e) {                                                        \
            TEST_FAIL_FMT("%s: expected %lld, got %lld", (msg), _e, _a);       \
        }                                                                      \
    } while (0)

/** @brief Assert inequality */
#define ASSERT_NE(actual, expected, msg)                                       \
    do {                                                                       \
        long long _a = (long long)(actual);                                    \
        long long _e = (long long)(expected);                                  \
        if (_a == _e) {                                                        \
            TEST_FAIL_FMT("%s: expected != %lld, got equal", (msg), _e);       \
        }                                                                      \
    } while (0)

/** @brief Assert less-than */
#define ASSERT_LT(actual, bound, msg)                                          \
    do {                                                                       \
        long long _a = (long long)(actual);                                    \
        long long _b = (long long)(bound);                                     \
        if (!(_a < _b)) {                                                      \
            TEST_FAIL_FMT("%s: expected %lld < %lld", (msg), _a, _b);          \
        }                                                                      \
    } while (0)

/** @brief Assert less-than-or-equal */
#define ASSERT_LE(actual, bound, msg)                                          \
    do {                                                                       \
        long long _a = (long long)(actual);                                    \
        long long _b = (long long)(bound);                                     \
        if (!(_a <= _b)) {                                                     \
            TEST_FAIL_FMT("%s: expected %lld <= %lld", (msg), _a, _b);         \
        }                                                                      \
    } while (0)

/** @brief Assert greater-than */
#define ASSERT_GT(actual, bound, msg)                                          \
    do {                                                                       \
        long long _a = (long long)(actual);                                    \
        long long _b = (long long)(bound);                                     \
        if (!(_a > _b)) {                                                      \
            TEST_FAIL_FMT("%s: expected %lld > %lld", (msg), _a, _b);          \
        }                                                                      \
    } while (0)

/** @brief Assert greater-than-or-equal */
#define ASSERT_GE(actual, bound, msg)                                          \
    do {                                                                       \
        long long _a = (long long)(actual);                                    \
        long long _b = (long long)(bound);                                     \
        if (!(_a >= _b)) {                                                     \
            TEST_FAIL_FMT("%s: expected %lld >= %lld", (msg), _a, _b);         \
        }                                                                      \
    } while (0)

/** @brief Assert pointer is NULL */
#define ASSERT_NULL(ptr, msg)                                                  \
    do {                                                                       \
        if ((ptr) != NULL) {                                                   \
            TEST_FAIL_FMT("%s: expected NULL, got non-NULL pointer", (msg));   \
        }                                                                      \
    } while (0)

/** @brief Assert pointer is non-NULL */
#define ASSERT_NOT_NULL(ptr, msg)                                              \
    do {                                                                       \
        if ((ptr) == NULL) {                                                   \
            TEST_FAIL_FMT("%s: expected non-NULL, got NULL", (msg));           \
        }                                                                      \
    } while (0)

/** @brief Assert two strings are equal (NULL-safe) */
#define ASSERT_STR_EQ(actual, expected, msg)                                   \
    do {                                                                       \
        const char *_a = (actual);                                             \
        const char *_e = (expected);                                           \
        if (_a == NULL && _e == NULL) {                                        \
            /* both NULL — equal */                                            \
        } else if (_a == NULL || _e == NULL || strcmp(_a, _e) != 0) {          \
            TEST_FAIL_FMT("%s: expected \"%s\", got \"%s\"", (msg),            \
                          _e ? _e : "(NULL)", _a ? _a : "(NULL)");             \
        }                                                                      \
    } while (0)

/** @brief Assert two strings are NOT equal (NULL-safe) */
#define ASSERT_STR_NE(actual, expected, msg)                                   \
    do {                                                                       \
        const char *_a = (actual);                                             \
        const char *_e = (expected);                                           \
        if ((_a == NULL && _e == NULL) ||                                      \
            (_a != NULL && _e != NULL && strcmp(_a, _e) == 0)) {               \
            TEST_FAIL_FMT("%s: expected != \"%s\", got equal", (msg),          \
                          _e ? _e : "(NULL)");                                 \
        }                                                                      \
    } while (0)

#endif // LUSH_TEST_FRAMEWORK_H
