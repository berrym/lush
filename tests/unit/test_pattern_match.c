/**
 * @file test_pattern_match.c
 * @brief Unit tests for the shell pattern matcher
 *
 * Covers POSIX glob, bash extglob (?(...), *(...), +(...), @(...), !(...))
 * and zsh's bare-alternation form (...). Each test corresponds to a
 * concrete behavioral promise of the matcher; this is not coverage
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

/// Extglob operators are gated behind LUSH_PATTERN_EXTGLOB (issue #567);
/// the engine tests below exercise the operators directly, so match with
/// the flag enabled.
static bool eg_match(const char *s, const char *p) {
    return lush_pattern_match_ex(s, p, LUSH_PATTERN_EXTGLOB);
}

TEST(extglob_at_exact_one) {
    ASSERT(eg_match("abc", "@(abc|def)"), "first alt");
    ASSERT(eg_match("def", "@(abc|def)"), "second alt");
    ASSERT(!eg_match("xyz", "@(abc|def)"), "no alt matches");
    ASSERT(!eg_match("abcdef", "@(abc|def)"), "concatenation banned");
}

TEST(extglob_at_with_empty_alt) {
    ASSERT(eg_match("abc", "@(abc|)"), "non-empty alt");
    ASSERT(eg_match("", "@(abc|)"), "empty alt matches empty");
    ASSERT(!eg_match("ab", "@(abc|)"), "partial non-match");
}

TEST(extglob_question_zero_or_one) {
    ASSERT(eg_match("", "?(abc)"), "zero occurrences");
    ASSERT(eg_match("abc", "?(abc)"), "one occurrence");
    ASSERT(!eg_match("abcabc", "?(abc)"), "two occurrences banned");
}

TEST(extglob_star_zero_or_more) {
    ASSERT(eg_match("", "*(abc)"), "zero");
    ASSERT(eg_match("abc", "*(abc)"), "one");
    ASSERT(eg_match("abcabc", "*(abc)"), "two");
    ASSERT(eg_match("abcabcabc", "*(abc)"), "three");
    ASSERT(!eg_match("abcab", "*(abc)"), "trailing partial");
}

TEST(extglob_plus_one_or_more) {
    ASSERT(!eg_match("", "+(abc)"), "zero banned");
    ASSERT(eg_match("abc", "+(abc)"), "one");
    ASSERT(eg_match("abcabc", "+(abc)"), "two");
}

TEST(extglob_negation) {
    ASSERT(eg_match("xyz", "!(abc)"), "non-matching content");
    ASSERT(!eg_match("abc", "!(abc)"), "matching content excluded");
    ASSERT(eg_match("", "!(abc)"), "empty does not match abc");
    ASSERT(eg_match("ab", "!(abc)"), "prefix is not exact");
}

TEST(extglob_with_outer_suffix) {
    /// `@(foo|bar).txt`: composition with literal suffix
    ASSERT(eg_match("foo.txt", "@(foo|bar).txt"), "first + suffix");
    ASSERT(eg_match("bar.txt", "@(foo|bar).txt"), "second + suffix");
    ASSERT(!eg_match("baz.txt", "@(foo|bar).txt"), "non-alt");
    ASSERT(!eg_match("foo.tx", "@(foo|bar).txt"), "wrong suffix");
}

TEST(extglob_nested) {
    /// Nested @(...) inside @(...)
    ASSERT(eg_match("foo", "@(@(foo)|bar)"), "inner first");
    ASSERT(eg_match("bar", "@(@(foo)|bar)"), "outer alt");
}

/* ============================================================================
 * Zsh bare alternation
 * ============================================================================
 */

TEST(zsh_bare_alt_equiv_to_at) {
    /// Bare paren without operator prefix is the zsh form.
    ASSERT(lush_pattern_match("abc", "(abc|def)"), "first alt");
    ASSERT(lush_pattern_match("def", "(abc|def)"), "second alt");
    ASSERT(!lush_pattern_match("xyz", "(abc|def)"), "no alt");
}

TEST(zsh_bare_alt_with_empty) {
    /// The prezto editor-init `(emacs|)` case.
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
    /// An unmatched `(` should not crash; treated as a literal character.
    ASSERT(lush_pattern_match("(abc", "(abc"), "literal opening paren");
}

/* ============================================================================
 * Unicode codepoint awareness (audit C-verdict #6)
 * ============================================================================
 */

