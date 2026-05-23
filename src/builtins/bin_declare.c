/**
 * @file bin_declare.c
 * @brief `declare` / `typeset` builtin -- declare variables with attributes
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "arithmetic.h"
#include "builtins.h"
#include "shell_mode.h"
#include "symtable.h"

#include <ctype.h>

static void declare_print_var_callback(const char *key, const char *value,
                                       void *userdata) {
    (void)userdata;
    if (!key)
        return;
    // Skip internal variables starting with __
    if (key[0] == '_' && key[1] == '_')
        return;
    // Skip if this is actually an array (handled separately)
    if (symtable_is_array(key))
        return;
    printf("declare -- %s=\"%s\"\n", key, value ? value : "");
}

static void declare_print_array_callback(const char *name, array_value_t *array,
                                         void *userdata) {
    (void)userdata;
    if (!name || !array)
        return;
    if (array->is_associative) {
        printf("declare -A %s=(", name);
        /* Print associative array elements. Iteration order matches
         * the rest of the shell (issue #69): zsh / lush mode use
         * insertion order for predictable output; bash / POSIX use
         * hashtable bucket order. The insertion-order list lives on
         * the array itself; we look up each value via ht_strstr_get. */
        if (array->assoc_map) {
            shell_mode_t mode = shell_mode_get();
            bool use_insertion_order =
                (mode == SHELL_MODE_ZSH || mode == SHELL_MODE_LUSH) &&
                array->assoc_insertion_order &&
                array->assoc_insertion_count == array->count;

            if (use_insertion_order) {
                bool first = true;
                for (size_t i = 0; i < array->assoc_insertion_count; i++) {
                    const char *k = array->assoc_insertion_order[i];
                    const char *v = ht_strstr_get(array->assoc_map, k);
                    printf("%s[%s]=\"%s\"", first ? "" : " ", k, v ? v : "");
                    first = false;
                }
            } else {
                ht_enum_t *e = ht_strstr_enum_create(array->assoc_map);
                if (e) {
                    const char *k, *v;
                    bool first = true;
                    while (ht_strstr_enum_next(e, &k, &v)) {
                        printf("%s[%s]=\"%s\"", first ? "" : " ", k,
                               v ? v : "");
                        first = false;
                    }
                    ht_strstr_enum_destroy(e);
                }
            }
        }
        printf(")\n");
    } else {
        printf("declare -a %s=(", name);
        // Print indexed array elements
        bool first = true;
        for (size_t i = 0; i < array->count; i++) {
            if (array->elements[i]) {
                int idx = array->indices ? array->indices[i] : (int)i;
                printf("%s[%d]=\"%s\"", first ? "" : " ", idx,
                       array->elements[i]);
                first = false;
            }
        }
        printf(")\n");
    }
}

