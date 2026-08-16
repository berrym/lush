/**
 * @file param_op.c
 * @brief Pure parameter-expansion operator core (see param_op.h).
 *
 * Moved here verbatim from src/executor.c so the executor and the Word CST
 * bench share ONE implementation of every pure `${var OP operand}` operator
 * and of the primitives they are built from (issue #681).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "param_op.h"

#include "lle/unicode_case.h"
#include "lle/unicode_grapheme.h"
#include "lle/utf8_support.h"
#include "pattern_match.h"
#include "shell_mode.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// A value is "empty or null" when the variable is unset (NULL) or set to the
/// empty string -- the test the `:`-prefixed operators use.
static bool is_empty_or_null(const char *str) { return !str || str[0] == '\0'; }

/**
 * @brief Slice a string by grapheme cluster positions (TR#29 correct)
 *
 * Used by ${var[N]} / ${var[N,M]} string subscripts on scalar (non-array)
 * variables. The bracket operators here are grapheme-indexed, not byte-
 * indexed.
 *
 * Iterates by *codepoint* using lle_utf8_decode_codepoint (the canonical
 * pattern used elsewhere in the shell — see src/tokenizer.c:2075). At
 * each codepoint boundary, lle_is_grapheme_boundary determines whether
 * the codepoint also starts a new *grapheme cluster*. A multi-codepoint
 * grapheme (emoji+ZWJ+emoji, base+combining-mark, etc.) increments the
 * grapheme counter only at its first codepoint, so all internal
 * codepoints are correctly grouped under one grapheme index.
 *
 * Indexing is 0-based at this layer; the caller is responsible for
 * converting from 1-based (zsh-style) where applicable.
 *
 * @param str Source string (UTF-8)
 * @param str_len Length in bytes
 * @param start_grapheme 0-based grapheme index to start at
 * @param count Number of graphemes to extract (-1 for "to end")
 * @return Newly malloc'd substring, or strdup("") on out-of-range / OOM
 */
char *lush_slice_graphemes(const char *str, size_t str_len, int start_grapheme,
                           int count) {
    if (!str || str_len == 0 || start_grapheme < 0) {
        return strdup("");
    }

    int grapheme_idx = 0;
    size_t byte_start = SIZE_MAX;
    size_t byte_end = str_len;
    int target_end = (count < 0) ? -1 : start_grapheme + count;

    size_t i = 0;
    while (i < str_len) {
        /// Check grapheme boundary at this codepoint start (not at every
        /// byte — continuation bytes would falsely register as boundaries
        /// because lle_is_grapheme_boundary treats invalid UTF-8 as a
        /// boundary, and continuation bytes alone are invalid as a
        /// standalone codepoint).
        if (lle_is_grapheme_boundary(str + i, str, str + str_len)) {
            if (grapheme_idx == start_grapheme) {
                byte_start = i;
            }
            if (target_end >= 0 && grapheme_idx == target_end) {
                byte_end = i;
                break;
            }
            grapheme_idx++;
        }

        /// Advance by one codepoint.
        uint32_t cp;
        int cp_len = lle_utf8_decode_codepoint(str + i, str_len - i, &cp);
        if (cp_len <= 0) {
            i++; /// Skip invalid byte to avoid infinite loop.
        } else {
            i += (size_t)cp_len;
        }
    }

    if (byte_start == SIZE_MAX) {
        return strdup("");
    }
    size_t slice_len = byte_end - byte_start;
    char *result = malloc(slice_len + 1);
    if (!result) {
        return strdup("");
    }
    memcpy(result, str + byte_start, slice_len);
    result[slice_len] = '\0';
    return result;
}
/**
 * @brief Extract a substring with offset and length (TR#29 grapheme-aware)
 *
 * Implements ${var:offset:length} substring expansion. Offsets and
 * lengths are measured in Unicode grapheme clusters (TR#29), not bytes,
 * so multi-byte UTF-8 sequences are never split mid-character. A
 * negative offset counts from the end of the string in graphemes. A
 * negative length is an offset from the end: the substring ends
 * |length| graphemes before the string's end (bash semantics). When no
 * length is given (@p has_length false), the substring runs to the end.
 *
 * Uses the project's TR#29 primitives (lle_utf8_count_graphemes +
 * lush_slice_graphemes) so user content with combining marks, emoji
 * sequences, regional indicators, ZWJ joins, and other multi-codepoint
 * graphemes is handled correctly.
 *
 * @param str Source string (UTF-8)
 * @param offset Starting grapheme position (negative for from-end)
 * @param length Grapheme count; negative = from-end (see above)
 * @param has_length false when ${var:offset} was written with no length
 * @return Newly malloc'd substring (caller must free)
 */
