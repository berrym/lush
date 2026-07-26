/**
 * @file brace_match.c
 * @brief Implementation of lush_find_matching_brace (see brace_match.h).
 *
 * Codepoint-aware scan via lle_utf8_decode_codepoint: ASCII control
 * bytes drive the state machine; multi-byte sequences advance
 * atomically and never falsely trigger on a continuation byte that
 * happens to alias an ASCII quote/brace byte (UTF-8's self-synchronizing
 * property already makes this safe at the byte level, but going through
 * the canonical decoder is the principled choice and keeps this file
 * consistent with the rest of lush's text-handling code).
 *
 * A `(` group is scanned by a case-aware state machine so that a `case`
 * pattern's closing `)` -- which has no matching `(` -- is recognized as
 * shell syntax rather than a stray group close. Without this,
 * `$(case x in x) echo M;; esac)` mis-terminates at the pattern `)` (#494).
 * A `{` group is a plain quote-aware depth match (parameter/brace callers).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "brace_match.h"

#include "lle/utf8_support.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// Advance past one input unit at `s[*i]`. Multi-byte codepoints advance
/// atomically; malformed UTF-8 falls back to a single byte so the caller
/// never gets stuck. Returns the byte length advanced (always >= 1).
static size_t step_one(const char *s, size_t len, size_t i) {
    uint32_t cp = 0;
    int step = lle_utf8_decode_codepoint(s + i, len - i, &cp);
    return (step > 1) ? (size_t)step : 1;
}

/// Result of skip_span: a quote/escape/multi-byte unit was consumed, a single
/// plain ASCII byte is pending (caller handles it), or a quote ran
/// unterminated.
typedef enum { SPAN_PLAIN, SPAN_CONSUMED, SPAN_UNTERMINATED } span_result_t;

/**
 * @brief Skip a quote span, backslash escape, or multi-byte codepoint at
 * s[*ip].
 *
 * Handles the four quote dialects (`'...'`, `"..."`, `` `...` ``, `$'...'`)
 * with their escape rules, a bare `\X` (consumed as one unit), and multi-byte
 * UTF-8. On a plain single-byte ASCII character `*ip` is left unchanged and
 * SPAN_PLAIN is returned so the caller can interpret the byte structurally.
 * This is the quote logic shared by both the `{` and `(` scans.
 */
static span_result_t skip_span(const char *s, size_t len, size_t *ip) {
    size_t i = *ip;
    uint32_t cp = 0;
    int step = lle_utf8_decode_codepoint(s + i, len - i, &cp);
    if (step <= 0) {
        /// Malformed UTF-8: skip one byte defensively.
        *ip = i + 1;
        return SPAN_CONSUMED;
    }
    if (step > 1) {
        /// Multi-byte codepoint: advance atomically; no ASCII control byte can
        /// be embedded in it.
        *ip = i + (size_t)step;
        return SPAN_CONSUMED;
    }

    unsigned char c = (unsigned char)s[i];

    /// Outside any quote: backslash consumes the NEXT byte as one atomic unit,
    /// so `\\` advances by 2 and the byte after the pair is processed cleanly.
    if (c == '\\') {
        *ip = i + 2;
        return SPAN_CONSUMED;
    }

    /// `$'...'` -- ANSI-C quoting. `\X` IS an escape inside, so `\'` is a
    /// literal quote and does NOT close the quote.
    if (c == '$' && i + 1 < len && s[i + 1] == '\'') {
        i += 2; ///< past the `$'`
        while (i < len && s[i] != '\'') {
            if (s[i] == '\\' && i + 1 < len) {
                i += 2;
            } else {
                i += step_one(s, len, i);
            }
        }
        if (i >= len) {
            return SPAN_UNTERMINATED;
        }
        *ip = i + 1; ///< past the closing `'`
        return SPAN_CONSUMED;
    }

    /// `'...'` -- POSIX single-quoted: literal, no escape processing. Backslash
    /// is a literal byte (so the `'\''` idiom scans correctly).
    if (c == '\'') {
        i += 1;
        while (i < len && s[i] != '\'') {
            i += step_one(s, len, i);
        }
        if (i >= len) {
            return SPAN_UNTERMINATED;
        }
        *ip = i + 1;
        return SPAN_CONSUMED;
    }

    /// `"..."` and `` `...` `` -- backslash escapes the next byte as a unit.
    if (c == '"' || c == '`') {
        char q = (char)c;
        i += 1;
        while (i < len && s[i] != q) {
            if (s[i] == '\\' && i + 1 < len) {
                i += 2;
            } else {
                i += step_one(s, len, i);
            }
        }
        if (i >= len) {
            return SPAN_UNTERMINATED;
        }
        *ip = i + 1;
        return SPAN_CONSUMED;
    }

    return SPAN_PLAIN; ///< *ip unchanged
}

