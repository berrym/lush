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

#include "executor.h"
#include "lle/unicode_compare.h"
#include "lle/utf8_support.h"

#include <ctype.h>
#include <pwd.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * Walker — internal state held during one analyze() call
 * ============================================================================
 */

/* Keyword sequence state. The walker recognizes `for X in <list>` and
 * `case X in <patterns>` by transitioning through these states as
 * non-quoted words are consumed. The KW_AFTER_*_IN states represent
 * "cursor is currently in the for-list" or "in the case-pattern" and
 * persist until a statement terminator (;, &, |, newline) closes them
 * out. They reset to KW_NONE on any unexpected transition. */
typedef enum {
    KW_NONE,
    KW_AFTER_FOR,       /**< saw `for` at command position; expect var */
    KW_AFTER_FOR_VAR,   /**< saw `for X`; expect `in` */
    KW_AFTER_FOR_IN,    /**< saw `for X in`; cursor is in the list */
    KW_AFTER_CASE,      /**< saw `case` at command position; expect word */
    KW_AFTER_CASE_WORD, /**< saw `case X`; expect `in` */
    KW_AFTER_CASE_IN,   /**< saw `case X in`; cursor is in patterns */
} keyword_state_t;

/* Maximum heredoc delimiter length the walker captures. Real shell
 * delimiters are typically short identifiers; truncating at 256 bytes
 * covers every realistic case and bounds the walker's memory. */
#define WALKER_HEREDOC_DELIM_MAX 256

/* Maximum number of completed arguments (between the command word and
 * the current cursor's word) the walker captures. Real-world commands
 * fit within 64 arguments; longer commands have their tail
 * arguments dropped from the captured set, which only affects
 * subcommand-recognition heuristics that don't apply at deep
 * positions anyway. */
#define WALKER_MAX_CAPTURED_ARGS 64