char *lush_substring_extract(const char *str, int offset, int length,
                             bool has_length) {
    if (!str) {
        return strdup("");
    }

    size_t byte_len = strlen(str);
    int total = (int)lle_utf8_count_graphemes(str, byte_len);

    if (offset < 0) {
        offset = total + offset;
        if (offset < 0) {
            offset = 0;
        }
    }
    if (offset >= total) {
        return strdup("");
    }
    int remaining = total - offset;
    int final_len;
    if (!has_length) {
        /// ${var:offset} -- to end.
        final_len = remaining;
    } else if (length < 0) {
        /// Negative length: end |length| graphemes before the string
        /// end, i.e. keep (remaining - |length|) graphemes. An end that
        /// falls at or before the offset yields the empty string.
        final_len = remaining + length;
        if (final_len < 0) {
            final_len = 0;
        }
    } else {
        final_len = (length > remaining) ? remaining : length;
    }
    if (final_len <= 0) {
        return strdup("");
    }
    char *result = lush_slice_graphemes(str, byte_len, offset, final_len);
    return result ? result : strdup("");
}

/**
 * @brief Find prefix match length for # and ## operators
 *
 * Finds how many characters from the beginning of str match pattern.
 * Used for ${var#pattern} and ${var##pattern} expansion.
 *
 * @param str String to search
 * @param pattern Pattern to match
 * @param longest If true, find longest match (##), else shortest (#)
 * @return Number of characters matched from beginning
 */
/// True when byte offset @p off in @p str starts a TR#29 grapheme cluster.
///
/// The prefix and suffix strip operators try candidate split points and ask the
/// shared matcher whether each candidate matches the pattern. Offering a
/// candidate that falls INSIDE a character is what produced issue #682: for
/// `caf` + U+00E9 the byte-aligned suffix loop handed the matcher a lone
/// continuation byte, `decode_one` charitably treated that invalid byte as a
/// one-byte codepoint, `?` matched it, and one byte was stripped -- leaving a
/// truncated UTF-8 sequence on stdout.
///
/// Restricting candidates to character boundaries makes that unrepresentable
/// rather than merely unlikely: a split the operator cannot express is a split
/// it cannot emit. Offsets 0 and strlen are boundaries by definition.
static bool at_grapheme_boundary(const char *str, size_t str_len, size_t off) {
    if (off == 0 || off >= str_len) {
        return true;
    }
    /// A UTF-8 continuation byte is not a codepoint start, and
    /// lle_is_grapheme_boundary reports invalid UTF-8 as a boundary -- a lone
    /// continuation byte is invalid standing alone, so asking it directly
    /// would answer "yes" for exactly the mid-character offsets this is meant
    /// to reject. Screen those out first, the same way lush_substring_extract
    /// only consults the boundary algorithm at codepoint starts.
    if (((unsigned char)str[off] & 0xC0) == 0x80) {
        return false;
    }
    return lle_is_grapheme_boundary(str + off, str, str + str_len);
}

int lush_prefix_match_len(const char *str, const char *pattern, bool longest) {
    if (!str || !pattern) {
        return 0;
    }

    int str_len = strlen(str);
    int match_len = 0;

    for (int i = 0; i <= str_len; i++) {
        /// Only split on character boundaries -- see at_grapheme_boundary.
        if (!at_grapheme_boundary(str, (size_t)str_len, (size_t)i)) {
            continue;
        }
        char *substr = malloc(i + 1);
        if (!substr) {
            break;
        }

        strncpy(substr, str, i);
        substr[i] = '\0';

        if (lush_shell_pattern_match(substr, pattern)) {
            match_len = i;
            if (!longest) {
                free(substr);
                break; /// Return first (shortest) match
            }
        }
        free(substr);
    }

    return match_len;
}

/**
 * @brief Find suffix match length for % and %% operators
 *
 * Finds how many characters from the end of str match pattern.
 * Used for ${var%pattern} and ${var%%pattern} expansion.
 *
 * @param str String to search
 * @param pattern Pattern to match
 * @param longest If true, find longest match (%%), else shortest (%)
 * @return Number of characters matched from end
 */
