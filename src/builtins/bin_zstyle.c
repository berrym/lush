/**
 * @file bin_zstyle.c
 * @brief zsh `zstyle` builtin: pattern-based style configuration
 *
 * zsh's zstyle is the configuration interface for completion styles
 * (and several other subsystems). 59 sites in oh-my-zsh; 53 of them
 * are query forms (-t/-s/-T/-a/-b). A silent-no-op approach is
 * verifiably wrong: a script that sets a style and then queries it
 * sees stale state under lush versus the real value under zsh.
 *
 * Implementation:
 *
 *   zstyle PATTERN STYLE VALUE...     -- set
 *   zstyle -d [PATTERN [STYLE...]]    -- delete
 *   zstyle -L [PATTERN [STYLE...]]    -- list as re-runnable
 *   zstyle -e PATTERN STYLE BODY      -- set evaluated style (recorded
 *                                        but body not actually
 *                                        re-evaluated on query)
 *   zstyle -t PATTERN STYLE [VALUE]   -- true if set and value matches
 *   zstyle -T PATTERN STYLE           -- like -t but true when unset
 *   zstyle -s PATTERN STYLE VAR [SEP] -- get value into VAR
 *   zstyle -a PATTERN STYLE VAR       -- get array-shaped value into VAR
 *   zstyle -b PATTERN STYLE VAR       -- get boolean into VAR
 *   zstyle -g VAR PATTERN STYLE       -- get pattern back (rare)
 *
 * Pattern matching: zsh uses fnmatch-style patterns. We use POSIX
 * fnmatch(3) for queries; exact-match patterns work without any
 * meta characters, wildcards like `:completion:*` work too. The
 * matching semantic for queries: "the most-specific recorded pattern
 * whose pattern matches the query pattern wins." For non-interactive
 * corpus use, exact-match equivalence covers the common case; richer
 * specificity ranking is a future fix.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "pattern_match.h"
#include "symtable.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Side table
 * ============================================================================
 */

typedef struct zstyle_entry {
    char *pattern; ///< Matching pattern (e.g., ":completion:*:cd:*")
    char *style;   ///< Style name (e.g., "menu", "list-colors")
    char *value;   ///< Space-joined value as the user supplied it
    struct zstyle_entry *next;
} zstyle_entry_t;

static zstyle_entry_t *g_zstyle_table = NULL;

void zstyle_table_reset(void) {
    while (g_zstyle_table) {
        zstyle_entry_t *next = g_zstyle_table->next;
        free(g_zstyle_table->pattern);
        free(g_zstyle_table->style);
        free(g_zstyle_table->value);
        free(g_zstyle_table);
        g_zstyle_table = next;
    }
}

static zstyle_entry_t *find_exact(const char *pattern, const char *style) {
    for (zstyle_entry_t *e = g_zstyle_table; e; e = e->next) {
        if (strcmp(e->pattern, pattern) == 0 && strcmp(e->style, style) == 0) {
            return e;
        }
    }
    return NULL;
}

/// Find the recorded entry whose pattern matches the query pattern via
/// lush_pattern_match (i.e., the query pattern is the "context" being
/// looked up). Returns the first match; future enhancement: specificity
/// ranking.
static zstyle_entry_t *find_matching(const char *query_pattern,
                                     const char *style) {
    if (!query_pattern || !style) {
        return NULL;
    }
    /// Exact match first.
    zstyle_entry_t *exact = find_exact(query_pattern, style);
    if (exact) {
        return exact;
    }
    /// Then glob-match: recorded pattern's wildcards expand against the
    /// query pattern. (Reverse of the conventional zsh lookup, where the
    /// query is a specific context string and recorded patterns match
    /// against it. For corpus purposes both directions cover the same
    /// cases; refining is a future enhancement.)
    for (zstyle_entry_t *e = g_zstyle_table; e; e = e->next) {
        if (strcmp(e->style, style) != 0) {
            continue;
        }
        if (lush_pattern_match(query_pattern, e->pattern) ||
            lush_pattern_match(e->pattern, query_pattern)) {
            return e;
        }
    }
    return NULL;
}

/// Join argv[start..end) with single spaces.
static char *join_args(char **argv, int start, int end) {
    if (start >= end) {
        return strdup("");
    }
    size_t total = 1;
    for (int i = start; i < end; i++) {
        total += strlen(argv[i]) + 1;
    }
    char *out = malloc(total);
    if (!out) {
        return NULL;
    }
    out[0] = '\0';
    for (int i = start; i < end; i++) {
        if (i > start) {
            strcat(out, " ");
        }
        strcat(out, argv[i]);
    }
    return out;
}

static void record_style(const char *pattern, const char *style,
                         const char *value) {
    zstyle_entry_t *existing = find_exact(pattern, style);
    if (existing) {
        free(existing->value);
        existing->value = strdup(value);
        return;
    }
    zstyle_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return;
    }
    entry->pattern = strdup(pattern);
    entry->style = strdup(style);
    entry->value = strdup(value);
    entry->next = g_zstyle_table;
    g_zstyle_table = entry;
}

static void detach(zstyle_entry_t *target) {
    zstyle_entry_t **slot = &g_zstyle_table;
    while (*slot) {
        if (*slot == target) {
            *slot = target->next;
            free(target->pattern);
            free(target->style);
            free(target->value);
            free(target);
            return;
        }
        slot = &(*slot)->next;
    }
}

/* ============================================================================
 * Public dispatch
 * ============================================================================
 */

