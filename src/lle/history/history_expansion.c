/**
 * @file history_expansion.c
 * @brief LLE History System - History Expansion Implementation (Spec 09)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements bash-compatible history expansion for the LLE history system.
 *
 * Event designators (select a prior command):
 * - !!        Repeat last command
 * - !n        Repeat command number n
 * - !-n       Repeat command n entries back
 * - !string   Most recent command starting with string
 * - !?string? Most recent command containing string (closing ? optional)
 * - ^old^new  Quick substitution in the last command
 *
 * Word designators (select words from the referenced command, optionally
 * after a ':' separator; the standalone forms imply the last command):
 * - !$ / :$   Last word
 * - !^ / :^   First argument (word 1)
 * - !* / :*   All arguments (words 1 through last)
 * - :0        The command word
 * - :n        Word n
 * - :n-m      Words n through m (n- selects through the next-to-last word,
 *             n* selects through the last word)
 *
 * Modifiers (transform the selected text):
 * - :p             Print the expansion without executing it
 * - :s/old/new/    Substitute the first occurrence of old with new
 * - :gs/old/new/   Substitute every occurrence
 *
 * Behavior (matches bash):
 * - Expansion occurs before command execution
 * - A failed expansion reports an error and aborts the command
 * - A leading space disables expansion (configurable)
 * - Expansion does not occur inside quotes
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

#define EXPANSION_MAX_LENGTH 4096
#define EXPANSION_MAX_DEPTH 10  /// Prevent infinite recursion
#define EXPANSION_MAX_WORDS 256 /// Words tracked for word designators
#define EXPANSION_PATTERN_MAX 256

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================
 */

/**
 * Expansion context
 */
typedef struct {
    lle_history_core_t *history_core; ///< History core for lookups
    int recursion_depth;              ///< Current recursion depth
    bool space_disables_expansion;    ///< Leading space disables expansion
    bool verify_before_execute;       ///< Verify expansion before execute
} lle_expansion_context_t;

/// Global expansion context
static lle_expansion_context_t g_expansion_ctx = {
    .history_core = NULL,
    .recursion_depth = 0,
    .space_disables_expansion = true,
    .verify_before_execute = false};

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Find the next history expansion marker in a string
 *
 * Searches for unescaped '!' characters or '^' at position 0.
 *
 * @param str String to search (may be NULL)
 * @param start_pos Starting position in string
 * @return Position of expansion marker, or -1 if not found or str is NULL
 */
static ssize_t find_expansion_marker(const char *str, size_t start_pos) {
    if (!str)
        return -1;

    size_t len = strlen(str);
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    /// Track quote state from beginning of string
    for (size_t i = 0; i < start_pos && i < len; i++) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (str[i] == '\\' && !in_single_quote) {
            escaped = true;
            continue;
        }
        if (str[i] == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (str[i] == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        }
    }

    for (size_t i = start_pos; i < len; i++) {
        char c = str[i];

        /// Handle escape sequences
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && !in_single_quote) {
            escaped = true;
            continue;
        }

        /// Track quote state
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }

        /// Skip if inside quotes - no history expansion inside quotes
        if (in_single_quote || in_double_quote) {
            continue;
        }

        /// Check for expansion markers only outside quotes
        if (c == '!') {
            /// Check if it's escaped (already handled above, but double-check
            /// for cases where escape wasn't at i-1 due to other chars)
            if (i > 0 && str[i - 1] == '\\') {
                continue; /// Escaped, not an expansion
            }
            return (ssize_t)i;
        } else if (c == '^' && i == 0) {
            /// Quick substitution must be at start
            return (ssize_t)i;
        }
    }

    return -1;
}

/**
 * @brief Parse history number from string (handles !n and !-n)
 *
 * Parses a numeric history reference, distinguishing between absolute
 * (!n) and relative (!-n) references.
 *
 * @param str String starting with number or -number (must not be NULL)
 * @param number Output for parsed number (must not be NULL)
 * @param is_relative Output - true if relative (!-n), false if absolute (!n)
 * (must not be NULL)
 * @param consumed Output - number of characters consumed (must not be NULL)
 * @return true on success, false on parse error or NULL parameters
 */
