/**
 * @file opts.c
 * @brief Legacy shell options interface
 *
 * Provides backward-compatible interface to shell options that are
 * now managed by the configuration system. Maps legacy option names
 * to their configuration equivalents.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "config.h"
#include "lush.h"
#include "symtable.h"

#include <getopt.h>
#include <stdbool.h>

/**
 * @brief Initialize legacy shell options
 *
 * Sets up shell option variables in the symbol table based on
 * current configuration values. This provides compatibility with
 * scripts that reference these variables directly.
 */
void init_shell_opts(void) {
    /// Legacy function - now uses config system values
    symtable_set_global_int("ENHANCED_COMPLETION", config.completion_enabled);
}

/**
 * @brief Check if enhanced completion is enabled
 *
 * @return true if enhanced tab completion is enabled, false otherwise
 */
bool get_enhanced_completion(void) { return config.completion_enabled; }