int bin_zstyle(int argc, char **argv) {
    if (argc < 2) {
        /// Bare zstyle: list everything in re-runnable form.
        for (zstyle_entry_t *e = g_zstyle_table; e; e = e->next) {
            printf("zstyle %s %s %s\n", e->pattern, e->style, e->value);
        }
        return 0;
    }

    const char *first = argv[1];
    if (first[0] != '-' || first[1] == '\0') {
        /// Set form: zstyle PATTERN STYLE VALUE...
        if (argc < 4) {
            return 1;
        }
        char *joined = join_args(argv, 3, argc);
        if (!joined) {
            return 1;
        }
        record_style(argv[1], argv[2], joined);
        free(joined);
        return 0;
    }

    /// Flag forms.
    char mode = first[1];
    switch (mode) {
    case 'd':
        /// zstyle -d [PATTERN [STYLE ...]]
        if (argc == 2) {
            zstyle_table_reset();
            return 0;
        }
        if (argc == 3) {
            /// Delete every entry with this pattern.
            zstyle_entry_t *e = g_zstyle_table;
            while (e) {
                zstyle_entry_t *next = e->next;
                if (strcmp(e->pattern, argv[2]) == 0) {
                    detach(e);
                }
                e = next;
            }
            return 0;
        }
        for (int i = 3; i < argc; i++) {
            zstyle_entry_t *e = find_exact(argv[2], argv[i]);
            if (e) {
                detach(e);
            }
        }
        return 0;

    case 'L':
        /// List as re-runnable form, optionally filtered.
        for (zstyle_entry_t *e = g_zstyle_table; e; e = e->next) {
            if (argc >= 3 && strcmp(e->pattern, argv[2]) != 0) {
                continue;
            }
            if (argc >= 4 && strcmp(e->style, argv[3]) != 0) {
                continue;
            }
            printf("zstyle %s %s %s\n", e->pattern, e->style, e->value);
        }
        return 0;

    case 'e':
        /// zstyle -e PATTERN STYLE BODY -- evaluated style. Record the
        /// body verbatim; lush doesn't re-evaluate the body on query
        /// (a future enhancement). Most corpus uses set a constant
        /// value via -e for compatibility with later -t / -s queries.
        if (argc < 5) {
            return 1;
        }
        record_style(argv[2], argv[3], argv[4]);
        return 0;

    case 't': {
        /// zstyle -t PATTERN STYLE [VALUE]
        /// Exit 0 if pattern+style is recorded and (value matches OR
        /// recorded value == "true"). Exit 1 if recorded with a
        /// different value. Exit 2 if not recorded.
        if (argc < 4) {
            return 2;
        }
        zstyle_entry_t *e = find_matching(argv[2], argv[3]);
        if (!e) {
            return 2;
        }
        if (argc >= 5) {
            return strcmp(e->value, argv[4]) == 0 ? 0 : 1;
        }
        return strcmp(e->value, "true") == 0 ? 0 : 1;
    }

    case 'T': {
        /// zstyle -T PATTERN STYLE
        /// Like -t but treats not-recorded as true (returns 0).
        if (argc < 4) {
            return 2;
        }
        zstyle_entry_t *e = find_matching(argv[2], argv[3]);
        if (!e) {
            return 0;
        }
        return strcmp(e->value, "true") == 0 ? 0 : 1;
    }

    case 's': {
        /// zstyle -s PATTERN STYLE VAR [SEP]
        /// Set VAR to the value (joined by SEP if multiple values; we
        /// store the joined form already). Exit 0 if found, 1 if not.
        if (argc < 5) {
            return 1;
        }
        zstyle_entry_t *e = find_matching(argv[2], argv[3]);
        if (!e) {
            symtable_set_global(argv[4], "");
            return 1;
        }
        symtable_set_global(argv[4], e->value);
        return 0;
    }

    case 'b': {
        /// zstyle -b PATTERN STYLE VAR -- boolean form
        if (argc < 5) {
            return 1;
        }
        zstyle_entry_t *e = find_matching(argv[2], argv[3]);
        if (!e) {
            symtable_set_global(argv[4], "no");
            return 1;
        }
        bool yes = strcmp(e->value, "true") == 0 ||
                   strcmp(e->value, "yes") == 0 ||
                   strcmp(e->value, "on") == 0 || strcmp(e->value, "1") == 0;
        symtable_set_global(argv[4], yes ? "yes" : "no");
        return 0;
    }

    case 'a': {
        /// zstyle -a PATTERN STYLE VAR -- array form. Set VAR to the
        /// joined value; full array-of-words support would need to
        /// route through the array storage. Documented limitation.
        if (argc < 5) {
            return 1;
        }
        zstyle_entry_t *e = find_matching(argv[2], argv[3]);
        if (!e) {
            symtable_set_global(argv[4], "");
            return 1;
        }
        symtable_set_global(argv[4], e->value);
        return 0;
    }

    case 'g':
        /// zstyle -g VAR PATTERN STYLE -- get the pattern that matched.
        /// Niche; covered minimally for completeness.
        if (argc < 5) {
            return 1;
        }
        {
            zstyle_entry_t *e = find_matching(argv[3], argv[4]);
            if (!e) {
                symtable_set_global(argv[2], "");
                return 1;
            }
            symtable_set_global(argv[2], e->pattern);
            return 0;
        }

    default:
        /// Unknown flag: silent no-op so scripts continue.
        return 0;
    }
}