static bool parse_history_number(const char *str, int64_t *number,
                                 bool *is_relative, size_t *consumed) {
    if (!str || !number || !is_relative || !consumed) {
        return false;
    }

    *consumed = 0;
    *is_relative = false;

    /// Check for relative notation (!-n)
    if (str[0] == '-') {
        *is_relative = true;
        str++;
        (*consumed)++;
    }

    /// Parse the number
    char *endptr;
    errno = 0;
    long parsed = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str) {
        return false; /// Parse error
    }

    *number = parsed;
    *consumed += (endptr - str);

    return true;
}

/**
 * @brief Extract a prefix-search string from a !string expansion
 *
 * Copies characters until whitespace, a command terminator, or the ':'
 * modifier separator, so a trailing word designator or modifier is left
 * for the caller to parse.
 *
 * @param str String after the leading ! (must not be NULL)
 * @param output Output buffer for extracted string (must not be NULL)
 * @param max_len Maximum length of output buffer in bytes
 * @return Number of characters consumed, or 0 on error or empty result
 */
static size_t extract_expansion_string(const char *str, char *output,
                                       size_t max_len) {
    if (!str || !output || max_len == 0) {
        return 0;
    }

    size_t i = 0;
    while (str[i] != '\0' && i < max_len - 1) {
        /// Stop at whitespace, command terminators, or the ':' modifier
        /// separator (so word designators and modifiers can follow).
        if (isspace((unsigned char)str[i]) || str[i] == ';' || str[i] == '|' ||
            str[i] == '&' || str[i] == '>' || str[i] == '<' || str[i] == '(' ||
            str[i] == ')' || str[i] == ':' || str[i] == '\n') {
            break;
        }
        output[i] = str[i];
        i++;
    }

    output[i] = '\0';
    return i;
}

/**
 * @brief Perform quick substitution (^old^new)
 *
 * Replaces the first occurrence of old_pattern with new_pattern in
 * last_command and stores the result.
 *
 * @param last_command Last command from history (must not be NULL)
 * @param old_pattern Pattern to replace (must not be NULL)
 * @param new_pattern Replacement pattern (must not be NULL)
 * @param result Output buffer for result (must not be NULL)
 * @param max_len Maximum length of result buffer in bytes
 * @return true on success, false if pattern not found or result too long
 */
static bool perform_quick_substitution(const char *last_command,
                                       const char *old_pattern,
                                       const char *new_pattern, char *result,
                                       size_t max_len) {
    if (!last_command || !old_pattern || !new_pattern || !result ||
        max_len == 0) {
        return false;
    }

    /// Find the old pattern in the last command. A byte-level search is
    /// correct for UTF-8: a valid multibyte needle only matches at code-point
    /// boundaries of a valid multibyte haystack.
    const char *match_pos = strstr(last_command, old_pattern);
    if (!match_pos) {
        /// Pattern not found - substitution fails
        return false;
    }

    /// Build the substituted command
    size_t prefix_len = match_pos - last_command;
    size_t old_len = strlen(old_pattern);
    size_t new_len = strlen(new_pattern);
    size_t suffix_len = strlen(match_pos + old_len);

    /// Check if result will fit
    if (prefix_len + new_len + suffix_len >= max_len) {
        /// Result too long
        return false;
    }

    /// Copy prefix
    memcpy(result, last_command, prefix_len);

    /// Copy replacement
    memcpy(result + prefix_len, new_pattern, new_len);

    /// Copy suffix
    memcpy(result + prefix_len + new_len, match_pos + old_len, suffix_len);

    /// Null terminate
    result[prefix_len + new_len + suffix_len] = '\0';

    return true;
}

/**
 * @brief Split a command into whitespace-delimited words
 *
 * Records the start and length of each word for word-designator selection.
 * Splitting is on ASCII whitespace, matching bash word designators.
 *
 * @param cmd Command text (must not be NULL)
 * @param starts Output array of word start pointers (size >= max)
 * @param lens Output array of word byte lengths (size >= max)
 * @param max Maximum number of words to record
 * @return Number of words recorded
 */
static size_t split_into_words(const char *cmd, const char **starts,
                               size_t *lens, size_t max) {
    size_t count = 0;
    const char *p = cmd;
    while (*p && count < max) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        starts[count] = start;
        lens[count] = (size_t)(p - start);
        count++;
    }
    return count;
}

