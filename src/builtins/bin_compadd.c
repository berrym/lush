/**
 * @file bin_compadd.c
 * @brief `compadd` builtin -- emit completion candidates from inside a
 *        compdef-bound function.
 *
 * compadd appends candidates (and optional descriptions) to the
 * completion-result accumulator that the LLE completion source set up
 * before invoking the user function. Outside of that context the
 * builtin errors with rc=1.
 *
 * Flags:
 *   -d <desc>     Description applied to every positional candidate in
 *                 this call. Does not interact with -a / -k (those source
 *                 their own per-item descriptions from array values).
 *   -a <name>     Indexed array `name`: each element value becomes a
 *                 candidate (no description).
 *   -k <name>     Associative array `name`: each key becomes a candidate
 *                 with its corresponding value as the description. This
 *                 is the lush-additive extension over zsh's `-k`
 *                 (zsh emits keys only); the additive form preserves
 *                 the common zsh practice of building descriptions into
 *                 the assoc map's values.
 *   --            End of options.
 *
 * Map-in-command-position (`compadd %mapvar`) is intentionally not
 * supported here: the language-level question of what %map means in
 * argv position is tracked in #222. When that decision lands compadd
 * gets the additional syntactic form for free in lush mode.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "completion_bridge.h"
#include "executor.h"
#include "shell_error.h"
#include "shell_mode.h"
#include "symtable.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int emit_one(const char *candidate, const char *description) {
    if (!current_executor) {
        return -1;
    }
    return completion_bridge_add(current_executor, candidate, description);
}

static int emit_array_values(const char *array_name) {
    array_value_t *arr = symtable_get_array(array_name);
    /// Missing or non-array name is silently treated as zero candidates,
    /// matching the dynamic-completion shape: the bound function should
    /// not abort just because its data source is empty / absent.
    if (!arr) {
        return 0;
    }
    size_t count = 0;
    char **values = symtable_array_get_values(arr, &count);
    if (!values) {
        return 0;
    }
    int rc = 0;
    for (size_t i = 0; i < count; i++) {
        if (values[i] && values[i][0] != '\0') {
            if (emit_one(values[i], NULL) != 0) {
                rc = -1;
            }
        }
        free(values[i]);
    }
    free(values);
    return rc;
}

static int emit_assoc_pairs(const char *array_name) {
    array_value_t *arr = symtable_get_array(array_name);
    if (!arr || !arr->is_associative) {
        return 0;
    }
    size_t count = 0;
    char **keys = symtable_array_get_keys(arr, &count);
    if (!keys) {
        return 0;
    }
    int rc = 0;
    for (size_t i = 0; i < count; i++) {
        const char *desc = symtable_array_get_assoc(arr, keys[i]);
        if (keys[i] && keys[i][0] != '\0') {
            if (emit_one(keys[i], desc) != 0) {
                rc = -1;
            }
        }
        free(keys[i]);
    }
    free(keys);
    return rc;
}

static void report_outside_context(void) {
    source_location_t loc = builtin_get_source_location();
    shell_error_t *err = shell_error_create(
        SHELL_ERR_COMPADD_OUTSIDE_CONTEXT, SHELL_SEVERITY_ERROR, loc,
        "compadd: only valid inside a compdef-bound completion function");
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
            for (size_t i = 0; i < current_executor->context_depth &&
                               i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (current_executor->context_stack[i]) {
                    shell_error_push_context(
                        err, "%s", current_executor->context_stack[i]);
                }
            }
        }
        shell_error_set_suggestion(
            err, "register the function with `compdef FN CMD` and let the "
                 "completion engine invoke it on TAB; compadd has no effect "
                 "outside that path");
        shell_error_display(err, stderr, isatty(STDERR_FILENO));
        shell_error_free(err);
    } else {
        fprintf(stderr, "lush: compadd: only valid inside a compdef-bound "
                        "completion function\n");
    }
}

static void report_invalid_option(const char *opt) {
    if (current_executor) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                              builtin_get_source_location(),
                              "compadd: invalid option: %s", opt);
    } else {
        fprintf(stderr, "lush: compadd: invalid option: %s\n", opt);
    }
}

static void report_missing_arg(const char *opt) {
    if (current_executor) {
        executor_error_report(current_executor, SHELL_ERR_MISSING_ARGUMENT,
                              builtin_get_source_location(),
                              "compadd: option %s requires an argument", opt);
    } else {
        fprintf(stderr, "lush: compadd: option %s requires an argument\n", opt);
    }
}

int bin_compadd(int argc, char **argv) {
    if (!shell_mode_allows(FEATURE_COMPLETION_DSL)) {
        shell_error_t *err = shell_error_create(
            SHELL_ERR_COMMAND_NOT_FOUND, SHELL_SEVERITY_ERROR,
            builtin_get_source_location(), "compadd: command not found");
        shell_error_display(err, stderr, isatty(STDERR_FILENO));
        shell_error_free(err);
        return 127;
    }

    if (!completion_bridge_active(current_executor)) {
        report_outside_context();
        return 1;
    }

    const char *uniform_desc = NULL;
    int rc = 0;
    int i = 1;

    while (i < argc) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') {
            break;
        }
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (strcmp(a, "-d") == 0) {
            if (i + 1 >= argc) {
                report_missing_arg("-d");
                return 1;
            }
            uniform_desc = argv[++i];
            i++;
            continue;
        }
        if (strcmp(a, "-a") == 0) {
            if (i + 1 >= argc) {
                report_missing_arg("-a");
                return 1;
            }
            if (emit_array_values(argv[++i]) != 0) {
                rc = 1;
            }
            i++;
            continue;
        }
        if (strcmp(a, "-k") == 0) {
            if (i + 1 >= argc) {
                report_missing_arg("-k");
                return 1;
            }
            if (emit_assoc_pairs(argv[++i]) != 0) {
                rc = 1;
            }
            i++;
            continue;
        }
        report_invalid_option(a);
        return 1;
    }

    for (; i < argc; i++) {
        if (argv[i] && argv[i][0] != '\0') {
            if (emit_one(argv[i], uniform_desc) != 0) {
                rc = 1;
            }
        }
    }

    return rc;
}