/// A plain byte that begins a new shell word (word_start becomes true after
/// it).
static bool is_word_separator(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v' ||
           c == '\n' || c == ';' || c == '&' || c == '|' || c == '(' ||
           c == ')';
}

/// A plain byte after which the NEXT word is at command position (the first
/// word of a command, where `case`/`esac` are reserved words rather than
/// ordinary arguments). `(` opens a subshell/group; `;`/`&`/`|`/newline end a
/// command. Deliberately excludes `{` to avoid entangling `${...}`.
static bool is_command_separator(unsigned char c) {
    return c == ';' || c == '&' || c == '|' || c == '(' || c == '\n';
}

/// True if `s[i..i+kwlen)` is exactly `kw` followed by a word boundary (end of
/// input or a delimiter that terminates an unquoted word). The caller ensures
/// `i` is at a word start.
static bool keyword_at(const char *s, size_t len, size_t i, const char *kw,
                       size_t kwlen) {
    if (i + kwlen > len || memcmp(s + i, kw, kwlen) != 0) {
        return false;
    }
    if (i + kwlen == len) {
        return true;
    }
    unsigned char after = (unsigned char)s[i + kwlen];
    /// A word ends at whitespace, a metacharacter separator, or a redirection
    /// operator. `#` does NOT end a word (it only starts a comment at a word
    /// start), so `case#x` is one literal word, not the keyword `case`.
    return is_word_separator(after) || after == '<' || after == '>';
}

/// The compound-command reserved words that introduce a command position for
/// what follows on the SAME line (`do case ...`, `then case ...`), so a `case`
/// after one of them is recognized rather than treated as an argument. Returns
/// the keyword's length, or 0 if `s[i]` is not one of them at a word boundary.
static size_t command_introducer_len(const char *s, size_t len, size_t i) {
    static const char *const kw[] = {"do", "then",  "else", "elif",
                                     "if", "while", "until"};
    static const size_t kwlen[] = {2, 4, 4, 4, 2, 5, 5};
    for (size_t k = 0; k < sizeof(kw) / sizeof(kw[0]); k++) {
        if (keyword_at(s, len, i, kw[k], kwlen[k])) {
            return kwlen[k];
        }
    }
    return 0;
}

/// One frame of the case-nesting stack.
typedef enum {
    CF_AWAIT_IN, ///< after `case`, consuming the subject word until `in`
    CF_PATTERN,  ///< after `in` or `;;`: reading a pattern, `)` closes it
    CF_BODY      ///< after a pattern `)`: the arm's commands
} case_state_t;

typedef struct {
    case_state_t state;
    int patdepth; ///< nested `(` depth WITHIN a pattern (extglob groups)
    bool
        alt_started; ///< a significant byte was seen in the current alternative
    bool at_item_start; ///< no pattern content since `in`/`;;` (where `esac`
                        ///< terminates the case rather than being a pattern)
} case_frame_t;

/**
 * @brief Find the `)` closing the `(` at s[0], treating a case-pattern `)` as
 *        case syntax rather than a group close (see file banner, #494).
 *
 * Layers a small case state machine over the shared quote-aware skip. `case`
 * and `esac` are recognized only at command position (so an `esac` argument in
 * an arm body does not pop the frame); `in` is recognized in the AWAIT_IN
 * state. A pattern `)` (patdepth 0) moves the arm to BODY without touching the
 * real group depth. Nesting uses a small inline frame stack with heap spill.
 */
