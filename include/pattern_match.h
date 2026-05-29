/**
 * @file pattern_match.h
 * @brief Shell pattern matcher with bash extglob and zsh bare-alternation
 *
 * Lush's single canonical pattern matcher for `[[ ... == pat ]]`,
 * `case`, parameter expansion (`${var/pat/repl}`, `${var^^[pat]}`),
 * filename globbing, and the zsh `(r)`/`(i)` array-subscript flags. It
 * recognizes the standard glob subset (`*`, `?`, `[...]`, `\`) inline,
 * with codepoint-aware `[...]` ranges and POSIX/Unicode `[:class:]`,
 * and adds extended-pattern support:
 *
 *   ?(p1|p2|...)   zero or one occurrence of one of the patterns
 *   *(p1|p2|...)   zero or more occurrences
 *   +(p1|p2|...)   one or more occurrences
 *   @(p1|p2|...)   exactly one occurrence
 *   !(p1|p2|...)   anything that does not match any of the patterns
 *
 *   (p1|p2|...)    zsh bare-alternation form; equivalent to @(p1|p2|...)
 *
 * With LUSH_PATTERN_ZSH_EXTENDED, the zsh extended-glob postfix
 * quantifiers `x#` (zero or more of x) and `x##` (one or more) and a
 * leading `^` (negate the whole pattern) are additionally honored.
 *
 * Patterns are matched against the entire string; the matcher does not
 * locate substrings. Returns true on exact match.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_PATTERN_MATCH_H
#define LUSH_PATTERN_MATCH_H

#include <stdbool.h>

/**
 * @brief Match `str` against shell pattern `pattern`
 *
 * Full-string match. Supports POSIX glob plus bash extglob plus zsh's
 * bare-alternation form (see file header for the operator inventory).
 * Returns false if either argument is NULL.
 *
 * @param str     String to match
 * @param pattern Pattern to match against
 * @return true on exact match, false otherwise
 */
bool lush_pattern_match(const char *str, const char *pattern);

/// Enable zsh extended-glob operators in lush_pattern_match_ex: the
/// postfix quantifiers `x#` / `x##` and a leading `^` whole-pattern
/// negation.
#define LUSH_PATTERN_ZSH_EXTENDED 0x1u

/**
 * @brief Match `str` against `pattern` with extended options
 *
 * Same as lush_pattern_match, but `flags` (a bitwise OR of LUSH_PATTERN_*
 * values) selects optional dialect behavior. With flags == 0 it is
 * identical to lush_pattern_match.
 *
 * @param str     String to match
 * @param pattern Pattern to match against
 * @param flags   Bitwise OR of LUSH_PATTERN_* option flags
 * @return true on exact match, false otherwise
 */
bool lush_pattern_match_ex(const char *str, const char *pattern,
                           unsigned flags);

#endif /* LUSH_PATTERN_MATCH_H */