TEST(question_matches_unicode_codepoint) {
    ASSERT(lush_pattern_match("café", "c?fé"),
           "? matches multi-byte é as one codepoint");
    ASSERT(lush_pattern_match("Naïve", "Na?ve"), "? matches ï in middle");
    ASSERT(!lush_pattern_match("café", "c?f"),
           "? + literal mismatch fails cleanly");
}

TEST(question_matches_decomposed_grapheme_cluster) {
    /// THE DISCRIMINATING CASE. Every pre-existing Unicode test here uses
    /// PRECOMPOSED literals ("café" = U+00E9), where one codepoint and one
    /// TR#29 grapheme cluster are the same thing -- so they pass under
    /// codepoint-only semantics and cannot detect the difference. A DECOMPOSED
    /// sequence separates them: "cafe" + U+0301 is five codepoints but four
    /// characters.
    ///
    /// lush is grapheme-aware everywhere it handles text that is not provably
    /// ASCII, so `?` is one CHARACTER: it consumes the base and its combining
    /// mark together. Under codepoint semantics `?` would eat the `e` and
    /// leave U+0301 unmatched, and under byte semantics it would split the
    /// mark itself and emit invalid UTF-8 (issue #682).
    ASSERT(lush_pattern_match("cafe\xcc\x81", "caf?"),
           "? matches a decomposed e+U+0301 as ONE character");
    ASSERT(lush_pattern_match("cafe\xcc\x81", "c?f?"),
           "two ? match c-a-f-e-combining as four characters");
    ASSERT(!lush_pattern_match("cafe\xcc\x81", "caf??"),
           "the cluster is ONE character, so two ? overshoot");

    /// A multi-codepoint emoji cluster is the same rule at a larger scale:
    /// family emoji joined by ZWJ are one user-perceived character.
    ASSERT(lush_pattern_match("\xf0\x9f\x91\xa9\xe2\x80\x8d"
                              "\xf0\x9f\x92\xbb",
                              "?"),
           "? matches a ZWJ emoji sequence as one character");
}

TEST(star_anchors_on_grapheme_boundaries) {
    /// `*` offers candidate anchor positions to the rest of the pattern. A
    /// mid-cluster anchor is not a character boundary, so offering one lets the
    /// tail match starting INSIDE a character -- the same defect shape that
    /// made `${v%?}` strip a lone continuation byte.
    ASSERT(lush_pattern_match("cafe\xcc\x81", "*e\xcc\x81"),
           "* anchors before a whole cluster");
    ASSERT(!lush_pattern_match("cafe\xcc\x81", "*\xcc\x81"),
           "* never anchors mid-cluster, so a bare combining mark is not a "
           "character the tail can start at");
}

TEST(literal_multibyte_matches) {
    ASSERT(lush_pattern_match("café", "café"),
           "literal multi-byte pattern matches identical input");
    ASSERT(lush_pattern_match("中文", "中文"), "CJK literal matches");
    ASSERT(!lush_pattern_match("café", "cafe"),
           "é != e, no first-byte ambiguity");
}

TEST(star_matches_with_unicode_tail) {
    ASSERT(lush_pattern_match("café", "c*é"), "* + multi-byte tail");
    ASSERT(lush_pattern_match("café au lait", "c*lait"),
           "* across multi-byte content");
}

TEST(bracket_range_unicode_lowercase) {
    ASSERT(lush_pattern_match("α", "[α-ω]"), "α at lower bound of Greek range");
    ASSERT(lush_pattern_match("ω", "[α-ω]"), "ω at upper bound");
    ASSERT(lush_pattern_match("ν", "[α-ω]"), "ν in middle of Greek range");
    ASSERT(!lush_pattern_match("Α", "[α-ω]"),
           "uppercase Α outside lowercase Greek range");
}

TEST(bracket_range_latin1_supplement) {
    ASSERT(lush_pattern_match("À", "[À-ÿ]"),
           "À at lower bound of Latin-1 Supplement range");
    ASSERT(lush_pattern_match("é", "[À-ÿ]"), "é in Latin-1 Supplement range");
    ASSERT(!lush_pattern_match("a", "[À-ÿ]"), "ASCII a outside Latin-1 range");
}

TEST(bracket_literal_unicode_chars) {
    ASSERT(lush_pattern_match("ä", "[äöü]"), "literal ä in Unicode bracket");
    ASSERT(lush_pattern_match("ö", "[äöü]"), "literal ö in Unicode bracket");
    ASSERT(!lush_pattern_match("a", "[äöü]"), "ASCII a not in Unicode bracket");
}

