/**
 * @file bin_setopt.c
 * @brief `setopt` builtin -- enable feature-matrix entries (zsh canonical)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "config_registry.h"
#include "lush.h"
#include "shell_mode.h"

/**
 * @brief Set shell options (enable features)
 *
 * Zsh-style setopt command for enabling shell features.
 * Usage:
 *   setopt              - List all options with current state
 *   setopt -p           - Print in re-usable format
 *   setopt -q <opt>     - Query silently (exit status only)
 *   setopt <opt> [...]  - Enable one or more options
 *
 * Options can be specified using:
 *   - Canonical name: extended_glob
 *   - Short alias: extglob
 *   - Underscore or no underscore: extended_glob / extendedglob
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_setopt(int argc, char **argv) {
    bool print_format = false;
    bool query_mode = false;
    int start_idx = 1;

    /* Parse flags */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-p") == 0) {
                print_format = true;
                start_idx = i + 1;
            } else if (strcmp(argv[i], "-q") == 0) {
                query_mode = true;
                start_idx = i + 1;
            } else if (strcmp(argv[i], "--") == 0) {
                start_idx = i + 1;
                break;
            } else {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_OPTION,
                                      builtin_get_source_location(),
                                      "invalid option: %s", argv[i]);
                return 1;
            }
        } else {
            break;
        }
    }

    /* No option specified - list all options */
    if (start_idx >= argc) {
        if (query_mode) {
            executor_error_report(current_executor, SHELL_ERR_MISSING_ARGUMENT,
                                  builtin_get_source_location(),
                                  "-q requires an option name");
            return 1;
        }

        printf("Shell options:\n");
        for (int i = 0; i < (int)FEATURE_COUNT; i++) {
            shell_feature_t feature = (shell_feature_t)i;
            const char *name = shell_feature_name(feature);
            bool enabled = shell_mode_allows(feature);

            if (print_format) {
                printf("%s %s\n", enabled ? "setopt" : "unsetopt", name);
            } else {
                printf("  %-30s %s\n", name, enabled ? "on" : "off");
            }
        }
        return 0;
    }

    /* Process each option */
    for (int i = start_idx; i < argc; i++) {
        shell_feature_t feature;
        bool invert = false;

        if (!shell_feature_parse(argv[i], &feature, &invert)) {
            if (!query_mode) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_OPTION,
                                      builtin_get_source_location(),
                                      "unknown option: %s", argv[i]);
            }
            return 1;
        }

        if (query_mode) {
            /* For query, return status based on the *aliased* sense:
             * `setopt -q bsd_echo` is true when xpg_echo is OFF. */
            bool effective = shell_mode_allows(feature) ^ invert;
            return effective ? 0 : 1;
        }

        /* Enable from the alias's perspective; flip on the underlying
         * feature when the alias is inverted. */
        bool target_value = !invert;
        if (target_value) {
            shell_feature_enable(feature);
        } else {
            shell_feature_disable(feature);
        }

        /* Sync to registry if initialized */
        if (config_registry_is_initialized()) {
            char key[CREG_KEY_MAX];
            snprintf(key, sizeof(key), "shell.features.%s",
                     shell_feature_name(feature));
            config_registry_set_boolean(key, target_value);
        }
    }

    return 0;
}
