/**
 * @file bin_declare.c
 * @brief `declare` / `typeset` builtin -- declare variables with attributes
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "arithmetic.h"
#include "builtins.h"
#include "identifier.h"
#include "lle/lle_pager.h"
#include "shell_mode.h"
#include "symtable.h"

#include <ctype.h>

static void declare_print_var_callback(const char *key, const char *value,
                                       void *userdata) {
    FILE *out = (FILE *)userdata;
    if (!out || !key)
        return;
    /// Skip internal variables starting with __
    if (key[0] == '_' && key[1] == '_')
        return;
    /// Skip if this is actually an array (handled separately)
    if (symtable_is_array(key))
        return;
    fprintf(out, "declare -- %s=\"%s\"\n", key, value ? value : "");
}

static void declare_print_array_callback(const char *name, array_value_t *array,
                                         void *userdata) {
    FILE *out = (FILE *)userdata;
    if (!out || !name || !array)
        return;
    if (array->is_associative) {
        fprintf(out, "declare -A %s=(", name);
        /// Print associative array elements. Iteration order matches
        /// the rest of the shell (issue #69): zsh / lush mode use
        /// insertion order for predictable output; bash / POSIX use
        /// hashtable bucket order. The insertion-order list lives on
        /// the array itself; we look up each value via ht_strstr_get.
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
                    fprintf(out, "%s[%s]=\"%s\"", first ? "" : " ", k,
                            v ? v : "");
                    first = false;
                }
            } else {
                ht_enum_t *e = ht_strstr_enum_create(array->assoc_map);
                if (e) {
                    const char *k, *v;
                    bool first = true;
                    while (ht_strstr_enum_next(e, &k, &v)) {
                        fprintf(out, "%s[%s]=\"%s\"", first ? "" : " ", k,
                                v ? v : "");
                        first = false;
                    }
                    ht_strstr_enum_destroy(e);
                }
            }
        }
        fputs(")\n", out);
    } else {
        fprintf(out, "declare -a %s=(", name);
        /// Print indexed array elements
        bool first = true;
        for (size_t i = 0; i < array->count; i++) {
            if (array->elements[i]) {
                int idx = array->indices ? array->indices[i] : (int)i;
                fprintf(out, "%s[%d]=\"%s\"", first ? "" : " ", idx,
                        array->elements[i]);
                first = false;
            }
        }
        fputs(")\n", out);
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

    /// Parse options
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
            case 'H':
                /// zsh: hide variable from `typeset -p` listing. Pure
                /// display attribute; lush's typeset listing path
                /// doesn't enumerate variables anyway, so the no-op
                /// accept produces observably-identical behavior.
                break;
            case 'U':
                /// zsh: unique-element array attribute. Per-array
                /// dedup is not yet implemented (would need an
                /// attribute on the array storage). Accept silently
                /// for now; declaring an array with -U produces an
                /// ordinary array. Documented limitation: scripts
                /// that depend on automatic dedup will diverge when
                /// duplicates arrive.
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

    /// Can't have both -l and -u
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

    /// Can't have both -a and -A
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

    /// If no variable names provided and -p not specified, just return success
    if (opt_idx >= argc && !opt_print) {
        return 0;
    }

    /// Handle -p (print) option with no arguments - list all variables.
    /// Builds the combined arrays + scalars listing into an
    /// open_memstream buffer and hands it to lle_pager_present so
    /// large environments paginate in interactive shells. memstream
    /// allocation failure falls back to streaming directly to stdout
    /// so the listing still surfaces.
    if (opt_print && opt_idx >= argc) {
        char *buf = NULL;
        size_t buf_len = 0;
        FILE *out = open_memstream(&buf, &buf_len);
        FILE *sink = out ? out : stdout;
        /// Print all arrays first
        symtable_enumerate_arrays(declare_print_array_callback, sink);
        /// Then print all scalar variables
        symtable_enumerate_global_vars(declare_print_var_callback, sink);
        if (out) {
            fclose(out);
            lle_pager_present(NULL, buf);
            free(buf);
        }
        return 0;
    }

    /// Process each variable argument
    for (int i = opt_idx; i < argc; i++) {
        char *arg = argv[i];
        /// Parser-internal sentinel: an arg whose first byte is \x1F
        /// came from the unquoted `name=(...)` array-literal form
        /// (see src/parser.c). Strip it; this is implicitly the -a
        /// (indexed array) case unless -A is set. The same value
        /// shape from a quoted scalar `declare data="(...)"` does
        /// NOT carry the sentinel and stays a scalar.
        if (arg[0] == '\x1F') {
            arg++;
            if (!opt_indexed_array && !opt_assoc_array) {
                opt_indexed_array = true;
            }
        }
        char *eq = strchr(arg, '=');
        char *name = NULL;
        char *value = NULL;

        if (eq) {
            /// Assignment: declare var=value or declare -a arr=(...)
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
            /// Declaration only: declare var
            name = strdup(arg);
            if (!name) {
                executor_error_report(current_executor, SHELL_ERR_OUT_OF_MEMORY,
                                      builtin_get_source_location(),
                                      "memory allocation failed");
                return 1;
            }
            value = NULL;
        }

        /// Validate variable name via the central lush identifier
        /// predicate (honors FEATURE_UNICODE_IDENTIFIERS). Replaces
        /// the prior inline isalpha/isalnum byte tests so a non-ASCII
        /// identifier is accepted under lush-mode default (or any
        /// mode with the feature opt-in) and rejected under
        /// POSIX/bash/zsh defaults.
        if (!lush_is_valid_identifier(name)) {
            executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                                  builtin_get_source_location(),
                                  "`%s': not a valid identifier", name);
            free(name);
            return 1;
        }

        /// Handle -p for specific variable
        if (opt_print) {
            symtable_manager_t *manager = symtable_get_global_manager();
            /// Check if it's an array
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

        /// Handle array declarations
        if (opt_indexed_array || opt_assoc_array) {
            array_value_t *arr = symtable_array_create(opt_assoc_array);
            if (!arr) {
                executor_error_report(current_executor, SHELL_ERR_SCOPE_ERROR,
                                      builtin_get_source_location(),
                                      "failed to create array");
                free(name);
                return 1;
            }

            /// If value is provided and starts with (, parse as array literal
            if (value && value[0] == '(') {
                /// Parse array literal (elem1 elem2 ...)
                const char *p = value + 1;
                int idx = 0;

                while (*p && *p != ')') {
                    /// Skip whitespace
                    while (*p && isspace(*p))
                        p++;
                    if (*p == ')' || !*p)
                        break;

                    /// Find end of element
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

                            /// Check for [n]=value syntax
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
                                /// Regular element
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

            /// declare -ar / -Ar / -ax / -Ax: thread the requested
            /// attribute flags onto the array record so subsequent
            /// element writes (arr[idx]=value) and bulk assignments
            /// honor the attribute. The scalar branch builds `flags`
            /// the same way; this branch routes them at the array
            /// layer because the array record carries its own flags
            /// field (the scalar SYMVAR_READONLY check at
            /// symtable_set_var doesn't see element writes).
            symvar_flags_t array_flags = SYMVAR_NONE;
            if (opt_readonly) {
                array_flags |= SYMVAR_READONLY;
            }
            if (opt_export) {
                array_flags |= SYMVAR_EXPORTED;
            }
            if (array_flags != SYMVAR_NONE) {
                symtable_array_add_flags(name, array_flags);
            }
        }
        /// Handle integer declaration
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
            /// For integer variables, evaluate value as arithmetic
            if (value) {
                char *result = arithm_expand(value);
                if (result) {
                    symtable_set(manager, name, result);
                    free(result);
                } else {
                    /// If arithmetic eval fails, set to 0
                    symtable_set(manager, name, "0");
                }
            } else {
                symtable_set(manager, name, "0");
            }
            /// Mark the variable as integer-typed so subsequent
            /// assignments arith-evaluate their RHS (issue #102).
            /// Without this, `declare -i n=5; n=n+10` would only
            /// apply arithmetic to the initial 5; the n=n+10 line
            /// would store the literal string "n+10".
            symvar_flags_t flags = symtable_get_flags(manager, name);
            symtable_set_flags(manager, name, flags | SYMVAR_INTEGER_ATTR);
        }
        /// Handle nameref declaration (-n)
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
            /// Locality is determined by which scope we write into,
            /// not by a flag on the variable record. opt_global is
            /// honored via the scope choice at the actual write
            /// (symtable_set_nameref dispatches into the correct
            /// scope), so we no longer set a redundant SYMVAR_LOCAL
            /// bit -- it was never read.
            symvar_flags_t flags = SYMVAR_NAMEREF_FLAG;
            if (symtable_set_nameref(manager, name, value, flags) != 0) {
                executor_error_report(current_executor, SHELL_ERR_SCOPE_ERROR,
                                      builtin_get_source_location(),
                                      "failed to create nameref");
                free(name);
                return 1;
            }
        }
        /// Regular variable declaration
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

            /// Apply case transformations if requested. The earlier
            /// implementation folded per-byte via toupper/tolower from
            /// <ctype.h>, which only handles ASCII; non-ASCII codepoints
            /// passed through unfolded. symtable_apply_case_attr_alloc
            /// uses the project's Unicode case table via
            /// lle_utf8_tolower / lle_utf8_toupper, so accented Latin,
            /// Greek, Cyrillic, etc. fold correctly. The helper returns
            /// NULL when the input does not need transformation (no
            /// attribute, empty value); fall back to strdup in that case
            /// so final_value is always heap-allocated when value is set.
            char *final_value = NULL;
            if (value) {
                symvar_flags_t case_flags = SYMVAR_NONE;
                if (opt_lowercase) {
                    case_flags = SYMVAR_LOWERCASE;
                } else if (opt_uppercase) {
                    case_flags = SYMVAR_UPPERCASE;
                }
                if (case_flags != SYMVAR_NONE) {
                    final_value =
                        symtable_apply_case_attr_alloc(value, case_flags);
                }
                if (!final_value) {
                    final_value = strdup(value);
                }
            }

            /// Build flags.
            /// declare -t on variables is a documented bash no-op
            /// ("Has no effect on variables"); lush accepts the
            /// option for script-portability but stores no
            /// observable attribute. Locality is tracked by the
            /// scope chain itself; opt_global selects the write
            /// path below rather than encoding the choice as a flag
            /// bit. opt_nameref / opt_integer are honored by the
            /// respective dedicated set functions called later.
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
            (void)opt_trace; /// accepted-but-no-op; see comment above

            if (final_value) {
                if (opt_global) {
                    /// Use global scope for -g flag
                    symtable_set_global_var(manager, name, final_value);
                } else {
                    symtable_set_var(manager, name, final_value, flags);
                }
                free(final_value);
            } else {
                /// Declare without value. If the variable already
                /// exists, preserve its current value and just update
                /// the flags so attribute promotion (declare -l X
                /// after X already had a value) does not clobber the
                /// content. When SYMVAR_LOWERCASE / SYMVAR_UPPERCASE
                /// is among the new flags, apply the fold retroactively
                /// to the preserved value -- this matches bash, which
                /// folds existing values when the attribute is added.
                /// For nonexistent variables the historical empty-
                /// string default still applies.
                char *existing = symtable_get_var(manager, name);
                const char *write_value = existing ? existing : "";
                char *folded = NULL;
                if (existing &&
                    (flags & (SYMVAR_LOWERCASE | SYMVAR_UPPERCASE))) {
                    folded = symtable_apply_case_attr_alloc(existing, flags);
                    if (folded) {
                        write_value = folded;
                    }
                }
                if (opt_global) {
                    symtable_set_global_var(manager, name, write_value);
                } else {
                    symtable_set_var(manager, name, write_value, flags);
                }
                free(folded);
                free(existing);
            }
        }

        /// Handle export (also set in environment)
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