/**
 * @brief Apply a word designator to a referenced command
 *
 * Selects ^, $, *, a single word n, or a range (n-m, n-, n*) and writes the
 * space-joined selection to out.
 *
 * @param cmd Referenced command text (must not be NULL)
 * @param spec Designator characters (must not be NULL)
 * @param out Output buffer for the selection (must not be NULL)
 * @param out_sz Size of the output buffer in bytes
 * @param consumed Output - designator characters consumed (must not be NULL)
 * @return true on success, false on a malformed designator or overflow
 */
static bool select_word_range(const char *cmd, const char *spec, char *out,
                              size_t out_sz, size_t *consumed) {
    const char *starts[EXPANSION_MAX_WORDS];
    size_t lens[EXPANSION_MAX_WORDS];
    size_t nwords = split_into_words(cmd, starts, lens, EXPANSION_MAX_WORDS);
    if (nwords == 0) {
        return false;
    }

    size_t last = nwords - 1;
    size_t first_sel;
    size_t last_sel;
    size_t used = 0;

    char c = spec[0];
    if (c == '^') {
        first_sel = 1;
        last_sel = 1;
        used = 1;
    } else if (c == '$') {
        first_sel = last;
        last_sel = last;
        used = 1;
    } else if (c == '*') {
        first_sel = 1;
        last_sel = last;
        used = 1;
    } else if (isdigit((unsigned char)c)) {
        char *end;
        long n = strtol(spec, &end, 10);
        used = (size_t)(end - spec);
        first_sel = (size_t)n;
        last_sel = (size_t)n;
        if (*end == '-') {
            used++; /// consume '-'
            const char *q = end + 1;
            if (isdigit((unsigned char)*q)) {
                char *end2;
                long m = strtol(q, &end2, 10);
                used += (size_t)(end2 - q);
                last_sel = (size_t)m;
            } else {
                /// n- selects through the next-to-last word
                last_sel = (last > 0) ? last - 1 : 0;
            }
        } else if (*end == '*') {
            used++; /// consume '*'
            last_sel = last;
        }
    } else {
        return false; /// not a word designator
    }

    /// A '*' selection that exceeds the available words yields an empty
    /// string (bash: !* with no arguments expands to nothing); other
    /// out-of-range selections are an error.
    if (first_sel > last) {
        if (c == '*') {
            out[0] = '\0';
            *consumed = used;
            return true;
        }
        return false;
    }
    if (last_sel > last) {
        last_sel = last;
    }
    if (last_sel < first_sel) {
        out[0] = '\0';
        *consumed = used;
        return true;
    }

    size_t pos = 0;
    for (size_t w = first_sel; w <= last_sel; w++) {
        if (w > first_sel) {
            if (pos + 1 >= out_sz) {
                return false;
            }
            out[pos++] = ' ';
        }
        if (pos + lens[w] >= out_sz) {
            return false;
        }
        memcpy(out + pos, starts[w], lens[w]);
        pos += lens[w];
    }
    out[pos] = '\0';
    *consumed = used;
    return true;
}

/**
 * @brief Apply a :s substitution to text in place
 *
 * Replaces the first occurrence of old_pat with new_pat, or every occurrence
 * when global is set.
 *
 * @param text Text to transform in place (must not be NULL)
 * @param text_sz Size of the text buffer in bytes
 * @param old_pat Pattern to replace (must not be NULL, must be non-empty)
 * @param new_pat Replacement text (must not be NULL)
 * @param global true to replace all occurrences
 * @return true on success, false if old_pat is empty, not found, or overflow
 */
static bool apply_substitution(char *text, size_t text_sz, const char *old_pat,
                               const char *new_pat, bool global) {
    if (!old_pat[0]) {
        return false;
    }

    char buf[EXPANSION_MAX_LENGTH];
    size_t bp = 0;
    size_t old_len = strlen(old_pat);
    size_t new_len = strlen(new_pat);
    const char *p = text;
    bool did = false;

    while (*p) {
        if ((!did || global) && strncmp(p, old_pat, old_len) == 0) {
            if (bp + new_len >= sizeof(buf)) {
                return false;
            }
            memcpy(buf + bp, new_pat, new_len);
            bp += new_len;
            p += old_len;
            did = true;
        } else {
            if (bp + 1 >= sizeof(buf)) {
                return false;
            }
            buf[bp++] = *p++;
        }
    }

    if (!did) {
        return false; /// pattern not found
    }

    buf[bp] = '\0';
    if (bp + 1 > text_sz) {
        return false;
    }
    memcpy(text, buf, bp + 1);
    return true;
}

