/**
 * @file test_glob_sort.c
 * @brief Regression tests: pathname expansion sorts its matches.
 *
 * POSIX requires the results of pathname (glob) expansion to be sorted
 * according to the current LC_COLLATE; bash and zsh do the same. lush
 * previously passed GLOB_NOSORT to glob(), so matches came back in raw
 * readdir order, which is filesystem-dependent: the same script produced
 * different output on different systems and intermittently broke order
 * sensitive tests.
 *
 * Each test creates its files in reverse order so a sorted result is
 * provably different from creation/readdir order. The assertions are
 * therefore independent of the underlying directory iteration order.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "test_framework.h"
#include "test_shell_harness.h"

#include <stdio.h>

/// Make an isolated temp dir, create files in reverse order, cd into it,
/// and clean up on exit. Files are touched 6 then 5 (and z..x) so the
/// creation order is the opposite of the expected sorted order.
#define GLOB_SORT_PREAMBLE                                                     \
    "DIR=/tmp/lush_test_globsort_$$ && mkdir -p \"$DIR\" && "                  \
    "touch \"$DIR/6.txt\" \"$DIR/5.txt\" && "                                  \
    "trap 'rm -rf \"$DIR\"' EXIT && "                                          \
    "cd \"$DIR\" && "

#define GLOB_SORT_PREAMBLE_LETTERS                                             \
    "DIR=/tmp/lush_test_globsort_$$ && mkdir -p \"$DIR\" && "                  \
    "touch \"$DIR/zebra\" \"$DIR/mango\" \"$DIR/apple\" && "                   \
    "trap 'rm -rf \"$DIR\"' EXIT && "                                          \
    "cd \"$DIR\" && "

TEST(star_glob_is_sorted) {
    /// echo * -> matches sorted ascending regardless of creation order.
    run_result_t r = run_shell_subprocess(GLOB_SORT_PREAMBLE "echo *.txt");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5.txt 6.txt\n");
}

TEST(range_glob_is_sorted) {
    /// echo [0-9].txt -> sorted ascending.
    run_result_t r = run_shell_subprocess(GLOB_SORT_PREAMBLE "echo [0-9].txt");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5.txt 6.txt\n");
}

TEST(letter_glob_is_sorted) {
    /// echo * over alphabetic names created in non-sorted order.
    run_result_t r = run_shell_subprocess(GLOB_SORT_PREAMBLE_LETTERS "echo *");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "apple mango zebra\n");
}

TEST(for_loop_glob_is_sorted) {
    /// for-loop word-list expansion sees the same sorted order.
    run_result_t r = run_shell_subprocess(
        GLOB_SORT_PREAMBLE "for f in *.txt; do echo \"$f\"; done");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5.txt\n6.txt\n");
}

int main(void) {
    init_symtable();

    printf("=== pathname expansion sort-order regression tests ===\n\n");
    RUN_TEST(star_glob_is_sorted);
    RUN_TEST(range_glob_is_sorted);
    RUN_TEST(letter_glob_is_sorted);
    RUN_TEST(for_loop_glob_is_sorted);

    return TEST_RESULT();
}
