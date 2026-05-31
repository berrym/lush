/**
 * @file restricted_mode.c
 * @brief Restricted-shell mode engagement + per-site enforcement helpers.
 *
 * See include/restricted_mode.h for the full restriction set, its
 * provenance (bash rbash / zsh RESTRICTED / POSIX 2024 set -r), and
 * the explicit "usability boundary, NOT a security boundary"
 * disclaimer that mirrors bash's own manual.
 *
 * Per-site enforcement lives where the operation does -- cd in
 * bin_cd.c, exec in bin_exec.c, output redirection in redirection.c,
 * etc. This module owns only the engagement transition (readonly
 * lockdown of the protected variables) and the shared rejection
 * reporter so every call site emits identical structured-error
 * shape.
 */

#include "restricted_mode.h"

#include "executor.h"
#include "lush.h"
#include "symtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Protected variables -- bash's rbash list. Made readonly when
/// restricted mode engages so a restricted user cannot redirect
/// command lookup via PATH or hijack init via ENV/BASH_ENV.
static const char *const kProtectedVars[] = {
    "SHELL", "PATH", "HISTFILE", "ENV", "BASH_ENV", NULL,
};

void restricted_mode_engage(void) {
    if (shell_opts.restricted_mode_engaged) {
        return; /// Idempotent: already engaged.
    }

    /// Lock down each protected variable as readonly. Bash sets these
    /// readonly via maybe_make_restricted(); the same effect via the
    /// existing symtable readonly machinery -- no new infrastructure
    /// needed. If a variable isn't currently set we still mark it
    /// (creates the binding with the empty value so a later
    /// assignment is refused) -- matches bash's semantics where
    /// `PATH=foo` from a restricted shell fails regardless of
    /// PATH's prior state.
    for (size_t i = 0; kProtectedVars[i]; i++) {
        const char *name = kProtectedVars[i];
        symvar_flags_t cur_flags = symtable_get_flags(symtable_manager(), name);
        char *cur_value = symtable_get_var(symtable_manager(), name);
        const char *value = cur_value ? cur_value : "";
        symtable_set_var(symtable_manager(), name, value,
                         cur_flags | SYMVAR_READONLY);
        free(cur_value);
    }

    shell_opts.restricted_mode_engaged = true;
}

bool restricted_mode_is_engaged(void) {
    return shell_opts.restricted_mode_engaged;
}

int restricted_mode_reject(source_location_t loc, const char *what) {
    executor_t *executor = get_global_executor();
    if (executor) {
        executor_error_report(executor, SHELL_ERR_PERMISSION_DENIED, loc,
                              "%s: restricted", what ? what : "operation");
    } else {
        /// Pre-executor path (e.g. init failure): plain stderr is
        /// the only thing available.
        fprintf(stderr, "lush: %s: restricted\n", what ? what : "operation");
    }
    return 1;
}