/**
 * @brief Resolve an event designator to the referenced command text
 *
 * Handles !!, !n, !-n, !?string?, !string, and the implicit last-command
 * forms (!$, !^, !*, !:...). The returned command is pool-allocated and the
 * caller frees it with lle_pool_free().
 *
 * @param spec Text after the leading ! (must not be NULL)
 * @param out_cmd Output for the referenced command text (must not be NULL)
 * @param consumed Output - event-designator characters consumed, excluding the
 * leading ! (must not be NULL)
 * @return LLE_SUCCESS, LLE_ERROR_INVALID_PARAMETER, LLE_ERROR_NOT_FOUND, or
 * LLE_ERROR_OUT_OF_MEMORY
 */
static lle_result_t resolve_event_command(const char *spec, char **out_cmd,
                                          size_t *consumed) {
    *out_cmd = NULL;
    *consumed = 0;

    if (!spec || !g_expansion_ctx.history_core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// !! and the implicit last-command forms (!$, !^, !*, !:...)
    if (spec[0] == '!' || spec[0] == '$' || spec[0] == '^' || spec[0] == '*' ||
        spec[0] == ':') {
        lle_history_entry_t *entry = NULL;
        lle_result_t res = lle_history_bridge_get_by_reverse_index(0, &entry);
        if (res != LLE_SUCCESS || !entry) {
            return LLE_ERROR_NOT_FOUND;
        }
        *out_cmd = lle_pool_strdup(entry->command);
        if (!*out_cmd) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                             "history expansion allocation failed");
        }
        *consumed = (spec[0] == '!') ? 1 : 0;
        return LLE_SUCCESS;
    }

    /// !n or !-n (number reference)
    if (isdigit((unsigned char)spec[0]) || spec[0] == '-') {
        int64_t number;
        bool is_relative;
        size_t used;
        if (!parse_history_number(spec, &number, &is_relative, &used)) {
            return LLE_ERROR_INVALID_PARAMETER;
        }

        lle_history_entry_t *entry = NULL;
        lle_result_t res;
        if (is_relative) {
            /// !-n is the command n entries back, where !-1 is the most recent
            /// (matching bash, in which !-1 is equivalent to !!). Reverse index
            /// 0 is the most recent entry, so !-n maps to reverse index n-1.
            if (number < 1) {
                return LLE_ERROR_INVALID_PARAMETER;
            }
            res = lle_history_bridge_get_by_reverse_index((size_t)(number - 1),
                                                          &entry);
        } else {
            res = lle_history_bridge_get_by_number((uint64_t)number, &entry);
        }

        if (res != LLE_SUCCESS || !entry) {
            return LLE_ERROR_NOT_FOUND;
        }
        *out_cmd = lle_pool_strdup(entry->command);
        if (!*out_cmd) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                             "history expansion allocation failed");
        }
        *consumed = used;
        return LLE_SUCCESS;
    }

    /// !?string? (substring search, closing ? optional)
    if (spec[0] == '?') {
        char search[EXPANSION_PATTERN_MAX];
        size_t i = 0;
        const char *q = spec + 1;
        while (*q && *q != '?' && !isspace((unsigned char)*q) &&
               i < sizeof(search) - 1) {
            search[i++] = *q++;
        }
        search[i] = '\0';
        if (i == 0) {
            return LLE_ERROR_INVALID_PARAMETER;
        }

        size_t total = 1 + i; /// '?' + string
        if (*q == '?') {
            total++; /// optional closing '?'
        }

        lle_history_search_results_t *results = lle_history_search_substring(
            g_expansion_ctx.history_core, search, 1);
        if (!results || lle_history_search_results_get_count(results) == 0) {
            if (results) {
                lle_history_search_results_destroy(results);
            }
            return LLE_ERROR_NOT_FOUND;
        }

        const lle_search_result_t *first =
            lle_history_search_results_get(results, 0);
        *out_cmd = lle_pool_strdup(first->command);
        lle_history_search_results_destroy(results);
        if (!*out_cmd) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                             "history expansion allocation failed");
        }
        *consumed = total;
        return LLE_SUCCESS;
    }

    /// !string (prefix search)
    char search[EXPANSION_PATTERN_MAX];
    size_t used = extract_expansion_string(spec, search, sizeof(search));
    if (used == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_history_search_results_t *results =
        lle_history_search_prefix(g_expansion_ctx.history_core, search, 1);
    if (!results || lle_history_search_results_get_count(results) == 0) {
        if (results) {
            lle_history_search_results_destroy(results);
        }
        return LLE_ERROR_NOT_FOUND;
    }

    const lle_search_result_t *first =
        lle_history_search_results_get(results, 0);
    *out_cmd = lle_pool_strdup(first->command);
    lle_history_search_results_destroy(results);
    if (!*out_cmd) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history expansion allocation failed");
    }
    *consumed = used;
    return LLE_SUCCESS;
}

