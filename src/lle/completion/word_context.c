/**
 * @file word_context.c
 * @brief Quote/escape/expansion-aware word-context analyzer for completion
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements:
 *   - Codepoint-by-codepoint walk with quote/escape state tracking
 *   - Word boundary detection respecting quote/escape state
 *   - Path splitting (filename_portion_start = byte after last unquoted '/')
 *   - NFC-normalized dequoted_filename_prefix extraction
 *   - context_type detection (COMMAND_POSITION, ARGUMENT, REDIRECT_TARGET,
 *     VARIABLE_NAME, UNKNOWN)
 *   - Command name + arg index extraction
 *   - In-progress expansion-kind detection (cursor inside an open
 *     ${, $(, $((, `, {, glob)
 *
 * Not yet implemented in this revision:
 *   - Resolved expanded_directory via src/expand.c (the field is left NULL
 *     when expansion bytes appear in the typed shell-word).
 *   - Multi-value brace branches[] population (left NULL/0).
 *   - FOR_IN_LIST / CASE_PATTERN / FUNCTION_BODY / HEREDOC_BODY context
 *     types (fall through to UNKNOWN; callers may elect to refuse
 *     completion in those positions until populated).
 *
 * Walk strategy:
 *   The analyzer walks from byte 0 of the buffer up to the cursor, tracking
 *   quote/escape state, paren/brace/bracket depth, and command/argument
 *   structure. At the cursor it freezes the state and computes the output
 *   fields. Multiline buffers are walked the same way; newlines inside an
 *   open quote do not terminate the word.
 *
 *   This is intentionally simpler than re-using the parser tokenizer
 *   (src/tokenizer.c). The tokenizer is heavyweight, allocates token
 *   structures, and is designed for parsing rather than partial-line
 *   inspection. The analyzer's walker shares the same lexical rules
 *   (POSIX shell quoting + the lush-recognized expansions) but stops
 *   strictly at the cursor and produces only what completion needs.
 *
 * Memory:
 *   All allocations are made from the supplied pool. The struct itself and
 *   its owned strings are reclaimed when the pool is destroyed; the
 *   public lle_word_context_free is therefore a near-no-op kept for
 *   symmetry and future-proofing.
 */

#include "lle/completion/word_context.h"

#include "lle/unicode_compare.h"
#include "lle/utf8_support.h"

#include <stddef.h>
#include <string.h>

/* ============================================================================
 * Walker — internal state held during one analyze() call
 * ============================================================================
 */

typedef struct walker {
    const char *buffer;
    size_t      buffer_len;
    size_t      cursor;

    /* Position. Always advances by full UTF-8 sequence. */
    size_t pos;

    /* Quote / escape state. ESCAPE_PENDING is encoded as the bool
     * `escape_pending` flag plus the underlying quote_state (which may be
     * NONE or DOUBLE — escape inside SINGLE is impossible). The cursor's
     * quote_state in the public output reflects this combined state. */
    bool in_single;
    bool in_double;
    bool in_backtick;
    bool escape_pending;

    /* Bracket / paren tracking outside of single quotes (single quotes
     * suppress everything). Used to detect $(, $(( and matching close. */
    int paren_depth;
    int brace_depth;
    int bracket_depth;

    /* Tracking the most recent unambiguous statement boundary (start of a
     * fresh command). Updated when we see ;, &, &&, ||, |, newline, or (
     * outside any open quote/expression. The most recent value is where
     * the next non-whitespace word begins as a command name. */
    size_t last_statement_start;

    /* Tracking the start of the CURRENT shell-word and CURRENT command's
     * first word. The current word's start is updated on each whitespace
     * boundary outside open quote/escape. The command's first word is
     * the first non-whitespace word seen since the most recent statement
     * boundary. */
    size_t current_word_start;        /* SIZE_MAX if not in a word */
    size_t current_command_word_start; /* SIZE_MAX if not seen yet */
    size_t current_command_word_end;   /* SIZE_MAX if not seen yet */
    int    current_arg_index;          /* -1 if no command word yet */

    /* Set when we cross a redirect operator and the next non-ws word
     * should be a redirect target. Cleared when we consume that word. */
    bool next_word_is_redirect_target;

    /* Set when we are at command position (start of buffer or after a
     * statement separator). Cleared when we consume a word at this
     * position. */
    bool at_command_position;
} walker_t;

