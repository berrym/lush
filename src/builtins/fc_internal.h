/**
 * @file fc_internal.h
 * @brief Internal fc builtin interface for white-box testing
 *
 * Exposes the deterministic, history-driven pieces of the fc builtin so
 * tests exercise the real implementation rather than a reimplementation.
 * The functions take an explicit history core and option struct; only the
 * bin_fc entry point reaches for the global editor's history.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_FC_INTERNAL_H
#define LUSH_FC_INTERNAL_H

#include "lle/history.h"
#include <stdbool.h>

/// fc command options resolved from the command line and range arguments.
typedef struct fc_options {
    bool list_mode;
    bool reverse_order;
    bool suppress_numbers;
    bool substitute_mode;
    char *editor;
    char *old_pattern;
    char *new_pattern;
    int first;
    int last;
    bool range_valid;
} fc_options_t;

/**
 * @brief Parse an old=new substitution pattern.
 *
 * Splits on the first '=' so the replacement text may itself contain '='.
 * With no '=', the whole pattern is the old text and the replacement is
 * empty.
 *
 * @param pattern Pattern text (must be non-NULL)
 * @param old Output for the old text (caller frees)
 * @param new_str Output for the replacement text (caller frees)
 * @return true on success, false on NULL argument or allocation failure
 */
bool fc_parse_substitution_pattern(const char *pattern, char **old,
                                   char **new_str);

/**
 * @brief Resolve the first/last range specifiers against history.
 *
 * Populates opts->first, opts->last (0-based indices) and opts->range_valid.
 * Honors list-mode defaults and reorders so first <= last.
 *
 * @param history History core to query
 * @param first_str First range specifier (number, -offset, or prefix; NULL
 *                  for the mode default)
 * @param last_str Last range specifier (may be NULL)
 * @param opts Options to populate
 * @return true on success, false on empty history or out-of-range specifier
 */
bool fc_parse_range(lle_history_core_t *history, const char *first_str,
                    const char *last_str, fc_options_t *opts);

/**
 * @brief List history entries in opts->first..opts->last to stdout.
 *
 * Emits "%5d  %s\n" per entry, or "%s\n" when opts->suppress_numbers, in
 * forward or reverse order per opts->reverse_order.
 *
 * @param history History core to query
 * @param opts Options containing the resolved range and display flags
 * @return 0 on success, 1 on error
 */
int fc_list(lle_history_core_t *history, fc_options_t *opts);

/**
 * @brief Resolve the editor command per FCEDIT, EDITOR, VISUAL, then "ed".
 *
 * @return Newly allocated editor command (caller frees)
 */
char *fc_get_default_editor(void);

#endif /* LUSH_FC_INTERNAL_H */
