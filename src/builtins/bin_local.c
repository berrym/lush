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

/**
 * @brief Declare local variables within function scope
 *
 * `local` is `declare`/`typeset` with function-local scope: it accepts the
 * same option grammar (-a, -A, -i, -r, -n, -x, ...), the `name=(...)`
 * array-literal form, and namerefs. bin_declare already creates
 * function-local variables when it runs inside a function (a top-level
 * declare makes a global; a declare inside a function body makes a local),
 * so `local` delegates to it for the whole package rather than maintaining a
 * second, divergent option parser.
 *
 * The only behavior `local` adds over `declare` is scope strictness: bash and
 * POSIX -- and lush's curated default -- require `local` to be called inside
 * a function and error otherwise, because a "local" with no enclosing scope
 * is meaningless. zsh permits a top-level `local` (it simply declares a
 * global), so that mode skips the check.
 *
 * @param argc Argument count
 * @param argv Argument vector with variable declarations
 * @return 0 on success, non-zero on error or if not in a function
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

    /// Scope strictness: bash, POSIX, and the lush default require `local` to
    /// run inside a function. zsh treats a top-level `local` as a global
    /// declaration, so it skips the check (bin_declare handles that scope).
    if (shell_mode_get() != SHELL_MODE_ZSH &&
        symtable_current_level(manager) == 0) {
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

    /// Delegate to declare/typeset for the full option grammar, array
    /// literals, and nameref handling. Inside a function scope (guaranteed
    /// above for non-zsh modes) bin_declare creates function-local variables.
    return bin_declare(argc, argv);
}