static bool scan_paren_group_case_aware(const char *s, size_t len,
                                        size_t *out_offset) {
    int depth = 1; ///< real group depth; pattern `)` is excluded from this
    size_t i = 1;
    bool word_start = true; ///< right after the opening `(`
    bool cmd_pos = true;    ///< the next word is at command position

    case_frame_t inline_frames[16];
    case_frame_t *frames = inline_frames;
    size_t nframes = 0, fcap = 16;
    bool heap = false;
    bool matched = false;

    while (i < len) {
        span_result_t r = skip_span(s, len, &i);
        if (r == SPAN_UNTERMINATED) {
            goto done; ///< unterminated quote: no match
        }
        if (r == SPAN_CONSUMED) {
            /// A quoted/escaped/multi-byte run is part of a word and never a
            /// keyword; it consumes command position. In a pattern it is
            /// significant content, so a following `esac` is not a terminator.
            word_start = false;
            cmd_pos = false;
            if (nframes > 0 && frames[nframes - 1].state == CF_PATTERN) {
                frames[nframes - 1].alt_started = true;
                frames[nframes - 1].at_item_start = false;
            }
            continue;
        }

        unsigned char c = (unsigned char)s[i];
        case_state_t st = (nframes > 0) ? frames[nframes - 1].state : CF_BODY;

        /// `#` at a word boundary starts a comment to end of line, in any state
        /// (a comment between arms sits at pattern position). Mid-word `#`
        /// (word_start false) is ordinary text/glob and is left alone.
        if (word_start && c == '#') {
            while (i < len && s[i] != '\n') {
                i += 1;
            }
            word_start = true; ///< the newline (if any) is a separator
            continue;
        }

        /// Inside a pattern, only `esac` (case terminator at item start),
        /// pattern grouping, and alternation matter; the pattern `)` (at
        /// patdepth 0) is the fix's crux.
        if (nframes > 0 && st == CF_PATTERN) {
            case_frame_t *top = &frames[nframes - 1];
            /// `esac` at the start of a pattern item ends the case; elsewhere
            /// in a pattern list it is ordinary pattern text.
            if (word_start && top->at_item_start &&
                keyword_at(s, len, i, "esac", 4)) {
                nframes--;
                i += 4;
                word_start = false;
                cmd_pos = false;
                continue;
            }
            if (c == '(') {
                if (!top->alt_started) {
                    /// Optional leading pattern group `(pat)`: consume without
                    /// counting; its `)` will be the pattern close.
                    top->alt_started = true;
                } else {
                    top->patdepth++; ///< extglob `@(...)` etc.
                }
                top->at_item_start = false;
                word_start = true;
                i += 1;
                continue;
            }
            if (c == ')') {
                if (top->patdepth == 0) {
                    top->state = CF_BODY; ///< pattern close: depth untouched
                    word_start = true;
                    cmd_pos = true; ///< the arm body starts at command position
                } else {
                    top->patdepth--;
                    word_start = true;
                }
                i += 1;
                continue;
            }
            if (c == '|' && top->patdepth == 0) {
                top->alt_started = false; ///< next alternative, same item
                top->at_item_start = false;
                word_start = true;
                i += 1;
                continue;
            }
            if (is_word_separator(c)) {
                word_start = true;
            } else {
                top->alt_started = true;
                top->at_item_start = false;
                word_start = false;
            }
            i += 1;
            continue;
        }

        /// Command-position states (BODY / AWAIT_IN / no frame): recognize
        /// reserved words at a word start.
        if (word_start) {
            if (cmd_pos && keyword_at(s, len, i, "case", 4)) {
                if (nframes == fcap) {
                    size_t ncap = fcap * 2;
                    case_frame_t *grown =
                        heap ? realloc(frames, ncap * sizeof(case_frame_t))
                             : malloc(ncap * sizeof(case_frame_t));
                    if (!grown) {
                        goto done; ///< OOM: give up (no match) rather than
                                   ///< crash
                    }
                    if (!heap) {
                        memcpy(grown, inline_frames,
                               fcap * sizeof(case_frame_t));
                        heap = true;
                    }
                    frames = grown;
                    fcap = ncap;
                }
                frames[nframes].state = CF_AWAIT_IN;
                frames[nframes].patdepth = 0;
                frames[nframes].alt_started = false;
                frames[nframes].at_item_start = false;
                nframes++;
                i += 4;
                word_start = false;
                cmd_pos = false; ///< the case subject word follows
                continue;
            }
            if (nframes > 0 && st == CF_AWAIT_IN &&
                keyword_at(s, len, i, "in", 2)) {
                frames[nframes - 1].state = CF_PATTERN;
                frames[nframes - 1].patdepth = 0;
                frames[nframes - 1].alt_started = false;
                frames[nframes - 1].at_item_start = true;
                i += 2;
                word_start = false;
                cmd_pos = false;
                continue;
            }
            if (nframes > 0 && cmd_pos && keyword_at(s, len, i, "esac", 4)) {
                nframes--;
                i += 4;
                word_start = false;
                cmd_pos = false;
                continue;
            }
            /// A compound-command reserved word (`do case ...`, `then case
            /// ...`) keeps command position for the word that follows on the
            /// same line, so the `case` after it is recognized rather than
            /// treated as an argument.
            if (cmd_pos) {
                size_t intro = command_introducer_len(s, len, i);
                if (intro > 0) {
                    i += intro;
                    word_start = false;
                    /// cmd_pos stays true: a command follows the introducer.
                    continue;
                }
            }
            /// A `{` command group or `!` negation, as a standalone token,
            /// likewise introduces a command position (`{ case ...; }`,
            /// `! case ...`). Require a following word boundary so `{a,b}`
            /// brace expansion and `${...}` are not misread.
            if (cmd_pos && (c == '{' || c == '!') &&
                (i + 1 >= len || is_word_separator((unsigned char)s[i + 1]))) {
                word_start = true;
                /// cmd_pos stays true.
                i += 1;
                continue;
            }
        }

        /// Structural bytes and word-position bookkeeping.
        if (c == '(') {
            depth++;
            word_start = true;
            cmd_pos = true;
            i += 1;
            continue;
        }
        if (c == ')') {
            depth--;
            if (depth == 0) {
                *out_offset = i;
                matched = true;
                goto done;
            }
            word_start = true;
            cmd_pos = true;
            i += 1;
            continue;
        }
        if (c == ';') {
            /// In an arm body, `;;` / `;&` / `;;&` return to pattern position.
            if (nframes > 0 && st == CF_BODY) {
                if (i + 1 < len && s[i + 1] == ';') {
                    size_t adv = (i + 2 < len && s[i + 2] == '&') ? 3 : 2;
                    frames[nframes - 1].state = CF_PATTERN;
                    frames[nframes - 1].patdepth = 0;
                    frames[nframes - 1].alt_started = false;
                    frames[nframes - 1].at_item_start = true;
                    i += adv;
                    word_start = true;
                    cmd_pos = false; ///< a pattern, not a command, follows
                    continue;
                }
                if (i + 1 < len && s[i + 1] == '&') {
                    frames[nframes - 1].state = CF_PATTERN;
                    frames[nframes - 1].patdepth = 0;
                    frames[nframes - 1].alt_started = false;
                    frames[nframes - 1].at_item_start = true;
                    i += 2;
                    word_start = true;
                    cmd_pos = false;
                    continue;
                }
            }
            /// A lone `;` is a command separator.
            word_start = true;
            cmd_pos = true;
            i += 1;
            continue;
        }
        if (is_word_separator(c)) {
            word_start = true;
            if (is_command_separator(c)) {
                cmd_pos = true;
            }
            i += 1;
            continue;
        }

        /// An ordinary word byte: past the command name now, so subsequent
        /// words are arguments (not reserved words) until the next separator.
        word_start = false;
        cmd_pos = false;
        i += 1;
    }

done:
    if (heap) {
        free(frames);
    }
    return matched;
}

