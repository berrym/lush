/**
 * @file bin_shopt.c
 * @brief `shopt` builtin -- bash-spelling sugar over setopt
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "config_registry.h"
#include "lush.h"
#include "shell_mode.h"

/**
 * @brief Bash-style shopt builtin for shell options
 *
 * Provides bash-compatible syntax for managing shell options.
 * Usage:
 *   shopt                  - List all shopt options with current state
 *   shopt -p               - Print in re-usable format (shopt -s/-u)
 *   shopt -s <opt> [...]   - Set (enable) one or more options
 *   shopt -u <opt> [...]   - Unset (disable) one or more options
 *   shopt -q <opt>         - Query option silently (exit status only)
 *   shopt -o               - Operate on set -o options instead
 *
 * Option names accepted:
 *   - Bash names: extglob, nullglob, globstar, dotglob, etc.
 *   - Lush names: extended_glob, null_glob, etc.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error, 2 if -q and option is off
 */
int bin_shopt(int argc, char **argv) {
    bool set_mode = false;   /// -s: enable options
    bool unset_mode = false; /// -u: disable options
    bool query_mode = false; /// -q: query silently
    bool print_mode = false; /// -p: print format
    bool set_o_mode = false; /// -o: use set -o options
    int opt_end = 1;

    /// Parse flags
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            break;
        }
        if (strcmp(argv[i], "--") == 0) {
            opt_end = i + 1;
            break;
        }

        /// Process each character in the option string
        for (const char *p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 's':
                set_mode = true;
                break;
            case 'u':
                unset_mode = true;
                break;
            case 'q':
                query_mode = true;
                break;
            case 'p':
                print_mode = true;
                break;
            case 'o':
                set_o_mode = true;
                break;
            default: {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_ERROR, loc,
                    "-%c: invalid option", *p);
                if (err) {
                    if (current_executor && SOURCE_LOC_VALID(loc)) {
                        char *src_line = executor_get_source_line(
                            current_executor, loc.line);
                        if (src_line) {
                            shell_error_set_source_line(
                                err, src_line, loc.column,
                                loc.column + loc.length);
                            free(src_line);
                        }
                    }
                    if (current_executor) {
                        for (size_t k = 0;
                             k < current_executor->context_depth &&
                             k < SHELL_ERROR_CONTEXT_MAX;
                             k++) {
                            if (current_executor->context_stack[k]) {
                                shell_error_push_context(
                                    err, "%s",
                                    current_executor->context_stack[k]);
                            }
                        }
                    }
                    shell_error_set_suggestion(
                        err, "usage: shopt [-pqsu] [-o] [optname ...]");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr, "lush: shopt: -%c: invalid option\n", *p);
                }
                return 1;
            }
            }
        }
        opt_end = i + 1;
    }

    /// -s and -u are mutually exclusive
    if (set_mode && unset_mode) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err = shell_error_create(
            SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_ERROR, loc,
            "cannot set and unset options simultaneously");
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t k = 0; k < current_executor->context_depth &&
                                   k < SHELL_ERROR_CONTEXT_MAX;
                     k++) {
                    if (current_executor->context_stack[k]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[k]);
                    }
                }
            }
            shell_error_set_suggestion(
                err, "use either -s (enable) or -u (disable), not both");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: shopt: cannot set and unset options "
                            "simultaneously\n");
        }
        return 1;
    }

    /// -o mode: operate on set -o options (not implemented, just note it)
    if (set_o_mode) {
        /// For now, -o options map to the same features
        /// In bash, -o uses different option namespace, but we unify them
    }

    /// No option names given - list all options
    if (opt_end >= argc) {
        if (query_mode) {
            executor_error_report(current_executor, SHELL_ERR_MISSING_ARGUMENT,
                                  builtin_get_source_location(),
                                  "-q: option name required");
            return 1;
        }

        /// List all features
        for (int i = 0; i < (int)FEATURE_COUNT; i++) {
            shell_feature_t feature = (shell_feature_t)i;
            const char *name = shell_feature_name(feature);
            bool enabled = shell_mode_allows(feature);

            /// If -s or -u specified without names, filter by state
            if (set_mode && !enabled) {
                continue;
            }
            if (unset_mode && enabled) {
                continue;
            }

            if (print_mode) {
                printf("shopt %s %s\n", enabled ? "-s" : "-u", name);
            } else {
                printf("%-30s %s\n", name, enabled ? "on" : "off");
            }
        }
        return 0;
    }

    /// Process each option name
    int result = 0;
    for (int i = opt_end; i < argc; i++) {
        shell_feature_t feature;
        bool invert = false;

        if (!shell_feature_parse(argv[i], &feature, &invert)) {
            if (!query_mode) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_OPTION,
                                      builtin_get_source_location(),
                                      "%s: invalid shell option name", argv[i]);
            }
            result = 1;
            continue;
        }

        bool underlying = shell_mode_allows(feature);
        /// From the alias's perspective: an inverted alias is "on" when the
        /// underlying feature is off.
        bool effective = underlying ^ invert;

        if (query_mode) {
            /// -q: return status based on option state (alias perspective)
            if (!effective) {
                result = 1;
            }
        } else if (set_mode) {
            /// -s: enable the option (alias perspective). Route through the
            /// registry under the canonical shell.feature.<name> key; the
            /// shell.feature.* subscriber mirrors it into the matrix.
            bool target = !invert;
            if (config_registry_is_initialized()) {
                char key[CREG_KEY_MAX];
                snprintf(key, sizeof(key), "shell.feature.%s",
                         shell_feature_name(feature));
                config_registry_set_boolean(key, target);
            } else if (target) {
                shell_feature_enable(feature);
            } else {
                shell_feature_disable(feature);
            }
        } else if (unset_mode) {
            /// -u: disable the option (alias perspective). Route through the
            /// registry under the canonical shell.feature.<name> key; the
            /// shell.feature.* subscriber mirrors it into the matrix.
            bool target = invert;
            if (config_registry_is_initialized()) {
                char key[CREG_KEY_MAX];
                snprintf(key, sizeof(key), "shell.feature.%s",
                         shell_feature_name(feature));
                config_registry_set_boolean(key, target);
            } else if (target) {
                shell_feature_enable(feature);
            } else {
                shell_feature_disable(feature);
            }
        } else {
            /// No -s/-u: just print the option state.
            /// Print under the user-supplied alias name and its effective
            /// sense; round-tripping that through `shopt -s/-u` yields the
            /// same configuration.
            const char *display_name =
                invert ? argv[i] : shell_feature_name(feature);
            if (print_mode) {
                printf("shopt %s %s\n", effective ? "-s" : "-u", display_name);
            } else {
                printf("%-30s %s\n", display_name, effective ? "on" : "off");
            }
        }
    }

    return result;
}
