/**
 * @file test_symtable_stubs.c
 * @brief Stub implementations for symbol table unit tests
 */

#include "lush.h"
#include "shell_mode.h"
#include <stdbool.h>

/* Global variable stub */
int last_exit_status = 0;

/* Shell options stub */
shell_options_t shell_opts = {0};

/* Shell mode stub */
bool shell_mode_allows(shell_feature_t feature) {
    (void)feature;
    return true; /* Allow all features in tests */
}

/* Shell mode getter stub — symtable.c uses this for issue-#69
 * insertion-order vs hashtable-order dispatch on assoc array
 * iteration. Tests run in lush mode (default) by convention so
 * insertion-order ordering applies. */
shell_mode_t shell_mode_get(void) { return SHELL_MODE_LUSH; }

/* Interactive shell stub */
bool is_interactive_shell(void) { return false; /* Non-interactive in tests */ }
