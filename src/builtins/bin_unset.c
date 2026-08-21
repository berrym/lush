/**
 * @file bin_unset.c
 * @brief `unset` builtin -- remove variables and functions
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "arithmetic.h"
#include "builtins.h"
#include "shell_mode.h"
#include "symtable.h"

/**
 * @brief Remove a variable from the global symbol table
 *
 * Unsets the specified shell variable, removing it from the environment.
 *
 * @param argc Argument count (must be 2)
 * @param argv Argument vector (argv[1] is variable name)
 * @return 0 on success, 1 on invalid usage
 */
int bin_unset(int argc, char **argv) {
    /// No arguments - silently succeed (POSIX behavior)
    if (argc < 2) {
        return 0;
    }

    /// Accumulated status: a readonly target is refused (non-zero) while the
    /// remaining names are still processed, matching builtin argument-loop
    /// discipline.
    int rc = 0;

    /// Unset each variable specified
    for (int i = 1; i < argc; i++) {
        const char *var_name = argv[i];

        /// Declared at loop scope, not inside the subscript branch, so the
        /// bare name outlives that branch: bash mode retargets the unset at
        /// it and falls through to the shared nameref + readonly + unset tail
        /// below rather than carrying a second copy of those guards (#634).
        char name_buf[256];

        /// Array-element form: `unset arr[N]` or `unset assoc[key]`
        /// removes just the matching element, not the whole array.
        /// Bash semantics; without this lush ignored the subscript
        /// and either no-op'd or tried to unset a scalar named
        /// "arr[N]" (issue #101). Detect a `[` in the name; if
        /// followed by a matching `]`, route through the array
        /// element-unset API.
        const char *bracket = strchr(var_name, '[');
        if (bracket && bracket > var_name) {
            const char *close = strrchr(bracket, ']');
            if (close && close > bracket + 1) {
                size_t name_len = (size_t)(bracket - var_name);
                size_t sub_len = (size_t)(close - bracket - 1);
                char sub_buf[256];
                if (name_len < sizeof(name_buf) && sub_len < sizeof(sub_buf)) {
                    memcpy(name_buf, var_name, name_len);
                    name_buf[name_len] = '\0';
                    memcpy(sub_buf, bracket + 1, sub_len);
                    sub_buf[sub_len] = '\0';
                    array_value_t *array = symtable_get_array(name_buf);
                    if (array) {
                        /// A readonly binding is immutable: it may be neither
                        /// reassigned nor destroyed. Removing an element
                        /// mutates the array, so refuse it the same way the
                        /// write surfaces refuse element writes (E1117),
                        /// leaving the array intact. Uniform readonly
                        /// diagnostic across all mutation surfaces.
                        if (array->flags & SYMVAR_READONLY) {
                            executor_error_report(
                                current_executor, SHELL_ERR_READONLY_VAR,
                                builtin_get_source_location(),
                                "%s: readonly variable", name_buf);
                            rc = 1;
                            continue;
                        }
                        if (array->is_associative) {
                            symtable_array_unset_assoc(array, sub_buf);
                        } else {
                            /// The subscript is an arithmetic expression, the
                            /// same as on every other element surface. strtoll
                            /// read `i` as 0 and destroyed the FIRST element
                            /// instead of the one asked for, silently and with
                            /// status 0 -- `0xa` and `1+1` the same way
                            /// (issue #647). Full-width so a 64-bit index is
                            /// unset rather than truncated (issue #618).
                            arithm_clear_error();
                            char *ir = arithm_expand_with_executor(
                                current_executor, sub_buf);
                            if (!ir || arithm_error_is_flagged()) {
                                free(ir);
                                executor_report_arith_failure(
                                    current_executor,
                                    builtin_get_source_location(),
                                    "evaluating an array subscript");
                                rc = 1;
                                continue;
                            }
                            long long idx = strtoll(ir, NULL, 10);
                            free(ir);
                            /// Same indexing convention the read and write
                            /// surfaces use: in zsh mode user index 1 is the
                            /// first element, and 0 addresses nothing. Without
                            /// this, `${a[1]}` read the first element while
                            /// `unset a[1]` destroyed the second -- lush
                            /// disagreeing with itself (issue #68, array half).
                            if (!shell_mode_allows(
                                    FEATURE_ARRAY_ZERO_INDEXED)) {
                                if (idx == 0) {
                                    continue; /// addresses no element
                                }
                                if (idx > 0) {
                                    idx--; /// 1-based -> 0-based
                                }
                            }
                            symtable_array_unset_index(array, idx);
                        }
                        continue;
                    }

                    /// The name is not an array. On a scalar the subscript
                    /// does not address an element -- a scalar has none
                    /// (SEMANTICS S3.1) -- it names a grapheme-cluster slice,
                    /// a read-only view of the string. `unset` removes a
                    /// binding or a list/map element, and a view is neither.
                    /// Without this the request fell through and unset the
                    /// literal name "s[0]", which binds nothing, so it
                    /// vanished with status 0 and the value intact (#634).
                    /// The write surface already refuses to assign through
                    /// the view (E1134, the #621 kind-transition error);
                    /// refusing to remove through it is the same rule on the
                    /// same surface, so the two spellings of one category
                    /// error stop disagreeing.
                    ///
                    /// Gated the way S3.9 gates every kind-boundary crossing:
                    /// strict in lush mode, reconciled to the oracle in the
                    /// compatibility modes. bash reads a scalar as a
                    /// degenerate array whose element 0 IS the variable, so
                    /// bash mode retargets the unset at the bare name; zsh
                    /// and dash both reject a subscript on a scalar, so those
                    /// modes take the diagnostic.
                    lush_value_view_t sview = {0};
                    symtable_lookup(name_buf, &sview);
                    bool name_is_scalar = (sview.kind == LUSH_VALUE_SCALAR);
                    lush_value_view_clear(&sview);
                    if (name_is_scalar) {
                        if (!shell_mode_allows(FEATURE_STRICT_VALUE_TYPING) &&
                            shell_mode_get() == SHELL_MODE_BASH) {
                            /// Fall through to the shared tail with the bare
                            /// name, so the nameref resolution and the
                            /// readonly guard both still apply -- bash also
                            /// refuses `unset s[0]` on a readonly scalar.
                            var_name = name_buf;
                        } else {
                            shell_error_t *err = shell_error_create(
                                SHELL_ERR_TYPE_MISMATCH, SHELL_SEVERITY_ERROR,
                                builtin_get_source_location(),
                                "type mismatch: cannot unset an element of "
                                "scalar '%s'",
                                name_buf);
                            if (err) {
                                /// Sized so the worst case cannot truncate:
                                /// the name is bounded by name_buf (255 chars)
                                /// and appears three times, plus ~170 bytes of
                                /// fixed text. GCC proves the bound at -O3 and
                                /// rejects a buffer that could overflow it
                                /// (-Werror=format-truncation); clang does not
                                /// implement that warning, so this only ever
                                /// showed up on the Linux builds.
                                char sugg[1024];
                                snprintf(
                                    sugg, sizeof(sugg),
                                    "'%s' holds a scalar; a subscript on it "
                                    "names a read-only grapheme slice, not an "
                                    "element. Use `unset %s` to remove the "
                                    "whole variable, or declare it a list "
                                    "first (declare -a %s).",
                                    name_buf, name_buf, name_buf);
                                shell_error_set_suggestion(err, sugg);
                                shell_error_display(err, stderr,
                                                    isatty(STDERR_FILENO));
                                shell_error_free(err);
                            } else {
                                executor_error_report(
                                    current_executor, SHELL_ERR_TYPE_MISMATCH,
                                    builtin_get_source_location(),
                                    "type mismatch: cannot unset an element "
                                    "of scalar '%s'",
                                    name_buf);
                            }
                            rc = 1;
                            continue;
                        }
                    }
                }
            }
        }

        /// Resolve nameref if applicable - unset the target, not the nameref
        /// itself
        symtable_manager_t *mgr = symtable_get_global_manager();
        char *resolved_owned = NULL;
        if (mgr && symtable_is_nameref(mgr, var_name)) {
            resolved_owned =
                (char *)symtable_resolve_nameref(mgr, var_name, 10);
            if (resolved_owned) {
                var_name = resolved_owned;
            }
        }

        /// A readonly binding is immutable across every mutation surface, so
        /// `unset` must refuse it the way assignment/append/element/arithmetic
        /// already do (E1117), leaving the binding intact -- otherwise a
        /// readonly array or associative would be silently destroyed and a
        /// readonly scalar protected only incidentally and without diagnostic.
        /// Both queries are scope-chain aware, so a readonly outer/global
        /// binding is caught from within a function scope. Checked on the
        /// nameref-resolved target, matching where the unset itself lands.
        if ((symtable_array_get_flags(var_name) & SYMVAR_READONLY) ||
            (mgr && (symtable_get_flags(mgr, var_name) & SYMVAR_READONLY))) {
            executor_error_report(current_executor, SHELL_ERR_READONLY_VAR,
                                  builtin_get_source_location(),
                                  "%s: readonly variable", var_name);
            rc = 1;
            free(resolved_owned);
            continue;
        }

        /// Use legacy API function for unsetting variables
        symtable_unset_global(var_name);
        free(resolved_owned);
    }
    return rc;
}