/**
 * @brief Declare variables with attributes
 *
 * Bash/Zsh-compatible declare builtin for declaring variables with
 * special attributes like indexed arrays, associative arrays, or integers.
 *
 * Options:
 *   -a  Declare indexed array
 *   -A  Declare associative array
 *   -i  Declare integer variable (auto-evaluates arithmetic)
 *   -r  Declare readonly variable
 *   -x  Export variable to environment
 *   -p  Print variable attributes and values
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_declare(int argc, char **argv) {
    bool opt_indexed_array = false;
    bool opt_assoc_array = false;
    bool opt_integer = false;
    bool opt_readonly = false;
    bool opt_export = false;
    bool opt_print = false;
    bool opt_nameref = false;
    bool opt_global = false;
    bool opt_lowercase = false;
    bool opt_uppercase = false;
    bool opt_trace = false;

    int opt_idx = 1;

    // Parse options
    while (opt_idx < argc && argv[opt_idx][0] == '-') {
        const char *opt = argv[opt_idx];

        // Handle -- to stop option processing
        if (strcmp(opt, "--") == 0) {
            opt_idx++;
            break;
        }

        // Process each character in the option string
        for (int i = 1; opt[i]; i++) {
            switch (opt[i]) {
            case 'a':
                opt_indexed_array = true;
                break;
            case 'A':
                opt_assoc_array = true;
                break;
            case 'i':
                opt_integer = true;
                break;
            case 'r':
                opt_readonly = true;
                break;
            case 'x':
                opt_export = true;
                break;
            case 'p':
                opt_print = true;
                break;
            case 'n':
                opt_nameref = true;
                break;
            case 'g':
                opt_global = true;
                break;
            case 'l':
                opt_lowercase = true;
                break;
            case 'u':
                opt_uppercase = true;
                break;
            case 't':
                opt_trace = true;
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

    // Can't have both -l and -u
    if (opt_lowercase && opt_uppercase) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_ERROR,
                               loc, "cannot use -l and -u simultaneously");
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
                err, "choose one of -l (lowercase) or -u (uppercase)");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: declare: cannot use -l and -u simultaneously\n");
        }
        return 1;
    }

    // Can't have both -a and -A
    if (opt_indexed_array && opt_assoc_array) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_ERROR,
                               loc, "cannot use -a and -A simultaneously");
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
                err, "choose one of -a (indexed) or -A (associative)");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: declare: cannot use -a and -A simultaneously\n");
        }
        return 1;
    }

    // If no variable names provided and -p not specified, just return success
    if (opt_idx >= argc && !opt_print) {
        return 0;
    }

    // Handle -p (print) option with no arguments - list all variables
    if (opt_print && opt_idx >= argc) {
        // Print all arrays first
        symtable_enumerate_arrays(declare_print_array_callback, NULL);
        // Then print all scalar variables
        symtable_enumerate_global_vars(declare_print_var_callback, NULL);
        return 0;
    }

    // Process each variable argument
    for (int i = opt_idx; i < argc; i++) {
        char *arg = argv[i];
        char *eq = strchr(arg, '=');
        char *name = NULL;
        char *value = NULL;

        if (eq) {
            // Assignment: declare var=value or declare -a arr=(...)
            size_t name_len = eq - arg;
            name = malloc(name_len + 1);
            if (!name) {
                executor_error_report(current_executor, SHELL_ERR_OUT_OF_MEMORY,
                                      builtin_get_source_location(),
                                      "memory allocation failed");
                return 1;
            }
            strncpy(name, arg, name_len);
            name[name_len] = '\0';
            value = eq + 1;
        } else {
            // Declaration only: declare var
            name = strdup(arg);
            if (!name) {
                executor_error_report(current_executor, SHELL_ERR_OUT_OF_MEMORY,
                                      builtin_get_source_location(),
                                      "memory allocation failed");
                return 1;
            }
            value = NULL;
        }

        // Validate variable name
        if (!name[0] || (!isalpha(name[0]) && name[0] != '_')) {
            executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                                  builtin_get_source_location(),
                                  "`%s': not a valid identifier", name);
            free(name);
            return 1;
        }
        for (size_t j = 1; name[j]; j++) {
            if (!isalnum(name[j]) && name[j] != '_') {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "`%s': not a valid identifier", name);
                free(name);
                return 1;
            }
        }

        // Handle -p for specific variable
        if (opt_print) {
            symtable_manager_t *manager = symtable_get_global_manager();
            // Check if it's an array
            array_value_t *arr = symtable_get_array(name);
            if (arr) {
                if (arr->is_associative) {
                    printf("declare -A %s\n", name);
                } else {
                    printf("declare -a %s\n", name);
                }
            } else if (manager) {
                char *var_value = symtable_get(manager, name);
                if (var_value) {
                    printf("declare -- %s=\"%s\"\n", name, var_value);
                } else {
                    executor_error_report(
                        current_executor, SHELL_ERR_INVALID_ARGUMENT,
                        builtin_get_source_location(), "%s: not found", name);
                }
            } else {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "%s: not found", name);
            }
            free(name);
            continue;
        }

        // Handle array declarations
        if (opt_indexed_array || opt_assoc_array) {
            array_value_t *arr = symtable_array_create(opt_assoc_array);
            if (!arr) {
                executor_error_report(current_executor, SHELL_ERR_SCOPE_ERROR,
                                      builtin_get_source_location(),
                                      "failed to create array");
                free(name);
                return 1;
            }

            // If value is provided and starts with (, parse as array literal
            if (value && value[0] == '(') {
                // Parse array literal (elem1 elem2 ...)
                const char *p = value + 1;
                int idx = 0;

                while (*p && *p != ')') {
                    // Skip whitespace
                    while (*p && isspace(*p))
                        p++;
                    if (*p == ')' || !*p)
                        break;

                    // Find end of element
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

                    size_t elem_len = p - elem_start;
                    if (elem_len > 0) {
                        char *elem = malloc(elem_len + 1);
                        if (elem) {
                            strncpy(elem, elem_start, elem_len);
                            elem[elem_len] = '\0';

                            // Check for [n]=value syntax
                            if (elem[0] == '[') {
                                char *bracket_end = strchr(elem, ']');
                                if (bracket_end && bracket_end[1] == '=') {
                                    *bracket_end = '\0';
                                    const char *idx_str = elem + 1;
                                    const char *elem_val = bracket_end + 2;

                                    if (opt_assoc_array) {
                                        symtable_array_set_assoc(arr, idx_str,
                                                                 elem_val);
                                    } else {
                                        int parsed_idx = atoi(idx_str);
                                        symtable_array_set_index(
                                            arr, parsed_idx, elem_val);
                                    }
                                }
                            } else {
                                // Regular element
                                symtable_array_set_index(arr, idx++, elem);
                            }
                            free(elem);
                        }
                    }
                }
            }

            if (symtable_set_array(name, arr) != 0) {
                executor_error_report(current_executor, SHELL_ERR_SCOPE_ERROR,
                                      builtin_get_source_location(),
                                      "failed to store array");
                symtable_array_free(arr);
                free(name);
                return 1;
            }
        }
        // Handle integer declaration
        else if (opt_integer) {
            symtable_manager_t *manager = symtable_get_global_manager();
            if (!manager) {
                executor_error_report(current_executor,
                                      SHELL_ERR_STATE_CORRUPTION,
                                      builtin_get_source_location(),
                                      "symbol table not available");
                free(name);
                return 1;
            }
            // For integer variables, evaluate value as arithmetic
            if (value) {
                char *result = arithm_expand(value);
                if (result) {
                    symtable_set(manager, name, result);
                    free(result);
                } else {
                    // If arithmetic eval fails, set to 0
                    symtable_set(manager, name, "0");
                }
            } else {
                symtable_set(manager, name, "0");
            }
            /* Mark the variable as integer-typed so subsequent
             * assignments arith-evaluate their RHS (issue #102).
             * Without this, `declare -i n=5; n=n+10` would only
             * apply arithmetic to the initial 5; the n=n+10 line
             * would store the literal string "n+10". */
            symvar_flags_t flags = symtable_get_flags(manager, name);
            symtable_set_flags(manager, name, flags | SYMVAR_INTEGER_ATTR);
        }
        // Handle nameref declaration (-n)
        else if (opt_nameref) {
            symtable_manager_t *manager = symtable_get_global_manager();
            if (!manager) {
                executor_error_report(current_executor,
                                      SHELL_ERR_STATE_CORRUPTION,
                                      builtin_get_source_location(),
                                      "symbol table not available");
                free(name);
                return 1;
            }
            if (!value) {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                    "-n requires a target variable");
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
                        err, "use 'declare -n ref=target' to bind a nameref");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr,
                            "lush: declare: -n requires a target variable\n");
                }
                free(name);
                return 1;
            }
            symvar_flags_t flags = SYMVAR_NAMEREF_FLAG;
            if (!opt_global) {
                flags |= SYMVAR_LOCAL;
            }
            if (symtable_set_nameref(manager, name, value, flags) != 0) {
                executor_error_report(current_executor, SHELL_ERR_SCOPE_ERROR,
                                      builtin_get_source_location(),
                                      "failed to create nameref");
                free(name);
                return 1;
            }
        }
        // Regular variable declaration
        else {
            symtable_manager_t *manager = symtable_get_global_manager();
            if (!manager) {
                executor_error_report(current_executor,
                                      SHELL_ERR_STATE_CORRUPTION,
                                      builtin_get_source_location(),
                                      "symbol table not available");
                free(name);
                return 1;
            }

            // Apply case transformations if requested
            char *final_value = NULL;
            if (value) {
                if (opt_lowercase) {
                    final_value = strdup(value);
                    if (final_value) {
                        for (char *p = final_value; *p; p++) {
                            *p = tolower((unsigned char)*p);
                        }
                    }
                } else if (opt_uppercase) {
                    final_value = strdup(value);
                    if (final_value) {
                        for (char *p = final_value; *p; p++) {
                            *p = toupper((unsigned char)*p);
                        }
                    }
                } else {
                    final_value = strdup(value);
                }
            }

            // Build flags
            symvar_flags_t flags = SYMVAR_NONE;
            if (opt_readonly) {
                flags |= SYMVAR_READONLY;
            }
            if (opt_export) {
                flags |= SYMVAR_EXPORTED;
            }
            if (opt_lowercase) {
                flags |= SYMVAR_LOWERCASE;
            }
            if (opt_uppercase) {
                flags |= SYMVAR_UPPERCASE;
            }
            if (opt_trace) {
                flags |= SYMVAR_TRACE;
            }
            if (!opt_global) {
                flags |= SYMVAR_LOCAL;
            }

            if (final_value) {
                if (opt_global) {
                    // Use global scope for -g flag
                    symtable_set_global_var(manager, name, final_value);
                } else {
                    symtable_set_var(manager, name, final_value, flags);
                }
                free(final_value);
            } else {
                // Just declare without value
                if (opt_global) {
                    symtable_set_global_var(manager, name, "");
                } else {
                    symtable_set_var(manager, name, "", flags);
                }
            }
        }

        // Handle export (also set in environment)
        if (opt_export) {
            symtable_manager_t *manager = symtable_get_global_manager();
            if (manager) {
                char *var_value = symtable_get(manager, name);
                if (var_value) {
                    setenv(name, var_value, 1);
                }
            }
        }

        free(name);
    }

    return 0;
}