int lush_suffix_match_len(const char *str, const char *pattern, bool longest) {
    if (!str || !pattern) {
        return 0;
    }

    int str_len = strlen(str);
    int match_len = 0;

    for (int i = 0; i <= str_len; i++) {
        /// Only split on character boundaries -- see at_grapheme_boundary.
        if (!at_grapheme_boundary(str, (size_t)str_len,
                                  (size_t)(str_len - i))) {
            continue;
        }
        const char *suffix = str + str_len - i;
        if (lush_shell_pattern_match(suffix, pattern)) {
            match_len = i;
            if (!longest) {
                break; /// Return first (shortest) match
            }
        }
    }

    return match_len;
}

/**
 * @brief Convert first character to uppercase
 *
 * Used for ${var^} parameter expansion.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
char *lush_case_first_upper(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    /// Allocate buffer for Unicode conversion (may need more space)
    size_t buf_size = len * 4 + 1; /// UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_toupper_first(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        /// Fallback to simple copy on error
        free(result);
        return strdup(str);
    }

    return result;
}

/**
 * @brief Convert first character to lowercase
 *
 * Used for ${var,} parameter expansion.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
char *lush_case_first_lower(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    /// Allocate buffer for Unicode conversion (may need more space)
    size_t buf_size = len * 4 + 1; /// UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_tolower_first(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        /// Fallback to simple copy on error
        free(result);
        return strdup(str);
    }

    return result;
}

/**
 * @brief Convert all characters to uppercase
 *
 * Used for ${var^^} parameter expansion.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
/**
 * @brief Pattern-restricted case modification
 *
 * `${var^^[pat]}` / `${var,,[pat]}` / `${var^[pat]}` / `${var,[pat]}`
 * apply case conversion only to characters that match `pattern`.
 * `pattern` is a glob pattern matched against each codepoint's UTF-8
 * byte sequence via lush_pattern_match. If `first_only` is true, only
 * the first matching codepoint is converted (the `^` / `,` operators);
 * otherwise all matches convert (`^^` / `,,`).
 *
 * Iteration is by codepoint via lle_utf8_decode_codepoint; case
 * mapping goes through lle_unicode_toupper_codepoint /
 * lle_unicode_tolower_codepoint so non-ASCII letters convert correctly.
 * lush_pattern_match is codepoint-aware, so ASCII patterns like
 * `[aeiou]`, Unicode literals/ranges like `[äöü]`, and Unicode
 * general-category char-classes like `[[:alpha:]]` all work.
 *
 * @param str Input string
 * @param pattern Glob pattern (may be NULL/empty -- treated as "any char")
 * @param to_upper true for uppercase conversion, false for lowercase
 * @param first_only true for `^` / `,`, false for `^^` / `,,`
 * @return Newly malloc'd converted string (caller frees with free())
 */
char *lush_case_pattern(const char *str, const char *pattern, bool to_upper,
                        bool first_only) {
    if (!str) {
        return strdup("");
    }
    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }
    /// Unicode case mapping can produce more bytes than the input
    /// (a single codepoint can map to a longer sequence), so over-
    /// allocate to UTF-8 worst-case 4x input + NUL.
    size_t out_cap = len * 4 + 1;
    char *result = malloc(out_cap);
    if (!result) {
        return strdup("");
    }
    bool any_pattern = (pattern && pattern[0]);

    size_t in_pos = 0;
    size_t out_pos = 0;
    size_t cp_index = 0;
    while (in_pos < len) {
        uint32_t cp;
        int consumed =
            lle_utf8_decode_codepoint(str + in_pos, len - in_pos, &cp);
        if (consumed <= 0) {
            /// Malformed UTF-8: copy the byte through unchanged and advance
            /// one. Defensive — most input is well-formed.
            if (out_pos < out_cap - 1) {
                result[out_pos++] = str[in_pos];
            }
            in_pos++;
            continue;
        }

        bool should_convert;
        if (first_only && cp_index > 0) {
            /// `^pat` / `,pat` only inspect the first codepoint of the
            /// expanded value; subsequent codepoints copy through.
            should_convert = false;
        } else if (!any_pattern) {
            should_convert = true;
        } else {
            char utf8_buf[5] = {0};
            int enc = lle_utf8_encode_codepoint(cp, utf8_buf);
            if (enc <= 0) {
                should_convert = false;
            } else {
                utf8_buf[enc] = '\0';
                should_convert = lush_shell_pattern_match(utf8_buf, pattern);
            }
        }

        uint32_t out_cp = cp;
        if (should_convert) {
            out_cp = to_upper ? lle_unicode_toupper_codepoint(cp)
                              : lle_unicode_tolower_codepoint(cp);
        }

        char enc_buf[4];
        int enc_len = lle_utf8_encode_codepoint(out_cp, enc_buf);
        if (enc_len <= 0) {
            /// Encoder failure: emit the original bytes verbatim.
            for (int k = 0; k < consumed && out_pos < out_cap - 1; k++) {
                result[out_pos++] = str[in_pos + k];
            }
        } else {
            for (int k = 0; k < enc_len && out_pos < out_cap - 1; k++) {
                result[out_pos++] = enc_buf[k];
            }
        }

        in_pos += (size_t)consumed;
        cp_index++;
    }
    result[out_pos] = '\0';
    return result;
}