TEST(bracket_mixed_ascii_and_unicode) {
    ASSERT(lush_pattern_match("a", "[a-zα-ω]"), "ASCII alpha in mixed bracket");
    ASSERT(lush_pattern_match("ν", "[a-zα-ω]"), "Greek alpha in mixed bracket");
    ASSERT(!lush_pattern_match("5", "[a-zα-ω]"), "digit not in any range");
}

TEST(bracket_negation_unicode) {
    ASSERT(lush_pattern_match("Α", "[!α-ω]"),
           "uppercase Α matches negated lowercase range");
    ASSERT(!lush_pattern_match("α", "[!α-ω]"),
           "α excluded by negated lowercase range");
}

/* ============================================================================
 * Zsh extended-glob (LUSH_PATTERN_ZSH_EXTENDED): `#`/`##` + leading `^`
 * ============================================================================
 */

TEST(zsh_ext_hash_zero_or_more) {
    /// `x#` matches zero or more of x (like `*(x)`).
    unsigned f = LUSH_PATTERN_ZSH_EXTENDED;
    ASSERT(lush_pattern_match_ex("", "a#", f), "zero a's");
    ASSERT(lush_pattern_match_ex("aaaa", "a#", f), "many a's");
    ASSERT(!lush_pattern_match_ex("aaab", "a#", f), "trailing b fails");
    /// Without the flag, `#` is a literal character.
    ASSERT(lush_pattern_match_ex("a#", "a#", 0), "# literal without flag");
    ASSERT(!lush_pattern_match_ex("aaaa", "a#", 0),
           "no quantifier without flag");
}

TEST(zsh_ext_hashhash_one_or_more) {
    /// `x##` matches one or more of x (like `+(x)`).
    unsigned f = LUSH_PATTERN_ZSH_EXTENDED;
    ASSERT(!lush_pattern_match_ex("", "a##", f), "zero a's fails one-or-more");
    ASSERT(lush_pattern_match_ex("a", "a##", f), "one a");
    ASSERT(lush_pattern_match_ex("aaaa", "a##", f), "many a's");
}

TEST(zsh_ext_hash_charclass_and_suffix) {
    /// Quantifier applies to a `[...]` atom, with a literal suffix after.
    unsigned f = LUSH_PATTERN_ZSH_EXTENDED;
    ASSERT(lush_pattern_match_ex("123.txt", "[0-9]#.txt", f),
           "digits then .txt");
    ASSERT(lush_pattern_match_ex(".txt", "[0-9]#.txt", f),
           "zero digits then .txt");
    ASSERT(lush_pattern_match_ex("7.txt", "[0-9]##.txt", f),
           "one-or-more digits");
    ASSERT(!lush_pattern_match_ex(".txt", "[0-9]##.txt", f),
           "zero digits fails one-or-more");
}

TEST(zsh_ext_leading_caret_negates) {
    /// A leading `^` negates the whole pattern.
    unsigned f = LUSH_PATTERN_ZSH_EXTENDED;
    ASSERT(lush_pattern_match_ex("world", "^h*", f), "world is not h*");
    ASSERT(!lush_pattern_match_ex("hello", "^h*", f),
           "hello matches h*, negated");
    /// Without the flag, leading `^` is a literal.
    ASSERT(lush_pattern_match_ex("^h", "^h", 0), "^ literal without flag");
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

    printf("\nZsh extended glob (#/##/^):\n");
    RUN_TEST(zsh_ext_hash_zero_or_more);
    RUN_TEST(zsh_ext_hashhash_one_or_more);
    RUN_TEST(zsh_ext_hash_charclass_and_suffix);
    RUN_TEST(zsh_ext_leading_caret_negates);

    printf("\nEdge cases:\n");
    RUN_TEST(null_inputs);
    RUN_TEST(empty_pattern_only_matches_empty);
    RUN_TEST(unbalanced_paren_treated_as_literal);

    printf("\nUnicode codepoint awareness:\n");
    RUN_TEST(question_matches_unicode_codepoint);
    RUN_TEST(question_matches_decomposed_grapheme_cluster);
    RUN_TEST(star_anchors_on_grapheme_boundaries);
    RUN_TEST(literal_multibyte_matches);
    RUN_TEST(star_matches_with_unicode_tail);
    RUN_TEST(bracket_range_unicode_lowercase);
    RUN_TEST(bracket_range_latin1_supplement);
    RUN_TEST(bracket_literal_unicode_chars);
    RUN_TEST(bracket_mixed_ascii_and_unicode);
    RUN_TEST(bracket_negation_unicode);

    return TEST_RESULT();
}
