/**
 * @file pattern_match.c
 * @brief Shell pattern matcher with bash extglob and zsh bare-alternation
 *
 * Hand-rolled recursive matcher. The standard glob subset (`*`, `?`,
 * `[...]`, `\`) is recognized inline rather than delegated to fnmatch so
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

#include "lle/unicode_case.h"
#include "lle/unicode_class.h"
#include "lle/unicode_grapheme.h"
#include "lle/utf8_support.h"
#include "shell_mode.h"

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
 * paren) until the matching `)` at depth zero. Honors `\`-escapes and
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
 * @brief Decode the next UTF-8 codepoint at `p`
 *
 * Returns the codepoint and writes its byte length into `*bytes`. On
 * decode failure (malformed UTF-8 or end-of-string), returns the raw
 * byte as a codepoint with byte length 1 -- this lets ASCII-only
 * patterns work unchanged while multi-byte sequences are handled
 * correctly when present.
 */
static uint32_t decode_one(const char *p, size_t *bytes) {
    if (!p || !*p) {
        *bytes = 0;
        return 0;
    }
    uint32_t cp = (uint32_t)(unsigned char)*p;
    *bytes = 1;
    uint32_t decoded = 0;
    int consumed = lle_utf8_decode_codepoint(p, strlen(p), &decoded);
    if (consumed > 0) {
        cp = decoded;
        *bytes = (size_t)consumed;
    }
    return cp;
}

/**
 * @brief Byte length of the TR#29 grapheme cluster starting at `s`
 *
 * lush is grapheme-aware everywhere it processes text that is not provably
 * ASCII: a "character" in this shell is a user-perceived character (Unicode
 * TR#29 extended grapheme cluster), not a codepoint and not a byte. The
 * wildcards honour that, so `?` consumes `e` + U+0301 as ONE character rather
 * than leaving a combining mark stranded -- and never splits a multi-byte
 * sequence into an invalid byte stream (issue #682).
 *
 * Steps codepoint by codepoint via lle_utf8_decode_codepoint and stops at the
 * next position lle_is_grapheme_boundary reports -- the same idiom
 * lush_substring_extract uses in src/param_op.c.
 *
 * The boundary algorithm takes the enclosing string so it can see the
 * character preceding the position under test. Stepping FORWARD from `s` that
 * character is always at or after `s` itself (the cluster's own base), so `s`
 * serves as the start bound -- which matters because the matcher recurses with
 * a moving `s` and does not carry the original string head. Returns at least 1
 * for a malformed byte, so a bad sequence can never stall the matcher.
 */
static size_t grapheme_step(const char *s) {
    if (!s || !*s) {
        return 0;
    }
    const char *end = s + strlen(s);
    size_t step = 0;
    (void)decode_one(s, &step);
    if (step == 0) {
        step = 1;
    }
    const char *q = s + step;
    while (q < end && *q && !lle_is_grapheme_boundary(q, s, end)) {
        size_t more = 0;
        (void)decode_one(q, &more);
        if (more == 0) {
            more = 1;
        }
        q += more;
    }
    return (size_t)(q - s);
}

/**
 * @brief Match a single character of `s` against a `[...]` class
 *
 * `p` points at the opening `[`. On match, returns 1, writes the
 * position just past the closing `]` into `*end`, and writes the
 * byte length of the matched input GRAPHEME CLUSTER into `*s_consumed`.
 * On no-match, returns 0; `*end` is still advanced so the caller can
 * skip the class. Pattern codepoints in the bracket -- both literal
 * characters and the `-` range endpoints -- are decoded as UTF-8
 * codepoints so ranges like `[α-ω]` and literals like `[äöü]` work
 * correctly.
 *
 * A bracket class asks the same question `?` does -- "match one
 * character" -- so it answers it the same way: membership is tested on
 * the cluster's BASE codepoint, and the whole cluster is consumed on a
 * match. `[a-z]` therefore matches `é` in both NFC (U+00E9) and NFD
 * (`e` + U+0301), which is how a reader sees it.
 *
 * Consuming only the base left the combining marks behind, so against
 * decomposed text `${v%[[:alpha:]]}` on `cafe`+U+0301 stripped nothing
 * at all: the only candidate suffix is the whole cluster, and a matcher
 * that reported one byte consumed could never account for it (issue
 * #702). #682 fixed the same defect for `?` and `*`; this is that
 * question asked at a different code path.
 */
