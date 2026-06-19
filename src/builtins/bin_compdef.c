/**
 * @file bin_compdef.c
 * @brief `compdef` builtin -- bind a completion function to a command.
 *
 * Stores command-name -> function-name bindings in a static hash
 * table. Resolution to the function value happens at completion time
 * via the LLE completion source; compdef itself only records the
 * binding by name, so functions may be defined later (autoload-style).
 *
 * Forms:
 *   compdef FN CMD [CMD2 ...]   bind one function to one or more commands
 *   compdef -d CMD              remove a binding
 *   compdef                     list all bindings
 *
 * Aliasing forms zsh accepts (`compdef _git git=git-foo`) are not
 * supported here. If they prove necessary for real-world scripts,
 * add a parallel handling pass.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "compdef.h"

#include "builtins.h"
#include "executor.h"
#include "ht.h"
#include "shell_error.h"
#include "shell_mode.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static ht_strstr_t *compdef_bindings = NULL;

void init_compdef_bindings(void) {
    if (compdef_bindings == NULL) {
        compdef_bindings =
            ht_strstr_create(&(ht_str_options_t){.case_insensitive = true});
    }
}

void free_compdef_bindings(void) {
    if (compdef_bindings) {
        ht_strstr_destroy(compdef_bindings);
        compdef_bindings = NULL;
    }
}

const char *compdef_lookup(const char *cmd) {
    if (!compdef_bindings || !cmd) {
        return NULL;
    }
    return ht_strstr_get(compdef_bindings, cmd);
}

bool compdef_set(const char *cmd, const char *fn) {
    if (!compdef_bindings || !cmd || !fn || cmd[0] == '\0' || fn[0] == '\0') {
        return false;
    }
    ht_strstr_insert(compdef_bindings, cmd, fn);
    return ht_strstr_get(compdef_bindings, cmd) != NULL;
}

bool compdef_unset(const char *cmd) {
    if (!compdef_bindings || !cmd) {
        return false;
    }
    if (ht_strstr_get(compdef_bindings, cmd) == NULL) {
        return false;
    }
    ht_strstr_remove(compdef_bindings, cmd);
    return true;
}

void compdef_enum(compdef_enum_fn cb, void *user_data) {
    if (!compdef_bindings || !cb) {
        return;
    }
    ht_enum_t *e = ht_strstr_enum_create(compdef_bindings);
    if (!e) {
        return;
    }
    const char *cmd = NULL;
    const char *fn = NULL;
    while (ht_strstr_enum_next(e, &cmd, &fn)) {
        if (!cb(cmd, fn, user_data)) {
            break;
        }
    }
    ht_strstr_enum_destroy(e);
}

size_t compdef_count(void) {
    if (!compdef_bindings) {
        return 0;
    }
    size_t n = 0;
    ht_enum_t *e = ht_strstr_enum_create(compdef_bindings);
    if (!e) {
        return 0;
    }
    const char *k = NULL;
    const char *v = NULL;
    while (ht_strstr_enum_next(e, &k, &v)) {
        n++;
    }
    ht_strstr_enum_destroy(e);
    return n;
}

static bool list_one(const char *cmd, const char *fn, void *user_data) {
    (void)user_data;
    printf("compdef %s %s\n", fn, cmd);
    return true;
}

static void report_missing_command(void) {
    if (current_executor) {
        executor_error_report(current_executor, SHELL_ERR_MISSING_ARGUMENT,
                              builtin_get_source_location(),
                              "compdef: -d requires a command name");
    } else {
        fprintf(stderr, "lush: compdef: -d requires a command name\n");
    }
}

static void report_invalid_option(const char *opt) {
    if (current_executor) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                              builtin_get_source_location(),
                              "compdef: invalid option: %s", opt);
    } else {
        fprintf(stderr, "lush: compdef: invalid option: %s\n", opt);
    }
}

static void report_usage(void) {
    if (current_executor) {
        executor_error_report(
            current_executor, SHELL_ERR_MISSING_ARGUMENT,
            builtin_get_source_location(),
            "compdef: usage: compdef FN CMD [CMD2 ...] | compdef -d CMD | "
            "compdef");
    } else {
        fprintf(stderr, "lush: compdef: usage: compdef FN CMD [CMD2 ...] | "
                        "compdef -d CMD | compdef\n");
    }
}

int bin_compdef(int argc, char **argv) {
    if (!shell_mode_allows(FEATURE_COMPLETION_DSL)) {
        shell_error_t *err = shell_error_create(
            SHELL_ERR_COMMAND_NOT_FOUND, SHELL_SEVERITY_ERROR,
            builtin_get_source_location(), "compdef: command not found");
        shell_error_display(err, stderr, isatty(STDERR_FILENO));
        shell_error_free(err);
        return 127;
    }

    if (!compdef_bindings) {
        init_compdef_bindings();
    }

    if (argc == 1) {
        compdef_enum(list_one, NULL);
        return 0;
    }

    if (strcmp(argv[1], "-d") == 0) {
        if (argc < 3) {
            report_missing_command();
            return 1;
        }
        int rc = 0;
        for (int i = 2; i < argc; i++) {
            if (!compdef_unset(argv[i])) {
                rc = 1;
            }
        }
        return rc;
    }

    if (argv[1][0] == '-' && argv[1][1] != '\0') {
        report_invalid_option(argv[1]);
        return 1;
    }

    if (argc < 3) {
        report_usage();
        return 1;
    }

    const char *fn = argv[1];
    int rc = 0;
    for (int i = 2; i < argc; i++) {
        if (!compdef_set(argv[i], fn)) {
            rc = 1;
        }
    }
    return rc;
}