char *lush_case_all_upper(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    /// Allocate buffer for Unicode conversion (may need more space)
    size_t buf_size = len * 4 + 1; /// UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_toupper(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        /// Fallback to simple copy on error
        free(result);
        return strdup(str);
    }

    return result;
}

/**
 * @brief Convert all characters to lowercase
 *
 * Used for ${var,,} parameter expansion.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
char *lush_case_all_lower(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    /// Allocate buffer for Unicode conversion (may need more space)
    size_t buf_size = len * 4 + 1; /// UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_tolower(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        /// Fallback to simple copy on error
        free(result);
        return strdup(str);
    }

    return result;
}

/**
 * @brief Capitalize each word (zsh-style ${(C)var})
 *
 * Converts the first character of each word to uppercase and the rest
 * to lowercase. Words are delimited by whitespace.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
char *lush_case_capitalize_words(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    /// Allocate buffer - capitalize shouldn't change length significantly
    size_t buf_size = len * 4 + 1; /// UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    const char *src = str;
    char *dst = result;
    bool word_start = true;

    while (*src) {
        /// Get UTF-8 codepoint length
        size_t cp_len = 1;
        unsigned char c = (unsigned char)*src;
        if (c >= 0xC0 && c < 0xE0)
            cp_len = 2;
        else if (c >= 0xE0 && c < 0xF0)
            cp_len = 3;
        else if (c >= 0xF0)
            cp_len = 4;

        /// Ensure we don't read past end
        size_t remaining = strlen(src);
        if (cp_len > remaining)
            cp_len = remaining;

        if (isspace((unsigned char)*src)) {
            *dst++ = *src++;
            word_start = true;
        } else if (word_start) {
            /// Uppercase the first character of word
            char temp[8] = {0};
            memcpy(temp, src, cp_len);
            char upper[16] = {0};
            size_t upper_len =
                lle_utf8_toupper(temp, cp_len, upper, sizeof(upper));
            if (upper_len != (size_t)-1 && upper_len < sizeof(upper)) {
                memcpy(dst, upper, upper_len);
                dst += upper_len;
            } else {
                memcpy(dst, src, cp_len);
                dst += cp_len;
            }
            src += cp_len;
            word_start = false;
        } else {
            /// Lowercase the rest
            char temp[8] = {0};
            memcpy(temp, src, cp_len);
            char lower[16] = {0};
            size_t lower_len =
                lle_utf8_tolower(temp, cp_len, lower, sizeof(lower));
            if (lower_len != (size_t)-1 && lower_len < sizeof(lower)) {
                memcpy(dst, lower, lower_len);
                dst += lower_len;
            } else {
                memcpy(dst, src, cp_len);
                dst += cp_len;
            }
            src += cp_len;
        }
    }
    *dst = '\0';

    return result;
}

/// True when a pattern opens any bash extglob group -- `?(`, `*(`, `+(`,
/// `@(`, `!(` -- and the feature is enabled. Two callers rely on this:
///   - the glob routing test needs it for `@(`/`+(`/`!(`, whose sigils are
///     not plain glob metacharacters (`*(` and `?(` also route via their
///     leading `*`/`?`, but detecting all five here is harmless);
///   - the longest-match test needs it for every VARIABLE-LENGTH group
///     including `?(a|abc)` and `+(o)` -- there `?` is not enough, since a
///     plain `?` is a single-character wildcard that needs no longest-match.
/// Gated on FEATURE_EXTENDED_GLOB, the same gate the matcher uses (#567), so
/// with the feature off the sigils stay literal.
bool lush_pattern_opens_extglob_group(const char *pattern) {
    if (!pattern || !shell_mode_allows(FEATURE_EXTENDED_GLOB)) {
        return false;
    }
    for (const char *p = pattern; *p; p++) {
        if ((*p == '?' || *p == '*' || *p == '+' || *p == '@' || *p == '!') &&
            p[1] == '(') {
            return true;
        }
    }
    return false;
}

/**
 * @brief Pattern substitution for ${var/pattern/replacement}
 *
 * Replaces pattern matches in str with replacement.
 * Supports glob patterns (* and ?).
 *
 * @param str Source string
 * @param pattern Pattern to match (supports * and ?)
 * @param replacement Replacement string
 * @param global If true, replace all occurrences; if false, only first
 * @return New string with substitutions (caller must free)
 */