static int match_char_class(const char *s, const char *p, const char **end,
                            size_t *s_consumed) {
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
        if (s_consumed) {
            *s_consumed = 0;
        }
        return 0;
    }

    /// Decode the cluster's base codepoint once for the whole bracket:
    /// every membership test below is asked about the base. `s` is always
    /// at a codepoint start here (the matcher advances by whole clusters),
    /// which is the precondition grapheme_step needs -- lle_is_grapheme_
    /// boundary reports invalid UTF-8 as a boundary, so it must never be
    /// asked about a continuation byte.
    size_t cp_bytes = 1;
    uint32_t input_cp = decode_one(s, &cp_bytes);
    if (s_consumed) {
        /// Report the whole cluster: matching one character consumes one
        /// character, marks included.
        size_t cluster = grapheme_step(s);
        *s_consumed = cluster > cp_bytes ? cluster : cp_bytes;
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

        /// POSIX / Unicode character class [:CLASS:] inside the bracket.
        /// Matched against the same decoded input codepoint, via the
        /// project's Unicode general-category predicates.
        if (p[0] == '[' && p[1] == ':') {
            const char *class_start = p + 2;
            const char *class_end = strstr(class_start, ":]");
            if (class_end) {
                size_t cl = (size_t)(class_end - class_start);
                bool cls = false;
                if (cl == 5 && memcmp(class_start, "space", 5) == 0) {
                    cls = lle_unicode_is_space(input_cp);
                } else if (cl == 5 && memcmp(class_start, "alpha", 5) == 0) {
                    cls = lle_unicode_is_alpha(input_cp);
                } else if (cl == 5 && memcmp(class_start, "digit", 5) == 0) {
                    cls = lle_unicode_is_digit(input_cp);
                } else if (cl == 5 && memcmp(class_start, "alnum", 5) == 0) {
                    cls = lle_unicode_is_alnum(input_cp);
                } else if (cl == 5 && memcmp(class_start, "upper", 5) == 0) {
                    cls = lle_unicode_is_upper(input_cp);
                } else if (cl == 5 && memcmp(class_start, "lower", 5) == 0) {
                    cls = lle_unicode_is_lower(input_cp);
                } else if (cl == 5 && memcmp(class_start, "punct", 5) == 0) {
                    cls = lle_unicode_is_punct(input_cp);
                } else if (cl == 5 && memcmp(class_start, "print", 5) == 0) {
                    cls = lle_unicode_is_print(input_cp);
                } else if (cl == 5 && memcmp(class_start, "graph", 5) == 0) {
                    cls = lle_unicode_is_graph(input_cp);
                } else if (cl == 5 && memcmp(class_start, "blank", 5) == 0) {
                    cls = lle_unicode_is_blank(input_cp);
                } else if (cl == 5 && memcmp(class_start, "cntrl", 5) == 0) {
                    cls = lle_unicode_is_cntrl(input_cp);
                } else if (cl == 6 && memcmp(class_start, "xdigit", 6) == 0) {
                    cls = lle_unicode_is_xdigit(input_cp);
                }
                if (cls) {
                    matched = true;
                }
                p = class_end + 2; /// past `:]`
                continue;
            }
        }

        /// Decode the low codepoint (may be `\`-escaped).
        uint32_t lo;
        size_t lo_bytes;
        if (*p == '\\' && p[1]) {
            lo = decode_one(p + 1, &lo_bytes);
            p += 1 + lo_bytes;
        } else {
            lo = decode_one(p, &lo_bytes);
            p += lo_bytes;
        }

        uint32_t hi = lo;
        if (*p == '-' && p[1] && p[1] != ']') {
            p++; /// past '-'
            size_t hi_bytes;
            if (*p == '\\' && p[1]) {
                hi = decode_one(p + 1, &hi_bytes);
                p += 1 + hi_bytes;
            } else {
                hi = decode_one(p, &hi_bytes);
                p += hi_bytes;
            }
        }
        if (input_cp >= lo && input_cp <= hi) {
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
 * alternative-branch composer. `flags` carries LUSH_PATTERN_* options
 * (currently only the zsh-extended `#`/`##` postfix quantifiers). */
static bool match(const char *s, const char *p, unsigned flags);

/**
 * @brief End of a quantifiable atom for zsh `#`/`##`, or NULL
 *
 * A "quantifiable atom" is the smallest pattern unit that a trailing
 * zsh `#` (zero-or-more) or `##` (one-or-more) repeats: a `[...]`
 * character class, a `\`-escaped character, or a single literal
 * codepoint. The glob-control characters `*?()|` do not begin a
 * quantifiable atom (they are handled by their own matcher branches),
 * so this returns NULL for them and for an unterminated `[`.
 */
static const char *find_quantifiable_atom_end(const char *p) {
    if (*p == '[') {
        const char *q = p + 1;
        if (*q == '!' || *q == '^') {
            q++;
        }
        if (*q == ']') {
            q++;
        }
        while (*q && *q != ']') {
            if (*q == '\\' && q[1]) {
                q += 2;
            } else {
                q++;
            }
        }
        return (*q == ']') ? q + 1 : NULL;
    }
    if (*p == '\\' && p[1]) {
        size_t b;
        (void)decode_one(p + 1, &b);
        return p + 1 + b;
    }
    if (*p && !strchr("*?()|", *p)) {
        size_t b;
        (void)decode_one(p, &b);
        return p + b;
    }
    return NULL;
}

/**
 * @brief Match `s` against (alt-text + rest_pattern) by composition
 *
 * Used when handling extglob constructs: pick one alternative, splice it
 * back into the outer pattern in place of the construct, and recurse.
 * The composed buffer is heap-allocated; on OOM, the match is reported
 * as failure (the caller continues with other alternatives).
 */
static bool match_alt_plus_rest(const char *s, const char *alt_start,
                                const char *alt_end, const char *rest,
                                unsigned flags) {
    size_t alt_len = (size_t)(alt_end - alt_start);
    size_t rest_len = strlen(rest);
    char *composite = malloc(alt_len + rest_len + 1);
    if (!composite) {
        return false;
    }
    memcpy(composite, alt_start, alt_len);
    memcpy(composite + alt_len, rest, rest_len);
    composite[alt_len + rest_len] = '\0';
    bool r = match(s, composite, flags);
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
                                     const char *alt_start, const char *alt_end,
                                     unsigned flags) {
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
    bool r = match(s_buf, alt_buf, flags);
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
static bool match(const char *s, const char *p, unsigned flags) {
    while (*p) {
        /// zsh-extended `#` (zero-or-more) / `##` (one-or-more) postfix
        /// quantifiers. An atom immediately followed by `#`/`##` is
        /// equivalent to the extglob group `*(atom)` / `+(atom)`, so
        /// synthesize that form and reuse the tested extglob machinery.
        if (flags & LUSH_PATTERN_ZSH_EXTENDED) {
            const char *atom_end = find_quantifiable_atom_end(p);
            if (atom_end && *atom_end == '#') {
                bool one_or_more = (atom_end[1] == '#');
                const char *rest = one_or_more ? atom_end + 2 : atom_end + 1;
                size_t atom_len = (size_t)(atom_end - p);
                size_t rest_len = strlen(rest);
                char *synth = malloc(2 + atom_len + 1 + rest_len + 1);
                if (!synth) {
                    return false;
                }
                synth[0] = one_or_more ? '+' : '*';
                synth[1] = '(';
                memcpy(synth + 2, p, atom_len);
                synth[2 + atom_len] = ')';
                memcpy(synth + 3 + atom_len, rest, rest_len);
                synth[3 + atom_len + rest_len] = '\0';
                /// The synthesized `*(atom)` / `+(atom)` is an extglob group;
                /// the zsh `#`/`##` feature reuses that engine, so enable it
                /// for this recursion regardless of the caller's extglob flag.
                bool r = match(s, synth, flags | LUSH_PATTERN_EXTGLOB);
                free(synth);
                return r;
            }
        }

        char op = 0;
        const char *paren_inside = NULL;
        if ((flags & LUSH_PATTERN_EXTGLOB) &&
            (*p == '?' || *p == '*' || *p == '+' || *p == '@' || *p == '!') &&
            p[1] == '(') {
            /// bash extglob group, gated by FEATURE_EXTENDED_GLOB. When the
            /// feature is off the sigil is left to literal-token handling
            /// below, so `@(foo|bar)` matches the literal `@` and the `(...)`
            /// falls to the bare-alternation branch.
            op = *p;
            paren_inside = p + 2;
        } else if (*p == '(') {
            op = '@'; /// zsh bare-alternation = @ (separate feature, not #567)
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
                    if (match_alt_plus_rest(s, alt_start, sep, rest, flags)) {
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
                if (match(s, rest, flags)) {
                    return true;
                }
                /// One occurrence: identical to @.
                const char *alt_start = paren_inside;
                while (alt_start <= close) {
                    const char *sep = find_next_alt_separator(alt_start, close);
                    if (match_alt_plus_rest(s, alt_start, sep, rest, flags)) {
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
                if (op == '*' && match(s, rest, flags)) {
                    return true;
                }
                /// One-or-more: pick an alternative, match it against a
                /// non-empty prefix of s, then recurse with `*(...)` + rest
                /// on the suffix. The non-empty-prefix requirement guards
                /// the empty-alt case (`(a|)`) from looping forever.
                ///
                /// Building the inner `*(...)` pattern once per iteration
                /// keeps the alt-text identical across recursion so a
                /// positional iterator isn't needed.
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
                        if (alt_matches_exact_prefix(s, split, alt_start, sep,
                                                     flags)) {
                            if (match(s + split, kleene, flags)) {
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
                /// Match a prefix of s that fails ALL alternatives, with
                /// the remainder consumed by `rest`. The empty prefix is
                /// permitted (i.e. !(foo) at end of pattern matches the
                /// empty string when foo isn't empty).
                size_t s_len = strlen(s);
                for (size_t split = 0; split <= s_len; split++) {
                    bool any_alt_matches = false;
                    const char *alt_start = paren_inside;
                    while (alt_start <= close) {
                        const char *sep =
                            find_next_alt_separator(alt_start, close);
                        if (alt_matches_exact_prefix(s, split, alt_start, sep,
                                                     flags)) {
                            any_alt_matches = true;
                            break;
                        }
                        if (sep == close) {
                            break;
                        }
                        alt_start = sep + 1;
                    }
                    if (!any_alt_matches && match(s + split, rest, flags)) {
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
            /// Step through input by GRAPHEME anchors. A mid-cluster anchor is
            /// not a character boundary, so offering one lets the rest of the
            /// pattern match starting INSIDE a user-perceived character --
            /// which is how `${v%?}` came to strip a lone continuation byte and
            /// emit invalid UTF-8 (issue #682). Anchoring on clusters is also
            /// strictly fewer positions to try than codepoints or bytes.
            while (*s) {
                if (match(s, p, flags)) {
                    return true;
                }
                size_t step = grapheme_step(s);
                if (step == 0) {
                    step = 1; /// defensive against malformed UTF-8
                }
                s += step;
            }
            return match(s, p, flags); /// try with empty tail
        }
        if (*p == '?') {
            if (*s == '\0') {
                return false;
            }
            /// `?` matches one CHARACTER -- a TR#29 grapheme cluster, which
            /// is what a user means by one character. A codepoint would consume
            /// `e` out of `e`+U+0301 and strand the combining mark; a byte
            /// would split the sequence and emit invalid UTF-8 (issue #682).
            size_t step = grapheme_step(s);
            if (step == 0) {
                step = 1;
            }
            s += step;
            p++;
            continue;
        }
        if (*p == '[') {
            const char *end;
            size_t s_consumed = 1;
            int matched = match_char_class(s, p, &end, &s_consumed);
            if (!matched) {
                return false;
            }
            p = end;
            s += s_consumed;
            continue;
        }
        /// Literal character. Decode both sides as codepoints so a
        /// multi-byte literal in the pattern matches its multi-byte
        /// counterpart in the input rather than only the first byte.
        if (*p == '\\' && p[1]) {
            p++;
        }
        size_t p_bytes;
        uint32_t pat_cp = decode_one(p, &p_bytes);
        size_t s_bytes;
        uint32_t in_cp = decode_one(s, &s_bytes);
        if (pat_cp != in_cp) {
            return false;
        }
        p += p_bytes;
        s += s_bytes;
    }
    return *s == '\0';
}

bool lush_pattern_match(const char *str, const char *pattern) {
    return lush_pattern_match_ex(str, pattern, 0);
}

bool lush_pattern_match_ex(const char *str, const char *pattern,
                           unsigned flags) {
    if (!str || !pattern) {
        return false;
    }
    /// zsh-extended leading `^` negates the whole pattern (the rest is
    /// matched and the result inverted). Only honored at pattern start,
    /// matching the historical zsh extended-glob filename behavior.
    if ((flags & LUSH_PATTERN_ZSH_EXTENDED) && pattern[0] == '^') {
        return !match(str, pattern + 1, flags);
    }
    return match(str, pattern, flags);
}

/// Pattern-matcher flags derived from the active shell mode: bash extglob
/// (?( *( +( @( !() when FEATURE_EXTENDED_GLOB is enabled, and the zsh
/// extended-glob operators (`x#`/`x##`, leading `^`) when
/// FEATURE_ZSH_EXTENDED_GLOB is enabled. Central builder so every shell
/// pattern-matching site gates these dialects consistently on the mode.
unsigned lush_shell_pattern_flags(void) {
    unsigned f = 0;
    if (shell_mode_allows(FEATURE_EXTENDED_GLOB)) {
        f |= LUSH_PATTERN_EXTGLOB;
    }
    if (shell_mode_allows(FEATURE_ZSH_EXTENDED_GLOB)) {
        f |= LUSH_PATTERN_ZSH_EXTENDED;
    }
    return f;
}

/// Mode-aware shell pattern match. Case patterns, parameter-expansion
/// matchers (`${v#p}` / `${v%p}` / `${v/p/r}`), `[[ == ]]`, and array/value
/// filters route through here so extglob and the zsh operators are gated by
/// the current mode's feature flags rather than always active.
bool lush_shell_pattern_match(const char *str, const char *pattern) {
    return lush_pattern_match_ex(str, pattern, lush_shell_pattern_flags());
}
