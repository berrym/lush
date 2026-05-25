/**
 * @file bin_unsetopt.c
 * @brief `unsetopt` builtin -- disable feature-matrix entries (zsh canonical)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "config_registry.h"
#include "lush.h"
#include "shell_mode.h"

/**
 * @brief Unset shell options (disable features)
 *
 * Zsh-style unsetopt command for disabling shell features.
 * Usage:
 *   unsetopt              - List all disabled options
 *   unsetopt <opt> [...]  - Disable one or more options
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_unsetopt(int argc, char **argv) {
    // No option specified - list disabled options
    if (argc < 2) {
        printf("Disabled options:\n");
        for (int i = 0; i < (int)FEATURE_COUNT; i++) {
            shell_feature_t feature = (shell_feature_t)i;
            if (!shell_mode_allows(feature)) {
                printf("  %s\n", shell_feature_name(feature));
            }
        }
        return 0;
    }

    // Process each option
    for (int i = 1; i < argc; i++) {
        // Skip flags
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0) {
                continue;
            }
            executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                                  builtin_get_source_location(),
                                  "invalid option: %s", argv[i]);
            return 1;
        }

        shell_feature_t feature;
        bool invert = false;

        if (!shell_feature_parse(argv[i], &feature, &invert)) {
            // Zsh-compat names: silently accepted no-op (see bin_setopt.c).
            if (shell_feature_is_noop_alias(argv[i])) {
                shell_feature_record_noop_alias_state(argv[i], false);
                continue;
            }
            executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                                  builtin_get_source_location(),
                                  "unknown option: %s", argv[i]);
            return 1;
        }

        /* Disable from the alias's perspective; flip on the underlying
         * feature when the alias is inverted. */
        bool target_value = invert;
        if (target_value) {
            shell_feature_enable(feature);
        } else {
            shell_feature_disable(feature);
        }

        // Sync to registry if initialized
        if (config_registry_is_initialized()) {
            char key[CREG_KEY_MAX];
            snprintf(key, sizeof(key), "shell.features.%s",
                     shell_feature_name(feature));
            config_registry_set_boolean(key, target_value);
        }
    }

    return 0;
}