char *lush_pattern_substitute(const char *str, const char *pattern,
                              const char *replacement, bool global) {
    if (!str) {
        return strdup("");
    }
    if (!pattern || !pattern[0]) {
        return strdup(str);
    }
    if (!replacement) {
        replacement = "";
    }

    /// Bash anchored-substitution prefixes:
    ///   ${var/#pat/repl}  match pat at the START of str only
    ///   ${var/%pat/repl}  match pat at the END of str only
    /// Detect and strip the marker; the remainder is the real pattern.
    /// Anchored substitution implies a single replacement -- there is
    /// only one start and one end -- so global is ignored when anchored.
    /// Issue #96.
    bool anchor_start = false;
    bool anchor_end = false;
    if (pattern[0] == '#') {
        anchor_start = true;
        pattern++;
    } else if (pattern[0] == '%') {
        anchor_end = true;
        pattern++;
    }
    if (!pattern[0]) {
        return strdup(str);
    }

    size_t str_len = strlen(str);
    size_t pattern_len = strlen(pattern);
    size_t replacement_len = strlen(replacement);

    /// Detect glob metacharacters that route through lush_pattern_match.
    /// The original check missed `[` (character class) and treated `[bd]`
    /// patterns as exact-substring matches, which never matched because
    /// the literal string never contained `[bd]`. lush_pattern_match
    /// supports character classes natively.
    bool is_glob =
        (strchr(pattern, '*') || strchr(pattern, '?') || strchr(pattern, '[') ||
         lush_pattern_opens_extglob_group(pattern));

    /// Anchored-start: match pattern once at position 0, then copy the
    /// remainder. Anchored-end: match pattern once at the suffix, copy
    /// the prefix then the replacement. Both are simpler one-shot
    /// cases than the general scanner below.
    if (anchor_start) {
        size_t match_len = 0;
        bool matched = false;
        if (is_glob) {
            for (size_t try_len = 1; try_len <= str_len; try_len++) {
                char *substr = malloc(try_len + 1);
                if (!substr) {
                    break;
                }
                memcpy(substr, str, try_len);
                substr[try_len] = '\0';
                if (lush_shell_pattern_match(substr, pattern)) {
                    matched = true;
                    match_len = try_len;
                    /// Variable-length patterns (`*`, and extglob groups like
                    /// `+(o)` / `@(a|abc)`) take the longest match at this
                    /// position, matching lush's own longest-leftmost rule.
                    if (strchr(pattern, '*') ||
                        lush_pattern_opens_extglob_group(pattern)) {
                        for (size_t longer = try_len + 1; longer <= str_len;
                             longer++) {
                            char *l = malloc(longer + 1);
                            if (!l) {
                                break;
                            }
                            memcpy(l, str, longer);
                            l[longer] = '\0';
                            if (lush_shell_pattern_match(l, pattern)) {
                                match_len = longer;
                            }
                            free(l);
                        }
                    }
                    free(substr);
                    break;
                }
                free(substr);
            }
        } else {
            if (str_len >= pattern_len &&
                strncmp(str, pattern, pattern_len) == 0) {
                matched = true;
                match_len = pattern_len;
            }
        }
        if (!matched) {
            return strdup(str);
        }
        size_t tail_len = str_len - match_len;
        char *result = malloc(replacement_len + tail_len + 1);
        if (!result) {
            return strdup(str);
        }
        memcpy(result, replacement, replacement_len);
        memcpy(result + replacement_len, str + match_len, tail_len);
        result[replacement_len + tail_len] = '\0';
        return result;
    }

    if (anchor_end) {
        size_t match_len = 0;
        bool matched = false;
        if (is_glob) {
            /// Try suffixes from longest to shortest. For * patterns we
            /// want longest; for fixed-length patterns either order is
            /// fine. Longest-first matches bash.
            for (size_t try_len = str_len; try_len >= 1; try_len--) {
                size_t start = str_len - try_len;
                char *substr = malloc(try_len + 1);
                if (!substr) {
                    break;
                }
                memcpy(substr, str + start, try_len);
                substr[try_len] = '\0';
                if (lush_shell_pattern_match(substr, pattern)) {
                    matched = true;
                    match_len = try_len;
                    free(substr);
                    break;
                }
                free(substr);
            }
        } else {
            if (str_len >= pattern_len && strncmp(str + str_len - pattern_len,
                                                  pattern, pattern_len) == 0) {
                matched = true;
                match_len = pattern_len;
            }
        }
        if (!matched) {
            return strdup(str);
        }
        size_t head_len = str_len - match_len;
        char *result = malloc(head_len + replacement_len + 1);
        if (!result) {
            return strdup(str);
        }
        memcpy(result, str, head_len);
        memcpy(result + head_len, replacement, replacement_len);
        result[head_len + replacement_len] = '\0';
        return result;
    }

    /// Allocate result buffer - estimate size
    size_t result_size = str_len * 2 + 1;
    char *result = malloc(result_size);
    if (!result) {
        return strdup(str);
    }
    result[0] = '\0';
    size_t result_pos = 0;

    size_t i = 0;
    bool replaced = false;

    while (i < str_len) {
        /// Try to match pattern at current position
        bool matched = false;
        size_t match_len = 0;

        /// Simple pattern matching - check for exact match or glob
        if (is_glob) {
            /// Use lush_pattern_match for glob patterns
            /// Try increasing lengths to find the match
            for (size_t try_len = 1; try_len <= str_len - i; try_len++) {
                char *substr = malloc(try_len + 1);
                if (substr) {
                    strncpy(substr, str + i, try_len);
                    substr[try_len] = '\0';
                    if (lush_shell_pattern_match(substr, pattern)) {
                        matched = true;
                        match_len = try_len;
                        /// Variable-length patterns (`*`, and extglob groups
                        /// like `+(o)` / `@(a|abc)`) take the longest match at
                        /// this position, matching lush's own longest-leftmost
                        /// rule.
                        if (strchr(pattern, '*') ||
                            lush_pattern_opens_extglob_group(pattern)) {
                            for (size_t longer = try_len + 1;
                                 longer <= str_len - i; longer++) {
                                char *longer_substr = malloc(longer + 1);
                                if (longer_substr) {
                                    strncpy(longer_substr, str + i, longer);
                                    longer_substr[longer] = '\0';
                                    if (lush_shell_pattern_match(longer_substr,
                                                                 pattern)) {
                                        match_len = longer;
                                    }
                                    free(longer_substr);
                                }
                            }
                        }
                        free(substr);
                        break;
                    }
                    free(substr);
                }
            }
        } else {
            /// Exact substring match
            if (strncmp(str + i, pattern, pattern_len) == 0) {
                matched = true;
                match_len = pattern_len;
            }
        }

        if (matched && (!replaced || global)) {
            /// Ensure we have enough space
            if (result_pos + replacement_len + 1 >= result_size) {
                result_size = result_size * 2 + replacement_len;
                char *new_result = realloc(result, result_size);
                if (!new_result) {
                    free(result);
                    return strdup(str);
                }
                result = new_result;
            }

            /// Copy replacement
            strcpy(result + result_pos, replacement);
            result_pos += replacement_len;
            i += match_len;
            replaced = true;
        } else {
            /// No match, copy current character
            if (result_pos + 1 >= result_size) {
                result_size *= 2;
                char *new_result = realloc(result, result_size);
                if (!new_result) {
                    free(result);
                    return strdup(str);
                }
                result = new_result;
            }
            result[result_pos++] = str[i++];
        }
    }

    result[result_pos] = '\0';
    return result;
}
/// Operators computed entirely from (value, operand). See param_op.h.
bool lush_param_op_is_pure(int op_type) {
    switch (op_type) {
    case 0:  /// ${var:-default}
    case 1:  /// ${var:+alternative}
    case 2:  /// ${var##pattern}
    case 3:  /// ${var%%pattern}
    case 4:  /// ${var^^[pat]}
    case 5:  /// ${var,,[pat]}
    case 6:  /// ${var#pattern}
    case 7:  /// ${var%pattern}
    case 8:  /// ${var^[pat]}
    case 9:  /// ${var,[pat]}
    case 10: /// ${var-default}
    case 11: /// ${var+alternative}
    case 12: /// ${var:=default}
    case 13: /// ${var=default}
    case 14: /// ${var:offset:length} -- the SUBSTRING form only; the caller
             /// resolves zsh modifier chains and `$`-expanding specs first
    case 15: /// ${var//pattern/replacement}
    case 16: /// ${var/pattern/replacement}
        return true;
    default: /// 17 (@ transforms), 18/19 (:? / ? errors)
        return false;
    }
}

