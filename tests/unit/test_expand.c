/**
 * @file test_expand.c
 * @brief Unit tests for word expansion module
 *
 * Tests the expansion context and flags including:
 * - Context initialization
 * - Mode flag checking
 * - Quote and backtick state
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "expand.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Test framework macros

/* ============================================================================
 * CONTEXT INITIALIZATION TESTS
 * ============================================================================
 */

TEST(expand_ctx_init_normal) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NORMAL);

    ASSERT_EQ(ctx.mode, EXPAND_NORMAL, "Mode should be NORMAL");
    ASSERT(!ctx.in_quotes, "Should not be in quotes initially");
    ASSERT(!ctx.in_backticks, "Should not be in backticks initially");
}

TEST(expand_ctx_init_alias) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_ALIAS);

    ASSERT_EQ(ctx.mode, EXPAND_ALIAS, "Mode should include ALIAS");
    ASSERT(!ctx.in_quotes, "Should not be in quotes");
    ASSERT(!ctx.in_backticks, "Should not be in backticks");
}

TEST(expand_ctx_init_noquote) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOQUOTE);

    ASSERT_EQ(ctx.mode, EXPAND_NOQUOTE, "Mode should include NOQUOTE");
}

TEST(expand_ctx_init_novar) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOVAR);

    ASSERT_EQ(ctx.mode, EXPAND_NOVAR, "Mode should include NOVAR");
}

TEST(expand_ctx_init_nocmd) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOCMD);

    ASSERT_EQ(ctx.mode, EXPAND_NOCMD, "Mode should include NOCMD");
}

TEST(expand_ctx_init_noglob) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOGLOB);

    ASSERT_EQ(ctx.mode, EXPAND_NOGLOB, "Mode should include NOGLOB");
}

TEST(expand_ctx_init_combined) {
    expand_ctx_t ctx;
    int mode = EXPAND_NOVAR | EXPAND_NOCMD;
    expand_ctx_init(&ctx, mode);

    ASSERT_EQ(ctx.mode, mode, "Mode should be combined flags");
}

TEST(expand_ctx_init_all_flags) {
    expand_ctx_t ctx;
    int mode = EXPAND_ALIAS | EXPAND_NOQUOTE | EXPAND_NOVAR | EXPAND_NOCMD |
               EXPAND_NOGLOB;
    expand_ctx_init(&ctx, mode);

    ASSERT_EQ(ctx.mode, mode, "Mode should have all flags");
}

/* ============================================================================
 * MODE FLAG CHECKING TESTS
 * ============================================================================
 */

TEST(expand_ctx_check_normal_has_nothing) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NORMAL);

    ASSERT(!expand_ctx_check(&ctx, EXPAND_ALIAS),
           "NORMAL should not have ALIAS");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOQUOTE),
           "NORMAL should not have NOQUOTE");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOVAR),
           "NORMAL should not have NOVAR");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOCMD),
           "NORMAL should not have NOCMD");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOGLOB),
           "NORMAL should not have NOGLOB");
}

TEST(expand_ctx_check_alias) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_ALIAS);

    ASSERT(expand_ctx_check(&ctx, EXPAND_ALIAS), "Should have ALIAS flag");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOQUOTE), "Should not have NOQUOTE");
}

TEST(expand_ctx_check_noquote) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOQUOTE);

    ASSERT(expand_ctx_check(&ctx, EXPAND_NOQUOTE), "Should have NOQUOTE flag");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_ALIAS), "Should not have ALIAS");
}

TEST(expand_ctx_check_novar) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOVAR);

    ASSERT(expand_ctx_check(&ctx, EXPAND_NOVAR), "Should have NOVAR flag");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOCMD), "Should not have NOCMD");
}

TEST(expand_ctx_check_nocmd) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOCMD);

    ASSERT(expand_ctx_check(&ctx, EXPAND_NOCMD), "Should have NOCMD flag");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOVAR), "Should not have NOVAR");
}

TEST(expand_ctx_check_noglob) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOGLOB);

    ASSERT(expand_ctx_check(&ctx, EXPAND_NOGLOB), "Should have NOGLOB flag");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_ALIAS), "Should not have ALIAS");
}

TEST(expand_ctx_check_combined) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOVAR | EXPAND_NOCMD);

    ASSERT(expand_ctx_check(&ctx, EXPAND_NOVAR), "Should have NOVAR");
    ASSERT(expand_ctx_check(&ctx, EXPAND_NOCMD), "Should have NOCMD");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOGLOB), "Should not have NOGLOB");
}

TEST(expand_ctx_check_all_flags) {
    expand_ctx_t ctx;
    int mode = EXPAND_ALIAS | EXPAND_NOQUOTE | EXPAND_NOVAR | EXPAND_NOCMD |
               EXPAND_NOGLOB;
    expand_ctx_init(&ctx, mode);

    ASSERT(expand_ctx_check(&ctx, EXPAND_ALIAS), "Should have ALIAS");
    ASSERT(expand_ctx_check(&ctx, EXPAND_NOQUOTE), "Should have NOQUOTE");
    ASSERT(expand_ctx_check(&ctx, EXPAND_NOVAR), "Should have NOVAR");
    ASSERT(expand_ctx_check(&ctx, EXPAND_NOCMD), "Should have NOCMD");
    ASSERT(expand_ctx_check(&ctx, EXPAND_NOGLOB), "Should have NOGLOB");
}