bool lush_find_matching_brace(const char *s, size_t len, size_t *out_offset) {
    if (!s || !out_offset) {
        return false;
    }
    if (len == 0) {
        len = strlen(s);
    }
    if (len < 2) {
        return false;
    }

    char open = s[0];
    char close;
    if (open == '{') {
        close = '}';
    } else if (open == '(') {
        /// A `(` group may contain a `case` whose pattern `)` must not be read
        /// as a group close; delegate to the structure-aware scanner.
        return scan_paren_group_case_aware(s, len, out_offset);
    } else {
        return false;
    }

    /// `{` group: plain quote-aware depth match.
    int depth = 1;
    size_t i = 1;
    while (i < len) {
        span_result_t r = skip_span(s, len, &i);
        if (r == SPAN_UNTERMINATED) {
            return false;
        }
        if (r == SPAN_CONSUMED) {
            continue;
        }
        unsigned char c = (unsigned char)s[i];
        if (c == (unsigned char)open) {
            depth++;
        } else if (c == (unsigned char)close) {
            depth--;
            if (depth == 0) {
                *out_offset = i;
                return true;
            }
        }
        i += 1;
    }
    return false;
}

/// Count codepoints over the first `nbytes` of `s` (a whole-codepoint walk via
/// the same atomic-advance rule as the scan), for a caller's column math.
static size_t count_codepoints(const char *s, size_t nbytes) {
    size_t n = 0, i = 0;
    while (i < nbytes) {
        i += step_one(s, nbytes, i);
        n++;
    }
    return n;
}

