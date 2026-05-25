/**
 * @file test_pattern_match.c
 * @brief Unit tests for the shell pattern matcher
 *
 * Covers POSIX glob, bash extglob (?(...), *(...), +(...), @(...), !(...))
 * and zsh's bare-alternation form (...). Each test corresponds to a
 * concrete behavioural promise of the matcher; this is not coverage
 * theatre.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "pattern_match.h"
#include "test_framework.h"
#include <stdio.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * Plain glob
 * ============================================================================
 */

TEST(literal_match) { ASSERT(lush_pattern_match("hello", "hello"), "exact"); }

TEST(literal_mismatch) {
    ASSERT(!lush_pattern_match("hello", "hellox"), "trailing char");
    ASSERT(!lush_pattern_match("hello", "ello"), "missing prefix");
}

TEST(star_matches_anything) {
    ASSERT(lush_pattern_match("anything", "*"), "single star");
    ASSERT(lush_pattern_match("", "*"), "star matches empty");
    ASSERT(lush_pattern_match("abc", "a*c"), "star middle");
    ASSERT(lush_pattern_match("aXYc", "a*c"), "star middle non-empty");
    ASSERT(lush_pattern_match("ac", "a*c"), "star middle empty");
}

TEST(question_matches_one_char) {
    ASSERT(lush_pattern_match("a", "?"), "one char");
    ASSERT(!lush_pattern_match("", "?"), "empty does not match ?");
    ASSERT(!lush_pattern_match("ab", "?"), "two chars does not match ?");
    ASSERT(lush_pattern_match("abc", "a?c"), "middle question");
}

TEST(char_class_basic) {
    ASSERT(lush_pattern_match("a", "[abc]"), "a in class");
    ASSERT(lush_pattern_match("b", "[abc]"), "b in class");
    ASSERT(!lush_pattern_match("d", "[abc]"), "d not in class");
}

TEST(char_class_range) {
    ASSERT(lush_pattern_match("c", "[a-z]"), "lowercase in range");
    ASSERT(!lush_pattern_match("C", "[a-z]"), "uppercase not in lower range");
    ASSERT(lush_pattern_match("5", "[0-9]"), "digit in range");
}

TEST(char_class_negation) {
    ASSERT(!lush_pattern_match("a", "[!abc]"), "negated, in set");
    ASSERT(lush_pattern_match("d", "[!abc]"), "negated, out of set");
    ASSERT(lush_pattern_match("d", "[^abc]"), "caret negation alias");
}

TEST(escape_metacharacter) {
    ASSERT(lush_pattern_match("*", "\\*"), "literal star via escape");
    ASSERT(!lush_pattern_match("a", "\\*"), "escaped star is literal");
    ASSERT(lush_pattern_match("a*c", "a\\*c"), "embedded escaped star");
}

/* ============================================================================
 * Bash extglob
 * ============================================================================
 */

TEST(extglob_at_exact_one) {
    ASSERT(lush_pattern_match("abc", "@(abc|def)"), "first alt");
    ASSERT(lush_pattern_match("def", "@(abc|def)"), "second alt");
    ASSERT(!lush_pattern_match("xyz", "@(abc|def)"), "no alt matches");
    ASSERT(!lush_pattern_match("abcdef", "@(abc|def)"), "concatenation banned");
}

TEST(extglob_at_with_empty_alt) {
    ASSERT(lush_pattern_match("abc", "@(abc|)"), "non-empty alt");
    ASSERT(lush_pattern_match("", "@(abc|)"), "empty alt matches empty");
    ASSERT(!lush_pattern_match("ab", "@(abc|)"), "partial non-match");
}

TEST(extglob_question_zero_or_one) {
    ASSERT(lush_pattern_match("", "?(abc)"), "zero occurrences");
    ASSERT(lush_pattern_match("abc", "?(abc)"), "one occurrence");
    ASSERT(!lush_pattern_match("abcabc", "?(abc)"), "two occurrences banned");
}

TEST(extglob_star_zero_or_more) {
    ASSERT(lush_pattern_match("", "*(abc)"), "zero");
    ASSERT(lush_pattern_match("abc", "*(abc)"), "one");
    ASSERT(lush_pattern_match("abcabc", "*(abc)"), "two");
    ASSERT(lush_pattern_match("abcabcabc", "*(abc)"), "three");
    ASSERT(!lush_pattern_match("abcab", "*(abc)"), "trailing partial");
}