/// Split a `pattern/replacement` substitution spec at the first UNESCAPED `/`
/// and canonicalize `\/` to `/` in the pattern half. `${path//\//.}` has the
/// pattern `\/` (a literal slash) and the replacement `.`; a plain strchr split
/// would take that escaped slash as the separator and silently return the
/// original string. Backslash escapes other than `\/` pass through to the
/// matcher, which handles them per the glob spec. Issue #96.
/// Returns the owned pattern; *replacement points into @p spec (or "" when the
/// spec carries no separator, i.e. delete the match).
char *lush_param_op_split_substitution_spec(const char *spec,
                                            const char **replacement) {
    *replacement = "";
    const char *sep = NULL;
    for (const char *p = spec; *p; p++) {
        if (*p == '\\' && p[1] == '/') {
            p++;
            continue;
        }
        if (*p == '/') {
            sep = p;
            break;
        }
    }
    size_t plen = sep ? (size_t)(sep - spec) : strlen(spec);
    char *pattern = malloc(plen + 1);
    if (!pattern) {
        return NULL;
    }
    size_t pj = 0;
    for (size_t pi = 0; pi < plen; pi++) {
        if (spec[pi] == '\\' && pi + 1 < plen && spec[pi + 1] == '/') {
            pattern[pj++] = '/';
            pi++;
        } else {
            pattern[pj++] = spec[pi];
        }
    }
    pattern[pj] = '\0';
    if (sep) {
        *replacement = sep + 1;
    }
    return pattern;
}

