/**
 * @file bin_local.c
 * @brief `local` builtin -- declare function-local variables
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "shell_mode.h"
#include "symtable.h"

#include <ctype.h>

/**
 * @brief Declare local variables within function scope
 *
 * Creates variables that are local to the current function.
 * Can only be used inside a function. Supports assignment syntax
 * (local var=value) or declaration only (local var).
 *
 * @param argc Argument count
 * @param argv Argument vector with variable declarations
 * @return 0 on success, 1 on error or if not in a function
 */
int bin_local(int argc, char **argv) {
    if (argc == 1) {
        /// No arguments - just return success (bash behavior)
        return 0;
    }

    /// Get the current symbol table manager
    symtable_manager_t *manager = symtable_get_global_manager();
    if (!manager) {
        executor_error_report(current_executor, SHELL_ERR_STATE_CORRUPTION,
                              builtin_get_source_location(),
                              "symbol table not available");
        return 1;
    }

    /// Zsh's local is a thin alias for typeset/declare with the full
    /// option grammar (-a, -A, -i, -r, -x, ...) and accepts top-level
    /// calls (treats them as declaring a global). Delegate to bin_declare
    /// for the whole package when running under zsh mode. Bash and POSIX
    /// modes keep the strict bin_local semantics: top-level call errors,
    /// and the only recognized option is -n (nameref).
    if (shell_mode_get() == SHELL_MODE_ZSH) {
        return bin_declare(argc, argv);
    }

    /// Check if we're in a function scope.
    size_t current_level = symtable_current_level(manager);
    if (current_level == 0) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_FUNCTION_ERROR, SHELL_SEVERITY_ERROR,
                               loc, "can only be used in a function");
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
                err, "local is only valid inside a function body");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: local: can only be used in a function\n");
        }
        return 1;
    }

    /// Parse options
    bool opt_nameref = false;
    int opt_idx = 1;

    while (opt_idx < argc && argv[opt_idx][0] == '-') {
        const char *opt = argv[opt_idx];

        /// Handle -- to stop option processing
        if (strcmp(opt, "--") == 0) {
            opt_idx++;
            break;
        }

        /// Process each character in the option string
        for (int i = 1; opt[i]; i++) {
            switch (opt[i]) {
            case 'n':
                opt_nameref = true;
                break;
            default:
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_OPTION,
                                      builtin_get_source_location(),
                                      "-%c: invalid option", opt[i]);
                return 2;
            }
        }
        opt_idx++;
    }

    /// Process each argument
    for (int i = opt_idx; i < argc; i++) {
        char *arg = argv[i];
        /// Parser-internal sentinel: an arg whose first byte is \x1F
        /// came from the unquoted `name=(...)` array-literal form
        /// (see src/parser.c). Strip the sentinel and remember we are
        /// looking at an array-literal assignment; without the
        /// sentinel, the same value shape (after quote-stripping)
        /// could equally be a scalar `local data="(...)"`.
        bool is_array_literal = false;
        if (arg[0] == '\x1F') {
            arg++;
            is_array_literal = true;
        }
        char *eq = strchr(arg, '=');

        if (eq) {
            /// Assignment: local var=value or local -n ref=target
            size_t name_len = eq - arg;
            char *name = malloc(name_len + 1);
            if (!name) {
                executor_error_report(current_executor, SHELL_ERR_OUT_OF_MEMORY,
                                      builtin_get_source_location(),
                                      "memory allocation failed");
                return 1;
            }

            strncpy(name, arg, name_len);
            name[name_len] = '\0';

            /// Validate variable name via the canonical, Unicode-aware
            /// is_valid_identifier (matches the bin_declare/export/readonly
            /// pattern; honors FEATURE_UNICODE_IDENTIFIERS).
            if (!is_valid_identifier(name)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "invalid variable name: %s", name);
                free(name);
                return 1;
            }

            char *value = eq + 1;

            if (opt_nameref) {
                /// Create local nameref: local -n ref=target.
                /// Locality is determined by the scope this write
                /// targets (the function's current scope), not by a
                /// flag bit; the prior SYMVAR_LOCAL is gone.
                symvar_flags_t flags = SYMVAR_NAMEREF_FLAG;
                if (symtable_set_nameref(manager, name, value, flags) != 0) {
                    executor_error_report(current_executor,
                                          SHELL_ERR_SCOPE_ERROR,
                                          builtin_get_source_location(),
                                          "failed to create nameref");
                    free(name);
                    return 1;
                }
            } else if (is_array_literal && value && value[0] == '(') {
                /// Array literal: local arr=(a b c). The parser
                /// recognized the unquoted (...) form and prefixed the
                /// argv with the sentinel above. Build the array
                /// element-by-element, matching bin_declare's existing
                /// -a parser.
                array_value_t *arr = symtable_array_create(false);
                if (!arr) {
                    executor_error_report(current_executor,
                                          SHELL_ERR_SCOPE_ERROR,
                                          builtin_get_source_location(),
                                          "failed to create array");
                    free(name);
                    return 1;
                }
                const char *p = value + 1;
                int idx = 0;
                while (*p && *p != ')') {
                    while (*p && isspace(*p)) {
                        p++;
                    }
                    if (*p == ')' || !*p) {
                        break;
                    }
                    const char *elem_start = p;
                    bool in_quote = false;
                    char quote_char = 0;
                    while (*p && (in_quote || (!isspace(*p) && *p != ')'))) {
                        if (!in_quote && (*p == '"' || *p == '\'')) {
                            in_quote = true;
                            quote_char = *p;
                        } else if (in_quote && *p == quote_char) {
                            in_quote = false;
                        }
                        p++;
                    }
                    size_t elem_len = (size_t)(p - elem_start);
                    if (elem_len > 0) {
                        char *elem = malloc(elem_len + 1);
                        if (elem) {
                            const char *src = elem_start;
                            size_t copy_len = elem_len;
                            if (copy_len >= 2 &&
                                (src[0] == '"' || src[0] == '\'') &&
                                src[copy_len - 1] == src[0]) {
                                src++;
                                copy_len -= 2;
                            }
                            memcpy(elem, src, copy_len);
                            elem[copy_len] = '\0';
                            symtable_array_set_index(arr, idx++, elem);
                            free(elem);
                        }
                    }
                }
                if (symtable_set_array(name, arr) != 0) {
                    executor_error_report(
                        current_executor, SHELL_ERR_SCOPE_ERROR,
                        builtin_get_source_location(), "failed to store array");
                    symtable_array_free(arr);
                    free(name);
                    return 1;
                }
            } else {
                /// Set the local variable
                if (symtable_set_local_var(manager, name, value) != 0) {
                    executor_error_report(current_executor,
                                          SHELL_ERR_SCOPE_ERROR,
                                          builtin_get_source_location(),
                                          "failed to set variable");
                    free(name);
                    return 1;
                }
            }

            free(name);
        } else {
            /// Declaration only: local var
            /// Validate variable name via the canonical, Unicode-aware
            /// is_valid_identifier (matches the bin_declare/export/readonly
            /// pattern; honors FEATURE_UNICODE_IDENTIFIERS).
            if (!is_valid_identifier(arg)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "invalid variable name: %s", arg);
                return 1;
            }

            if (opt_nameref) {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                    "-n requires assignment");
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
                        for (size_t i = 0;
                             i < current_executor->context_depth &&
                             i < SHELL_ERROR_CONTEXT_MAX;
                             i++) {
                            if (current_executor->context_stack[i]) {
                                shell_error_push_context(
                                    err, "%s",
                                    current_executor->context_stack[i]);
                            }
                        }
                    }
                    shell_error_set_suggestion(
                        err, "use 'local -n ref=target' to bind a nameref");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr, "lush: local: -n requires assignment "
                                    "(local -n ref=target)\n");
                }
                return 1;
            }

            /// Declare the local variable with empty value
            if (symtable_set_local_var(manager, arg, "") != 0) {
                executor_error_report(current_executor, SHELL_ERR_SCOPE_ERROR,
                                      builtin_get_source_location(),
                                      "failed to declare variable");
                return 1;
            }
        }
    }

    return 0;
}