/* ============================================================================
 * Codepoint helpers
 * ============================================================================
 */

/* Decode the codepoint at pos. Returns bytes consumed (>= 1) and the cp
 * via *out. On invalid UTF-8 returns 1 and out is the raw byte. */
static int decode_at(const char *buf, size_t len, size_t pos, uint32_t *out) {
    int n = lle_utf8_decode_codepoint(buf + pos, len - pos, out);
    if (n <= 0) {
        *out = (unsigned char)buf[pos];
        return 1;
    }
    return n;
}

/* Whitespace check restricted to ASCII whitespace meaningful to the shell.
 * The buffer's quote-aware walker treats whitespace as a word boundary
 * only outside open quotes/escape; inside quotes whitespace is literal. */
static bool is_shell_whitespace(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n';
}

/* True if the codepoint terminates a statement at the lexical level
 * (outside any quote/expression nesting). ;, &, |, newline, ( all end the
 * current command. */
static bool is_statement_terminator(uint32_t cp) {
    return cp == ';' || cp == '&' || cp == '|' || cp == '\n' || cp == '(';
}

/* True if cp is a redirect operator first char (>, <). The tokenizer
 * recognizes >, >>, <, <<, <<<, &> etc.; the analyzer treats any
 * occurrence outside a quoted span as "redirect coming up; next word is
 * the target." Compound operators like >> and <<< are still detected by
 * a single < or > byte being present. */
static bool is_redirect_char(uint32_t cp) { return cp == '>' || cp == '<'; }