/// Which branch of a conditional operator consumes the operand. See param_op.h.
/// The trigger conditions are the same ones lush_param_op_apply switches on, so
/// this predicate and the operator it gates cannot disagree about which branch
/// runs. Verified against bash and zsh across every (operator, value-state)
/// pair: both evaluate the operand only on the consuming branch.
bool lush_param_op_consumes_operand(int op_type, const char *var_value) {
    bool unset = (var_value == NULL);
    bool empty_or_unset = (!var_value || var_value[0] == '\0');
    switch (op_type) {
    case 0:  /// ${var:-w}   -- w only when unset OR null
    case 12: /// ${var:=w}   -- assigns w only when unset OR null
    case 18: /// ${var:?w}   -- w is the message, printed only when it fires
        return empty_or_unset;
    case 10: /// ${var-w}    -- w only when unset
    case 13: /// ${var=w}    -- assigns w only when unset
    case 19: /// ${var?w}    -- message, only when it fires
        return unset;
    case 1: /// ${var:+w}    -- w only when set AND non-null
        return !empty_or_unset;
    case 11: /// ${var+w}    -- w only when set
        return !unset;
    default:
        /// Pattern strip, case conversion, substring, substitution and the
        /// transforms all need their operand on every branch.
        return true;
    }
}

/// The required-parameter operators' pure half. See param_op.h.
bool lush_param_op_required_fires(int op_type, const char *var_value) {
    switch (op_type) {
    case 18: /// ${var:?word} -- unset OR null
        return !var_value || var_value[0] == '\0';
    case 19: /// ${var?word} -- unset only (a null value is permitted)
        return !var_value;
    default:
        return false;
    }
}

char *lush_param_op_required_value(const char *var_value) {
    return strdup(var_value ? var_value : "");
}

