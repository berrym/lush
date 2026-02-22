/**
 * @file display_test_stubs.c
 * @brief Mock implementations of shell functions for display system tests
 *
 * The display system (libdisplay.a) has dependencies on various shell
 * subsystems. This file provides stub implementations so that LLE tests
 * can link against libdisplay.a without pulling in the entire shell.
 *
 * These stubs return sensible defaults or no-ops for testing purposes.
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "builtins.h"
#include "config.h"
#include "input_continuation.h"
#include "libhashtable/ht.h"

/* ============================================================================
 * Global Variables
 * ============================================================================
 */

/**
 * Mock config structure - provides default configuration for tests
 * Only initializes fields that are commonly accessed by display code
 */
config_values_t config = {
    .history_enabled = true,
    .history_size = 1000,
    .colors_enabled = true,
    .multiline_mode = true,
    .display_syntax_highlighting = true,
    .display_autosuggestions = true,
    .tab_width = 8,
};

/* ============================================================================
 * Prompt Functions
 * ============================================================================
 */

char *build_prompt(void) {
    static char default_prompt[] = "$ ";
    return default_prompt;
}

char *rebuild_prompt(void) { return build_prompt(); }

void lle_shell_update_prompt(void) { /* No-op in tests */ }

const char *lle_shell_get_rendered_prompt(void) {
    return "$ "; /* Default prompt in tests */
}

/* Note: lush_generate_prompt() is now in libdisplay.a via
 * display_integration.c */

char *format_git_prompt(void) { return NULL; /* No git info in tests */ }

void update_git_info(void) { /* No-op */ }

/* ============================================================================
 * Continuation/Multiline Functions
 * ============================================================================
 */

void continuation_state_init(continuation_state_t *state) {
    if (state) {
        memset(state, 0, sizeof(*state));
    }
}

void continuation_state_cleanup(continuation_state_t *state) {
    (void)state; /* No-op */
}

void continuation_analyze_line(const char *line, continuation_state_t *state) {
    (void)line;
    (void)state;
    /* No-op - line is complete */
}

const char *continuation_get_prompt(const continuation_state_t *state) {
    (void)state;
    static char cont_prompt[] = "> ";
    return cont_prompt;
}

/* ============================================================================
 * Autosuggestion Functions
 * ============================================================================
 */

void lush_autosuggestions_init(void) { /* No-op */ }

char *lush_get_suggestion(const char *prefix) {
    (void)prefix;
    return NULL; /* No suggestions in tests */
}

void lush_free_autosuggestion(char *suggestion) {
    (void)suggestion; /* No-op - our stubs don't allocate */
}

/* ============================================================================
 * Symbol Table Functions
 * ============================================================================
 */

char *symtable_get_global(const char *name) {
    (void)name;
    return NULL; /* No variables in tests */
}

/* ============================================================================
 * Alias Functions
 * ============================================================================
 */

/* Global aliases hash table - NULL means no aliases defined */
ht_strstr_t *aliases = NULL;

const char *lookup_alias(const char *name) {
    (void)name;
    return NULL; /* No aliases in tests */
}

/* ============================================================================
 * Builtin Functions
 * ============================================================================
 */

/* Empty builtins array for tests */
builtin builtins[] = {
    {NULL, NULL, NULL} /* Terminator */
};
const size_t builtins_count = 0;

bool is_builtin(const char *name) {
    (void)name;
    return false; /* No builtins recognized in tests */
}

/* ============================================================================
 * Shell State Functions
 * ============================================================================
 */

bool is_interactive_shell(void) {
    return false; /* Tests run non-interactively */
}

/* ============================================================================
 * Fuzzy Matching Functions
 * ============================================================================
 */

/* Note: fuzzy_levenshtein_distance is provided by libfuzzy.a */

/* ============================================================================
 * Executor Functions (for job count in prompt context)
 * ============================================================================
 */

#include "executor.h"

/* Mock executor for tests - no jobs */
executor_t *current_executor = NULL;

void executor_update_job_status(executor_t *executor) {
    (void)executor; /* No-op in tests */
}

int executor_count_jobs(executor_t *executor) {
    (void)executor;
    return 0; /* No jobs in tests */
}

executor_t *get_global_executor(void) {
    return NULL; /* No executor in tests */
}

/* ============================================================================
 * Network Functions (for SSH host completion)
 * ============================================================================
 */

/* SSH host cache type stub */
typedef struct ssh_host_cache {
    void *hosts;
    size_t count;
    size_t capacity;
} ssh_host_cache_t;

ssh_host_cache_t *get_ssh_host_cache(void) {
    return NULL; /* No SSH hosts in tests */
}