/**
 * @brief Expand one history reference starting just past its leading !
 *
 * Resolves the event designator, applies an optional word designator and any
 * trailing modifiers (:p, :s, :gs), and returns the pool-allocated result.
 *
 * @param after_bang Text immediately after the ! (must not be NULL)
 * @param out Output for the expanded text (must not be NULL, caller frees)
 * @param consumed Output - characters consumed including the leading ! (must
 * not be NULL)
 * @param print_only Optional output - set true if a :p modifier is present
 * @return LLE_SUCCESS or an error code (NOT_FOUND, INVALID_PARAMETER,
 * BUFFER_OVERFLOW, OUT_OF_MEMORY)
 */
static lle_result_t expand_one(const char *after_bang, char **out,
                               size_t *consumed, bool *print_only) {
    char *event_cmd = NULL;
    size_t event_consumed = 0;
    lle_result_t res =
        resolve_event_command(after_bang, &event_cmd, &event_consumed);
    if (res != LLE_SUCCESS) {
        return res;
    }

    char text[EXPANSION_MAX_LENGTH];
    if (strlen(event_cmd) >= sizeof(text)) {
        lle_pool_free(event_cmd);
        return LLE_ERROR_BUFFER_OVERFLOW;
    }
    strcpy(text, event_cmd);

    const char *p = after_bang + event_consumed;

    /// Optional word designator: ':<wd>' or a bare $ ^ * (standalone forms).
    if (*p == ':' && (p[1] == '^' || p[1] == '$' || p[1] == '*' ||
                      isdigit((unsigned char)p[1]))) {
        char sel[EXPANSION_MAX_LENGTH];
        size_t wc = 0;
        if (!select_word_range(event_cmd, p + 1, sel, sizeof(sel), &wc)) {
            lle_pool_free(event_cmd);
            return LLE_ERROR_INVALID_PARAMETER;
        }
        strcpy(text, sel);
        p += 1 + wc;
    } else if (*p == '^' || *p == '$' || *p == '*') {
        char sel[EXPANSION_MAX_LENGTH];
        size_t wc = 0;
        if (!select_word_range(event_cmd, p, sel, sizeof(sel), &wc)) {
            lle_pool_free(event_cmd);
            return LLE_ERROR_INVALID_PARAMETER;
        }
        strcpy(text, sel);
        p += wc;
    }

    /// Optional modifiers: zero or more ':x'
    while (*p == ':') {
        char m = p[1];
        bool global = false;
        const char *mp = p + 1;

        if (m == 'p') {
            if (print_only) {
                *print_only = true;
            }
            p += 2;
            continue;
        }

        if (m == 'g' && mp[1] == 's') {
            global = true;
            mp++;
            m = 's';
        }

        if (m == 's') {
            const char *q = mp + 1;
            char delim = *q;
            if (!delim) {
                lle_pool_free(event_cmd);
                return LLE_ERROR_INVALID_PARAMETER;
            }
            q++;

            char old_pat[EXPANSION_PATTERN_MAX] = {0};
            char new_pat[EXPANSION_PATTERN_MAX] = {0};
            size_t i = 0;
            while (*q && *q != delim && i < sizeof(old_pat) - 1) {
                old_pat[i++] = *q++;
            }
            if (*q != delim) {
                lle_pool_free(event_cmd);
                return LLE_ERROR_INVALID_PARAMETER;
            }
            q++;
            i = 0;
            while (*q && *q != delim && i < sizeof(new_pat) - 1) {
                new_pat[i++] = *q++;
            }
            if (*q == delim) {
                q++; /// optional trailing delimiter
            }

            if (!apply_substitution(text, sizeof(text), old_pat, new_pat,
                                    global)) {
                lle_pool_free(event_cmd);
                return LLE_ERROR_INVALID_PARAMETER;
            }
            p = q;
            continue;
        }

        /// Unknown modifier: leave it for literal copy by the caller.
        break;
    }

    lle_pool_free(event_cmd);

    *out = lle_pool_strdup(text);
    if (!*out) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history expansion allocation failed");
    }
    *consumed = 1 + (size_t)(p - after_bang);
    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - EXPANSION OPERATIONS
 * ============================================================================
 */