char *lush_param_op_apply(int op_type, const char *var_value,
                          const char *operand, bool *assign_back) {
    if (assign_back) {
        *assign_back = false;
    }
    if (!lush_param_op_is_pure(op_type)) {
        return NULL;
    }
    /// An absent operand is the empty default / empty pattern; var_value keeps
    /// its NULL (unset) meaning.
    const char *deflt = operand ? operand : "";
    char *result = NULL;

    switch (op_type) {
    case 0: /// ${var:-default} - use default if var is unset or empty
        result = strdup(is_empty_or_null(var_value) ? deflt : var_value);
        break;

    case 1: /// ${var:+alternative} - use alternative if var is set and
            /// non-empty
        result = strdup(!is_empty_or_null(var_value) ? deflt : "");
        break;

    case 2: /// ${var##pattern} - remove longest match from the beginning
    case 6: /// ${var#pattern}  - remove shortest match from the beginning
        if (var_value) {
            int match_len =
                lush_prefix_match_len(var_value, deflt, op_type == 2);
            result = strdup(var_value + match_len);
        } else {
            result = strdup("");
        }
        break;

    case 3: /// ${var%%pattern} - remove longest match from the end
    case 7: /// ${var%pattern}  - remove shortest match from the end
        if (var_value) {
            int str_len = strlen(var_value);
            int match_len =
                lush_suffix_match_len(var_value, deflt, op_type == 3);
            int result_len = str_len - match_len;
            result = malloc((size_t)result_len + 1);
            if (result) {
                memcpy(result, var_value, (size_t)result_len);
                result[result_len] = '\0';
            } else {
                result = strdup("");
            }
        } else {
            result = strdup("");
        }
        break;

    case 4: /// ${var^^[pat]} - uppercase all (matching) characters
    case 5: /// ${var,,[pat]} - lowercase all (matching) characters
    case 8: /// ${var^[pat]}  - uppercase the first (matching) character
    case 9: /// ${var,[pat]}  - lowercase the first (matching) character
        if (var_value) {
            bool to_upper = (op_type == 4 || op_type == 8);
            bool first_only = (op_type == 8 || op_type == 9);
            /// Pattern restriction (issue #96): ${var^^[abc]} converts only
            /// the characters matching the glob pattern. An empty pattern
            /// falls through to the UTF-8-aware whole-string path so
            /// non-ASCII content converts correctly.
            if (deflt[0]) {
                result =
                    lush_case_pattern(var_value, deflt, to_upper, first_only);
            } else if (first_only) {
                result = to_upper ? lush_case_first_upper(var_value)
                                  : lush_case_first_lower(var_value);
            } else {
                result = to_upper ? lush_case_all_upper(var_value)
                                  : lush_case_all_lower(var_value);
            }
        } else {
            result = strdup("");
        }
        break;

    case 10: /// ${var-default} - use default if var is unset (not if empty)
        result = strdup(var_value ? var_value : deflt);
        break;

    case 11: /// ${var+alternative} - use alternative if var is set (even if
             /// empty)
        result = strdup(var_value ? deflt : "");
        break;

    case 12: /// ${var:=default} - assign default if var is unset or empty
    case 13: /// ${var=default}  - assign default if var is unset
        if (op_type == 12 ? is_empty_or_null(var_value) : !var_value) {
            if (assign_back) {
                *assign_back = true;
            }
            result = strdup(deflt);
        } else {
            result = strdup(var_value);
        }
        break;

    case 14: /// ${var:offset[:length]} - substring by grapheme
        if (var_value) {
            char *endptr = NULL;
            int offset = (int)strtol(deflt, &endptr, 10);
            int length = 0;
            bool has_length = false;
            if (*endptr == ':') {
                length = (int)strtol(endptr + 1, NULL, 10);
                has_length = true;
            }
            result =
                lush_substring_extract(var_value, offset, length, has_length);
        } else {
            result = strdup("");
        }
        break;

    case 15: /// ${var//pattern/replacement} - replace all occurrences
    case 16: /// ${var/pattern/replacement}  - replace the first occurrence
        if (var_value) {
            const char *replacement = "";
            char *pattern =
                lush_param_op_split_substitution_spec(deflt, &replacement);
            if (pattern) {
                result = lush_pattern_substitute(var_value, pattern,
                                                 replacement, op_type == 15);
                free(pattern);
            } else {
                result = strdup(var_value);
            }
        } else {
            result = strdup("");
        }
        break;
    }

    return result ? result : strdup("");
}
