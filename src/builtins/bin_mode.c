/**
 * @file bin_mode.c
 * @brief `mode` builtin -- select active shell mode preset
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"
#include "shell_mode.h"

/**
 * @brief `mode` builtin -- select the active shell mode preset
 *
 * Identity selector for lush's mutually-exclusive mode presets. Lush
 * does not encode mode as a `set -o` boolean; modes are a discriminated
 * enum and the `mode` builtin is the canonical way to switch between
 * them.
 *
 * Usage:
 *   mode                  -- print current mode + available modes
 *   mode <name>           -- switch to <name> (lush|posix|bash|zsh)
 *   mode --reset          -- re-apply the current mode (drops overrides)
 *   mode --show           -- verbose: current mode + seeded summary
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_mode(int argc, char **argv) {
    if (argc < 2) {
        shell_mode_t cur = shell_mode_get();
        printf("Current mode: %s\n\n", shell_mode_name(cur));
        printf("Available modes:\n");
        printf("  lush   - Curated lush-native preset (default)\n");
        printf("  posix  - Strict POSIX sh compliance\n");
        printf("  bash   - Bash 5.x compatibility preset\n");
        printf("  zsh    - Zsh compatibility preset\n\n");
        printf("Usage: mode <lush|posix|bash|zsh>\n");
        printf("       mode --reset    re-apply current preset\n");
        printf("       mode --show     verbose status\n");
        return 0;
    }

    const char *arg = argv[1];

    if (strcmp(arg, "--show") == 0) {
        shell_mode_t cur = shell_mode_get();
        printf("Active mode: %s\n", shell_mode_name(cur));
        printf("Strict mode lock: %s\n", shell_mode_is_strict() ? "on" : "off");
        printf("posix_mode bookkeeping: %s\n",
               shell_opts.posix_mode ? "on" : "off");

        size_t override_count = 0;
        for (int i = 0; i < (int)FEATURE_COUNT; i++) {
            if (shell_feature_is_overridden((shell_feature_t)i)) {
                override_count++;
            }
        }
        printf("Per-feature overrides active: %zu\n", override_count);
        if (override_count > 0) {
            printf("  (use `mode --reset` to drop overrides and re-seed)\n");
        }
        return 0;
    }

    if (strcmp(arg, "--reset") == 0) {
        shell_mode_t cur = shell_mode_get();
        if (!apply_mode_preset(cur)) {
            executor_error_report(
                current_executor, SHELL_ERR_FEATURE_DISABLED,
                builtin_get_source_location(),
                "cannot re-seed mode preset (strict mode enabled)");
            return 1;
        }
        printf("Re-seeded preset for mode: %s\n", shell_mode_name(cur));
        return 0;
    }

    if (arg[0] == '-') {
        executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                              builtin_get_source_location(),
                              "mode: unknown option '%s'", arg);
        return 1;
    }

    shell_mode_t target;
    if (!shell_mode_parse(arg, &target)) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                              builtin_get_source_location(),
                              "mode: unknown mode name '%s' "
                              "(expected lush|posix|bash|zsh)",
                              arg);
        return 1;
    }

    if (argc > 2) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                              builtin_get_source_location(),
                              "mode: too many arguments");
        return 1;
    }

    if (!apply_mode_preset(target)) {
        executor_error_report(current_executor, SHELL_ERR_FEATURE_DISABLED,
                              builtin_get_source_location(),
                              "cannot change shell mode (strict mode enabled)");
        return 1;
    }

    return 0;
}
