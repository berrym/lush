/**
 * @file pattern_match.c
 * @brief Shell pattern matcher with bash extglob and zsh bare-alternation
 *
 * Hand-rolled recursive matcher. The standard glob subset (`*`, `?`,
 * `[...]`, `\`) is recognised inline rather than delegated to fnmatch so
 * the extended-pattern operators can recurse into arbitrary subpatterns
 * without round-tripping through a portable-but-feature-limited libc
 * implementation. Alternative-pattern matching is done by composing a
 * temporary pattern (alt-text + outer rest) per branch and recursing —
 * O(branches * depth) memory and acceptable for the lengths real shell
 * patterns reach in practice.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "pattern_match.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Helpers
 * ============================================================================
 */

/**
 * @brief Find the closing paren for an extglob/zsh-alt group
 *
 * Walks `p` (which must point to the first character INSIDE the open
 * paren) until the matching `)` at depth zero. Honours `\`-escapes and
 * skips nested parens and bracket character classes. Returns NULL on an
 * unbalanced group (the caller falls back to literal matching).
 */
static const char *find_close_paren(const char *p) {
    int depth = 1;
    while (*p) {
        if (*p == '\\' && p[1]) {
            p += 2;
            continue;
        }
        if (*p == '[') {
            /// Skip char class so a `(` or `)` inside is literal.
            p++;
            if (*p == '!' || *p == '^') {
                p++;
            }
            if (*p == ']') {
                p++; /// first `]` after `[` (or `[!`) is literal
            }
            while (*p && *p != ']') {
                if (*p == '\\' && p[1]) {
                    p += 2;
                } else {
                    p++;
                }
            }
            if (*p == ']') {
                p++;
            }
            continue;
        }
        if (*p == '(') {
            depth++;
        } else if (*p == ')') {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

/**
 * @brief Find the next top-level `|` (or the close paren) in an alt group
 *
 * Walks from `p` (at depth 0 inside the group) until a `|` at depth 0 or
 * the trailing `)` at `close`. Returns the position of the `|` or `close`.
 */
static const char *find_next_alt_separator(const char *p, const char *close) {
    int depth = 0;
    while (p < close) {
        if (*p == '\\' && p[1]) {
            p += 2;
            continue;
        }
        if (*p == '[') {
            p++;
            if (*p == '!' || *p == '^') {
                p++;
            }
            if (*p == ']') {
                p++;
            }
            while (p < close && *p != ']') {
                if (*p == '\\' && p[1]) {
                    p += 2;
                } else {
                    p++;
                }
            }
            if (p < close) {
                p++;
            }
            continue;
        }
        if (*p == '(') {
            depth++;
        } else if (*p == ')') {
            depth--;
        }
        if (depth == 0 && *p == '|') {
            return p;
        }
        p++;
    }
    return close;
}

/**
 * @brief Match a single character of `s` against a `[...]` class
 *
 * `p` points at the opening `[`. On match, returns 1 and writes the
 * position just past the closing `]` into `*end`. On no-match, returns 0;
 * `*end` is still advanced so the caller can skip the class.
 */
static int match_char_class(const char *s, const char *p, const char **end) {
    if (*s == '\0') {
        /// Walk past the class so callers see the right end pointer.
        p++;
        if (*p == '!' || *p == '^') {
            p++;
        }
        if (*p == ']') {
            p++;
        }
        while (*p && *p != ']') {
            if (*p == '\\' && p[1]) {
                p += 2;
            } else {
                p++;
            }
        }
        if (*p == ']') {
            p++;
        }
        *end = p;
        return 0;
    }
    p++; /// past `[`
    bool negate = false;
    if (*p == '!' || *p == '^') {
        negate = true;
        p++;
    }
    bool matched = false;
    bool first = true;
    while (*p && (first || *p != ']')) {
        first = false;
        char lo = *p;
        if (lo == '\\' && p[1]) {
            lo = p[1];
            p += 2;
        } else {
            p++;
        }
        char hi = lo;
        if (*p == '-' && p[1] && p[1] != ']') {
            p++;
            hi = *p;
            if (hi == '\\' && p[1]) {
                hi = p[1];
                p += 2;
            } else {
                p++;
            }
        }
        if ((unsigned char)*s >= (unsigned char)lo &&
            (unsigned char)*s <= (unsigned char)hi) {
            matched = true;
        }
    }
    if (*p == ']') {
        p++;
    }
    *end = p;
    return (matched != negate) ? 1 : 0;
}

/* Forward declaration: the matcher is mutually recursive with the
 * alternative-branch composer. */
static bool match(const char *s, const char *p);

/**
 * @brief Match `s` against (alt-text + rest_pattern) by composition
 *
 * Used when handling extglob constructs: pick one alternative, splice it
 * back into the outer pattern in place of the construct, and recurse.
 * The composed buffer is heap-allocated; on OOM, the match is reported
 * as failure (the caller continues with other alternatives).
 */
static bool match_alt_plus_rest(const char *s, const char *alt_start,
                                const char *alt_end, const char *rest) {
    size_t alt_len = (size_t)(alt_end - alt_start);
    size_t rest_len = strlen(rest);
    char *composite = malloc(alt_len + rest_len + 1);
    if (!composite) {
        return false;
    }
    memcpy(composite, alt_start, alt_len);
    memcpy(composite + alt_len, rest, rest_len);
    composite[alt_len + rest_len] = '\0';
    bool r = match(s, composite);
    free(composite);
    return r;
}

/**
 * @brief Match `s_prefix` (length s_len) against alt-text exactly
 *
 * Used by `!(...)` to determine which prefixes of the input the negative
 * construct rules out. Builds a NUL-terminated copy of the prefix, then
 * calls match() against alt-text rendered as a standalone pattern. NULL
 * pattern or OOM yields false.
 */
static bool alt_matches_exact_prefix(const char *s, size_t s_len,
                                     const char *alt_start,
                                     const char *alt_end) {
    size_t alt_len = (size_t)(alt_end - alt_start);
    char *alt_buf = malloc(alt_len + 1);
    char *s_buf = malloc(s_len + 1);
    if (!alt_buf || !s_buf) {
        free(alt_buf);
        free(s_buf);
        return false;
    }
    memcpy(alt_buf, alt_start, alt_len);
    alt_buf[alt_len] = '\0';
    memcpy(s_buf, s, s_len);
    s_buf[s_len] = '\0';
    bool r = match(s_buf, alt_buf);
    free(alt_buf);
    free(s_buf);
    return r;
}

/* ============================================================================
 * Core matcher
 * ============================================================================
 */

/**
 * @brief Recursive matcher; returns true if `p` matches `s` end-to-end
 *
 * Detects an extglob/zsh-alt construct at the head of `p` and dispatches
 * to per-operator handling; otherwise consumes one glob token from `p`
 * (`*`, `?`, `[...]`, `\X`, or literal) and recurses on the remainder.
 */
static bool match(const char *s, const char *p) {
    while (*p) {
        char op = 0;
        const char *paren_inside = NULL;
        if ((*p == '?' || *p == '*' || *p == '+' || *p == '@' || *p == '!') &&
            p[1] == '(') {
            op = *p;
            paren_inside = p + 2;
        } else if (*p == '(') {
            op = '@'; /// zsh bare-alternation = @
            paren_inside = p + 1;
        }

        if (paren_inside) {
            const char *close = find_close_paren(paren_inside);
            if (!close) {
                /// Unbalanced; fall through to literal handling for `*p`.
                goto literal_token;
            }
            const char *rest = close + 1;

            if (op == '@') {
                const char *alt_start = paren_inside;
                while (alt_start <= close) {
                    const char *sep = find_next_alt_separator(alt_start, close);
                    if (match_alt_plus_rest(s, alt_start, sep, rest)) {
                        return true;
                    }
                    if (sep == close) {
                        break;
                    }
                    alt_start = sep + 1;
                }
                return false;
            }

            if (op == '?') {
                /// Zero occurrences: rest matches whole s.
                if (match(s, rest)) {
                    return true;
                }
                /// One occurrence: identical to @.
                const char *alt_start = paren_inside;
                while (alt_start <= close) {
                    const char *sep = find_next_alt_separator(alt_start, close);
                    if (match_alt_plus_rest(s, alt_start, sep, rest)) {
                        return true;
                    }
                    if (sep == close) {
                        break;
                    }
                    alt_start = sep + 1;
                }
                return false;
            }

            if (op == '*' || op == '+') {
                /// Zero (only valid for `*`): rest matches whole s.
                if (op == '*' && match(s, rest)) {
                    return true;
                }
                /* One-or-more: pick an alternative, match it against a
                 * non-empty prefix of s, then recurse with `*(...)` + rest
                 * on the suffix. The non-empty-prefix requirement guards
                 * the empty-alt case (`(a|)`) from looping forever.
                 *
                 * Building the inner `*(...)` pattern once per iteration
                 * keeps the alt-text identical across recursion so a
                 * positional iterator isn't needed. */
                size_t group_inside_len = (size_t)(close - paren_inside);
                size_t rest_len = strlen(rest);
                /// Reserve "*(" + content + ")" + rest + NUL
                char *kleene = malloc(group_inside_len + rest_len + 4);
                if (!kleene) {
                    return false;
                }
                kleene[0] = '*';
                kleene[1] = '(';
                memcpy(kleene + 2, paren_inside, group_inside_len);
                kleene[2 + group_inside_len] = ')';
                memcpy(kleene + 3 + group_inside_len, rest, rest_len);
                kleene[3 + group_inside_len + rest_len] = '\0';

                bool found = false;
                size_t s_len = strlen(s);
                for (size_t split = 1; split <= s_len && !found; split++) {
                    /// Build candidate: try matching some alt against the
                    /// L-length prefix, then `kleene` against the suffix.
                    const char *alt_start = paren_inside;
                    while (alt_start <= close && !found) {
                        const char *sep =
                            find_next_alt_separator(alt_start, close);
                        if (alt_matches_exact_prefix(s, split, alt_start,
                                                     sep)) {
                            if (match(s + split, kleene)) {
                                found = true;
                            }
                        }
                        if (sep == close) {
                            break;
                        }
                        alt_start = sep + 1;
                    }
                }
                free(kleene);
                return found;
            }

            if (op == '!') {
                /* Match a prefix of s that fails ALL alternatives, with
                 * the remainder consumed by `rest`. The empty prefix is
                 * permitted (i.e. !(foo) at end of pattern matches the
                 * empty string when foo isn't empty). */
                size_t s_len = strlen(s);
                for (size_t split = 0; split <= s_len; split++) {
                    bool any_alt_matches = false;
                    const char *alt_start = paren_inside;
                    while (alt_start <= close) {
                        const char *sep =
                            find_next_alt_separator(alt_start, close);
                        if (alt_matches_exact_prefix(s, split, alt_start,
                                                     sep)) {
                            any_alt_matches = true;
                            break;
                        }
                        if (sep == close) {
                            break;
                        }
                        alt_start = sep + 1;
                    }
                    if (!any_alt_matches && match(s + split, rest)) {
                        return true;
                    }
                }
                return false;
            }
        }

    literal_token:
        if (*p == '*') {
            p++;
            if (*p == '\0') {
                return true;
            }
            while (*s) {
                if (match(s, p)) {
                    return true;
                }
                s++;
            }
            return match(s, p); /// try with empty tail
        }
        if (*p == '?') {
            if (*s == '\0') {
                return false;
            }
            p++;
            s++;
            continue;
        }
        if (*p == '[') {
            const char *end;
            int matched = match_char_class(s, p, &end);
            if (!matched) {
                return false;
            }
            p = end;
            s++;
            continue;
        }
        if (*p == '\\' && p[1]) {
            p++;
        }
        if (*p != *s) {
            return false;
        }
        p++;
        s++;
    }
    return *s == '\0';
}

bool lush_pattern_match(const char *str, const char *pattern) {
    if (!str || !pattern) {
        return false;
    }
    return match(str, pattern);
}