subscript_span_t scan_subscript_bounds(const char *s, size_t len) {
    /// Every failure returns this crisp 0/false span so an invalid result never
    /// carries a stale offset or has_ws (the header promises 0/false there).
    const subscript_span_t invalid = {0, 0, false, false};
    subscript_span_t span = {0, 0, false, false};
    if (!s) {
        return invalid;
    }
    if (len == 0) {
        len = strlen(s);
    }
    if (len < 2 || s[0] != '[') {
        return invalid;
    }

    /// Depth-counted `[`/`]` match over the SAME quote/escape/multi-byte core
    /// (`skip_span`) the brace matcher uses. `skip_span` already handles the
    /// four quote dialects, atomic `\X`, and multi-byte codepoints, so a `]`
    /// inside `'...'`/`"..."`/`` `...` ``/`$'...'` or after a `\` never closes.
    int depth = 1;
    size_t i = 1;
    while (i < len) {
        span_result_t r = skip_span(s, len, &i);
        if (r == SPAN_UNTERMINATED) {
            return invalid; ///< unterminated quote: no valid span
        }
        if (r == SPAN_CONSUMED) {
            continue; ///< quoted/escaped/multi-byte run: not a bracket or ws
        }
        unsigned char c = (unsigned char)s[i];

        /// Skip a nested command-substitution / parameter-expansion /
        /// arithmetic span so a `]` inside `$(...)`, `${...}`, or `$((...))`
        /// does not close the subscript. Compose with the canonical matcher
        /// rather than re-implement its quote/escape/nesting logic; `$((expr))`
        /// resolves correctly by anchoring at the first `(` (depth reaches 0 at
        /// the final
        /// `)`). (`` `...` `` is already consumed by skip_span above.)
        if (c == '$' && i + 1 < len && (s[i + 1] == '(' || s[i + 1] == '{')) {
            size_t sub = 0;
            if (lush_find_matching_brace(s + i + 1, len - (i + 1), &sub)) {
                i = i + 1 + sub + 1; ///< past the matched `)` or `}`
                continue;
            }
            /// Unbalanced nested `$(...)`/`${...}`: the construct swallows to
            /// end of buffer, so the subscript cannot be validly closed. Report
            /// invalid rather than recovering -- otherwise a later `]` that is
            /// semantically INSIDE the unterminated construct would falsely
            /// close the span (mirrors the SPAN_UNTERMINATED quote path).
            return invalid;
        }

        if (c == '[') {
            depth++;
        } else if (c == ']') {
            depth--;
            if (depth == 0) {
                span.close = i;
                span.codepoints = count_codepoints(s, i + 1);
                span.is_valid = true;
                return span;
            }
        } else if (c == ' ' || c == '\t') {
            /// Unquoted, unescaped interior whitespace (an escaped `\ ` or a
            /// quoted space is consumed by skip_span above, so it is not
            /// flagged -- only the whitespace that would break an outer word).
            span.has_ws = true;
        }
        i += 1;
    }
    return invalid; ///< no matching `]` within [s, s+len)
}