TEST(extglob_plus_one_or_more) {
    ASSERT(!lush_pattern_match("", "+(abc)"), "zero banned");
    ASSERT(lush_pattern_match("abc", "+(abc)"), "one");
    ASSERT(lush_pattern_match("abcabc", "+(abc)"), "two");
}

TEST(extglob_negation) {
    ASSERT(lush_pattern_match("xyz", "!(abc)"), "non-matching content");
    ASSERT(!lush_pattern_match("abc", "!(abc)"), "matching content excluded");
    ASSERT(lush_pattern_match("", "!(abc)"), "empty does not match abc");
    ASSERT(lush_pattern_match("ab", "!(abc)"), "prefix is not exact");
}

TEST(extglob_with_outer_suffix) {
    // `@(foo|bar).txt`: composition with literal suffix
    ASSERT(lush_pattern_match("foo.txt", "@(foo|bar).txt"), "first + suffix");
    ASSERT(lush_pattern_match("bar.txt", "@(foo|bar).txt"), "second + suffix");
    ASSERT(!lush_pattern_match("baz.txt", "@(foo|bar).txt"), "non-alt");
    ASSERT(!lush_pattern_match("foo.tx", "@(foo|bar).txt"), "wrong suffix");
}

TEST(extglob_nested) {
    // Nested @(...) inside @(...)
    ASSERT(lush_pattern_match("foo", "@(@(foo)|bar)"), "inner first");
    ASSERT(lush_pattern_match("bar", "@(@(foo)|bar)"), "outer alt");
}

/* ============================================================================
 * Zsh bare alternation
 * ============================================================================
 */

TEST(zsh_bare_alt_equiv_to_at) {
    // Bare paren without operator prefix is the zsh form.
    ASSERT(lush_pattern_match("abc", "(abc|def)"), "first alt");
    ASSERT(lush_pattern_match("def", "(abc|def)"), "second alt");
    ASSERT(!lush_pattern_match("xyz", "(abc|def)"), "no alt");
}

TEST(zsh_bare_alt_with_empty) {
    // The prezto editor-init `(emacs|)` case.
    ASSERT(lush_pattern_match("emacs", "(emacs|)"), "emacs matches");
    ASSERT(lush_pattern_match("", "(emacs|)"), "empty matches");
    ASSERT(!lush_pattern_match("vi", "(emacs|)"), "vi does not");
}

/* ============================================================================
 * Edge cases
 * ============================================================================
 */

TEST(null_inputs) {
    ASSERT(!lush_pattern_match(NULL, "abc"), "NULL string");
    ASSERT(!lush_pattern_match("abc", NULL), "NULL pattern");
    ASSERT(!lush_pattern_match(NULL, NULL), "both NULL");
}

TEST(empty_pattern_only_matches_empty) {
    ASSERT(lush_pattern_match("", ""), "empty/empty");
    ASSERT(!lush_pattern_match("a", ""), "non-empty string, empty pattern");
}

TEST(unbalanced_paren_treated_as_literal) {
    // An unmatched `(` should not crash; treated as a literal character.
    ASSERT(lush_pattern_match("(abc", "(abc"), "literal opening paren");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running pattern_match tests...\n\n");

    printf("Plain glob:\n");
    RUN_TEST(literal_match);
    RUN_TEST(literal_mismatch);
    RUN_TEST(star_matches_anything);
    RUN_TEST(question_matches_one_char);
    RUN_TEST(char_class_basic);
    RUN_TEST(char_class_range);
    RUN_TEST(char_class_negation);
    RUN_TEST(escape_metacharacter);

    printf("\nBash extglob:\n");
    RUN_TEST(extglob_at_exact_one);
    RUN_TEST(extglob_at_with_empty_alt);
    RUN_TEST(extglob_question_zero_or_one);
    RUN_TEST(extglob_star_zero_or_more);
    RUN_TEST(extglob_plus_one_or_more);
    RUN_TEST(extglob_negation);
    RUN_TEST(extglob_with_outer_suffix);
    RUN_TEST(extglob_nested);

    printf("\nZsh bare alternation:\n");
    RUN_TEST(zsh_bare_alt_equiv_to_at);
    RUN_TEST(zsh_bare_alt_with_empty);

    printf("\nEdge cases:\n");
    RUN_TEST(null_inputs);
    RUN_TEST(empty_pattern_only_matches_empty);
    RUN_TEST(unbalanced_paren_treated_as_literal);

    return TEST_RESULT();
}
