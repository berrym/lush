/**
 * @file pattern_match.h
 * @brief Shell pattern matcher with bash extglob and zsh bare-alternation
 *
 * Lush's pattern matcher for `[[ ... == pat ]]`, `case`, parameter
 * expansion (`${var/pat/repl}`), etc. Wraps fnmatch for the standard
 * glob subset (`*`, `?`, `[...]`, `\`) and adds extended-pattern support:
 *
 *   ?(p1|p2|...)   zero or one occurrence of one of the patterns
 *   *(p1|p2|...)   zero or more occurrences
 *   +(p1|p2|...)   one or more occurrences
 *   @(p1|p2|...)   exactly one occurrence
 *   !(p1|p2|...)   anything that does not match any of the patterns
 *
 *   (p1|p2|...)    zsh bare-alternation form; equivalent to @(p1|p2|...)
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

#endif /* LUSH_PATTERN_MATCH_H */