typedef struct walker {
    const char *buffer;
    size_t buffer_len;
    size_t cursor;

    // Position. Always advances by full UTF-8 sequence.
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
    size_t current_word_start;         /**< SIZE_MAX if not in a word */
    size_t current_command_word_start; /**< SIZE_MAX if not seen yet */
    size_t current_command_word_end;   /**< SIZE_MAX if not seen yet */
    int current_arg_index;             /**< -1 if no command word yet */

    /* Set when we cross a redirect operator and the next non-ws word
     * should be a redirect target. Cleared when we consume that word. */
    bool next_word_is_redirect_target;

    /* Set when we are at command position (start of buffer or after a
     * statement separator). Cleared when we consume a word at this
     * position. */
    bool at_command_position;

    // Keyword sequence state for `for X in` and `case X in` tracking.
    keyword_state_t kw_state;

    /* Heredoc tracking. The walker recognizes `<<DELIM` and `<<-DELIM`
     * at top level, captures DELIM, and after the next newline enters
     * heredoc-body mode where bytes are literal until a line that
     * matches DELIM (allowing leading tabs if heredoc_dash). The
     * here-string operator `<<<` is distinguished from `<<` by lookahead
     * and is not treated as a heredoc. */
    bool expecting_heredoc_delim; /**< just consumed `<<` or `<<-` */
    bool heredoc_dash;            /* `<<-` form: leading tabs allowed
                                     on the delimiter line */
    bool heredoc_pending;         /* delimiter captured; body starts at
                                     next newline */
    bool in_heredoc_body;         /* between body-start newline and
                                     delimiter line */
    char heredoc_delim[WALKER_HEREDOC_DELIM_MAX];
    size_t heredoc_delim_len;
    size_t current_line_start; /* byte offset of the current line's
                                  first byte (start of buffer or
                                  byte after the most recent \n) */

    /* Captured byte ranges for completed arguments (arguments before
     * the cursor's current word, in command order, excluding the
     * command word itself). Used by builtin-arg sources to walk
     * subcommand hierarchies. */
    size_t arg_starts[WALKER_MAX_CAPTURED_ARGS];
    size_t arg_ends[WALKER_MAX_CAPTURED_ARGS];
    size_t arg_capture_count;
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

// True if cp can begin a variable name (per POSIX: [_A-Za-z]).
static bool is_var_name_start(uint32_t cp) {
    return cp == '_' || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

// True if cp can continue a variable name (per POSIX: [_A-Za-z0-9]).
static bool is_var_name_cont(uint32_t cp) {
    return is_var_name_start(cp) || (cp >= '0' && cp <= '9');
}

/* Compare a buffer range against a literal keyword. Keywords in shell
 * are ASCII so a byte compare is sufficient; this is one of the cases
 * where the unicode-comparison rule's ASCII-guaranteed exception
 * applies. */
static bool word_equals_keyword(const char *buf, size_t start, size_t end,
                                const char *kw) {
    if (end < start)
        return false;
    size_t len = end - start;
    size_t kw_len = strlen(kw);
    if (len != kw_len)
        return false;
    return memcmp(buf + start, kw, len) == 0;
}

/* True if a word at command position is one of the shell keywords that
 * introduces a fresh command position for the *next* word, rather than
 * itself being a command followed by arguments. After `do`, `then`,
 * `else`, `elif`, `until`, `while`, `if`, the next non-whitespace word
 * is at command position again. */
static bool word_is_command_introducer(const char *buf, size_t start,
                                       size_t end) {
    return word_equals_keyword(buf, start, end, "do") ||
           word_equals_keyword(buf, start, end, "then") ||
           word_equals_keyword(buf, start, end, "else") ||
           word_equals_keyword(buf, start, end, "elif") ||
           word_equals_keyword(buf, start, end, "until") ||
           word_equals_keyword(buf, start, end, "while") ||
           word_equals_keyword(buf, start, end, "if");
}

/* When a non-quoted word ends, advance the keyword state machine by
 * looking at the bytes that just made up the word and the position at
 * which it ended. Called from walker_advance_one when whitespace or a
 * redirect/statement separator terminates a word. */
static void advance_keyword_state(walker_t *w, size_t word_start,
                                  size_t word_end) {
    // Word at command position is the candidate keyword for state-entry.
    bool entered_at_cmd_position =
        (word_start == w->current_command_word_start);

    if (entered_at_cmd_position) {
        if (word_equals_keyword(w->buffer, word_start, word_end, "for")) {
            w->kw_state = KW_AFTER_FOR;
            return;
        }
        if (word_equals_keyword(w->buffer, word_start, word_end, "case")) {
            w->kw_state = KW_AFTER_CASE;
            return;
        }
        /* Any other word at command position resets the state machine —
         * the keyword sequence is broken by a non-`for`/`case` command. */
        w->kw_state = KW_NONE;
        return;
    }

    // Non-command-position word transitions.
    switch (w->kw_state) {
    case KW_AFTER_FOR:
        // Expecting variable name; any word advances state.
        w->kw_state = KW_AFTER_FOR_VAR;
        break;
    case KW_AFTER_FOR_VAR:
        if (word_equals_keyword(w->buffer, word_start, word_end, "in")) {
            w->kw_state = KW_AFTER_FOR_IN;
        } else {
            w->kw_state = KW_NONE; // malformed `for` sequence
        }
        break;
    case KW_AFTER_CASE:
        w->kw_state = KW_AFTER_CASE_WORD;
        break;
    case KW_AFTER_CASE_WORD:
        if (word_equals_keyword(w->buffer, word_start, word_end, "in")) {
            w->kw_state = KW_AFTER_CASE_IN;
        } else {
            w->kw_state = KW_NONE;
        }
        break;
    case KW_AFTER_FOR_IN:
    case KW_AFTER_CASE_IN:
        /* In list / pattern mode; words are list/pattern elements
         * and don't transition the state. The state ends on a
         * statement terminator (handled separately) or `do`/`esac`
         * keyword (treated as a normal terminator effect). */
        if (word_equals_keyword(w->buffer, word_start, word_end, "do") ||
            word_equals_keyword(w->buffer, word_start, word_end, "esac")) {
            w->kw_state = KW_NONE;
        }
        break;
    case KW_NONE:
    default:
        break;
    }
}

/* Capture the heredoc delimiter from a word, applying the bare/quoted
 * delimiter normalization (single or double quotes around the
 * delimiter are stripped; backslash escapes simplify to the escaped
 * char). Truncates at WALKER_HEREDOC_DELIM_MAX. */
static void capture_heredoc_delim(walker_t *w, size_t word_start,
                                  size_t word_end) {
    w->heredoc_delim_len = 0;
    bool esc = false;
    bool in_sgl = false;
    bool in_dbl = false;
    size_t i = word_start;

    while (i < word_end &&
           w->heredoc_delim_len < WALKER_HEREDOC_DELIM_MAX - 1) {
        char c = w->buffer[i];
        if (esc) {
            w->heredoc_delim[w->heredoc_delim_len++] = c;
            esc = false;
            i++;
            continue;
        }
        if (in_sgl) {
            if (c == '\'')
                in_sgl = false;
            else
                w->heredoc_delim[w->heredoc_delim_len++] = c;
            i++;
            continue;
        }
        if (in_dbl) {
            if (c == '"')
                in_dbl = false;
            else if (c == '\\')
                esc = true;
            else
                w->heredoc_delim[w->heredoc_delim_len++] = c;
            i++;
            continue;
        }
        if (c == '\'') {
            in_sgl = true;
            i++;
            continue;
        }
        if (c == '"') {
            in_dbl = true;
            i++;
            continue;
        }
        if (c == '\\') {
            esc = true;
            i++;
            continue;
        }
        w->heredoc_delim[w->heredoc_delim_len++] = c;
        i++;
    }
    w->heredoc_delim[w->heredoc_delim_len] = '\0';
}

/* Test whether the walker's current line (bytes [current_line_start, pos))
 * matches the captured heredoc delimiter. The match allows leading tabs
 * when heredoc_dash is true. The delimiter must be the entire line (no
 * trailing characters). */
static bool current_line_matches_heredoc_delim(const walker_t *w) {
    size_t line_start = w->current_line_start;
    if (w->heredoc_dash) {
        while (line_start < w->pos && w->buffer[line_start] == '\t') {
            line_start++;
        }
    }
    size_t line_len = w->pos - line_start;
    if (line_len != w->heredoc_delim_len)
        return false;
    return memcmp(w->buffer + line_start, w->heredoc_delim, line_len) == 0;
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

    /* Heredoc body suppresses everything else: bytes are literal until a
     * line that matches the captured delimiter. The walker advances
     * codepoint by codepoint, updating current_line_start at each
     * newline and exiting heredoc mode when the just-completed line
     * equals the delimiter. */
    if (w->in_heredoc_body) {
        if (cp == '\n') {
            if (current_line_matches_heredoc_delim(w)) {
                w->in_heredoc_body = false;
                w->heredoc_delim[0] = '\0';
                w->heredoc_delim_len = 0;
                // Statement boundary after heredoc body ends.
                w->last_statement_start = w->pos + (size_t)n;
                w->at_command_position = true;
                w->current_command_word_start = SIZE_MAX;
                w->current_command_word_end = SIZE_MAX;
                w->current_arg_index = -1;
                w->next_word_is_redirect_target = false;
                w->kw_state = KW_NONE;
            }
            w->current_line_start = w->pos + (size_t)n;
        }
        w->pos += (size_t)n;
        return;
    }

    // ESCAPE_PENDING absorbs whatever this codepoint is.
    if (w->escape_pending) {
        w->escape_pending = false;
        // We're in a word (the backslash itself opened/extended one).
        w->pos += (size_t)n;
        return;
    }

    // SINGLE quote: only ' returns to NONE.
    if (w->in_single) {
        if (cp == '\'') {
            w->in_single = false;
        }
        w->pos += (size_t)n;
        return;
    }

    // DOUBLE quote
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

    // BACKTICK quote
    if (w->in_backtick) {
        if (cp == '`') {
            w->in_backtick = false;
        } else if (cp == '\\') {
            w->escape_pending = true;
        }
        w->pos += (size_t)n;
        return;
    }

    // NONE: word/structure tracking

    // Quote starters
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
            w->last_statement_start = w->pos + (size_t)n;
            w->at_command_position = true;
            w->current_command_word_start = SIZE_MAX;
            w->current_command_word_end = SIZE_MAX;
            w->current_arg_index = -1;
            w->next_word_is_redirect_target = false;
        }
        w->paren_depth++;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }
    if (cp == ')') {
        if (w->paren_depth > 0)
            w->paren_depth--;
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
        if (w->brace_depth > 0)
            w->brace_depth--;
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
        if (w->bracket_depth > 0)
            w->bracket_depth--;
        if (w->current_word_start == SIZE_MAX) {
            w->current_word_start = w->pos;
        }
        w->pos += (size_t)n;
        return;
    }

    // Statement terminators outside any nesting.
    if (is_statement_terminator(cp) && w->paren_depth == 0 &&
        w->brace_depth == 0 && w->bracket_depth == 0) {
        /* If a word was in progress, run keyword-state and heredoc-
         * delimiter logic before closing it. */
        if (w->current_word_start != SIZE_MAX) {
            size_t we = w->pos;
            if (w->expecting_heredoc_delim) {
                capture_heredoc_delim(w, w->current_word_start, we);
                w->expecting_heredoc_delim = false;
                w->heredoc_pending = true;
            }
            advance_keyword_state(w, w->current_word_start, we);
            w->current_word_start = SIZE_MAX;
        }
        /* While in for-list / case-pattern modes, statement terminators
         * end the list / patterns. */
        if (w->kw_state == KW_AFTER_FOR_IN || w->kw_state == KW_AFTER_CASE_IN) {
            w->kw_state = KW_NONE;
        } else {
            /* Other unfinished keyword sequences are also broken by a
             * terminator. */
            w->kw_state = KW_NONE;
        }
        /* Newline both terminates the statement AND advances the line
         * counter. If a heredoc body is pending, the next byte begins
         * the body. */
        if (cp == '\n') {
            w->current_line_start = w->pos + (size_t)n;
            if (w->heredoc_pending) {
                w->heredoc_pending = false;
                w->in_heredoc_body = true;
            }
        }
        // Reset command tracking.
        w->last_statement_start = w->pos + (size_t)n;
        w->at_command_position = true;
        w->current_command_word_start = SIZE_MAX;
        w->current_command_word_end = SIZE_MAX;
        w->current_arg_index = -1;
        w->next_word_is_redirect_target = false;
        w->arg_capture_count = 0;
        w->pos += (size_t)n;
        return;
    }

    // Whitespace outside any quote ends the current word.
    if (is_shell_whitespace(cp)) {
        if (w->current_word_start != SIZE_MAX) {
            size_t we = w->pos;
            /* If we were expecting a heredoc delimiter, this word IS
             * it. Capture and prime body entry. */
            if (w->expecting_heredoc_delim) {
                capture_heredoc_delim(w, w->current_word_start, we);
                w->expecting_heredoc_delim = false;
                w->heredoc_pending = true;
            }
            /* End-of-word transition. If this was the command word,
             * either retain command position (if the word is a
             * command-introducer keyword) or transition to argument
             * position. Otherwise the word is an argument that has
             * just completed; capture its byte range for the
             * arguments[] output. */
            if (w->current_command_word_start == w->current_word_start) {
                if (word_is_command_introducer(w->buffer, w->current_word_start,
                                               we)) {
                    /* `do`/`then`/`else`/`elif`/`until`/`while`/`if`:
                     * the next word starts a fresh command position. */
                    w->current_command_word_start = SIZE_MAX;
                    w->current_command_word_end = SIZE_MAX;
                    w->current_arg_index = -1;
                    w->at_command_position = true;
                    w->arg_capture_count = 0;
                } else {
                    w->current_command_word_end = we;
                    w->at_command_position = false;
                    w->current_arg_index = 0;
                    // New command starting -- reset captured args.
                    w->arg_capture_count = 0;
                }
            } else if (w->current_arg_index >= 0) {
                if (w->arg_capture_count < WALKER_MAX_CAPTURED_ARGS) {
                    w->arg_starts[w->arg_capture_count] = w->current_word_start;
                    w->arg_ends[w->arg_capture_count] = we;
                    w->arg_capture_count++;
                }
                w->current_arg_index++;
            }
            // Run keyword-state transition for `for` / `case` / `in`.
            advance_keyword_state(w, w->current_word_start, we);
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
     * redirect target. The redirect char itself does not start a word.
     *
     * Special case: `<<` introduces a heredoc; `<<-` is the tab-stripping
     * variant; `<<<` is a here-string (just a value, NOT a heredoc).
     * Distinguish via lookahead: peek past the current `<` to detect the
     * `<<` pair, then peek again to rule out `<<<`. */
    if (is_redirect_char(cp) && w->paren_depth == 0 && w->brace_depth == 0 &&
        w->bracket_depth == 0) {
        // Close the current word if any.
        if (w->current_word_start != SIZE_MAX) {
            if (w->current_command_word_start == w->current_word_start) {
                w->current_command_word_end = w->pos;
                w->at_command_position = false;
                w->current_arg_index = 0;
            }
            w->current_word_start = SIZE_MAX;
        }

        // Heredoc detection on `<`.
        if (cp == '<' && w->pos + 1 < w->cursor &&
            w->buffer[w->pos + 1] == '<' &&
            !(w->pos + 2 < w->cursor && w->buffer[w->pos + 2] == '<')) {
            /* `<<` (heredoc) or `<<-` (tab-stripping). Consume the
             * second `<` here so the next codepoint is whatever
             * follows. */
            w->pos += 1; // the first `<` is consumed at end-of-block
            // Optional `-` for tab-stripping form.
            w->heredoc_dash = false;
            if (w->pos + 1 < w->cursor && w->buffer[w->pos + 1] == '-') {
                w->heredoc_dash = true;
                w->pos += 1;
            }
            w->expecting_heredoc_delim = true;
            /* Don't set next_word_is_redirect_target — the next word IS
             * the heredoc delimiter, not a file path. */
            w->pos += (size_t)n;
            return;
        }

        w->next_word_is_redirect_target = true;
        w->pos += (size_t)n;
        return;
    }

    /* Default: ordinary word codepoint. Start a new word if not already in
     * one; if at command position, mark it as the command word. */
    if (w->current_word_start == SIZE_MAX) {
        w->current_word_start = w->pos;
        if (w->at_command_position &&
            w->current_command_word_start == SIZE_MAX) {
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

    if (end < start)
        return LLE_ERROR_INVALID_PARAMETER;

    /* First pass: dequote into a working buffer. Worst-case size is the
     * input range itself (dequoting only removes bytes). */
    size_t span = end - start;
    char *raw = lle_pool_alloc(span + 1);
    if (!raw)
        return LLE_ERROR_OUT_OF_MEMORY;

    /* Dequoting state machine (mirrors walker_advance_one but accumulating
     * literal bytes). */
    bool in_single = false;
    bool in_double = false;
    bool esc_pending = false;
    size_t out_pos = 0;
    size_t i = start;

    while (i < end) {
        uint32_t cp;
        int n = decode_at(buf, end, i, &cp);

        if (esc_pending) {
            // Emit the codepoint's bytes literally.
            for (int k = 0; k < n; k++)
                raw[out_pos++] = buf[i + k];
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
            for (int k = 0; k < n; k++)
                raw[out_pos++] = buf[i + k];
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
                size_t peek_i = i + (size_t)n;
                uint32_t peek_cp;
                int peek_n =
                    (peek_i < end) ? decode_at(buf, end, peek_i, &peek_cp) : 0;
                if (peek_n > 0 && (peek_cp == '$' || peek_cp == '`' ||
                                   peek_cp == '\\' || peek_cp == '"')) {
                    // Skip the backslash, emit the escaped char.
                    for (int k = 0; k < peek_n; k++)
                        raw[out_pos++] = buf[peek_i + k];
                    i = peek_i + (size_t)peek_n;
                    continue;
                }
                // Otherwise emit the backslash literally.
                raw[out_pos++] = '\\';
                i += (size_t)n;
                continue;
            }
            for (int k = 0; k < n; k++)
                raw[out_pos++] = buf[i + k];
            i += (size_t)n;
            continue;
        }

        // NONE quote
        if (cp == '\'') {
            in_single = true;
            i += (size_t)n;
            continue;
        }
        if (cp == '"') {
            in_double = true;
            i += (size_t)n;
            continue;
        }
        if (cp == '\\') {
            esc_pending = true;
            i += (size_t)n;
            continue;
        }

        // Ordinary literal byte.
        for (int k = 0; k < n; k++)
            raw[out_pos++] = buf[i + k];
        i += (size_t)n;
    }
    raw[out_pos] = '\0';

    /* Second pass: NFC normalize. Worst case NFC output is roughly 3x
     * input for pathological cases; allocate generously. */
    size_t nfc_cap = (out_pos * 4) + 16;
    char *nfc = lle_pool_alloc(nfc_cap);
    if (!nfc)
        return LLE_ERROR_OUT_OF_MEMORY;

    size_t nfc_len = 0;
    int rc = lle_unicode_normalize_nfc(raw, out_pos, nfc, nfc_cap, &nfc_len);
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
    size_t result = word_start;
    bool in_single = false;
    bool in_double = false;
    bool esc_pending = false;
    size_t i = word_start;

    // Skip an open quote opener if it's at word_start.
    if (i < cursor) {
        uint32_t cp;
        int n = decode_at(buf, cursor, i, &cp);
        if (cp == '\'' || cp == '"') {
            i += (size_t)n;
            result = i;
        }
    }

    while (i < cursor) {
        uint32_t cp;
        int n = decode_at(buf, cursor, i, &cp);

        if (esc_pending) {
            esc_pending = false;
            i += (size_t)n;
            continue;
        }

        if (in_single) {
            if (cp == '\'')
                in_single = false;
            else if (cp == '/')
                result = i + 1;
            i += (size_t)n;
            continue;
        }
        if (in_double) {
            if (cp == '"')
                in_double = false;
            else if (cp == '\\')
                esc_pending = true;
            else if (cp == '/')
                result = i + 1;
            i += (size_t)n;
            continue;
        }

        // NONE
        if (cp == '\'') {
            in_single = true;
            i += (size_t)n;
            continue;
        }
        if (cp == '"') {
            in_double = true;
            i += (size_t)n;
            continue;
        }
        if (cp == '\\') {
            esc_pending = true;
            i += (size_t)n;
            continue;
        }
        if (cp == '/') {
            result = i + 1;
            i += (size_t)n;
            continue;
        }

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
static lle_expansion_kind_t
detect_expansion_kind(const char *buf, size_t word_start, size_t cursor) {
    // Track simple state: most recent unmatched-opener.
    bool esc = false;
    bool in_sgl = false;
    bool in_dbl = false;
    int paren_d = 0;
    int brace_d = 0;
    bool any_glob = false;
    bool in_dollar = false; // just saw $, expecting name/{/(
    bool in_var_name = false;
    bool in_braced_var_name = false;
    bool in_cmd_sub = false;
    bool in_arith = false;
    bool in_brace_list = false;
    bool brace_has_comma = false;

    size_t i = word_start;
    while (i < cursor) {
        uint32_t cp;
        int n = decode_at(buf, cursor, i, &cp);

        if (esc) {
            esc = false;
            i += (size_t)n;
            continue;
        }
        if (in_sgl) {
            if (cp == '\'')
                in_sgl = false;
            i += (size_t)n;
            continue;
        }
        if (in_dbl) {
            if (cp == '"')
                in_dbl = false;
            else if (cp == '\\')
                esc = true;
            else if (cp == '$' && !in_dollar)
                in_dollar = true;
            // `$` inside "..." can begin an expansion; we still detect it.
            i += (size_t)n;
            continue;
        }

        if (cp == '\'') {
            in_sgl = true;
            i += (size_t)n;
            continue;
        }
        if (cp == '"') {
            in_dbl = true;
            i += (size_t)n;
            continue;
        }
        if (cp == '\\') {
            esc = true;
            i += (size_t)n;
            continue;
        }

        if (in_dollar) {
            if (cp == '{') {
                in_braced_var_name = true;
                brace_d++;
            } else if (cp == '(') {
                // peek for second ( → arithmetic
                size_t peek_i = i + (size_t)n;
                uint32_t peek_cp;
                int peek_n = (peek_i < cursor)
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
                in_var_name = false; // name ended at this byte
            }
            i += (size_t)n;
            continue;
        }

        if (in_braced_var_name) {
            if (cp == '}') {
                in_braced_var_name = false;
                brace_d--;
            }
            i += (size_t)n;
            continue;
        }

        if (in_cmd_sub || in_arith) {
            if (cp == ')') {
                paren_d--;
                if (in_arith && paren_d == 0)
                    in_arith = false;
                else if (in_cmd_sub && paren_d == 0)
                    in_cmd_sub = false;
            } else if (cp == '(')
                paren_d++;
            i += (size_t)n;
            continue;
        }

        if (cp == '$') {
            in_dollar = true;
            i += (size_t)n;
            continue;
        }
        if (cp == '{') {
            in_brace_list = true;
            brace_d++;
            i += (size_t)n;
            continue;
        }
        if (cp == '}') {
            if (brace_d > 0)
                brace_d--;
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
        if (cp == '*' || cp == '?' || cp == '[')
            any_glob = true;

        i += (size_t)n;
    }

    /* Resolve precedence: in-progress expansions take priority; glob is
     * only reported as "in-progress" when no other expansion is
     * unfinished and the word contains a glob char. */
    if (in_arith)
        return LLE_EXPANSION_ARITHMETIC;
    if (in_cmd_sub)
        return LLE_EXPANSION_COMMAND_SUBST;
    if (in_braced_var_name)
        return LLE_EXPANSION_BRACED_VARIABLE_NAME;
    if (in_var_name)
        return LLE_EXPANSION_VARIABLE_NAME;
    if (in_dollar)
        return LLE_EXPANSION_VARIABLE_NAME;
    if (in_brace_list && brace_has_comma)
        return LLE_EXPANSION_BRACE_LIST;
    if (any_glob)
        return LLE_EXPANSION_GLOB;
    return LLE_EXPANSION_NONE;
}

/* ============================================================================
 * Expansion resolution
 * ============================================================================
 *
 * Two responsibilities:
 *
 *   resolve_path_prefix_to_directory()
 *       Single-value path: takes the typed bytes from word_start up to
 *       filename_portion_start (the path-prefix portion of the typed
 *       shell-word), runs them through expand_if_needed() to resolve any
 *       single-value expansions (~/, $VAR, ${VAR}, $((...)), $(...)),
 *       and produces the absolute directory path the source should
 *       open.
 *
 *   resolve_path_prefix_to_branches()
 *       Multi-value (brace) path: takes the same typed bytes, runs them
 *       through expand_brace_pattern() to enumerate per-branch path
 *       prefixes, then resolves each via expand_if_needed(). Each
 *       resulting branch is paired with the dequoted filename prefix
 *       (which is shared across branches; brace expansion in the
 *       path-prefix portion does not introduce per-branch filename
 *       differences).
 *
 * Both helpers fail soft: if expansion isn't possible (no executor
 * available, expansion produces an empty string, etc.), they leave the
 * relevant output fields NULL/0 and the engine treats the word as
 * "search cwd" or refuses, depending on context.
 *
 * Command-substitution evaluation is currently unconditional, matching
 * the bash/zsh consensus default. When the central-config keys are
 * registered, the analyzer will gate $(...) and `...` evaluation on
 * the completion.eval_command_subst flag; until then the safe-mode
 * opt-in is unavailable and expansion proceeds as in bash/zsh.
 */

// Allocate a pool-owned copy of [start, end) within buffer.
static char *pool_substring(const char *buffer, size_t start, size_t end) {
    if (end < start)
        return NULL;
    size_t len = end - start;
    char *out = lle_pool_alloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, buffer + start, len);
    out[len] = '\0';
    return out;
}

// Allocate a pool-owned copy of a NUL-terminated string.
static char *pool_strdup(const char *s) {
    if (!s)
        return NULL;
    size_t len = strlen(s);
    char *out = lle_pool_alloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, s, len + 1);
    return out;
}

/* True if the path-prefix bytes contain a comma-separated brace list
 * suitable for expand_brace_pattern. We accept either a list (`{a,b}`)
 * or a range (`{1..5}`); both are handled by the existing expander. */
static bool path_prefix_has_brace_list(const char *path_prefix) {
    if (!path_prefix)
        return false;
    const char *open = strchr(path_prefix, '{');
    if (!open)
        return false;
    const char *close = strchr(open + 1, '}');
    if (!close)
        return false;
    // Accept either a comma (list form) or `..` (range form).
    for (const char *p = open + 1; p < close; p++) {
        if (*p == ',')
            return true;
        if (*p == '.' && p + 1 < close && *(p + 1) == '.')
            return true;
    }
    return false;
}

/* Look up a user's home directory by name. Returns NULL if the user
 * doesn't exist. Result is borrowed from /etc/passwd via getpwnam --
 * caller copies before assuming any lifetime. */
static const char *lookup_user_home(const char *user) {
    if (!user || !user[0]) {
        const char *h = getenv("HOME");
        if (h && h[0])
            return h;
        struct passwd *pw = getpwuid(getuid());
        return pw ? pw->pw_dir : NULL;
    }
    struct passwd *pw = getpwnam(user);
    return pw ? pw->pw_dir : NULL;
}

/* Tilde expansion: ~/path or ~user/path. Returns a pool-owned string
 * with the tilde replaced by the resolved home directory, or NULL if
 * the resolution fails (no HOME / unknown user). */
static char *expand_tilde_local(const char *path_prefix,
                                lle_memory_pool_t *pool) {
    (void)pool;
    if (!path_prefix || path_prefix[0] != '~')
        return NULL;

    /* Find the username portion: bytes after `~` up to the first `/` or
     * end of string. Empty username means "current user." */
    const char *slash = strchr(path_prefix, '/');
    size_t name_len =
        slash ? (size_t)(slash - path_prefix - 1) : strlen(path_prefix) - 1;

    const char *home = NULL;
    if (name_len == 0) {
        home = lookup_user_home(NULL);
    } else {
        char user[256];
        if (name_len >= sizeof(user))
            return NULL;
        memcpy(user, path_prefix + 1, name_len);
        user[name_len] = '\0';
        home = lookup_user_home(user);
    }
    if (!home)
        return NULL;

    const char *rest = slash ? slash : "";
    size_t home_len = strlen(home);
    size_t rest_len = strlen(rest);
    char *result = lle_pool_alloc(home_len + rest_len + 1);
    if (!result)
        return NULL;
    memcpy(result, home, home_len);
    memcpy(result + home_len, rest, rest_len + 1);
    return result;
}

/* Variable expansion at start of path: $NAME/path or ${NAME}/path.
 * Only handles bare and braced names (no parameter operators like
 * :-default); returns NULL for unsupported forms or unset vars. */
static char *expand_variable_local(const char *path_prefix,
                                   lle_memory_pool_t *pool) {
    (void)pool;
    if (!path_prefix || path_prefix[0] != '$')
        return NULL;

    const char *name_start;
    const char *name_end;
    const char *rest;
    if (path_prefix[1] == '{') {
        name_start = path_prefix + 2;
        const char *close = strchr(name_start, '}');
        if (!close)
            return NULL;
        // Refuse parameter operators ${NAME:-default} etc.
        for (const char *p = name_start; p < close; p++) {
            if (*p == ':' || *p == '-' || *p == '+' || *p == '?' || *p == '=' ||
                *p == '#' || *p == '%' || *p == '/') {
                return NULL;
            }
        }
        name_end = close;
        rest = close + 1;
    } else {
        name_start = path_prefix + 1;
        name_end = name_start;
        while (*name_end &&
               (isalnum((unsigned char)*name_end) || *name_end == '_')) {
            name_end++;
        }
        if (name_end == name_start)
            return NULL; // bare $
        rest = name_end;
    }

    size_t name_len = (size_t)(name_end - name_start);
    char name_buf[256];
    if (name_len >= sizeof(name_buf))
        return NULL;
    memcpy(name_buf, name_start, name_len);
    name_buf[name_len] = '\0';

    const char *value = getenv(name_buf);
    if (!value)
        return NULL;
    size_t value_len = strlen(value);
    size_t rest_len = strlen(rest);
    char *result = lle_pool_alloc(value_len + rest_len + 1);
    if (!result)
        return NULL;
    memcpy(result, value, value_len);
    memcpy(result + value_len, rest, rest_len + 1);
    return result;
}

/* Resolve a single path-prefix string to an absolute or relative
 * directory path the file source can pass to opendir().
 *
 * The expansion is done locally for the side-effect-free cases that
 * don't require the shell's executor: plain paths pass through
 * unchanged, leading `~` and `~user` resolve via getenv / getpwnam,
 * leading `$NAME` and `${NAME}` resolve via getenv. For richer forms
 * (parameter expansion operators, command substitution, arithmetic,
 * mid-string variables) we delegate to lush's full expand_if_needed
 * when an executor is available; otherwise we return NULL and the
 * file source falls back to cwd.
 *
 * Returns a pool-owned string on success, NULL otherwise.
 */
static char *resolve_single_path_prefix(const char *path_prefix,
                                        lle_memory_pool_t *pool) {
    if (!path_prefix || path_prefix[0] == '\0')
        return NULL;

    // Tilde expansion at start: handled locally.
    if (path_prefix[0] == '~') {
        char *r = expand_tilde_local(path_prefix, pool);
        if (r)
            return r;
        // Fall through to executor path on failure.
    }

    // Bare / braced variable at start: handled locally if simple.
    if (path_prefix[0] == '$' && path_prefix[1] != '(') {
        char *r = expand_variable_local(path_prefix, pool);
        if (r)
            return r;
        // Fall through to executor path on more complex forms.
    }

    /* Anything containing $(... or backtick, or richer forms we don't
     * recognize, requires the shell's expansion machinery. Use it
     * when an executor is available; otherwise leave NULL. */
    if (current_executor) {
        char *expanded = expand_if_needed(current_executor, path_prefix);
        if (expanded && expanded[0]) {
            char *result = pool_strdup(expanded);
            free(expanded);
            return result;
        }
        free(expanded);
    }

    /* Final fallback: plain paths (no expansion markers) pass through
     * verbatim. opendir() handles relative and absolute paths
     * uniformly. */
    if (path_prefix[0] != '$' && path_prefix[0] != '`' &&
        path_prefix[0] != '~') {
        return pool_strdup(path_prefix);
    }

    /* Path begins with an expansion marker we couldn't resolve (e.g.,
     * `$(cmd)/` without an executor). Leave the directory NULL so the
     * source treats this as cwd or refuses, depending on its own
     * applicability rules. */
    return NULL;
}

/* Single-value path: populate ctx->expanded_directory if the typed
 * path-prefix bytes resolve to a meaningful directory. */
static void resolve_path_prefix_to_directory(lle_word_context_t *ctx,
                                             const char *buffer) {
    if (ctx->word_start >= ctx->filename_portion_start) {
        /* No path-prefix bytes typed (no '/' before cursor); engine uses
         * cwd by convention. */
        return;
    }
    char *path_prefix =
        pool_substring(buffer, ctx->word_start, ctx->filename_portion_start);
    if (!path_prefix)
        return;

    ctx->expanded_directory =
        resolve_single_path_prefix(path_prefix, ctx->pool);
}

/* Multi-value (brace) path: enumerate branches via expand_brace_pattern,
 * resolve each, attach the (shared) dequoted filename prefix. Returns
 * true if branches[] was populated, false otherwise. */
static bool resolve_path_prefix_to_branches(lle_word_context_t *ctx,
                                            const char *buffer) {
    if (ctx->word_start >= ctx->filename_portion_start)
        return false;

    char *path_prefix =
        pool_substring(buffer, ctx->word_start, ctx->filename_portion_start);
    if (!path_prefix)
        return false;
    if (!path_prefix_has_brace_list(path_prefix))
        return false;

    int branch_count = 0;
    char **raw = expand_brace_pattern(path_prefix, &branch_count);
    if (!raw || branch_count <= 1) {
        if (raw) {
            for (int i = 0; i < branch_count; i++)
                free(raw[i]);
            free(raw);
        }
        return false;
    }

    // Allocate the branches array from the pool.
    lle_word_context_branch_t *branches =
        lle_pool_alloc(sizeof(*branches) * (size_t)branch_count);
    if (!branches) {
        for (int i = 0; i < branch_count; i++)
            free(raw[i]);
        free(raw);
        return false;
    }

    size_t valid = 0;
    for (int i = 0; i < branch_count; i++) {
        char *resolved = resolve_single_path_prefix(raw[i], ctx->pool);
        if (resolved) {
            branches[valid].expanded_directory = resolved;
            branches[valid].dequoted_filename_prefix =
                ctx->dequoted_filename_prefix;
            valid++;
        }
        free(raw[i]);
    }
    free(raw);

    if (valid == 0)
        return false;
    ctx->branches = branches;
    ctx->branch_count = valid;
    return true;
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
    if (cursor_byte_offset > buf_len)
        cursor_byte_offset = buf_len;

    // Run the walker from byte 0 to the cursor.
    walker_t w = (walker_t){
        .buffer = buffer,
        .buffer_len = buf_len,
        .cursor = cursor_byte_offset,
        .pos = 0,
        .in_single = false,
        .in_double = false,
        .in_backtick = false,
        .escape_pending = false,
        .paren_depth = 0,
        .brace_depth = 0,
        .bracket_depth = 0,
        .last_statement_start = 0,
        .current_word_start = SIZE_MAX,
        .current_command_word_start = SIZE_MAX,
        .current_command_word_end = SIZE_MAX,
        .current_arg_index = -1,
        .next_word_is_redirect_target = false,
        .at_command_position = true,
        .kw_state = KW_NONE,
        .expecting_heredoc_delim = false,
        .heredoc_dash = false,
        .heredoc_pending = false,
        .in_heredoc_body = false,
        .heredoc_delim = {0},
        .heredoc_delim_len = 0,
        .current_line_start = 0,
        .arg_starts = {0},
        .arg_ends = {0},
        .arg_capture_count = 0,
    };

    while (w.pos < w.cursor) {
        walker_advance_one(&w);
    }

    // Allocate output struct.
    lle_word_context_t *ctx = lle_pool_alloc(sizeof(*ctx));
    if (!ctx)
        return LLE_ERROR_OUT_OF_MEMORY;
    memset(ctx, 0, sizeof(*ctx));
    ctx->pool = pool;

    /* Word coordinates. If the cursor sits at whitespace or at a position
     * with no active word, word_start = word_end = cursor (an empty word
     * being completed). */
    ctx->word_start =
        (w.current_word_start == SIZE_MAX) ? w.cursor : w.current_word_start;
    ctx->word_end = w.cursor;

    // Quote state at cursor.
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

    // In-progress expansion-kind.
    ctx->expansion_kind =
        detect_expansion_kind(buffer, ctx->word_start, ctx->word_end);

    // filename_portion_start: byte after last unquoted '/' in the word.
    ctx->filename_portion_start =
        compute_filename_portion_start(buffer, ctx->word_start, ctx->word_end);

    /* expansion_prefix_end currently equals filename_portion_start. The
     * bytes from word_start to here are the "preserved-typed-prefix"
     * zone the engine never modifies. This field will be refined to
     * carry the precise end-of-expansion-bytes once expansion
     * resolution is added; for now, equating the two is sufficient
     * because no caller distinguishes them yet. */
    ctx->expansion_prefix_end = ctx->filename_portion_start;

    // Dequote and NFC-normalize the filename prefix portion.
    lle_result_t r =
        dequote_range_to_nfc(buffer, ctx->filename_portion_start, ctx->word_end,
                             pool, &ctx->dequoted_filename_prefix);
    if (r != LLE_SUCCESS)
        return r;

    /* Context type. Order of precedence (most specific first):
     *   HEREDOC_BODY  — completion is refused; trumps everything else
     *   VARIABLE_NAME — cursor inside an in-progress $name / ${name
     *   FOR_IN_LIST   — cursor in `for X in <list>` past `in`, before ;
     *   CASE_PATTERN  — cursor in `case X in <patterns>` past `in`
     *   REDIRECT_TARGET — next word after >, <, >>, etc.
     *   COMMAND_POSITION — first word of a fresh statement
     *   ARGUMENT      — argument to the current command
     *   UNKNOWN       — none of the above */
    if (w.in_heredoc_body) {
        ctx->context_type = LLE_CONTEXT_HEREDOC_BODY;
    } else if (ctx->expansion_kind == LLE_EXPANSION_VARIABLE_NAME ||
               ctx->expansion_kind == LLE_EXPANSION_BRACED_VARIABLE_NAME) {
        ctx->context_type = LLE_CONTEXT_VARIABLE_NAME;
    } else if (w.kw_state == KW_AFTER_FOR_IN) {
        ctx->context_type = LLE_CONTEXT_FOR_IN_LIST;
    } else if (w.kw_state == KW_AFTER_CASE_IN) {
        ctx->context_type = LLE_CONTEXT_CASE_PATTERN;
    } else if (w.next_word_is_redirect_target) {
        ctx->context_type = LLE_CONTEXT_REDIRECT_TARGET;
    } else if (w.at_command_position) {
        ctx->context_type = LLE_CONTEXT_COMMAND_POSITION;
    } else if (w.current_arg_index >= 0) {
        ctx->context_type = LLE_CONTEXT_ARGUMENT;
    } else {
        ctx->context_type = LLE_CONTEXT_UNKNOWN;
    }

    // Command name (substring from buffer).
    if (w.current_command_word_start != SIZE_MAX &&
        w.current_command_word_end != SIZE_MAX &&
        w.current_command_word_end > w.current_command_word_start) {
        size_t cn_len =
            w.current_command_word_end - w.current_command_word_start;
        char *cn = lle_pool_alloc(cn_len + 1);
        if (!cn)
            return LLE_ERROR_OUT_OF_MEMORY;
        memcpy(cn, buffer + w.current_command_word_start, cn_len);
        cn[cn_len] = '\0';
        ctx->command_name = cn;
    } else {
        ctx->command_name = NULL;
    }

    ctx->arg_index = w.current_arg_index;

    /* Populate arguments[] from captured byte ranges. Each captured
     * range is dequoted and NFC-normalized so subcommand sources can
     * compare against builtin specs without further processing. */
    if (w.arg_capture_count > 0) {
        char **args =
            lle_pool_alloc(sizeof(char *) * (size_t)w.arg_capture_count);
        if (!args)
            return LLE_ERROR_OUT_OF_MEMORY;
        size_t valid = 0;
        for (size_t i = 0; i < w.arg_capture_count; i++) {
            char *arg_text = NULL;
            lle_result_t arr = dequote_range_to_nfc(
                buffer, w.arg_starts[i], w.arg_ends[i], pool, &arg_text);
            if (arr == LLE_SUCCESS && arg_text) {
                args[valid++] = arg_text;
            }
        }
        ctx->arguments = args;
        ctx->argument_count = valid;
    } else {
        ctx->arguments = NULL;
        ctx->argument_count = 0;
    }

    /* Resolve expansions in the path-prefix portion of the typed word
     * via lush's existing expansion machinery. Brace lists (and ranges)
     * produce a per-branch directory set; everything else (single
     * variable, tilde, parameter, arithmetic, command sub) produces one
     * resolved directory. The engine reads branches[] when
     * branch_count > 0; otherwise expanded_directory. Both can be NULL
     * when the typed word has no path prefix or no executor is
     * available, in which case the engine treats it as a cwd-relative
     * completion. */
    ctx->expanded_directory = NULL;
    ctx->branches = NULL;
    ctx->branch_count = 0;
    if (!resolve_path_prefix_to_branches(ctx, buffer)) {
        resolve_path_prefix_to_directory(ctx, buffer);
    }

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
    case LLE_QUOTE_NONE:
        return "NONE";
    case LLE_QUOTE_SINGLE:
        return "SINGLE";
    case LLE_QUOTE_DOUBLE:
        return "DOUBLE";
    case LLE_QUOTE_BACKTICK:
        return "BACKTICK";
    case LLE_QUOTE_ESCAPE_PENDING:
        return "ESCAPE_PENDING";
    }
    return "INVALID";
}

const char *lle_expansion_kind_name(lle_expansion_kind_t kind) {
    switch (kind) {
    case LLE_EXPANSION_NONE:
        return "NONE";
    case LLE_EXPANSION_VARIABLE_NAME:
        return "VARIABLE_NAME";
    case LLE_EXPANSION_BRACED_VARIABLE_NAME:
        return "BRACED_VARIABLE_NAME";
    case LLE_EXPANSION_COMMAND_SUBST:
        return "COMMAND_SUBST";
    case LLE_EXPANSION_ARITHMETIC:
        return "ARITHMETIC";
    case LLE_EXPANSION_BRACE_LIST:
        return "BRACE_LIST";
    case LLE_EXPANSION_GLOB:
        return "GLOB";
    }
    return "INVALID";
}

const char *lle_word_context_type_name(lle_word_context_type_t type) {
    switch (type) {
    case LLE_CONTEXT_COMMAND_POSITION:
        return "COMMAND_POSITION";
    case LLE_CONTEXT_ARGUMENT:
        return "ARGUMENT";
    case LLE_CONTEXT_REDIRECT_TARGET:
        return "REDIRECT_TARGET";
    case LLE_CONTEXT_VARIABLE_NAME:
        return "VARIABLE_NAME";
    case LLE_CONTEXT_ASSIGNMENT_VALUE:
        return "ASSIGNMENT_VALUE";
    case LLE_CONTEXT_FOR_IN_LIST:
        return "FOR_IN_LIST";
    case LLE_CONTEXT_CASE_PATTERN:
        return "CASE_PATTERN";
    case LLE_CONTEXT_HEREDOC_BODY:
        return "HEREDOC_BODY";
    case LLE_CONTEXT_UNKNOWN:
        return "UNKNOWN";
    }
    return "INVALID";
}