/**
 * @brief Initialize history expansion system
 *
 * Sets up the global expansion context with the provided history core.
 * Must be called before using any expansion functions.
 *
 * @param history_core History core instance (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if history_core
 * is NULL
 */
lle_result_t lle_history_expansion_init(lle_history_core_t *history_core) {
    if (!history_core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    g_expansion_ctx.history_core = history_core;
    g_expansion_ctx.recursion_depth = 0;
    g_expansion_ctx.space_disables_expansion = true;
    g_expansion_ctx.verify_before_execute = false;

    return LLE_SUCCESS;
}

/**
 * @brief Shutdown history expansion system
 *
 * Clears the global expansion context and resets state.
 *
 * @return LLE_SUCCESS always
 */
lle_result_t lle_history_expansion_shutdown(void) {
    g_expansion_ctx.history_core = NULL;
    g_expansion_ctx.recursion_depth = 0;

    return LLE_SUCCESS;
}

/**
 * @brief Check if a command line contains history expansion
 *
 * Checks for !, !!, !n, !-n, !string, !?string, or ^old^new patterns.
 * Respects the space-disables-expansion setting.
 *
 * @param command Command line to check (may be NULL or empty)
 * @return true if expansion is present, false otherwise
 */
bool lle_history_expansion_needed(const char *command) {
    if (!command || command[0] == '\0') {
        return false;
    }

    /// Check for space prefix disabling expansion
    if (g_expansion_ctx.space_disables_expansion &&
        isspace((unsigned char)command[0])) {
        return false;
    }

    /// Check for quick substitution (^old^new)
    if (command[0] == '^') {
        return true;
    }

    /// Check for ! expansion
    return (find_expansion_marker(command, 0) >= 0);
}

/**
 * @brief Expand history references in a command line
 *
 * Expands all history references (!!, !n, !-n, !string, !$, ^old^new, etc.)
 * in the command, applying any word designators and :p / :s modifiers. The
 * result must be freed by the caller using lle_pool_free().
 *
 * @param command Original command with history references (must not be NULL)
 * @param expanded Output pointer for expanded command (allocated, caller must
 * free) (must not be NULL)
 * @param print_only Optional output - set true when a :p modifier requests
 * that the expansion be printed but not executed (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters
 * invalid, LLE_ERROR_NOT_INITIALIZED if expansion system not initialized,
 *         LLE_ERROR_INVALID_STATE if recursion depth exceeded,
 *         LLE_ERROR_NOT_FOUND if referenced entry not found,
 *         LLE_ERROR_BUFFER_OVERFLOW if expanded command too long,
 *         LLE_ERROR_OUT_OF_MEMORY on allocation failure
 */
lle_result_t lle_history_expand_line(const char *command, char **expanded,
                                     bool *print_only) {
    if (!command || !expanded) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (print_only) {
        *print_only = false;
    }

    if (!g_expansion_ctx.history_core) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    *expanded = NULL;

    /// Check for recursion depth
    if (g_expansion_ctx.recursion_depth >= EXPANSION_MAX_DEPTH) {
        return LLE_ERROR_INVALID_STATE;
    }

    /// Check if expansion is needed
    if (!lle_history_expansion_needed(command)) {
        *expanded = lle_pool_strdup(command);
        return *expanded ? LLE_SUCCESS : LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Handle quick substitution (^old^new)
    if (command[0] == '^') {
        const char *p = command + 1;
        char old_pattern[EXPANSION_PATTERN_MAX] = {0};
        char new_pattern[EXPANSION_PATTERN_MAX] = {0};

        /// Extract old pattern
        size_t i = 0;
        while (*p != '\0' && *p != '^' && i < sizeof(old_pattern) - 1) {
            old_pattern[i++] = *p++;
        }

        if (*p != '^') {
            return LLE_ERROR_INVALID_PARAMETER;
        }

        p++; /// Skip second ^

        /// Extract new pattern
        i = 0;
        while (*p != '\0' && *p != ' ' && *p != '\n' &&
               i < sizeof(new_pattern) - 1) {
            new_pattern[i++] = *p++;
        }

        /// Get last command
        lle_history_entry_t *last_entry = NULL;
        lle_result_t res =
            lle_history_bridge_get_by_reverse_index(0, &last_entry);

        if (res != LLE_SUCCESS || !last_entry) {
            return LLE_ERROR_NOT_FOUND;
        }

        /// Perform substitution
        char result[EXPANSION_MAX_LENGTH];
        if (!perform_quick_substitution(last_entry->command, old_pattern,
                                        new_pattern, result, sizeof(result))) {
            return LLE_ERROR_INVALID_PARAMETER;
        }

        *expanded = lle_pool_strdup(result);
        return *expanded ? LLE_SUCCESS : LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Handle ! expansions
    char result[EXPANSION_MAX_LENGTH];
    size_t result_pos = 0;
    size_t cmd_pos = 0;
    size_t cmd_len = strlen(command);

    g_expansion_ctx.recursion_depth++;

    while (cmd_pos < cmd_len && result_pos < EXPANSION_MAX_LENGTH - 1) {
        ssize_t expansion_pos = find_expansion_marker(command + cmd_pos, 0);

        if (expansion_pos < 0) {
            /// No more expansions, copy rest of string
            size_t remaining = cmd_len - cmd_pos;
            if (result_pos + remaining >= EXPANSION_MAX_LENGTH) {
                g_expansion_ctx.recursion_depth--;
                return LLE_ERROR_BUFFER_OVERFLOW;
            }
            memcpy(result + result_pos, command + cmd_pos, remaining);
            result_pos += remaining;
            break;
        }

        /// Copy text before expansion
        if (expansion_pos > 0) {
            memcpy(result + result_pos, command + cmd_pos, expansion_pos);
            result_pos += expansion_pos;
            cmd_pos += expansion_pos;
        }

        /// Process the expansion
        char *expanded_text = NULL;
        size_t consumed = 0;
        lle_result_t res = expand_one(command + cmd_pos + 1, &expanded_text,
                                      &consumed, print_only);

        if (res != LLE_SUCCESS) {
            g_expansion_ctx.recursion_depth--;
            return res;
        }

        /// Copy expanded text
        size_t expanded_len = strlen(expanded_text);
        if (result_pos + expanded_len >= EXPANSION_MAX_LENGTH) {
            lle_pool_free(expanded_text);
            g_expansion_ctx.recursion_depth--;
            return LLE_ERROR_BUFFER_OVERFLOW;
        }

        memcpy(result + result_pos, expanded_text, expanded_len);
        result_pos += expanded_len;
        cmd_pos += consumed;

        lle_pool_free(expanded_text);
    }

    result[result_pos] = '\0';
    g_expansion_ctx.recursion_depth--;

    *expanded = lle_pool_strdup(result);
    return *expanded ? LLE_SUCCESS : LLE_ERROR_OUT_OF_MEMORY;
}

/**
 * @brief Set whether leading space disables expansion
 *
 * When enabled, commands starting with a space will not undergo
 * history expansion (matching bash behavior).
 *
 * @param enabled true to enable space-disables-expansion (bash behavior)
 * @return LLE_SUCCESS always
 */
lle_result_t lle_history_expansion_set_space_disables(bool enabled) {
    g_expansion_ctx.space_disables_expansion = enabled;
    return LLE_SUCCESS;
}

/**
 * @brief Get whether leading space disables expansion
 *
 * Returns the current setting for space-disables-expansion behavior.
 *
 * @return true if enabled, false otherwise
 */
bool lle_history_expansion_get_space_disables(void) {
    return g_expansion_ctx.space_disables_expansion;
}

/**
 * @brief Set whether to verify expansion before execution
 *
 * When enabled, expanded commands are displayed for user confirmation
 * before execution (like bash's 'verify' option).
 *
 * @param enabled true to enable verification
 * @return LLE_SUCCESS always
 */
lle_result_t lle_history_expansion_set_verify(bool enabled) {
    g_expansion_ctx.verify_before_execute = enabled;
    return LLE_SUCCESS;
}

/**
 * @brief Get whether verification is enabled
 *
 * Returns the current setting for expansion verification.
 *
 * @return true if verification is enabled, false otherwise
 */
bool lle_history_expansion_get_verify(void) {
    return g_expansion_ctx.verify_before_execute;
}