/* True if cp can begin a variable name (per POSIX: [_A-Za-z]). */
static bool is_var_name_start(uint32_t cp) {
    return cp == '_' || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

/* True if cp can continue a variable name (per POSIX: [_A-Za-z0-9]). */
static bool is_var_name_cont(uint32_t cp) {
    return is_var_name_start(cp) || (cp >= '0' && cp <= '9');
}

/* ============================================================================
 * Walker step — one codepoint forward
 * ============================================================================
 *
 * The walker advances pos by the full UTF-8 sequence length of the codepoint
 * at the current pos. State transitions:
 *
 *   ESCAPE_PENDING → cleared (the escaped char is consumed as literal).
 *   SINGLE quote   → only ' returns to NONE; everything else is literal.
 *   DOUBLE quote   → " returns to NONE; \ sets ESCAPE_PENDING; everything
 *                    else is literal.
 *   BACKTICK quote → ` returns to NONE; \ sets ESCAPE_PENDING.
 *   NONE quote     → ' enters SINGLE, " enters DOUBLE, ` enters BACKTICK,
 *                    \ sets ESCAPE_PENDING; whitespace ends the current
 *                    word; (, ;, &, |, newline are statement separators
 *                    (when paren/brace/bracket depth is 0).
 *
 * Outside of any open quote/expression nesting, command/argument tracking
 * runs:
 *   - Hitting whitespace ends the current word.
 *   - Hitting a statement terminator resets at_command_position to true
 *     and clears current_command_word state.
 *   - Hitting a redirect char primes next_word_is_redirect_target.
 *   - Hitting a non-whitespace, non-special char while not in a word
 *     starts a new word.
 */
static void walker_advance_one(walker_t *w) {
    uint32_t cp;
    int n = decode_at(w->buffer, w->buffer_len, w->pos, &cp);

    /* ESCAPE_PENDING absorbs whatever this codepoint is. */
    if (w->escape_pending) {
        w->escape_pending = false;
        /* We're in a word (the backslash itself opened/extended one). */
        w->pos += (size_t)n;
        return;
    }

    /* SINGLE quote: only ' returns to NONE. */
    if (w->in_single) {
        if (cp == '\'') {
            w->in_single = false;
        }
        w->pos += (size_t)n;
        return;
    }

    /* DOUBLE quote */
    if (w->in_double) {
        if (cp == '"') {
            w->in_double = false;
        } else if (cp == '\\') {
            w->escape_pending = true;
        }
        /* `, $ inside double quotes can begin nested command-sub /
         * variable expansion. The analyzer does not dive into them
         * structurally in this revision — they are treated as
         * literal-bearing text inside the double-quoted span. Nested
         * $VAR / $(...) inside "..." will be handled when expansion
         * resolution is added. */
        w->pos += (size_t)n;
        return;
    }

    /* BACKTICK quote */
    if (w->in_backtick) {
        if (cp == '`') {
            w->in_backtick = false;
        } else if (cp == '\\') {
            w->escape_pending = true;
        }
        w->pos += (size_t)n;
        return;
    }

    /* NONE: word/structure tracking */

    /* Quote starters */
    if (cp == '\'') {
        w->in_single = true;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }
    if (cp == '"') {
        w->in_double = true;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }
    if (cp == '`') {
        w->in_backtick = true;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }
    if (cp == '\\') {
        w->escape_pending = true;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }

    /* Paren / brace / bracket tracking. We track depth so $(...) and
     * $((...)) and {...} are seen as enclosed. Outside any quote, ( at
     * depth 0 is also a statement boundary in pipeline-grouping
     * contexts; we treat it as both (boundary + depth++). */
    if (cp == '(') {
        if (w->paren_depth == 0 && w->brace_depth == 0 &&
            w->bracket_depth == 0) {
            /* Subshell start at top level — also treat as statement
             * boundary. */
            w->last_statement_start              = w->pos + (size_t)n;
            w->at_command_position               = true;
            w->current_command_word_start        = SIZE_MAX;
            w->current_command_word_end          = SIZE_MAX;
            w->current_arg_index                 = -1;
            w->next_word_is_redirect_target      = false;
        }
        w->paren_depth++;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }
    if (cp == ')') {
        if (w->paren_depth > 0) w->paren_depth--;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }
    if (cp == '{') {
        w->brace_depth++;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }
    if (cp == '}') {
        if (w->brace_depth > 0) w->brace_depth--;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }
    if (cp == '[') {
        w->bracket_depth++;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }
    if (cp == ']') {
        if (w->bracket_depth > 0) w->bracket_depth--;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }

    /* Statement terminators outside any nesting. */
    if (is_statement_terminator(cp) && w->paren_depth == 0 &&
        w->brace_depth == 0 && w->bracket_depth == 0) {
        /* Close the current word, if any. */
        w->current_word_start = SIZE_MAX;
        /* Reset command tracking. */
        w->last_statement_start              = w->pos + (size_t)n;
        w->at_command_position               = true;
        w->current_command_word_start        = SIZE_MAX;
        w->current_command_word_end          = SIZE_MAX;
        w->current_arg_index                 = -1;
        w->next_word_is_redirect_target      = false;
        w->pos += (size_t)n;
        return;
    }

    /* Whitespace outside any quote ends the current word. */
    if (is_shell_whitespace(cp)) {
        if (w->current_word_start != SIZE_MAX) {
            /* End-of-word transition. If this was the command word,
             * remember its end. */
            if (w->current_command_word_start == w->current_word_start) {
                w->current_command_word_end = w->pos;
                w->at_command_position      = false;
                w->current_arg_index        = 0;
            } else if (w->current_arg_index >= 0) {
                w->current_arg_index++;
            }
            /* If the just-finished word was a redirect target, clear the
             * flag so the FOLLOWING word is a normal arg again. */
            if (w->next_word_is_redirect_target) {
                w->next_word_is_redirect_target = false;
            }
            w->current_word_start = SIZE_MAX;
        }
        w->pos += (size_t)n;
        return;
    }

    /* Redirect operator chars at top level. We don't try to consume the
     * whole >> / << operator; we just flag that the NEXT word is a
     * redirect target. The redirect char itself does not start a word. */
    if (is_redirect_char(cp) && w->paren_depth == 0 && w->brace_depth == 0 &&
        w->bracket_depth == 0) {
        /* Close the current word if any. */
        if (w->current_word_start != SIZE_MAX) {
            if (w->current_command_word_start == w->current_word_start) {
                w->current_command_word_end = w->pos;
                w->at_command_position      = false;
                w->current_arg_index        = 0;
            }
            w->current_word_start = SIZE_MAX;
        }
        w->next_word_is_redirect_target = true;
        w->pos += (size_t)n;
        return;
    }

    /* Default: ordinary word codepoint. Start a new word if not already in
     * one; if at command position, mark it as the command word. */
    if (w->current_word_start == SIZE_MAX) {
        w->current_word_start = w->pos;
        if (w->at_command_position && w->current_command_word_start == SIZE_MAX) {
            w->current_command_word_start = w->pos;
        }
    }
    w->pos += (size_t)n;
}

/* ============================================================================
 * Dequote and NFC normalize the typed filename prefix
 * ============================================================================
 *
 * Given the byte range [filename_portion_start .. cursor) within the buffer,
 * produce a NFC-normalized literal string suitable for prefix matching by
 * sources.
 *
 * Rules:
 *   - Bytes inside an open ' span are taken literally (no escape
 *     processing).
 *   - Bytes inside an open " span are mostly literal except \ before
 *     $, `, \, " is consumed and the next byte is emitted unescaped;
 *     other backslashes pass through literally.
 *   - Bytes outside any quote are mostly literal except \ before any byte
 *     consumes the backslash and emits the next byte unescaped.
 *
 * The walker is run from the start of the relevant span; this function
 * just re-uses the same lexical rules but emits bytes into a buffer
 * instead of advancing state.
 *
 * Returns LLE_SUCCESS and writes a pool-allocated NUL-terminated string to
 * *out. The string is NFC-normalized.
 */
static lle_result_t dequote_range_to_nfc(const char *buf, size_t start,
                                         size_t end, lle_memory_pool_t *pool,
                                         char **out) {
    (void)pool; /* lle_pool_alloc is the canonical allocator; pool param
                   is reserved for future targeted pools. */

    if (end < start) return LLE_ERROR_INVALID_PARAMETER;

    /* First pass: dequote into a working buffer. Worst-case size is the
     * input range itself (dequoting only removes bytes). */
    size_t span = end - start;
    char  *raw  = lle_pool_alloc(span + 1);
    if (!raw) return LLE_ERROR_OUT_OF_MEMORY;

    /* Dequoting state machine (mirrors walker_advance_one but accumulating
     * literal bytes). */
    bool   in_single     = false;
    bool   in_double     = false;
    bool   esc_pending   = false;
    size_t out_pos       = 0;
    size_t i             = start;

    while (i < end) {
        uint32_t cp;
        int      n = decode_at(buf, end, i, &cp);

        if (esc_pending) {
            /* Emit the codepoint's bytes literally. */
            for (int k = 0; k < n; k++) raw[out_pos++] = buf[i + k];
            esc_pending = false;
            i += (size_t)n;
            continue;
        }

        if (in_single) {
            if (cp == '\'') {
                in_single = false;
                i += (size_t)n;
                continue;
            }
            for (int k = 0; k < n; k++) raw[out_pos++] = buf[i + k];
            i += (size_t)n;
            continue;
        }

        if (in_double) {
            if (cp == '"') {
                in_double = false;
                i += (size_t)n;
                continue;
            }
            if (cp == '\\') {
                /* Look ahead one codepoint; if it's $, `, \, ", consume the
                 * \ and emit the next char unescaped. Otherwise, emit \
                 * literally and let the next iteration handle the next
                 * char. POSIX double-quote behavior. */
                size_t   peek_i = i + (size_t)n;
                uint32_t peek_cp;
                int      peek_n =
                    (peek_i < end)
                        ? decode_at(buf, end, peek_i, &peek_cp)
                        : 0;
                if (peek_n > 0 &&
                    (peek_cp == '$' || peek_cp == '`' || peek_cp == '\\' ||
                     peek_cp == '"')) {
                    /* Skip the backslash, emit the escaped char. */
                    for (int k = 0; k < peek_n; k++)
                        raw[out_pos++] = buf[peek_i + k];
                    i = peek_i + (size_t)peek_n;
                    continue;
                }
                /* Otherwise emit the backslash literally. */
                raw[out_pos++] = '\\';
                i += (size_t)n;
                continue;
            }
            for (int k = 0; k < n; k++) raw[out_pos++] = buf[i + k];
            i += (size_t)n;
            continue;
        }

        /* NONE quote */
        if (cp == '\'') { in_single = true; i += (size_t)n; continue; }
        if (cp == '"')  { in_double = true; i += (size_t)n; continue; }
        if (cp == '\\') { esc_pending = true; i += (size_t)n; continue; }

        /* Ordinary literal byte. */
        for (int k = 0; k < n; k++) raw[out_pos++] = buf[i + k];
        i += (size_t)n;
    }
    raw[out_pos] = '\0';

    /* Second pass: NFC normalize. Worst case NFC output is roughly 3x
     * input for pathological cases; allocate generously. */
    size_t nfc_cap = (out_pos * 4) + 16;
    char  *nfc     = lle_pool_alloc(nfc_cap);
    if (!nfc) return LLE_ERROR_OUT_OF_MEMORY;

    size_t nfc_len = 0;
    int    rc      = lle_unicode_normalize_nfc(raw, out_pos, nfc, nfc_cap,
                                               &nfc_len);
    if (rc != 0) {
        /* Fallback: emit dequoted bytes as-is (still valid UTF-8 if the
         * input was; just not normalized). */
        memcpy(nfc, raw, out_pos);
        nfc_len = out_pos;
    }
    nfc[nfc_len] = '\0';

    *out = nfc;
    return LLE_SUCCESS;
}

/* ============================================================================
 * Compute filename_portion_start
 * ============================================================================
 *
 * Walk from word_start to cursor, tracking quote/escape state, and remember
 * the byte offset just AFTER the last unquoted, unescaped '/' encountered.
 * Returns word_start (or word_start+N to skip an open quote opener) if no
 * such '/' exists.
 *
 * Inside single quotes, a '/' is literal but still serves as a path
 * separator from the shell's perspective at completion time (since the
 * literal content of '/path/foo' is path-shaped). We treat '/' as a
 * separator regardless of quote state. The tokenizer's view (where '/'
 * inside '...' is just a literal character with no special meaning) is
 * different from completion's view (where the user's intent of typing
 * '/path/' is path navigation).
 */
static size_t compute_filename_portion_start(const char *buf, size_t word_start,
                                             size_t cursor) {
    size_t result      = word_start;
    bool   in_single   = false;
    bool   in_double   = false;
    bool   esc_pending = false;
    size_t i           = word_start;

    /* Skip an open quote opener if it's at word_start. */
    if (i < cursor) {
        uint32_t cp;
        int      n = decode_at(buf, cursor, i, &cp);
        if (cp == '\'' || cp == '"') {
            i += (size_t)n;
            result = i;
        }
    }

    while (i < cursor) {
        uint32_t cp;
        int      n = decode_at(buf, cursor, i, &cp);

        if (esc_pending) {
            esc_pending = false;
            i += (size_t)n;
            continue;
        }

        if (in_single) {
            if (cp == '\'') in_single = false;
            else if (cp == '/') result = i + 1;
            i += (size_t)n;
            continue;
        }
        if (in_double) {
            if (cp == '"') in_double = false;
            else if (cp == '\\') esc_pending = true;
            else if (cp == '/') result = i + 1;
            i += (size_t)n;
            continue;
        }

        /* NONE */
        if (cp == '\'') { in_single = true; i += (size_t)n; continue; }
        if (cp == '"')  { in_double = true; i += (size_t)n; continue; }
        if (cp == '\\') { esc_pending = true; i += (size_t)n; continue; }
        if (cp == '/')  { result = i + 1; i += (size_t)n; continue; }

        i += (size_t)n;
    }

    return result;
}

/* ============================================================================
 * In-progress expansion-kind detection
 * ============================================================================
 *
 * If the cursor is inside an open expansion that has NOT closed, set the
 * expansion_kind. Otherwise return LLE_EXPANSION_NONE.
 *
 * Detection looks at the bytes between word_start and cursor:
 *   $name|        (no { or () after $)              → VARIABLE_NAME
 *   ${name|       (an unmatched ${ )                → BRACED_VARIABLE_NAME
 *   $(...|        (an unmatched $( without (( )     → COMMAND_SUBST
 *   $((...| ))    (an unmatched $((  )              → ARITHMETIC
 *   {a,b|         (an unmatched {  with at least one comma)
 *                                                    → BRACE_LIST
 *   *foo|, ?foo|, [...] glob char in word           → GLOB
 *
 * Detection is intentionally simple (and only inspects within the current
 * shell-word). A full grammar-driven detector lives in src/tokenizer.c;
 * the analyzer's view here is an approximation that is correct for the
 * cases listed in the design doc's walkthroughs.
 */
static lle_expansion_kind_t detect_expansion_kind(const char *buf,
                                                  size_t word_start,
                                                  size_t cursor) {
    /* Track simple state: most recent unmatched-opener. */
    bool   esc       = false;
    bool   in_sgl    = false;
    bool   in_dbl    = false;
    int    paren_d   = 0;
    int    brace_d   = 0;
    bool   any_glob  = false;
    bool   in_dollar = false; /* just saw $, expecting name/{/( */
    bool   in_var_name        = false;
    bool   in_braced_var_name = false;
    bool   in_cmd_sub         = false;
    bool   in_arith           = false;
    bool   in_brace_list      = false;
    bool   brace_has_comma    = false;

    size_t i = word_start;
    while (i < cursor) {
        uint32_t cp;
        int      n = decode_at(buf, cursor, i, &cp);

        if (esc) { esc = false; i += (size_t)n; continue; }
        if (in_sgl) {
            if (cp == '\'') in_sgl = false;
            i += (size_t)n;
            continue;
        }
        if (in_dbl) {
            if (cp == '"') in_dbl = false;
            else if (cp == '\\') esc = true;
            else if (cp == '$' && !in_dollar) in_dollar = true;
            /* `$` inside "..." can begin an expansion; we still detect it. */
            i += (size_t)n;
            continue;
        }

        if (cp == '\'') { in_sgl = true; i += (size_t)n; continue; }
        if (cp == '"')  { in_dbl = true; i += (size_t)n; continue; }
        if (cp == '\\') { esc   = true; i += (size_t)n; continue; }

        if (in_dollar) {
            if (cp == '{') {
                in_braced_var_name = true; brace_d++;
            } else if (cp == '(') {
                /* peek for second ( → arithmetic */
                size_t   peek_i = i + (size_t)n;
                uint32_t peek_cp;
                int      peek_n =
                    (peek_i < cursor)
                        ? decode_at(buf, cursor, peek_i, &peek_cp)
                        : 0;
                if (peek_n > 0 && peek_cp == '(') {
                    in_arith = true;
                    paren_d += 2;
                    i = peek_i + (size_t)peek_n;
                    in_dollar = false;
                    continue;
                }
                in_cmd_sub = true;
                paren_d++;
            } else if (is_var_name_start(cp)) {
                in_var_name = true;
            }
            in_dollar = false;
            i += (size_t)n;
            continue;
        }

        if (in_var_name) {
            if (!is_var_name_cont(cp)) {
                in_var_name = false; /* name ended at this byte */
            }
            i += (size_t)n;
            continue;
        }

        if (in_braced_var_name) {
            if (cp == '}') { in_braced_var_name = false; brace_d--; }
            i += (size_t)n;
            continue;
        }

        if (in_cmd_sub || in_arith) {
            if (cp == ')') {
                paren_d--;
                if (in_arith && paren_d == 0) in_arith = false;
                else if (in_cmd_sub && paren_d == 0) in_cmd_sub = false;
            } else if (cp == '(') paren_d++;
            i += (size_t)n;
            continue;
        }

        if (cp == '$') { in_dollar = true; i += (size_t)n; continue; }
        if (cp == '{') {
            in_brace_list = true; brace_d++; i += (size_t)n; continue;
        }
        if (cp == '}') {
            if (brace_d > 0) brace_d--;
            if (brace_d == 0) {
                in_brace_list = false;
                brace_has_comma = false;
            }
            i += (size_t)n;
            continue;
        }
        if (cp == ',' && in_brace_list) {
            brace_has_comma = true;
            i += (size_t)n;
            continue;
        }
        if (cp == '*' || cp == '?' || cp == '[') any_glob = true;

        i += (size_t)n;
    }

    /* Resolve precedence: in-progress expansions take priority; glob is
     * only reported as "in-progress" when no other expansion is
     * unfinished and the word contains a glob char. */
    if (in_arith)           return LLE_EXPANSION_ARITHMETIC;
    if (in_cmd_sub)         return LLE_EXPANSION_COMMAND_SUBST;
    if (in_braced_var_name) return LLE_EXPANSION_BRACED_VARIABLE_NAME;
    if (in_var_name)        return LLE_EXPANSION_VARIABLE_NAME;
    if (in_dollar)          return LLE_EXPANSION_VARIABLE_NAME;
    if (in_brace_list && brace_has_comma) return LLE_EXPANSION_BRACE_LIST;
    if (any_glob)           return LLE_EXPANSION_GLOB;
    return LLE_EXPANSION_NONE;
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

lle_result_t lle_word_context_analyze(const char *buffer,
                                      size_t cursor_byte_offset,
                                      lle_memory_pool_t *pool,
                                      lle_word_context_t **out_context) {
    if (!buffer || !pool || !out_context) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t buf_len = strlen(buffer);
    if (cursor_byte_offset > buf_len) cursor_byte_offset = buf_len;

    /* Run the walker from byte 0 to the cursor. */
    walker_t w = (walker_t){
        .buffer                          = buffer,
        .buffer_len                      = buf_len,
        .cursor                          = cursor_byte_offset,
        .pos                             = 0,
        .in_single                       = false,
        .in_double                       = false,
        .in_backtick                     = false,
        .escape_pending                  = false,
        .paren_depth                     = 0,
        .brace_depth                     = 0,
        .bracket_depth                   = 0,
        .last_statement_start            = 0,
        .current_word_start              = SIZE_MAX,
        .current_command_word_start      = SIZE_MAX,
        .current_command_word_end        = SIZE_MAX,
        .current_arg_index               = -1,
        .next_word_is_redirect_target    = false,
        .at_command_position             = true,
    };

    while (w.pos < w.cursor) {
        walker_advance_one(&w);
    }

    /* Allocate output struct. */
    lle_word_context_t *ctx = lle_pool_alloc(sizeof(*ctx));
    if (!ctx) return LLE_ERROR_OUT_OF_MEMORY;
    memset(ctx, 0, sizeof(*ctx));
    ctx->pool = pool;

    /* Word coordinates. If the cursor sits at whitespace or at a position
     * with no active word, word_start = word_end = cursor (an empty word
     * being completed). */
    ctx->word_start = (w.current_word_start == SIZE_MAX) ? w.cursor
                                                          : w.current_word_start;
    ctx->word_end   = w.cursor;

    /* Quote state at cursor. */
    if (w.escape_pending) {
        ctx->quote_state = LLE_QUOTE_ESCAPE_PENDING;
    } else if (w.in_single) {
        ctx->quote_state = LLE_QUOTE_SINGLE;
    } else if (w.in_double) {
        ctx->quote_state = LLE_QUOTE_DOUBLE;
    } else if (w.in_backtick) {
        ctx->quote_state = LLE_QUOTE_BACKTICK;
    } else {
        ctx->quote_state = LLE_QUOTE_NONE;
    }

    /* In-progress expansion-kind. */
    ctx->expansion_kind =
        detect_expansion_kind(buffer, ctx->word_start, ctx->word_end);

    /* filename_portion_start: byte after last unquoted '/' in the word. */
    ctx->filename_portion_start =
        compute_filename_portion_start(buffer, ctx->word_start, ctx->word_end);

    /* expansion_prefix_end currently equals filename_portion_start. The
     * bytes from word_start to here are the "preserved-typed-prefix"
     * zone the engine never modifies. This field will be refined to
     * carry the precise end-of-expansion-bytes once expansion
     * resolution is added; for now, equating the two is sufficient
     * because no caller distinguishes them yet. */
    ctx->expansion_prefix_end = ctx->filename_portion_start;

    /* Dequote and NFC-normalize the filename prefix portion. */
    lle_result_t r =
        dequote_range_to_nfc(buffer, ctx->filename_portion_start,
                             ctx->word_end, pool, &ctx->dequoted_filename_prefix);
    if (r != LLE_SUCCESS) return r;

    /* Context type. */
    if (ctx->expansion_kind == LLE_EXPANSION_VARIABLE_NAME ||
        ctx->expansion_kind == LLE_EXPANSION_BRACED_VARIABLE_NAME) {
        ctx->context_type = LLE_CONTEXT_VARIABLE_NAME;
    } else if (w.next_word_is_redirect_target) {
        ctx->context_type = LLE_CONTEXT_REDIRECT_TARGET;
    } else if (w.at_command_position) {
        ctx->context_type = LLE_CONTEXT_COMMAND_POSITION;
    } else if (w.current_arg_index >= 0) {
        ctx->context_type = LLE_CONTEXT_ARGUMENT;
    } else {
        ctx->context_type = LLE_CONTEXT_UNKNOWN;
    }

    /* Command name (substring from buffer). */
    if (w.current_command_word_start != SIZE_MAX &&
        w.current_command_word_end != SIZE_MAX &&
        w.current_command_word_end > w.current_command_word_start) {
        size_t cn_len =
            w.current_command_word_end - w.current_command_word_start;
        char *cn = lle_pool_alloc(cn_len + 1);
        if (!cn) return LLE_ERROR_OUT_OF_MEMORY;
        memcpy(cn, buffer + w.current_command_word_start, cn_len);
        cn[cn_len]       = '\0';
        ctx->command_name = cn;
    } else {
        ctx->command_name = NULL;
    }

    ctx->arg_index = w.current_arg_index;

    /* expanded_directory and branches[] are not populated by this
     * revision. They will be filled once expansion resolution via
     * src/expand.c is integrated. */
    ctx->expanded_directory = NULL;
    ctx->branches           = NULL;
    ctx->branch_count       = 0;

    *out_context = ctx;
    return LLE_SUCCESS;
}

void lle_word_context_free(lle_word_context_t *context) {
    /* Pool-backed: nothing to do beyond accepting NULL. The pool reclaims
     * all owned memory at its destruction. */
    (void)context;
}

const char *lle_quote_state_name(lle_quote_state_t state) {
    switch (state) {
        case LLE_QUOTE_NONE:           return "NONE";
        case LLE_QUOTE_SINGLE:         return "SINGLE";
        case LLE_QUOTE_DOUBLE:         return "DOUBLE";
        case LLE_QUOTE_BACKTICK:       return "BACKTICK";
        case LLE_QUOTE_ESCAPE_PENDING: return "ESCAPE_PENDING";
    }
    return "INVALID";
}

const char *lle_expansion_kind_name(lle_expansion_kind_t kind) {
    switch (kind) {
        case LLE_EXPANSION_NONE:                return "NONE";
        case LLE_EXPANSION_VARIABLE_NAME:       return "VARIABLE_NAME";
        case LLE_EXPANSION_BRACED_VARIABLE_NAME:return "BRACED_VARIABLE_NAME";
        case LLE_EXPANSION_COMMAND_SUBST:       return "COMMAND_SUBST";
        case LLE_EXPANSION_ARITHMETIC:          return "ARITHMETIC";
        case LLE_EXPANSION_BRACE_LIST:          return "BRACE_LIST";
        case LLE_EXPANSION_GLOB:                return "GLOB";
    }
    return "INVALID";
}

const char *lle_word_context_type_name(lle_word_context_type_t type) {
    switch (type) {
        case LLE_CONTEXT_COMMAND_POSITION: return "COMMAND_POSITION";
        case LLE_CONTEXT_ARGUMENT:         return "ARGUMENT";
        case LLE_CONTEXT_REDIRECT_TARGET:  return "REDIRECT_TARGET";
        case LLE_CONTEXT_VARIABLE_NAME:    return "VARIABLE_NAME";
        case LLE_CONTEXT_ASSIGNMENT_VALUE: return "ASSIGNMENT_VALUE";
        case LLE_CONTEXT_FOR_IN_LIST:      return "FOR_IN_LIST";
        case LLE_CONTEXT_CASE_PATTERN:     return "CASE_PATTERN";
        case LLE_CONTEXT_FUNCTION_BODY:    return "FUNCTION_BODY";
        case LLE_CONTEXT_HEREDOC_BODY:     return "HEREDOC_BODY";
        case LLE_CONTEXT_UNKNOWN:          return "UNKNOWN";
    }
    return "INVALID";
}
