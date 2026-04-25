/**
 * @file test_symtable_stubs.c
 * @brief Stub implementations for symbol table unit tests
 */

#include "lush.h"
#include <stdbool.h>

/* Global variable stub */
int last_exit_status = 0;

/* Shell options stub */
shell_options_t shell_opts = {0};

/* Shell mode stub */
bool shell_mode_allows(int feature) {
    (void)feature;
    return true; /* Allow all features in tests */
}

/* Interactive shell stub */
bool is_interactive_shell(void) { return false; /* Non-interactive in tests */ }