/* ============================================================================
 * QUOTE STATE TESTS
 * ============================================================================
 */

TEST(expand_ctx_quotes_initial) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NORMAL);

    ASSERT(!ctx.in_quotes, "Should not be in quotes initially");
}

/* ============================================================================
 * BACKTICK STATE TESTS
 * ============================================================================
 */

TEST(expand_ctx_backticks_initial) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NORMAL);

    ASSERT(!ctx.in_backticks, "Should not be in backticks initially");
}

TEST(expand_ctx_mode_with_quotes) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOVAR | EXPAND_NOCMD);

    ctx.in_quotes = true;

    ASSERT(expand_ctx_check(&ctx, EXPAND_NOVAR), "Should have NOVAR");
    ASSERT(ctx.in_quotes, "Should be in quotes");
}

/* ============================================================================
 * FLAG CONSTANT TESTS
 * ============================================================================
 */

TEST(expand_flags_orthogonal) {
    /// The expansion flags are OR-combined into a mode (see the init_combined
    /// tests), so each non-NORMAL flag must occupy a single distinct bit and
    /// no two flags may share a bit. NORMAL is the empty (zero) mode.
    ASSERT_EQ(EXPAND_NORMAL, 0, "NORMAL is the empty flag set");

    const int flags[] = {EXPAND_ALIAS, EXPAND_NOQUOTE, EXPAND_NOVAR,
                         EXPAND_NOCMD, EXPAND_NOGLOB};
    const size_t n = sizeof(flags) / sizeof(flags[0]);
    int combined = 0;
    for (size_t i = 0; i < n; i++) {
        /// Each flag is a single set bit (a power of two).
        ASSERT(flags[i] != 0 && (flags[i] & (flags[i] - 1)) == 0,
               "each flag is a single power-of-two bit");
        /// It does not collide with any flag already accumulated.
        ASSERT((flags[i] & combined) == 0, "flags occupy disjoint bits");
        combined |= flags[i];
    }
}

/* ============================================================================
 * EDGE CASES
 * ============================================================================
 */

TEST(expand_ctx_reinit) {
    expand_ctx_t ctx;

    /// First init
    expand_ctx_init(&ctx, EXPAND_ALIAS);
    ctx.in_quotes = true;
    ctx.in_backticks = true;

    /// Re-init should reset
    expand_ctx_init(&ctx, EXPAND_NOVAR);

    ASSERT_EQ(ctx.mode, EXPAND_NOVAR, "Mode should be new value");
    ASSERT(!ctx.in_quotes, "Quotes should be reset");
    ASSERT(!ctx.in_backticks, "Backticks should be reset");
}

TEST(expand_ctx_check_zero) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOVAR);

    /// Checking for flag 0 should always be false (nothing set)
    bool result = expand_ctx_check(&ctx, 0);
    ASSERT(!result, "Check for 0 should be false");
}

TEST(expand_ctx_multiple_contexts) {
    expand_ctx_t ctx1, ctx2;

    expand_ctx_init(&ctx1, EXPAND_ALIAS);
    expand_ctx_init(&ctx2, EXPAND_NOGLOB);

    ctx1.in_quotes = true;

    /// Contexts should be independent
    ASSERT(expand_ctx_check(&ctx1, EXPAND_ALIAS), "ctx1 should have ALIAS");
    ASSERT(!expand_ctx_check(&ctx2, EXPAND_ALIAS),
           "ctx2 should not have ALIAS");
    ASSERT(ctx1.in_quotes, "ctx1 should be in quotes");
    ASSERT(!ctx2.in_quotes, "ctx2 should not be in quotes");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running expand.c tests...\n\n");

    printf("Context Initialization Tests:\n");
    RUN_TEST(expand_ctx_init_normal);
    RUN_TEST(expand_ctx_init_alias);
    RUN_TEST(expand_ctx_init_noquote);
    RUN_TEST(expand_ctx_init_novar);
    RUN_TEST(expand_ctx_init_nocmd);
    RUN_TEST(expand_ctx_init_noglob);
    RUN_TEST(expand_ctx_init_combined);
    RUN_TEST(expand_ctx_init_all_flags);

    printf("\nMode Flag Checking Tests:\n");
    RUN_TEST(expand_ctx_check_normal_has_nothing);
    RUN_TEST(expand_ctx_check_alias);
    RUN_TEST(expand_ctx_check_noquote);
    RUN_TEST(expand_ctx_check_novar);
    RUN_TEST(expand_ctx_check_nocmd);
    RUN_TEST(expand_ctx_check_noglob);
    RUN_TEST(expand_ctx_check_combined);
    RUN_TEST(expand_ctx_check_all_flags);

    printf("\nQuote State Tests:\n");
    RUN_TEST(expand_ctx_quotes_initial);

    printf("\nBacktick State Tests:\n");
    RUN_TEST(expand_ctx_backticks_initial);

    printf("\nCombined State Tests:\n");
    RUN_TEST(expand_ctx_mode_with_quotes);

    printf("\nFlag Constant Tests:\n");
    RUN_TEST(expand_flags_orthogonal);

    printf("\nEdge Cases:\n");
    RUN_TEST(expand_ctx_reinit);
    RUN_TEST(expand_ctx_check_zero);
    RUN_TEST(expand_ctx_multiple_contexts);

    return TEST_RESULT();
}
