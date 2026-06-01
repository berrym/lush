/**
 * @file compdef_source.c
 * @brief LLE completion source that fires compdef-bound functions on TAB.
 *
 * Registers a custom completion source via lle_completion_register_source.
 * On each TAB:
 *   - is_applicable() returns true iff compdef_lookup(command_name) yields
 *     a function name (i.e. the user has run `compdef FN CMD` for the
 *     command at the cursor).
 *   - generate() saves the executor's prior active_comp_result, sets it
 *     to the result the engine is filling, builds argv (function name,
 *     command name, current-word prefix, previous word), invokes the
 *     function in-process via executor_run_function, and restores the
 *     prior active_comp_result. The function emits candidates by
 *     calling `compadd`, which appends through completion_bridge_add
 *     into the active accumulator.
 *
 * Lives on the shell side (src/, not src/lle/) because the generate
 * callback calls executor_run_function and reads/writes
 * executor->active_comp_result. The source is registered with liblle by
 * function pointer; no weak/strong bridge symbols are required because
 * the pointer itself transfers control across the boundary.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "compdef_source.h"

#include "compdef.h"
#include "executor.h"
#include "lle/completion/custom_source.h"
#include "lle/completion/word_context.h"

#include <stdbool.h>
#include <stddef.h>

/// get_global_executor returns the session-lifetime executor that
/// outlives any single command invocation. Using current_executor
/// here would be wrong: execute_builtin_command sets current_executor
/// at entry and clears it on exit, so it is NULL between commands --
/// exactly when LLE TAB completion fires.
extern executor_t *get_global_executor(void);

bool compdef_source_applicable(void *user_data,
                               const lle_word_context_t *context) {
    (void)user_data;
    if (!context || !context->command_name) {
        return false;
    }
    return compdef_lookup(context->command_name) != NULL;
}

lle_result_t compdef_source_generate(void *user_data,
                                     const lle_word_context_t *context,
                                     lle_completion_result_t *result) {
    (void)user_data;
    if (!context || !result) {
        return LLE_SUCCESS;
    }
    executor_t *exec = get_global_executor();
    if (!exec) {
        return LLE_SUCCESS;
    }
    const char *fn = compdef_lookup(context->command_name);
    if (!fn) {
        return LLE_SUCCESS;
    }

    /// argv layout follows bash-completion convention:
    ///   $1 = command name being completed
    ///   $2 = current word prefix (what the user has typed for the word
    ///        the cursor is on; we use dequoted_filename_prefix when it
    ///        is set, falling back to the empty string)
    ///   $3 = previous fully-completed word (or empty at command position)
    char *cmd = (char *)context->command_name;
    char *cur = (char *)(context->dequoted_filename_prefix
                             ? context->dequoted_filename_prefix
                             : "");
    char *prev = "";
    if (context->arguments && context->argument_count > 0 &&
        context->arguments[context->argument_count - 1]) {
        prev = context->arguments[context->argument_count - 1];
    }
    char *argv[] = {(char *)fn, cmd, cur, prev};
    int argc = 4;

    /// Save + restore the session executor's active_comp_result on
    /// the exec pointer (not on current_executor, which the inner
    /// compadd's execute_builtin_command path clears to NULL on
    /// return, so the restore would dereference NULL on a global
    /// read).
    void *prior = exec->active_comp_result;
    exec->active_comp_result = result;
    (void)executor_run_function(exec, fn, argc, argv);
    exec->active_comp_result = prior;
    return LLE_SUCCESS;
}

void compdef_source_init(void) {
    lle_custom_completion_source_t source = {
        .name = "compdef",
        .description = "compdef-bound user functions",
        .priority = 750, /// above generic file/command (default 500),
                         /// below built-in arg-specs (which know their
                         /// command shape exactly)
        .generate = compdef_source_generate,
        .is_applicable = compdef_source_applicable,
        .cleanup = NULL,
        .user_data = NULL,
    };
    (void)lle_completion_register_source(&source);
}
