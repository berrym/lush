/**
 * Mock Shell Data for Completion Testing
 *
 * Provides minimal mock implementations of shell data structures
 * so completion tests can run standalone without full lush.
 *
 * Stubs are also provided for the executor symbols the word_context
 * analyzer dereferences during expansion resolution. Tests that want
 * to exercise the resolution path set the corresponding mock control
 * variables before invoking the analyzer; the stubs read those
 * variables and produce the configured results. Tests that don't care
 * about resolution leave the controls at their defaults (current
 * executor NULL, no programmed expansion results) and the analyzer's
 * resolve helpers bail out early without crashing.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/// Mock builtin structure
typedef struct {
    char *name;
    void *function;
} builtin_t;

/// Mock builtins array
static builtin_t mock_builtins[] = {
    {  "cd", NULL},
    {"echo", NULL},
    {"exit", NULL},
    {  NULL, NULL}
};

builtin_t *builtins = mock_builtins;
int builtins_count = 3;

/// Mock aliases hashtable (NULL for now)
void *aliases = NULL;

/// Mock lookup_alias function
char *lookup_alias(const char *name) {
    (void)name;
    return NULL; /// No aliases in mock
}

/// Mock environ for variable completion
char **environ = NULL;

/* ---------- Executor stubs for the word_context resolution layer ----------
 *
 * The analyzer references current_executor and calls expand_if_needed and
 * expand_brace_pattern when resolving expansions in path-prefix bytes. The
 * test binary doesn't link the full executor; these stubs satisfy the
 * link and give tests a knob to control the simulated expansion result.
 *
 * Mock controls:
 *   mock_expand_result        — string returned by expand_if_needed (NULL
 *                               causes expand_if_needed to return NULL)
 *   mock_brace_branches[]     — pre-set branch list returned by
 *                               expand_brace_pattern when its input
 *                               contains a brace; NULL/0 means pass
 *                               through (single-element with the input)
 *   mock_brace_branch_count   — count for the above
 *
 * The analyzer treats current_executor == NULL as "no executor available,
 * skip resolution"; tests that want resolution to run set this to any
 * non-NULL value.
 */

/// Forward-declared opaque type — the stubs never dereference it.
typedef struct executor executor_t;

executor_t *current_executor = NULL;

const char *mock_expand_result = NULL;
const char *const *mock_brace_branches = NULL;
int mock_brace_branch_count = 0;

void mock_completion_reset(void) {
    current_executor = NULL;
    mock_expand_result = NULL;
    mock_brace_branches = NULL;
    mock_brace_branch_count = 0;
}

char *expand_if_needed(executor_t *executor, const char *text) {
    (void)executor;
    (void)text;
    if (!mock_expand_result)
        return NULL;
    size_t n = strlen(mock_expand_result);
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, mock_expand_result, n + 1);
    return out;
}

char **expand_brace_pattern(const char *pattern, int *expanded_count) {
    if (!pattern || !expanded_count) {
        if (expanded_count)
            *expanded_count = 0;
        return NULL;
    }
    /// Programmed branches: emit them verbatim.
    if (mock_brace_branches && mock_brace_branch_count > 0) {
        char **r =
            malloc(sizeof(char *) * (size_t)(mock_brace_branch_count + 1));
        if (!r) {
            *expanded_count = 0;
            return NULL;
        }
        for (int i = 0; i < mock_brace_branch_count; i++) {
            size_t n = strlen(mock_brace_branches[i]);
            r[i] = malloc(n + 1);
            if (!r[i]) {
                for (int j = 0; j < i; j++)
                    free(r[j]);
                free(r);
                *expanded_count = 0;
                return NULL;
            }
            memcpy(r[i], mock_brace_branches[i], n + 1);
        }
        r[mock_brace_branch_count] = NULL;
        *expanded_count = mock_brace_branch_count;
        return r;
    }
    /// Default: pass through (matches expand_brace_pattern's no-brace case).
    char **r = malloc(sizeof(char *) * 2);
    if (!r) {
        *expanded_count = 0;
        return NULL;
    }
    size_t n = strlen(pattern);
    r[0] = malloc(n + 1);
    if (!r[0]) {
        free(r);
        *expanded_count = 0;
        return NULL;
    }
    memcpy(r[0], pattern, n + 1);
    r[1] = NULL;
    *expanded_count = 1;
    return r;
}
