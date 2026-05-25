/**
 * @file executor.c
 * @brief Modern Execution Engine Implementation
 *
 * Clean, efficient execution engine designed for the modern parser and
 * tokenizer. Handles command execution, control structures, pipelines, and
 * variable management with proper POSIX compliance.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (c) 2025 Michael Berry. All rights reserved.
 */

#include "executor.h"

#include "alias.h"
#include "arithmetic.h"
#include "autocorrect.h"
#include "builtins.h"
#include "config.h"
#include "debug.h"
#include "ht.h"
#include "init.h"
#include "lle/lle_shell_event_hub.h"
#include "lle/lle_shell_integration.h"
#include "lle/unicode_case.h"
#include "lle/unicode_grapheme.h"
#include "lle/utf8_support.h"
#include "lush.h"
#include "lush_fork.h"
#include "node.h"
#include "parser.h"
#include "redirection.h"
#include "shell_mode.h"
#include "signals.h"
#include "strings.h"
#include "symtable.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glob.h>
#include <math.h>
#include <pwd.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Global executor pointer for job control builtins
executor_t *current_executor = NULL;

// Forward declarations
// Forward declarations - updated for symtable
static int execute_node(executor_t *executor, node_t *node);
static int execute_command(executor_t *executor, node_t *command);
static int execute_pipeline(executor_t *executor, node_t *pipeline);
static int execute_function_definition(executor_t *executor, node_t *function);
// Typed-function form -- declaration, call, return, let-capture.
static void executor_typed_fns_clear(executor_t *executor);
static int execute_typed_fn_decl(executor_t *executor, node_t *node);
static int execute_typed_fn_call(executor_t *executor, node_t *node);
static int execute_typed_fn_return(executor_t *executor, node_t *node);
static int execute_typed_let_fn(executor_t *executor, node_t *node);
static int execute_function_call(executor_t *executor,
                                 const char *function_name, char **argv,
                                 int argc, source_location_t loc);
static bool is_function_defined(executor_t *executor,
                                const char *function_name);
static function_def_t *find_function(executor_t *executor,
                                     const char *function_name);
static int store_function(executor_t *executor, const char *function_name,
                          node_t *body, function_param_t *params,
                          int param_count);
static int validate_function_parameters(executor_t *executor,
                                        function_def_t *func, char **argv,
                                        int argc, source_location_t loc);
node_t *copy_ast_node(node_t *node);
static node_t *copy_ast_chain(node_t *node);
static int execute_if(executor_t *executor, node_t *if_node);
static int execute_while(executor_t *executor, node_t *while_node);
static int execute_until(executor_t *executor, node_t *until_node);
static int execute_repeat(executor_t *executor, node_t *repeat_node);
static int execute_for(executor_t *executor, node_t *for_node);
static int execute_for_arith(executor_t *executor, node_t *for_arith_node);
static int execute_select(executor_t *executor, node_t *select_node);
static int execute_time(executor_t *executor, node_t *time_node);
static int execute_coproc(executor_t *executor, node_t *coproc_node);
static int execute_anonymous_function(executor_t *executor, node_t *anon_node);
static int execute_case(executor_t *executor, node_t *case_node);
static int execute_logical_and(executor_t *executor, node_t *and_node);
static int execute_logical_or(executor_t *executor, node_t *or_node);
static int execute_command_list(executor_t *executor, node_t *list);
static char **build_argv_from_ast(executor_t *executor, node_t *command,
                                  int *argc);
static bool try_expand_vector_arg(executor_t *executor, node_t *node,
                                  char ***out_vec, int *out_count);

/* Context for ${!prefix*} / ${!prefix@} name collection. Callback
 * appends every variable whose name starts with `prefix` into a
 * growable string array. Defined at file scope (rather than nested
 * in the use site) because C lacks nested functions. Issue #102. */
typedef struct {
    char **names;
    size_t count;
    size_t capacity;
    const char *prefix;
    size_t prefix_len;
} prefix_collect_ctx_t;

static void prefix_collect_cb(const char *key, const char *value,
                              void *userdata) {
    (void)value;
    prefix_collect_ctx_t *c = (prefix_collect_ctx_t *)userdata;
    if (strncmp(key, c->prefix, c->prefix_len) != 0) {
        return;
    }
    if (c->count >= c->capacity) {
        size_t newcap = c->capacity ? c->capacity * 2 : 16;
        char **nn = realloc(c->names, newcap * sizeof(char *));
        if (!nn) {
            return;
        }
        c->names = nn;
        c->capacity = newcap;
    }
    c->names[c->count++] = strdup(key);
}

static int strptr_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static bool is_stdout_captured(void);
static bool has_stdout_redirections(node_t *command);
static bool builtin_can_fork(const char *name);
static int execute_builtin_with_captured_stdout(executor_t *executor,
                                                char **argv, node_t *command);

static int add_to_argv_list(char ***argv_list, int *argv_count,
                            int *argv_capacity, char *arg);
static char **ifs_field_split(const char *text, const char *ifs, int *count);
static void cleanup_procsub_fds(executor_t *executor);

// Forward declarations for POSIX compliance
bool is_posix_mode_enabled(void);
bool is_pipefail_enabled(void);
bool is_pipeline_diagnostic_enabled(void);
static int execute_external_command_with_setup(executor_t *executor,
                                               char **argv,
                                               bool redirect_stderr,
                                               node_t *command);
static int execute_builtin_command(executor_t *executor, char **argv,
                                   source_location_t loc);
static int execute_brace_group(executor_t *executor, node_t *group);
static int execute_subshell(executor_t *executor, node_t *subshell);
static int execute_negate(executor_t *executor, node_t *negate_node);

/// Forward declarations for Phase 1: Arrays and Arithmetic
static int execute_arithmetic_command(executor_t *executor, node_t *arith_node);
static int execute_array_assignment(executor_t *executor, node_t *assign_node);
static int execute_array_append(executor_t *executor, node_t *append_node);

// Forward declarations for Phase 2: Extended Tests
static int execute_extended_test(executor_t *executor, node_t *test_node);

// Forward declarations for Phase 3: Process Substitution
char *expand_process_substitution(executor_t *executor, node_t *proc_sub);
static bool is_builtin_command(const char *cmd);
static void set_executor_error(executor_t *executor, const char *message);
static char *expand_variable(executor_t *executor, const char *var_text);
static char *expand_tilde(const char *text);
static char **expand_glob_pattern(const char *pattern, int *expanded_count);
static bool needs_glob_expansion(const char *str);
/* expand_brace_pattern is declared in include/executor.h so the
 * completion analyzer can enumerate per-branch directory targets for
 * brace expressions in path-prefix bytes. The forward declaration
 * here is preserved so existing static callers within this
 * translation unit don't need to be reordered. */
static bool needs_brace_expansion(const char *str);
/* Sentinel returned in *expanded_count when brace expansion exceeds the
 * configured cap (behavior.brace_expansion_max). Top-level callers detect
 * this and emit an expansion error rather than treating it as a malloc
 * failure. */
#define BRACE_EXPANSION_LIMIT_SENTINEL (-1)
static void initialize_job_control(executor_t *executor);
static char *expand_arithmetic(executor_t *executor, const char *arith_text);
static char *expand_command_substitution(executor_t *executor,
                                         const char *cmd_text);
static node_t *copy_node_simple(node_t *original);
static void copy_function_definitions(executor_t *dest, executor_t *src);
char *expand_if_needed(executor_t *executor, const char *text);
static char *expand_quoted_string(executor_t *executor, const char *str,
                                  bool in_double_quotes);
static char *expand_arg_node(executor_t *executor, node_t *node);
static char *expand_array_unsubscripted(executor_t *executor,
                                        array_value_t *array,
                                        const char *arr_name);
static void executor_request_posix_exit(executor_t *executor, int status);
static char *slice_string_graphemes(const char *str, size_t str_len,
                                    int start_grapheme, int count);
static char *expand_ansi_c_string(const char *str, size_t len);
static bool is_assignment(const char *text);
static int execute_assignment(executor_t *executor, const char *assignment,
                              source_location_t loc);
static bool match_pattern(const char *str, const char *pattern);

/**
 * @brief Check if command is allowed in privileged mode
 *
 * In privileged mode, certain commands are restricted for security:
 * - Commands with absolute/relative paths (containing '/')
 * - Dangerous builtins: exec, cd, set
 *
 * @param command Command name to check
 * @return true if command is allowed, false if blocked
 */
static bool is_privileged_command_allowed(const char *command) {
    if (!shell_opts.privileged_mode || !command) {
        return true; // Allow if not in privileged mode
    }

    // Block commands containing '/' (absolute/relative paths)
    if (strchr(command, '/') != NULL) {
        return false;
    }

    // Block dangerous built-in commands in privileged mode
    if (strcmp(command, "exec") == 0 || strcmp(command, "cd") == 0 ||
        strcmp(command, "set") == 0) {
        return false;
    }

    return true;
}

/**
 * @brief Check if redirection target is allowed in privileged mode
 *
 * In privileged mode, certain redirections are restricted:
 * - Absolute paths (starting with '/')
 * - Parent directory references ('../')
 *
 * @param target Redirection target path
 * @return true if redirection is allowed, false if blocked
 */
bool is_privileged_redirection_allowed(const char *target) {
    if (!shell_opts.privileged_mode || !target) {
        return true; // Allow if not in privileged mode
    }

    // Block absolute path redirections
    if (target[0] == '/') {
        return false;
    }

    // Block redirection to parent directories
    if (strstr(target, "../") != NULL || strcmp(target, "..") == 0) {
        return false;
    }

    return true;
}

/**
 * @brief Check if environment variable modification is allowed in privileged
 * mode
 *
 * In privileged mode, security-sensitive variables cannot be modified:
 * - PATH: Command search path
 * - IFS: Input field separator
 * - ENV: Startup script path
 * - SHELL: Shell path
 *
 * @param var_name Variable name to check
 * @return true if modification is allowed, false if blocked
 */
static bool is_privileged_path_modification_allowed(const char *var_name) {
    if (!shell_opts.privileged_mode || !var_name) {
        return true; // Allow if not in privileged mode
    }

    // Block PATH modifications
    if (strcmp(var_name, "PATH") == 0 || strcmp(var_name, "IFS") == 0 ||
        strcmp(var_name, "ENV") == 0 || strcmp(var_name, "SHELL") == 0) {
        return false;
    }

    return true;
}

/**
 * @brief Clean up resources before subshell _exit()
 *
 * Since _exit() doesn't run atexit() handlers, subshell processes must
 * explicitly clean up allocated memory to avoid valgrind leak reports.
 * This function frees the global symbol table which includes arrays
 * and other dynamically allocated variables.
 *
 * Call this before _exit() in forked child processes.
 */
static void subshell_cleanup(void) { free_global_symtable(); }

/**
 * @brief Create a new executor with global symbol table
 *
 * Allocates and initializes an executor context using the global
 * symbol table manager. Initializes job control and sets default state.
 *
 * @return New executor instance or NULL on failure
 */
executor_t *executor_new(void) {
    executor_t *executor = malloc(sizeof(executor_t));
    if (!executor) {
        return NULL;
    }

    // Use global symbol table manager from modernized legacy interface
    executor->symtable = symtable_get_global_manager();
    if (!executor->symtable) {
        free(executor);
        return NULL;
    }

    executor->interactive = false;
    executor->debug = false;
    executor->exit_status = 0;
    executor->error_message = NULL;
    executor->has_error = false;
    executor->functions = NULL;
    executor->current_script_file = NULL;
    executor->in_script_execution = false;
    executor->expansion_error = false;
    executor->expansion_exit_status = 0;
    executor->shell_exit_requested = false;
    executor->shell_exit_status = 0;
    executor->loop_control = LOOP_NORMAL;
    executor->loop_depth = 0;
    executor->source_depth = 0;
    executor->source_return = false;

    // Initialize error context stack (Phase 3)
    executor->context_depth = 0;
    for (size_t i = 0; i < EXECUTOR_CONTEXT_STACK_MAX; i++) {
        executor->context_stack[i] = NULL;
        executor->context_locations[i] = SOURCE_LOC_UNKNOWN;
    }
    executor->active_loc = SOURCE_LOC_UNKNOWN;

    // Initialize process substitution fd tracking
    executor->procsub_fd_count = 0;
    memset(executor->procsub_fds, -1, sizeof(executor->procsub_fds));
    memset(executor->procsub_pids, 0, sizeof(executor->procsub_pids));

    // Variable-allocated fd registry starts empty; grown on demand.
    executor->alloc_fds = NULL;
    executor->alloc_fd_count = 0;
    executor->alloc_fd_cap = 0;

    /* Source-text retention starts empty; populated per-batch by
     * executor_execute_command_line. */
    executor->source_text = NULL;
    executor->source_starting_line = 0;

    // Typed-function state starts empty.
    executor->typed_fns = NULL;
    executor->typed_fn_return_pending = false;
    executor->typed_fn_return_value.kind = LUSH_VALUE_NONE;
    executor->typed_fn_return_value.scalar_value = NULL;
    executor->typed_fn_return_value.array = NULL;

    initialize_job_control(executor);

    return executor;
}

/**
 * @brief Create a new executor with specified symbol table
 *
 * @param symtable Symbol table manager to use
 * @return New executor instance or NULL on failure
 */
executor_t *executor_new_with_symtable(symtable_manager_t *symtable) {
    executor_t *executor = malloc(sizeof(executor_t));
    if (!executor) {
        return NULL;
    }

    // Use provided symtable
    executor->symtable = symtable;

    executor->interactive = false;
    executor->debug = false;
    executor->exit_status = 0;
    executor->error_message = NULL;
    executor->has_error = false;
    executor->functions = NULL;
    executor->current_script_file = NULL;
    executor->in_script_execution = false;
    executor->expansion_error = false;
    executor->expansion_exit_status = 0;
    executor->shell_exit_requested = false;
    executor->shell_exit_status = 0;
    executor->loop_control = LOOP_NORMAL;
    executor->loop_depth = 0;
    executor->source_depth = 0;
    executor->source_return = false;

    // Initialize error context stack (Phase 3)
    executor->context_depth = 0;
    for (size_t i = 0; i < EXECUTOR_CONTEXT_STACK_MAX; i++) {
        executor->context_stack[i] = NULL;
        executor->context_locations[i] = SOURCE_LOC_UNKNOWN;
    }
    executor->active_loc = SOURCE_LOC_UNKNOWN;

    // Initialize process substitution fd tracking
    executor->procsub_fd_count = 0;
    memset(executor->procsub_fds, -1, sizeof(executor->procsub_fds));
    memset(executor->procsub_pids, 0, sizeof(executor->procsub_pids));

    // Variable-allocated fd registry starts empty; grown on demand.
    executor->alloc_fds = NULL;
    executor->alloc_fd_count = 0;
    executor->alloc_fd_cap = 0;

    /* Source-text retention starts empty; populated per-batch by
     * executor_execute_command_line. */
    executor->source_text = NULL;
    executor->source_starting_line = 0;

    // Typed-function state starts empty.
    executor->typed_fns = NULL;
    executor->typed_fn_return_pending = false;
    executor->typed_fn_return_value.kind = LUSH_VALUE_NONE;
    executor->typed_fn_return_value.scalar_value = NULL;
    executor->typed_fn_return_value.array = NULL;

    initialize_job_control(executor);

    return executor;
}

/**
 * @brief Free an executor and all associated resources
 *
 * Frees the function table and script context. Does not free the
 * symbol table as it may be externally managed.
 *
 * @param executor Executor to free
 */
void executor_free(executor_t *executor) {
    if (executor) {
        // Don't free global symtable - it's managed globally

        // Free function table
        function_def_t *func = executor->functions;
        while (func) {
            function_def_t *next = func->next;
            free(func->name);
            free_node_tree(func->body);
            free_function_params(func->params);
            free(func);
            func = next;
        }

        // Free typed-function registry.
        executor_typed_fns_clear(executor);

        // Free any pending typed-return value (defensive; cleared on
        // unwind under normal flow).
        lush_value_view_clear(&executor->typed_fn_return_value);

        // Free script context
        free(executor->current_script_file);

        // Free error context stack
        executor_clear_context(executor);

        // Close any variable-allocated fds the script never closed. Done
        // before freeing the array; errors here are intentionally silent
        // (the kernel will reclaim on process exit regardless, and a
        // script-side close that already raced us would return EBADF).
        if (executor->alloc_fds) {
            for (size_t i = 0; i < executor->alloc_fd_count; i++) {
                if (executor->alloc_fds[i] >= 0) {
                    close(executor->alloc_fds[i]);
                }
            }
            free(executor->alloc_fds);
            executor->alloc_fds = NULL;
            executor->alloc_fd_count = 0;
            executor->alloc_fd_cap = 0;
        }

        free(executor);
    }
}

void executor_track_alloc_fd(executor_t *executor, int fd) {
    if (!executor || fd < 0) {
        return;
    }
    if (executor->alloc_fd_count == executor->alloc_fd_cap) {
        size_t new_cap =
            executor->alloc_fd_cap == 0 ? 8 : executor->alloc_fd_cap * 2;
        int *grown = realloc(executor->alloc_fds, new_cap * sizeof(int));
        if (!grown) {
            // OOM during tracking: fd is still allocated and usable; just
            // not auto-cleaned at shell exit. The kernel reclaims on exit.
            return;
        }
        executor->alloc_fds = grown;
        executor->alloc_fd_cap = new_cap;
    }
    executor->alloc_fds[executor->alloc_fd_count++] = fd;
}

void executor_untrack_alloc_fd(executor_t *executor, int fd) {
    if (!executor || fd < 0 || !executor->alloc_fds) {
        return;
    }
    for (size_t i = 0; i < executor->alloc_fd_count; i++) {
        if (executor->alloc_fds[i] == fd) {
            // Swap-with-last, then shrink count. Order doesn't matter; the
            // registry is treated as a set.
            executor->alloc_fds[i] =
                executor->alloc_fds[executor->alloc_fd_count - 1];
            executor->alloc_fd_count--;
            return;
        }
    }
}

/**
 * @brief Enable or disable debug mode
 *
 * @param executor Executor context
 * @param debug True to enable debug output
 */
void executor_set_debug(executor_t *executor, bool debug) {
    if (executor) {
        executor->debug = debug;
        if (executor->symtable) {
            symtable_manager_set_debug(executor->symtable, debug);
        }
    }
}

/**
 * @brief Set interactive mode flag
 *
 * @param executor Executor context
 * @param interactive True for interactive shell mode
 */
void executor_set_interactive(executor_t *executor, bool interactive) {
    if (executor) {
        executor->interactive = interactive;
    }
}

/**
 * @brief Set the symbol table manager
 *
 * @param executor Executor context
 * @param symtable Symbol table manager to use
 */
void executor_set_symtable(executor_t *executor, symtable_manager_t *symtable) {
    if (executor) {
        // Don't free the old symtable if it exists - it might be external
        executor->symtable = symtable;
    }
}

/**
 * @brief Set script execution context for breakpoint matching
 *
 * Records the script file so the breakpoint check in execute_node can
 * fire. The source line is no longer tracked here -- breakpoints match
 * against node->loc.line, the parser-recorded absolute source line.
 *
 * @param executor Executor context
 * @param script_file Script file path (NULL to clear)
 */
void executor_set_script_context(executor_t *executor,
                                 const char *script_file) {
    if (!executor) {
        return;
    }

    // Free existing script file name
    free(executor->current_script_file);

    // Set new script context
    executor->current_script_file = script_file ? strdup(script_file) : NULL;
    executor->in_script_execution = (script_file != NULL);
}

/**
 * @brief Clear script execution context
 *
 * @param executor Executor context
 */
void executor_clear_script_context(executor_t *executor) {
    if (!executor) {
        return;
    }

    free(executor->current_script_file);
    executor->current_script_file = NULL;
    executor->in_script_execution = false;
}

/**
 * @brief Get current script file path
 *
 * @param executor Executor context
 * @return Script file path or NULL
 */
const char *executor_get_current_script_file(executor_t *executor) {
    return executor ? executor->current_script_file : NULL;
}

/**
 * @brief Check if executor has an error
 *
 * @param executor Executor context
 * @return True if an error occurred
 */
bool executor_has_error(executor_t *executor) {
    return executor && executor->has_error;
}

/**
 * @brief Get the last error message
 *
 * @param executor Executor context
 * @return Error message string or NULL
 */
const char *executor_error(executor_t *executor) {
    return executor ? executor->error_message : "Invalid executor";
}

/**
 * @brief Set an error on the executor
 *
 * @param executor Executor context
 * @param message Error message
 */
static void set_executor_error(executor_t *executor, const char *message) {
    if (executor) {
        executor->error_message = message;
        executor->has_error = true;
    }
}

/* ============================================================================
 * Error Context Stack (Phase 3)
 * ============================================================================
 */

/**
 * @brief Push a context frame onto the error context stack
 */
void executor_push_context(executor_t *executor, source_location_t loc,
                           const char *fmt, ...) {
    if (!executor || executor->context_depth >= EXECUTOR_CONTEXT_STACK_MAX) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char *context = NULL;
    if (vasprintf(&context, fmt, args) < 0) {
        context = NULL;
    }
    va_end(args);

    if (context) {
        executor->context_stack[executor->context_depth] = context;
        executor->context_locations[executor->context_depth] = loc;
        executor->context_depth++;
    }
}

/**
 * @brief Pop a context frame from the error context stack
 */
void executor_pop_context(executor_t *executor) {
    if (!executor || executor->context_depth == 0) {
        return;
    }

    executor->context_depth--;
    free(executor->context_stack[executor->context_depth]);
    executor->context_stack[executor->context_depth] = NULL;
    executor->context_locations[executor->context_depth] = SOURCE_LOC_UNKNOWN;
}

/**
 * @brief Best-effort current source location for an error site.
 *
 * Resolution order (most-specific first):
 *   1. Top of `context_locations[]` if any enclosing construct has
 *      been pushed (pipeline, while/for/until, brace group, subshell,
 *      case, function call, etc.). Same data that drives the
 *      `= while: ...` context lines in rendered errors.
 *   2. `executor->active_loc`, set by execute_command() to the
 *      currently-executing simple command's location. Covers the
 *      top-level naked-command case (no enclosing construct) so
 *      e.g. `lush -c 'x=${unset:?msg}'` still gets `--> file:line:col`.
 *   3. SOURCE_LOC_UNKNOWN -- only when nothing is executing.
 *
 * Mirrors `builtin_get_source_location()`. Used by the expansion
 * subsystem and by builtin paths that lack a direct AST node
 * (e.g. cd's CDPATH branch) so user-visible errors carry source-
 * location info instead of dropping the snippet block entirely.
 *
 * Expansion-byte-offset precision (highlighting the specific `${...}`
 * span within the command) is a precision pass for future work; it
 * would compose cleanly on top of this baseline by threading a
 * `source_location_t` through `expand_variable` /
 * `parse_parameter_expansion` and overriding tier 1/2 at error sites.
 */
static source_location_t executor_current_loc(executor_t *executor) {
    if (!executor) {
        return SOURCE_LOC_UNKNOWN;
    }
    if (executor->context_depth > 0) {
        return executor->context_locations[executor->context_depth - 1];
    }
    if (SOURCE_LOC_VALID(executor->active_loc) ||
        executor->active_loc.filename != NULL) {
        return executor->active_loc;
    }
    return SOURCE_LOC_UNKNOWN;
}

/**
 * @brief Clear all context frames
 */
void executor_clear_context(executor_t *executor) {
    if (!executor) {
        return;
    }

    while (executor->context_depth > 0) {
        executor_pop_context(executor);
    }
}

/**
 * @brief Report a structured runtime error with context chain
 */
void executor_error_report(executor_t *executor, shell_error_code_t code,
                           source_location_t loc, const char *fmt, ...) {
    if (!executor) {
        return;
    }

    // Create the error
    va_list args;
    va_start(args, fmt);
    shell_error_t *error =
        shell_error_createv(code, SHELL_SEVERITY_ERROR, loc, fmt, args);
    va_end(args);

    if (!error) {
        // Fallback to legacy error system
        set_executor_error(executor, "runtime error");
        return;
    }

    /* Attach source line for the rust-style snippet block (`N | ... / ^~~~~`).
     * Skipped for SOURCE_LOC_UNKNOWN since there is no line to look up. */
    if (SOURCE_LOC_VALID(loc)) {
        char *src_line = executor_get_source_line(executor, loc.line);
        if (src_line) {
            shell_error_set_source_line(error, src_line, loc.column,
                                        loc.column + loc.length);
            free(src_line);
        }
    }

    // Add context stack to error
    for (size_t i = 0;
         i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
        if (executor->context_stack[i]) {
            shell_error_push_context(error, "%s", executor->context_stack[i]);
        }
    }

    // Display the error immediately
    shell_error_display(error, stderr, isatty(STDERR_FILENO));

    /* Set legacy error state for compatibility - use NULL since error was
     * already displayed */
    executor->has_error = true;
    executor->error_message = NULL; // Already displayed via structured system

    shell_error_free(error);
}

/**
 * @brief Add a structured error and display it
 *
 * Convenience wrapper that creates an error with current node location.
 * For runtime errors, this is equivalent to executor_error_report but
 * provides a simpler API when you don't have a source_location_t ready.
 */
void executor_error_add(executor_t *executor, shell_error_code_t code,
                        source_location_t loc, const char *fmt, ...) {
    if (!executor) {
        return;
    }

    // Create the error
    va_list args;
    va_start(args, fmt);
    shell_error_t *error =
        shell_error_createv(code, SHELL_SEVERITY_ERROR, loc, fmt, args);
    va_end(args);

    if (!error) {
        // Fallback to legacy error system
        set_executor_error(executor, "runtime error");
        return;
    }

    /* Attach source line for the rust-style snippet block (`N | ... / ^~~~~`).
     * Skipped for SOURCE_LOC_UNKNOWN since there is no line to look up. */
    if (SOURCE_LOC_VALID(loc)) {
        char *src_line = executor_get_source_line(executor, loc.line);
        if (src_line) {
            shell_error_set_source_line(error, src_line, loc.column,
                                        loc.column + loc.length);
            free(src_line);
        }
    }

    // Add context stack to error
    for (size_t i = 0;
         i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
        if (executor->context_stack[i]) {
            shell_error_push_context(error, "%s", executor->context_stack[i]);
        }
    }

    // Display the error immediately
    shell_error_display(error, stderr, isatty(STDERR_FILENO));

    /* Set legacy error state for compatibility - use NULL since error was
     * already displayed */
    executor->has_error = true;
    executor->error_message = NULL; // Already displayed via structured system

    shell_error_free(error);
}

/**
 * @brief Report command-not-found error with autocorrect suggestions
 *
 * Creates a structured error for command not found, and if autocorrect
 * is available, adds "did you mean?" suggestions from builtins.
 *
 * Note: For performance, we only search builtins for suggestions here.
 * The full PATH search used by interactive autocorrect is too slow for
 * synchronous error reporting (scans thousands of executables).
 *
 * @param executor Executor context
 * @param command The command that was not found
 * @param loc Source location of the command
 */
static void report_command_not_found(executor_t *executor, const char *command,
                                     source_location_t loc) {
    if (!executor || !command) {
        return;
    }

    // Create the error
    shell_error_t *error =
        shell_error_create(SHELL_ERR_COMMAND_NOT_FOUND, SHELL_SEVERITY_ERROR,
                           loc, "%s: command not found", command);
    if (!error) {
        // Fallback to simple error message
        fprintf(stderr, "lush: %s: command not found\n", command);
        return;
    }

    // Attach source line for the rust-style snippet block
    if (SOURCE_LOC_VALID(loc)) {
        char *src_line = executor_get_source_line(executor, loc.line);
        if (src_line) {
            shell_error_set_source_line(error, src_line, loc.column,
                                        loc.column + loc.length);
            free(src_line);
        }
    }

    // Add context stack to error
    for (size_t i = 0;
         i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
        if (executor->context_stack[i]) {
            shell_error_push_context(error, "%s", executor->context_stack[i]);
        }
    }

    /* Get suggestions from autocorrect (builtins + PATH with fast pre-filter)
     */
    correction_results_t results;
    int num_suggestions =
        autocorrect_find_suggestions(executor, command, &results);

    if (num_suggestions > 0) {
        // Build suggestion string
        char suggestion[256];
        if (num_suggestions == 1) {
            snprintf(suggestion, sizeof(suggestion), "did you mean '%s'?",
                     results.suggestions[0].command);
        } else {
            int show_count = num_suggestions > 3 ? 3 : num_suggestions;
            int pos = snprintf(suggestion, sizeof(suggestion), "did you mean ");
            for (int i = 0;
                 i < show_count && pos < (int)sizeof(suggestion) - 20; i++) {
                if (i > 0) {
                    if (i == show_count - 1) {
                        pos += snprintf(suggestion + pos,
                                        sizeof(suggestion) - pos, ", or ");
                    } else {
                        pos += snprintf(suggestion + pos,
                                        sizeof(suggestion) - pos, ", ");
                    }
                }
                pos += snprintf(suggestion + pos, sizeof(suggestion) - pos,
                                "'%s'", results.suggestions[i].command);
            }
            snprintf(suggestion + pos, sizeof(suggestion) - pos, "?");
        }
        shell_error_set_suggestion(error, suggestion);
    }

    /* Always free autocorrect results (original_command is allocated even with
     * no suggestions) */
    autocorrect_free_results(&results);

    // Display the error
    shell_error_display(error, stderr, isatty(STDERR_FILENO));

    // Set legacy error state
    executor->has_error = true;
    executor->error_message = NULL;

    shell_error_free(error);
}

/**
 * @brief Execute an AST node
 *
 * Main execution entry point. Handles both single commands and
 * command sequences (commands with siblings).
 *
 * @param executor Executor context
 * @param ast Abstract syntax tree to execute
 * @return Exit status of executed command
 */
int executor_execute(executor_t *executor, node_t *ast) {
    if (!executor || !ast) {
        return 1;
    }

    executor->has_error = false;
    executor->error_message = NULL;

    // Check if this is a command sequence (has siblings) or a single command
    if (ast->next_sibling) {
        // This is a command sequence, execute all siblings
        int result = execute_command_list(executor, ast);
        executor->exit_status = result;
        return result;
    } else {
        // Single command, execute normally
        int result = execute_node(executor, ast);
        executor->exit_status = result;
        return result;
    }
}

/**
 * @brief Parse and execute a command line string
 *
 * Parses the input string into an AST and executes it. Handles syntax
 * check mode (set -n) where commands are parsed but not executed.
 *
 * @param executor Executor context
 * @param input Shell command string to parse and execute
 * @return Exit status of executed command, or error code
 */
int executor_execute_command_line(executor_t *executor, const char *input,
                                  size_t starting_line) {
    if (!executor || !input) {
        return 1;
    }
    if (starting_line == 0) {
        starting_line = 1;
    }

    /* Stash source text for the structured-error system. Any error
     * site emitting via shell_error_create() can pull the actual
     * source line via executor_get_source_line() and attach it via
     * shell_error_set_source_line() to produce the full rust-style
     * snippet block (`N | source line / ^~~~~`). The stash is set to
     * the input we're about to parse and restored on exit so re-entrant
     * dispatch (e.g. command substitution running its own batch
     * recursively) doesn't leak text from one batch into another. */
    const char *saved_source_text = executor->source_text;
    size_t saved_source_starting_line = executor->source_starting_line;
    executor->source_text = input;
    executor->source_starting_line = starting_line;

    // Preprocess input to handle line continuation (backslash-newline)
    // This is needed for -c option where the string comes directly without
    // going through get_input_complete() which normally handles this
    char *processed_input = NULL;
    const char *parse_input = input;

    if (strchr(input, '\\') != NULL) {
        // May contain line continuations - preprocess
        size_t len = strlen(input);
        processed_input = malloc(len + 1);
        if (processed_input) {
            size_t j = 0;
            for (size_t i = 0; i < len; i++) {
                if (input[i] == '\\' && i + 1 < len && input[i + 1] == '\n') {
                    // Skip backslash-newline (line continuation)
                    i++; // Skip the newline too (loop will increment past
                         // backslash)
                } else {
                    processed_input[j++] = input[i];
                }
            }
            processed_input[j] = '\0';
            parse_input = processed_input;
        }
    }

    // Parse the input, using script filename if executing a script
    const char *source_name = executor->current_script_file
                                  ? executor->current_script_file
                                  : "<stdin>";
    int result = 0;
    parser_t *parser =
        parser_new_with_source(parse_input, source_name, starting_line);
    if (!parser) {
        set_executor_error(executor, "Failed to create parser");
        result = 1;
        goto cleanup;
    }

    node_t *ast = parser_parse(parser);

    // Check syntax check mode (set -n) - parse but don't execute
    if (shell_opts.syntax_check) {
        if (parser_has_error(parser)) {
            // Display structured errors if available
            parser_display_errors(parser, stderr, isatty(STDERR_FILENO));
            /* Set executor error for legacy compatibility (may be NULL with new
             * system) */
            const char *legacy_err = parser_error(parser);
            if (legacy_err) {
                set_executor_error(executor, legacy_err);
            }
            result = 2; // Syntax error
        } else {
            result = 0; // Syntax check successful
        }
        parser_free(parser);
        goto cleanup;
    }

    if (parser_has_error(parser)) {
        // Display structured errors if available
        parser_display_errors(parser, stderr, isatty(STDERR_FILENO));
        /* Set executor error for legacy compatibility (may be NULL with new
         * system) */
        const char *legacy_err = parser_error(parser);
        if (legacy_err) {
            set_executor_error(executor, legacy_err);
        }
        parser_free(parser);
        result = 1;
        goto cleanup;
    }

    if (!ast) {
        parser_free(parser);
        result = 0; // Empty command
        goto cleanup;
    }

    result = executor_execute(executor, ast);

    free_node_tree(ast);
    parser_free(parser);

cleanup:
    free(processed_input);
    /* Restore previous source-text stash so re-entrant batches don't
     * leak text from one batch into another. */
    executor->source_text = saved_source_text;
    executor->source_starting_line = saved_source_starting_line;
    return result;
}

char *executor_get_source_line(executor_t *executor, size_t file_line) {
    if (!executor || !executor->source_text || file_line == 0 ||
        executor->source_starting_line == 0 ||
        file_line < executor->source_starting_line) {
        return NULL;
    }

    /* Translate file-relative line number into batch-relative line
     * number. The batch's first line is executor->source_starting_line
     * in the original file; that's batch line 1. */
    size_t batch_line = file_line - executor->source_starting_line + 1;

    const char *src = executor->source_text;
    size_t current_line = 1;
    size_t line_start = 0;
    size_t i = 0;

    // Walk to the start of the requested batch-relative line.
    while (src[i] != '\0' && current_line < batch_line) {
        if (src[i] == '\n') {
            current_line++;
            line_start = i + 1;
        }
        i++;
    }
    if (current_line != batch_line) {
        return NULL; // Requested line is past end of batch text.
    }

    // Find the end of the line (newline or end-of-string).
    size_t line_end = line_start;
    while (src[line_end] != '\0' && src[line_end] != '\n') {
        line_end++;
    }

    size_t line_len = line_end - line_start;
    char *result = malloc(line_len + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, src + line_start, line_len);
    result[line_len] = '\0';
    return result;
}

/**
 * @brief Core node execution dispatcher
 *
 * Dispatches execution to the appropriate handler based on node type.
 * Handles debug tracing, breakpoints, and loop control. This is the
 * central execution function that routes all AST node types.
 *
 * @param executor Executor context
 * @param node AST node to execute
 * @return Exit status of the executed node
 */
static int execute_node(executor_t *executor, node_t *node) {
    if (!node) {
        return 0;
    }

    // Check syntax check mode (set -n) - don't execute any nodes
    if (shell_opts.syntax_check) {
        return 0; // Syntax check mode - don't execute
    }

    // Enhanced debug tracing
    if (executor->debug) {
        printf("DEBUG: Executing node type %d\n", node->type);
        if (node->val.str) {
            printf("DEBUG: Node value: '%s'\n", node->val.str);
        }
    }

    // Advanced debug system integration
    DEBUG_TRACE_NODE(node, __FILE__, __LINE__);

    // Check for a breakpoint at this node's source line. node->loc.line
    // is the absolute 1-based source line (0 when the parser recorded no
    // location); debug_check_breakpoint ignores line 0, so an unlocated
    // node is skipped and its executable children carry the real line.
    if (executor->in_script_execution && executor->current_script_file) {
        DEBUG_BREAKPOINT_CHECK(executor->current_script_file,
                               (int)node->loc.line);
    }

    switch (node->type) {
    case NODE_COMMAND: {
        int result = execute_command(executor, node);
        // Clean up any process substitution fds after command execution
        cleanup_procsub_fds(executor);
        return result;
    }
    case NODE_PIPE:
        return execute_pipeline(executor, node);
    case NODE_IF:
        return execute_if(executor, node);
    case NODE_WHILE:
        return execute_while(executor, node);
    case NODE_UNTIL:
        return execute_until(executor, node);
    case NODE_REPEAT:
        return execute_repeat(executor, node);
    case NODE_FOR:
        return execute_for(executor, node);
    case NODE_FOR_ARITH:
        return execute_for_arith(executor, node);
    case NODE_SELECT:
        return execute_select(executor, node);
    case NODE_TIME:
        return execute_time(executor, node);
    case NODE_COPROC:
        return execute_coproc(executor, node);
    case NODE_CASE:
        return execute_case(executor, node);
    case NODE_LOGICAL_AND:
        return execute_logical_and(executor, node);
    case NODE_LOGICAL_OR:
        return execute_logical_or(executor, node);
    case NODE_FUNCTION:
        return execute_function_definition(executor, node);
    case NODE_BRACE_GROUP:
        return execute_brace_group(executor, node);
    case NODE_SUBSHELL:
        return execute_subshell(executor, node);
    case NODE_COMMAND_LIST:
        return execute_command_list(executor, node);
    case NODE_BACKGROUND:
        return executor_execute_background(executor, node);
    case NODE_NEGATE:
        return execute_negate(executor, node);
    case NODE_VAR:
        // Variable nodes are typically handled by their parent
        return 0;
    case NODE_ARITH_CMD:
        return execute_arithmetic_command(executor, node);
    case NODE_EXTENDED_TEST:
        return execute_extended_test(executor, node);
    case NODE_ARRAY_ASSIGN:
        return execute_array_assignment(executor, node);
    case NODE_ARRAY_APPEND:
        return execute_array_append(executor, node);
    case NODE_ARRAY_LITERAL:
        // Array literals are typically handled by NODE_ARRAY_ASSIGN
        return 0;
    case NODE_ARRAY_ACCESS:
        // Array access is typically handled during variable expansion
        return 0;
    case NODE_ANON_FUNCTION:
        return execute_anonymous_function(executor, node);
    case NODE_FN_DECL:
        return execute_typed_fn_decl(executor, node);
    case NODE_FN_CALL:
        return execute_typed_fn_call(executor, node);
    case NODE_FN_RETURN:
        return execute_typed_fn_return(executor, node);
    case NODE_LET_FN:
        return execute_typed_let_fn(executor, node);
    default:
        if (executor->debug) {
            printf("DEBUG: Unknown node type %d, skipping\n", node->type);
        }
        return 0;
    }
}

/**
 * @brief Execute a sequence of commands
 *
 * Executes commands in sequence, handling loop control (break/continue)
 * and exit-on-error mode (set -e). Updates exit status after each command.
 *
 * @param executor Executor context
 * @param list First node in the command list
 * @return Exit status of last executed command
 */
static int execute_command_list(executor_t *executor, node_t *list) {
    if (!list) {
        return 0;
    }

    int last_result = 0;
    node_t *current;

    // Handle NODE_COMMAND_LIST with children
    if (list->type == NODE_COMMAND_LIST) {
        current = list->first_child;
    } else {
        // Handle legacy case where list is the first command in a sibling chain
        current = list;
    }

    while (current) {
        // Check syntax check mode (set -n) - don't execute commands
        if (shell_opts.syntax_check) {
            return 0; // Syntax check mode - don't execute
        }

        /* Bash-style DEBUG pseudo-signal: fires BEFORE every command.
         * fire_debug_trap itself gates on functrace + function scope. */
        fire_debug_trap();

        last_result = execute_node(executor, current);

        // Check for loop control (break/continue) - stop executing list
        if (executor->loop_control != LOOP_NORMAL) {
            return last_result;
        }

        /* POSIX-required shell abort (set by executor_request_posix_exit
         * from sites like ${var:?word}). Subsequent statements in this
         * batch must not run; the REPL terminates the shell with
         * shell_exit_status after we return. */
        if (executor->shell_exit_requested) {
            return executor->shell_exit_status;
        }

        /* `exit` builtin requested shell termination. bin_exit sets the
         * global exit_flag (the REPL polls it to leave its top-level
         * loop) and stashes the chosen status in last_exit_status. The
         * REPL alone is not enough: when a script is run as a single
         * parsed AST, the whole tree executes inside ONE call to
         * execute_command_list, so without this check every statement
         * after `exit` still runs (real_world/posix/101 fell through
         * `exit $?` and ran trailing `rm -f` / `exit 0`). Honor it
         * here so `exit` inside any nested construct - case arm, if
         * body, brace group, loop - immediately propagates up. */
        if (exit_flag) {
            return last_exit_status;
        }

        // Flush stdout to prevent pipeline from picking up residual output
        fflush(stdout);

        // Update exit status after each command in the sequence
        set_exit_status(last_result);

        if (executor->debug) {
            printf("DEBUG: Command result: %d\n", last_result);
        }

        /* Bash-style ERR pseudo-signal: fires after a non-zero command
         * exit, before set -e gets a chance to abort. */
        if (last_result != 0) {
            fire_err_trap();
        }

        // Handle set -e (exit_on_error): exit if command failed
        if (shell_opts.exit_on_error && last_result != 0) {
            executor->exit_status = last_result;
            return last_result;
        }

        current = current->next_sibling;
    }

    return last_result;
}

/**
 * @brief Execute a simple command node
 *
 * Handles command execution including:
 * - Variable assignments
 * - Parameter expansions
 * - Alias expansion
 * - Builtin commands
 * - Function calls
 * - External commands with redirections
 * - Auto-cd when enabled
 * - Command auto-correction
 *
 * @param executor Executor context
 * @param command Command node to execute
 * @return Exit status of the command
 */
static int execute_command_dispatch(executor_t *executor, node_t *command);

/* ============================================================================
 * POSIX cmd_prefix: temporary-environment assignments
 * ============================================================================
 * A simple command may carry NODE_ASSIGN children (`VAR=value` parsed as a
 * cmd_prefix). For a command word these are transient (visible to the
 * command, including a function body, but restored afterward); bash-default
 * and zsh both treat them transient even for special builtins, so lush's
 * default follows that consensus. For a pure prefix (no command word) the
 * assignments persist, matching `VAR=value` / `VAR=value >file`.
 */

typedef struct {
    char *name;
    bool existed;
    char *old_value; /**< strdup of prior value when existed */
    bool was_exported;
} prefix_save_t;

/* Extract the variable name from a NODE_ASSIGN string ("name=v" or
 * "name+=v"). Returns a malloc'd name, or NULL. */
static char *prefix_assign_name(const char *s) {
    if (!s) {
        return NULL;
    }
    const char *eq = strchr(s, '=');
    if (!eq || eq == s) {
        return NULL;
    }
    size_t n = (size_t)(eq - s);
    if (n > 0 && s[n - 1] == '+') {
        n--; // += append
    }
    if (n == 0) {
        return NULL;
    }
    char *name = malloc(n + 1);
    if (!name) {
        return NULL;
    }
    memcpy(name, s, n);
    name[n] = '\0';
    return name;
}

/* Apply every NODE_ASSIGN prefix child of `command`. When `transient`,
 * prior variable state is snapshotted into *out_saves (count *out_n) so
 * prefix_restore_and_free() can undo it; otherwise the assignments
 * persist and *out_saves is left NULL. Each assignment is applied via
 * execute_assignment() (so +=, declare -i arithmetic, and value
 * expansion all behave identically to a standalone assignment) and then
 * exported, so a forked external child and env-reading builtins observe
 * it. Returns 0 on success, non-zero on the first failing assignment. */
static int prefix_apply(executor_t *executor, node_t *command, bool transient,
                        prefix_save_t **out_saves, int *out_n) {
    if (out_saves) {
        *out_saves = NULL;
    }
    if (out_n) {
        *out_n = 0;
    }

    int count = 0;
    for (node_t *c = command->first_child; c; c = c->next_sibling) {
        if (c->type == NODE_ASSIGN) {
            count++;
        }
    }
    if (count == 0) {
        return 0;
    }

    prefix_save_t *saves = NULL;
    if (transient) {
        saves = calloc((size_t)count, sizeof(*saves));
        if (!saves) {
            return 1;
        }
    }

    symtable_manager_t *mgr = symtable_get_global_manager();
    int idx = 0;
    for (node_t *c = command->first_child; c; c = c->next_sibling) {
        if (c->type != NODE_ASSIGN || !c->val.str) {
            continue;
        }
        char *name = prefix_assign_name(c->val.str);
        if (!name) {
            continue;
        }

        if (transient) {
            bool existed = mgr ? symtable_var_exists(mgr, name) : false;
            char *old = existed ? symtable_get_global(name) : NULL;
            symvar_flags_t fl =
                mgr ? symtable_get_flags(mgr, name) : SYMVAR_NONE;
            saves[idx].name = name; // ownership moves to saves
            saves[idx].existed = existed;
            saves[idx].old_value = old;
            saves[idx].was_exported = (fl & SYMVAR_EXPORTED) != 0;
        }

        int st = execute_assignment(executor, c->val.str, command->loc);
        if (st != 0) {
            if (!transient) {
                free(name);
            }
            /* On failure idx-th save (if transient) holds this name;
             * include it so restore undoes any partial apply. */
            if (transient) {
                idx++;
                *out_saves = saves;
                *out_n = idx;
            }
            return st;
        }
        symtable_export_global(name); // setenv: child/env-readers see it

        if (transient) {
            idx++;
        } else {
            free(name);
        }
    }

    if (transient) {
        *out_saves = saves;
        *out_n = idx;
    }
    return 0;
}

// Undo a transient prefix_apply() and free the snapshot array.
static void prefix_restore_and_free(prefix_save_t *saves, int n) {
    if (!saves) {
        return;
    }
    for (int i = n - 1; i >= 0; i--) {
        const char *name = saves[i].name;
        if (!name) {
            continue;
        }
        if (saves[i].existed) {
            symtable_set_global(name,
                                saves[i].old_value ? saves[i].old_value : "");
            if (saves[i].was_exported) {
                setenv(name, saves[i].old_value ? saves[i].old_value : "", 1);
            } else {
                unsetenv(name);
            }
        } else {
            symtable_unset_global(name);
            unsetenv(name);
        }
        free(saves[i].name);
        free(saves[i].old_value);
    }
    free(saves);
}

static int execute_command_inner(executor_t *executor, node_t *command);

/* Public execute_command(): thin wrapper that stashes the command's
 * source location on the executor so the expansion subsystem (and
 * any path that can't reach an AST node directly) can surface
 * `--> file:line:col` via executor_current_loc(). The inner function
 * holds the actual ~500-line dispatch body; the save/restore around
 * it lets nested execute_command calls (function bodies, sourced
 * scripts, etc.) update active_loc without clobbering an enclosing
 * caller's location on the way out. */
static int execute_command(executor_t *executor, node_t *command) {
    if (!command || command->type != NODE_COMMAND) {
        return 1;
    }
    source_location_t saved_active_loc = executor->active_loc;
    executor->active_loc = command->loc;
    int result = execute_command_inner(executor, command);
    executor->active_loc = saved_active_loc;
    return result;
}

static int execute_command_inner(executor_t *executor, node_t *command) {
    // command non-null and NODE_COMMAND is guaranteed by the wrapper.

    // Reset expansion error flags for this command
    executor->expansion_error = false;
    executor->expansion_exit_status = 0;

    // Check for assignment (legacy lone-assignment shape: val.str is
    // "var=value" with no NODE_ASSIGN children).
    if (command->val.str && is_assignment(command->val.str)) {
        return execute_assignment(executor, command->val.str, command->loc);
    }

    // POSIX cmd_prefix: NODE_ASSIGN children precede the command word.
    int n_prefix = 0;
    for (node_t *c = command->first_child; c; c = c->next_sibling) {
        if (c->type == NODE_ASSIGN) {
            n_prefix++;
        }
    }

    if (n_prefix > 0 && command->val.str == NULL) {
        // Pure prefix, no command word: `x=1`, `x=1 >file`. POSIX:
        // redirections are performed, then assignments persist.
        bool have_redir = count_redirections(command) > 0;
        redirection_state_t rs;
        if (have_redir) {
            save_file_descriptors(&rs);
            if (setup_redirections(executor, command) != 0) {
                restore_file_descriptors(&rs);
                return 1;
            }
        }
        int st =
            prefix_apply(executor, command, /*transient=*/false, NULL, NULL);
        if (have_redir) {
            restore_file_descriptors(&rs);
        }
        set_exit_status(st);
        return st;
    }

    if (n_prefix > 0) {
        // Command word present: apply prefix transiently, dispatch, then
        // restore. An external command forks (its child inherits the
        // exported vars via environ); the parent-side restore reverts the
        // shell's own view for all command kinds.
        prefix_save_t *saves = NULL;
        int nsaves = 0;
        int st = prefix_apply(executor, command, /*transient=*/true, &saves,
                              &nsaves);
        if (st != 0) {
            prefix_restore_and_free(saves, nsaves);
            set_exit_status(st);
            return st;
        }
        int r = execute_command_dispatch(executor, command);
        prefix_restore_and_free(saves, nsaves);
        return r;
    }

    return execute_command_dispatch(executor, command);
}

static int execute_command_dispatch(executor_t *executor, node_t *command) {

    // Note: Parameter expansions like ${CMD} in command position are handled
    // by build_argv_from_ast() which calls expand_if_needed() on the command
    // name. The expanded result becomes the command to execute, matching
    // bash/zsh behavior. Previously this code had an early-return that
    // discarded the expansion result without executing - that was a bug.

    // Check if command has redirections
    bool has_redirections = count_redirections(command) > 0;

    // Build argument vector (excluding redirection nodes)
    int argc;
    char **argv = build_argv_from_ast(executor, command, &argc);
    if (!argv || argc == 0) {
        return 1;
    }

    // Parser-internal array-literal sentinel (\x1F) housekeeping:
    // the parser prefixes any unquoted `name=(...)` argv element with
    // \x1F so assignment-aware builtins (local, declare, typeset,
    // readonly, export) can distinguish it from a quoted scalar.
    // External commands and non-assignment builtins must not see
    // that internal byte. Strip it from EVERY argv element when the
    // command is NOT one of the assignment-aware set; the targeted
    // builtins handle the sentinel themselves.
    if (argc > 0 && argv[0]) {
        const char *cmd = argv[0];
        bool sentinel_aware =
            (strcmp(cmd, "local") == 0 || strcmp(cmd, "declare") == 0 ||
             strcmp(cmd, "typeset") == 0 || strcmp(cmd, "readonly") == 0 ||
             strcmp(cmd, "export") == 0);
        if (!sentinel_aware) {
            for (int i = 0; i < argc; i++) {
                if (argv[i] && argv[i][0] == '\x1F') {
                    // Shift the string left by one to drop the prefix
                    // byte; the allocation came from build_argv_from_ast
                    // and is owned by us, so an in-place shift is safe.
                    memmove(argv[i], argv[i] + 1, strlen(argv[i]));
                }
            }
        }
    }

    // Privileged mode security check
    if (argc > 0 && !is_privileged_command_allowed(argv[0])) {
        executor_error_report(
            executor, SHELL_ERR_PERMISSION_DENIED, command->loc,
            "%s: restricted command in privileged mode", argv[0]);
        for (int i = 0; i < argc; i++) {
            free(argv[i]);
        }
        free(argv);
        return 1;
    }

    // Check for expansion errors (like arithmetic division by zero)
    if (executor->expansion_error) {
        // Free argv before returning
        for (int i = 0; i < argc; i++) {
            free(argv[i]);
        }
        free(argv);
        return executor->expansion_exit_status;
    }

    // Note: Parameter expansion arguments are already expanded by
    // build_argv_from_ast() via expand_if_needed(). The expanded values
    // are in argv and will be passed to the command. No need for
    // special handling here - let the command execute normally.

    // Check for stderr redirection pattern (2>/dev/null or 2> /dev/null)
    bool redirect_stderr = false;
    char **filtered_argv = NULL;
    int filtered_argc = 0;

    // Look for 2>/dev/null pattern in arguments
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "2>/dev/null") == 0) {
            redirect_stderr = true;
            break;
        } else if (i + 2 < argc && strcmp(argv[i], "2") == 0 &&
                   strcmp(argv[i + 1], ">") == 0 &&
                   strcmp(argv[i + 2], "/dev/null") == 0) {
            redirect_stderr = true;
            break;
        }
    }

    if (redirect_stderr) {
        // Create filtered argv without redirection tokens
        filtered_argv = malloc((argc + 1) * sizeof(char *));
        if (!filtered_argv) {
            // Free original argv and return error
            for (int i = 0; i < argc; i++) {
                free(argv[i]);
            }
            free(argv);
            return 1;
        }

        int j = 0;
        for (int i = 0; i < argc; i++) {
            if (strcmp(argv[i], "2>/dev/null") == 0) {
                // Skip this token
                continue;
            } else if (i + 2 < argc && strcmp(argv[i], "2") == 0 &&
                       strcmp(argv[i + 1], ">") == 0 &&
                       strcmp(argv[i + 2], "/dev/null") == 0) {
                // Skip these three tokens
                i += 2;
                continue;
            } else {
                filtered_argv[j] = strdup(argv[i]);
                j++;
            }
        }
        filtered_argv[j] = NULL;
        filtered_argc = j;
    } else {
        filtered_argv = argv;
        filtered_argc = argc;
    }

    if (executor->debug) {
        printf("DEBUG: Executing command: %s with %d args\n", filtered_argv[0],
               filtered_argc - 1);
        for (int i = 0; i < filtered_argc; i++) {
            printf("DEBUG: argv[%d] = '%s'\n", i, filtered_argv[i]);
        }
        if (redirect_stderr) {
            printf("DEBUG: stderr redirection enabled\n");
        }
    }

    // Check for alias expansion and rebuild argv if needed
    char *alias_expanded = lookup_alias(filtered_argv[0]);
    if (alias_expanded) {
        // Reconstruct original command for expansion
        char *original_command = NULL;
        size_t total_len = 1; // for null terminator

        for (int i = 0; i < filtered_argc; i++) {
            total_len += strlen(filtered_argv[i]) + (i > 0 ? 1 : 0);
        }

        original_command = malloc(total_len);
        if (original_command) {
            strcpy(original_command, filtered_argv[0]);
            for (int i = 1; i < filtered_argc; i++) {
                strcat(original_command, " ");
                strcat(original_command, filtered_argv[i]);
            }

            // Expand the full command line with recursive expansion
            char *recursive_expanded =
                expand_aliases_recursive(filtered_argv[0], 10); // max depth 10
            char *expanded_command = NULL;

            if (recursive_expanded) {
                // If recursive expansion succeeded, use it to build full
                // command
                if (filtered_argc > 1) {
                    // Add original arguments to recursively expanded command
                    size_t len = strlen(recursive_expanded) + 1;
                    for (int i = 1; i < filtered_argc; i++) {
                        len += strlen(filtered_argv[i]) + 1;
                    }
                    expanded_command = malloc(len);
                    if (expanded_command) {
                        strcpy(expanded_command, recursive_expanded);
                        for (int i = 1; i < filtered_argc; i++) {
                            strcat(expanded_command, " ");
                            strcat(expanded_command, filtered_argv[i]);
                        }
                    }
                } else {
                    expanded_command = strdup(recursive_expanded);
                }
                free(recursive_expanded);
            } else {
                // Fall back to simple first-word expansion
                expanded_command = expand_first_word_alias(original_command);
            }
            if (expanded_command &&
                strcmp(expanded_command, original_command) != 0) {
                // Create new argv array for expanded command
                char **new_argv =
                    malloc(256 * sizeof(char *)); // reasonable limit
                if (new_argv) {
                    // Tokenize expanded command into new argv
                    char *expanded_copy = strdup(expanded_command);
                    char *token = strtok(expanded_copy, " ");
                    int new_argc = 0;

                    while (token && new_argc < 255) {
                        new_argv[new_argc] = strdup(token);
                        new_argc++;
                        token = strtok(NULL, " ");
                    }
                    new_argv[new_argc] = NULL;

                    // Only replace if we successfully created the new argv
                    if (new_argc > 0) {
                        // Free old argv only if it's not the same as original
                        // argv
                        if (filtered_argv != argv) {
                            for (int i = 0; i < filtered_argc; i++) {
                                free(filtered_argv[i]);
                            }
                            free(filtered_argv);
                        }

                        filtered_argv = new_argv;
                        filtered_argc = new_argc;
                    } else {
                        // Failed to create new argv, clean up
                        free(new_argv);
                    }

                    free(expanded_copy);
                }
            }

            free(expanded_command);
            free(original_command);
        }
    }

    int result;

    // Get debug context for profiling and frame management
    const char *command_name = filtered_argv[0];

    // Push debug frame and start profiling for this command
    if (g_debug_context && g_debug_context->enabled) {
        debug_push_frame(g_debug_context, command_name, NULL, 0);

        if (g_debug_context->profile_enabled) {
            g_debug_context->total_commands++;
            debug_profile_function_enter(g_debug_context, command_name);
        }
    }

    if (is_function_defined(executor, filtered_argv[0])) {
        result =
            execute_function_call(executor, filtered_argv[0], filtered_argv,
                                  filtered_argc, command->loc);
    } else if (is_builtin_command(filtered_argv[0])) {
        // For builtin commands with stdout redirections, check if stdout is
        // captured. Only fork for "pure" builtins that don't modify shell
        // state.
        if (has_redirections && has_stdout_redirections(command) &&
            is_stdout_captured() && builtin_can_fork(filtered_argv[0])) {
            // When stdout is captured externally and command has stdout
            // redirections, use child process to avoid file descriptor
            // interference (only for pure builtins)
            result = execute_builtin_with_captured_stdout(
                executor, filtered_argv, command);
        } else {
            // Normal case: handle redirections in parent process
            redirection_state_t redir_state;
            if (has_redirections) {
                save_file_descriptors(&redir_state);
                int redir_result = setup_redirections(executor, command);
                if (redir_result != 0) {
                    restore_file_descriptors(&redir_state);
                    // Free argv
                    for (int i = 0; i < argc; i++) {
                        free(argv[i]);
                    }
                    free(argv);
                    // Free filtered argv if separately allocated
                    if (filtered_argv != NULL && filtered_argv != argv) {
                        for (int i = 0; i < filtered_argc; i++) {
                            free(filtered_argv[i]);
                        }
                        free(filtered_argv);
                    }
                    return redir_result;
                }
            }

            result =
                execute_builtin_command(executor, filtered_argv, command->loc);

            // Flush output streams after builtin execution
            // This ensures output appears immediately, especially under
            // valgrind/piping
            fflush(stdout);
            fflush(stderr);

            // Restore file descriptors after builtin execution
            // EXCEPT for 'exec' builtin - its redirections are permanent
            if (has_redirections &&
                !(filtered_argv[0] && strcmp(filtered_argv[0], "exec") == 0)) {
                restore_file_descriptors(&redir_state);
            }
        }
    } else {
        // Check auto_cd before attempting external command execution
        int auto_cd_enabled = symtable_get_global_int("AUTO_CD", 0);
        if (auto_cd_enabled && argc > 0 && argv[0]) {
            struct stat st;
            // Check if the command is actually a directory
            if (stat(argv[0], &st) == 0 && S_ISDIR(st.st_mode)) {
                /**
                 * @brief Save old directory for event firing
                 *
                 * Required by Spec 26 shell event hub to notify handlers
                 * of directory change with both old and new paths.
                 */
                char *old_pwd = getcwd(NULL, 0);

                // Auto-cd to the directory
                if (chdir(argv[0]) == 0) {
                    // Successfully changed directory, update PWD
                    char *new_pwd = getcwd(NULL, 0);
                    if (new_pwd) {
                        symtable_set_global("PWD", new_pwd);

                        /**
                         * @brief Fire directory changed event for LLE shell
                         * integration
                         *
                         * This notifies the prompt composer which:
                         * - Refreshes context.cwd
                         * - Invalidates all segment caches
                         * - Sets needs_regeneration flag
                         * - Triggers async git status refresh
                         */
                        lle_fire_directory_changed(old_pwd, new_pwd);

                        free(new_pwd);
                    }
                    result = 0; // Success
                } else {
                    // Failed to change directory, show error
                    shell_error_t *error = shell_error_create(
                        SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR,
                        executor_current_loc(executor), "cd: %s: %s", argv[0],
                        strerror(errno));
                    shell_error_display(error, stderr, isatty(STDERR_FILENO));
                    shell_error_free(error);
                    result = 1;
                }

                if (old_pwd) {
                    free(old_pwd);
                }
            } else {
                // Not a directory, proceed with normal command execution
                goto normal_execution;
            }
        } else {
        // Auto-cd disabled, proceed with normal command execution
        normal_execution:
            // Check if command exists first, offer auto-correction if not
            // Only do interactive autocorrect if:
            // 1. spell_correction is enabled
            // 2. autocorrect is enabled
            // 3. interactive prompts are enabled (otherwise no point in
            // searching)
            // 4. stdin is a tty (user can actually respond)
            if (config.spell_correction && autocorrect_is_enabled() &&
                config.autocorrect_interactive && isatty(STDIN_FILENO)) {
                // First, check if the command actually exists
                if (!autocorrect_command_exists(executor, filtered_argv[0])) {
                    // Command doesn't exist, try auto-correction
                    correction_results_t correction_results;
                    int suggestions = autocorrect_find_suggestions(
                        executor, filtered_argv[0], &correction_results);

                    if (suggestions > 0) {
                        char selected_command[MAX_COMMAND_LENGTH];
                        if (autocorrect_prompt_user(&correction_results,
                                                    selected_command)) {
                            // User selected a correction, replace the command
                            free(filtered_argv[0]);
                            filtered_argv[0] = strdup(selected_command);

                            // Learn the corrected command
                            autocorrect_learn_command(selected_command);

                            // Re-check if it's a builtin or function after
                            // correction
                            if (is_builtin_command(filtered_argv[0])) {
                                result = execute_builtin_command(
                                    executor, filtered_argv, command->loc);
                                fflush(stdout);
                                fflush(stderr);
                            } else if (is_function_defined(executor,
                                                           filtered_argv[0])) {
                                result = execute_function_call(
                                    executor, filtered_argv[0], filtered_argv,
                                    filtered_argc, command->loc);
                            } else {
                                // Execute the corrected external command
                                result = execute_external_command_with_setup(
                                    executor, filtered_argv, redirect_stderr,
                                    command);
                            }
                        } else {
                            // User declined correction, show original error
                            result = 127; // Command not found
                        }
                    } else {
                        // No suggestions or interactive prompts disabled
                        result = execute_external_command_with_setup(
                            executor, filtered_argv, redirect_stderr, command);
                    }

                    // Clean up correction results
                    autocorrect_free_results(&correction_results);
                } else {
                    // Command exists, execute normally
                    result = execute_external_command_with_setup(
                        executor, filtered_argv, redirect_stderr, command);
                }
            } else {
                // Auto-correction disabled, execute normally
                result = execute_external_command_with_setup(
                    executor, filtered_argv, redirect_stderr, command);
            }
        }
    }

    // Free argv
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);

    // Free filtered argv if it was separately allocated (from redirect or alias
    // expansion)
    if (filtered_argv != NULL && filtered_argv != argv) {
        for (int i = 0; i < filtered_argc; i++) {
            free(filtered_argv[i]);
        }
        free(filtered_argv);
    }

    // End profiling and pop debug frame for this command
    if (g_debug_context && g_debug_context->enabled) {
        if (g_debug_context->profile_enabled) {
            debug_profile_function_exit(g_debug_context, command_name);
        }
        debug_pop_frame(g_debug_context);
    }

    // Update exit status for $? variable
    set_exit_status(result);

    // Profile function exit
    DEBUG_PROFILE_EXIT("execute_command");

    return result;
}

/**
 * @brief Flatten a right-recursive NODE_PIPE chain into per-stage arrays.
 *
 * The parser builds `a | b | c` as NODE_PIPE(a, NODE_PIPE(b, c)). This walks
 * that chain, collecting leaf stage nodes and the |& bit between consecutive
 * stages. The caller owns the output arrays.
 *
 * @param pipeline           Root NODE_PIPE.
 * @param stages_out         Receives N stage nodes (not owned, alias into AST).
 * @param stderr_to_next_out Receives N-1 booleans; entry i is true iff the
 *                           junction between stage i and i+1 was `|&`.
 * @param count_out          Receives N, the total stage count.
 * @param capacity           Slots available in stages_out / count tracking.
 * @return true on success, false if capacity exceeded.
 */
static bool flatten_pipeline_chain(node_t *pipeline, node_t **stages_out,
                                   bool *stderr_to_next_out, size_t *count_out,
                                   size_t capacity) {
    size_t n = 0;
    node_t *cur = pipeline;
    while (cur && cur->type == NODE_PIPE) {
        node_t *left = cur->first_child;
        node_t *right = left ? left->next_sibling : NULL;
        if (!left || !right) {
            return false;
        }
        if (n >= capacity) {
            return false;
        }
        stages_out[n] = left;
        bool pipe_stderr = (cur->val_type == VAL_SINT && cur->val.sint == 1);
        stderr_to_next_out[n] = pipe_stderr;
        n++;
        cur = right;
    }
    if (n >= capacity) {
        return false;
    }
    stages_out[n++] = cur;
    *count_out = n;
    return true;
}

/**
 * @brief Execute a pipeline of N commands.
 *
 * Flattens the right-recursive NODE_PIPE chain to a linear list of N stages,
 * then forks N children connected by N-1 pipes. Returns the overall pipeline
 * exit status, applying pipefail when enabled.
 *
 * @param executor Executor context
 * @param pipeline Pipeline node containing commands
 * @return Exit status (last command's status, or rightmost non-zero with
 *         pipefail)
 */
static int execute_pipeline(executor_t *executor, node_t *pipeline) {
    if (!pipeline || pipeline->type != NODE_PIPE) {
        return 1;
    }

    executor_push_context(executor, pipeline->loc, "in pipeline");

    enum { MAX_PIPELINE_STAGES = 256 };
    node_t *stages[MAX_PIPELINE_STAGES];
    bool stderr_to_next[MAX_PIPELINE_STAGES];
    size_t nstages = 0;

    if (!flatten_pipeline_chain(pipeline, stages, stderr_to_next, &nstages,
                                MAX_PIPELINE_STAGES) ||
        nstages < 2) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           pipeline->loc, "malformed pipeline");
        executor_pop_context(executor);
        return 1;
    }

    // N-1 pipes connect N stages. pipes[i] connects stage i (writer) to stage
    // i+1 (reader).
    size_t npipes = nstages - 1;
    int (*pipes)[2] = calloc(npipes, sizeof(*pipes));
    if (!pipes) {
        executor_error_add(executor, SHELL_ERR_PIPE_FAILED, pipeline->loc,
                           "pipeline allocation failed");
        executor_pop_context(executor);
        return 1;
    }

    for (size_t i = 0; i < npipes; i++) {
        if (pipe(pipes[i]) == -1) {
            for (size_t j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            free(pipes);
            executor_error_add(executor, SHELL_ERR_PIPE_FAILED, pipeline->loc,
                               "failed to create pipe: %s", strerror(errno));
            executor_pop_context(executor);
            return 1;
        }
    }

    pid_t *pids = calloc(nstages, sizeof(pid_t));
    if (!pids) {
        for (size_t i = 0; i < npipes; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        free(pipes);
        executor_error_add(executor, SHELL_ERR_PIPE_FAILED, pipeline->loc,
                           "pipeline allocation failed");
        executor_pop_context(executor);
        return 1;
    }

    for (size_t i = 0; i < nstages; i++) {
        pid_t pid = lush_fork();
        if (pid == -1) {
            executor_error_add(executor, SHELL_ERR_FORK_FAILED, pipeline->loc,
                               "failed to fork for pipeline: %s",
                               strerror(errno));
            // Close all pipes and reap any children already forked.
            for (size_t j = 0; j < npipes; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            for (size_t j = 0; j < i; j++) {
                while (waitpid(pids[j], NULL, 0) == -1 && errno == EINTR)
                    ;
            }
            free(pids);
            free(pipes);
            executor_pop_context(executor);
            return 1;
        }

        if (pid == 0) {
            // Child: wire stdin from the previous pipe and stdout (plus stderr
            // if |&) to the next.
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < npipes) {
                dup2(pipes[i][1], STDOUT_FILENO);
                if (stderr_to_next[i]) {
                    dup2(pipes[i][1], STDERR_FILENO);
                }
            }
            // Close every pipe fd; dup2 already preserved what we need.
            for (size_t j = 0; j < npipes; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            int result = execute_node(executor, stages[i]);
            fflush(stdout);
            fflush(stderr);
            subshell_cleanup();
            _exit(result);
        }

        pids[i] = pid;
    }

    // Parent: close all pipes so children see EOF as their stages exit.
    for (size_t i = 0; i < npipes; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    free(pipes);

    int *stage_exit = calloc(nstages, sizeof(int));
    if (!stage_exit) {
        // Reap children before giving up so we don't leak zombies.
        for (size_t i = 0; i < nstages; i++) {
            while (waitpid(pids[i], NULL, 0) == -1 && errno == EINTR)
                ;
        }
        free(pids);
        executor_pop_context(executor);
        return 1;
    }

    for (size_t i = 0; i < nstages; i++) {
        int status;
        while (waitpid(pids[i], &status, 0) == -1 && errno == EINTR)
            ;
        stage_exit[i] =
            WIFEXITED(status)
                ? WEXITSTATUS(status)
                : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
    }
    free(pids);

    int last_exit = stage_exit[nstages - 1];
    int pipefail_exit = 0;
    if (is_pipefail_enabled()) {
        for (size_t i = nstages; i-- > 0;) {
            if (stage_exit[i] != 0) {
                pipefail_exit = stage_exit[i];
                break;
            }
        }
    }

    // pipeline-diagnostic: queue a structured error for each non-zero stage so
    // tools can see exactly which junction failed without parsing exit codes
    // out of the pipefail collapse.  The overall pipeline exit becomes strict
    // (rightmost non-zero) under this mode regardless of pipefail.
    bool diag = is_pipeline_diagnostic_enabled();
    if (diag) {
        for (size_t i = 0; i < nstages; i++) {
            if (stage_exit[i] != 0) {
                executor_error_add(executor, SHELL_ERR_PIPELINE_STAGE_FAILED,
                                   stages[i]->loc,
                                   "pipeline stage %zu of %zu exited %d", i + 1,
                                   nstages, stage_exit[i]);
                if (pipefail_exit == 0) {
                    pipefail_exit = stage_exit[i];
                }
            }
        }
    }

    // Publish per-stage exit codes under both polyglot names so callers can
    // diagnose which stage failed.  Both arrays are 0-indexed; the data is
    // identical.
    {
        array_value_t *bash_arr = symtable_array_create(false);
        array_value_t *lush_arr = symtable_array_create(false);
        if (bash_arr && lush_arr) {
            for (size_t i = 0; i < nstages; i++) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", stage_exit[i]);
                symtable_array_set_index(bash_arr, (int)i, buf);
                symtable_array_set_index(lush_arr, (int)i, buf);
            }
            if (symtable_set_array("PIPESTATUS", bash_arr) != 0) {
                symtable_array_free(bash_arr);
            }
            if (symtable_set_array("pipestatus", lush_arr) != 0) {
                symtable_array_free(lush_arr);
            }
        } else {
            if (bash_arr)
                symtable_array_free(bash_arr);
            if (lush_arr)
                symtable_array_free(lush_arr);
        }
    }

    free(stage_exit);
    executor_pop_context(executor);
    if (diag || is_pipefail_enabled()) {
        return pipefail_exit;
    }
    return last_exit;
}

/**
 * @brief Execute a chain of sibling commands
 *
 * Executes commands connected via next_sibling pointers.
 * Handles loop control and exit-on-error semantics.
 *
 * @param executor Executor context
 * @param first_command First command in the sibling chain
 * @return Exit status of last executed command
 */
static int execute_command_chain(executor_t *executor, node_t *first_command) {
    if (!first_command) {
        return 0;
    }

    int last_result = 0;
    node_t *current = first_command;

    while (current) {
        last_result = execute_node(executor, current);

        // Check for loop control (break/continue) - stop executing chain
        if (executor->loop_control != LOOP_NORMAL) {
            return last_result;
        }

        /* POSIX-required shell abort: short-circuit the chain so the
         * abort propagates up to execute_command_list and the REPL. */
        if (executor->shell_exit_requested) {
            return executor->shell_exit_status;
        }

        /* Bash-style ERR pseudo-signal: fires after a non-zero command
         * exit, before set -e gets a chance to abort. Matches bash's
         * "ERR trap before errexit" ordering. */
        if (last_result != 0) {
            fire_err_trap();
        }

        // Handle set -e (exit_on_error): exit if command failed and not part of
        // conditional
        if (shell_opts.exit_on_error && last_result != 0) {
            // Don't exit on error for certain contexts (conditionals,
            // pipelines, etc.) For now, implement basic exit-on-error behavior
            executor->exit_status = last_result;
            return last_result;
        }

        current = current->next_sibling;
    }

    return last_result;
}

/**
 * @brief Execute an if statement
 *
 * Handles if/elif/else control flow. Children are organized as:
 * - First child: if condition
 * - Second child: then body
 * - Subsequent pairs: elif condition/body
 * - Final unpaired child: else body (optional)
 *
 * @param executor Executor context
 * @param if_node If statement node
 * @return Exit status of executed branch
 */
static int execute_if(executor_t *executor, node_t *if_node) {
    if (!if_node || if_node->type != NODE_IF) {
        return 1;
    }

    // Check for trailing redirections on the if statement
    bool has_redirections = count_redirections(if_node) > 0;
    redirection_state_t redir_state;

    if (has_redirections) {
        save_file_descriptors(&redir_state);
        int redir_result = setup_redirections(executor, if_node);
        if (redir_result != 0) {
            restore_file_descriptors(&redir_state);
            return redir_result;
        }
    }

    int result = 0;

    // Traverse through all children of the if statement
    node_t *current = if_node->first_child;
    if (!current || is_redirection_node(current)) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           if_node->loc, "malformed if statement");
        result = 1;
        goto cleanup;
    }

    // First child is always the if condition
    node_t *condition = current;
    current = current->next_sibling;

    // Skip any redirection nodes to find then body
    while (current && is_redirection_node(current)) {
        current = current->next_sibling;
    }

    if (!current) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           if_node->loc,
                           "malformed if statement - missing then body");
        result = 1;
        goto cleanup;
    }

    // Execute the initial if condition
    int condition_result = execute_node(executor, condition);

    if (condition_result == 0) { // Success in shell terms
        // Execute the then body (second child)
        result = execute_node(executor, current);
        goto cleanup;
    }

    // Move to next child (elif condition or else body)
    current = current->next_sibling;

    // Skip any redirection nodes
    while (current && is_redirection_node(current)) {
        current = current->next_sibling;
    }

    // Process elif clauses - they come in pairs (condition, body)
    while (current && current->next_sibling) {
        // Skip redirection nodes
        node_t *next = current->next_sibling;
        while (next && is_redirection_node(next)) {
            next = next->next_sibling;
        }
        if (!next)
            break;

        // Execute elif condition
        condition_result = execute_node(executor, current);

        if (condition_result == 0) { // Success in shell terms
            // Execute elif body (next sibling)
            result = execute_node(executor, next);
            goto cleanup;
        }

        // Move past the elif body to the next elif condition or else body
        current = next->next_sibling;
        while (current && is_redirection_node(current)) {
            current = current->next_sibling;
        }
    }

    // Handle final else clause if present (no condition, just body)
    if (current && !is_redirection_node(current)) {
        result = execute_node(executor, current);
        goto cleanup;
    }

    result = 0;

cleanup:
    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }
    return result;
}

/* Runaway-loop safety: detect a loop body that fails with non-zero exit
 * status N consecutive times across at least T seconds. The "consecutive
 * non-zero" signal cleanly separates stuck-on-failure loops from any
 * legitimate idiom — counter loops, daemons (`while true; do work; done`),
 * read-line loops, retry-with-backoff that intermixes failures with
 * successes — none of which produce a long stretch of consecutive
 * non-zero body exits. The wall-clock floor prevents tripping on
 * legitimate fast-fail-many-times-then-succeed retry patterns where the
 * full streak fits inside hundreds of milliseconds.
 *
 * Tracking ANY non-zero (rather than the same status repeated) makes
 * the heuristic robust against a body that varies its error code per
 * iteration. Set behavior.loop_failure_streak = 0 to disable entirely
 * and match bash/zsh exactly.
 */
typedef struct {
    int streak;            /**< consecutive non-zero body exits */
    int last_status;       /**< last non-zero status seen */
    struct timespec start; /**< monotonic clock at first non-zero of streak */
    bool armed;            /**< streak start time captured */
} loop_monitor_t;

static void loop_monitor_init(loop_monitor_t *m) {
    m->streak = 0;
    m->last_status = 0;
    m->armed = false;
}

/* A `return` (or `exit`) executed inside a function surfaces as the
 * special 200-455 status code that bin_return / the executor use to
 * signal function unwinding. Loop bodies must recognize it, stop
 * iterating, and propagate the code unchanged so the enclosing
 * function actually returns. Without this check, `for x in ...; do
 * ... return 0; done` ran every remaining iteration and then fell
 * through to the post-loop statements (real_world/bash/201 is_in()).
 * This check must run BEFORE loop_errexit_tripped: a function-return
 * code is non-zero and would otherwise be misread as a failed body. */
static bool loop_body_is_function_return(int body_status) {
    return body_status >= 200 && body_status <= 455;
}

/* errexit_in_loops: when FEATURE_ERREXIT_IN_LOOPS is enabled (curated
 * lush-mode default; off in POSIX/bash/zsh for polyglot parity), the
 * first non-zero body exit aborts the loop. Caller should report
 * SHELL_ERR_LOOP_LIMIT with iteration context and break out. */
static bool loop_errexit_tripped(int body_status) {
    return body_status != 0 && shell_mode_allows(FEATURE_ERREXIT_IN_LOOPS);
}

/* Returns true when the streak satisfies both N and T thresholds.
 * Caller should report SHELL_ERR_LOOP_LIMIT and break out. */
static bool loop_monitor_check(loop_monitor_t *m, int body_status) {
    int n_threshold = config.loop_failure_streak;
    int t_threshold = config.loop_failure_seconds;
    if (n_threshold <= 0) {
        // heuristic disabled
        return false;
    }
    if (body_status == 0) {
        m->streak = 0;
        m->armed = false;
        return false;
    }
    m->last_status = body_status;
    m->streak++;
    if (!m->armed) {
        clock_gettime(CLOCK_MONOTONIC, &m->start);
        m->armed = true;
    }
    if (m->streak < n_threshold) {
        return false;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_sec = now.tv_sec - m->start.tv_sec;
    if (elapsed_sec < t_threshold) {
        return false;
    }
    return true;
}

/**
 * @brief Execute a while loop
 *
 * Executes body while condition returns success (0).
 * Supports break/continue with runaway-loop failure-streak detection
 * (behavior.loop_failure_streak / behavior.loop_failure_seconds).
 *
 * @param executor Executor context
 * @param while_node While loop node
 * @return Exit status of last executed body command
 */
static int execute_while(executor_t *executor, node_t *while_node) {
    if (!while_node || while_node->type != NODE_WHILE) {
        return 1;
    }

    node_t *condition = while_node->first_child;
    node_t *body = condition ? condition->next_sibling : NULL;

    // Skip past body to find any non-redirection node issues
    // Body might be followed by redirection nodes
    if (body && is_redirection_node(body)) {
        // If what we think is body is a redirection, we have malformed
        // structure
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           while_node->loc, "malformed while loop");
        return 1;
    }

    if (!condition || !body) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           while_node->loc, "malformed while loop");
        return 1;
    }

    int last_result = 0;
    loop_monitor_t monitor;
    loop_monitor_init(&monitor);
    bool runaway_tripped = false;
    bool errexit_tripped = false;
    int iteration = 0;

    // Push loop context for error reporting (Phase 3)
    executor_push_context(executor, while_node->loc, "in while loop");

    // Check for trailing redirections on the while loop
    bool has_redirections = count_redirections(while_node) > 0;
    redirection_state_t redir_state;

    if (has_redirections) {
        save_file_descriptors(&redir_state);
        int redir_result = setup_redirections(executor, while_node);
        if (redir_result != 0) {
            restore_file_descriptors(&redir_state);
            executor_pop_context(executor);
            return redir_result;
        }
    }

    // Increment loop depth - enables break/continue builtins
    executor->loop_depth++;

    for (;;) {
        // Execute condition
        int condition_result = execute_node(executor, condition);

        if (executor->debug) {
            printf("DEBUG: WHILE condition result: %d\n", condition_result);
        }

        // If condition fails, exit loop
        if (condition_result != 0) {
            break;
        }

        // Execute body
        last_result = execute_command_chain(executor, body);
        iteration++;

        // Check for break/continue
        if (executor->loop_control == LOOP_BREAK) {
            executor->loop_control = LOOP_NORMAL;
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            executor->loop_control = LOOP_NORMAL;
            // Continue to next iteration (just reset and loop again)
        }

        /* `return` inside the body: stop iterating, propagate the
         * function-return signal to the enclosing function. */
        if (loop_body_is_function_return(last_result)) {
            break;
        }

        if (loop_errexit_tripped(last_result)) {
            errexit_tripped = true;
            break;
        }

        if (loop_monitor_check(&monitor, last_result)) {
            runaway_tripped = true;
            break;
        }

        // POSIX-required shell abort fired from inside the body.
        if (executor->shell_exit_requested) {
            break;
        }
    }

    // Decrement loop depth before returning
    executor->loop_depth--;

    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    // Pop loop context
    executor_pop_context(executor);

    if (errexit_tripped) {
        executor_error_add(
            executor, SHELL_ERR_LOOP_LIMIT, while_node->loc,
            "while loop body failed with status %d in iteration %d "
            "(errexit_in_loops); use `unsetopt errexit_in_loops` to "
            "allow continued execution after a body failure",
            last_result, iteration);
        return last_result;
    }

    if (runaway_tripped) {
        executor_error_add(
            executor, SHELL_ERR_LOOP_LIMIT, while_node->loc,
            "while loop body failed with status %d for %d consecutive "
            "iterations over %d+ seconds — likely stuck "
            "(set behavior.loop_failure_streak = 0 to disable this check)",
            monitor.last_status, monitor.streak, config.loop_failure_seconds);
        return 1;
    }

    return last_result;
}

/**
 * @brief Execute an until loop
 *
 * Executes body until condition returns success (0).
 * Inverse of while loop - continues while condition fails.
 * Supports break/continue with 10000 iteration safety limit.
 *
 * @param executor Executor context
 * @param until_node Until loop node
 * @return Exit status of last executed body command
 */
static int execute_until(executor_t *executor, node_t *until_node) {
    if (!until_node || until_node->type != NODE_UNTIL) {
        return 1;
    }

    node_t *condition = until_node->first_child;
    node_t *body = condition ? condition->next_sibling : NULL;

    if (!condition || !body || is_redirection_node(body)) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           until_node->loc, "malformed until loop");
        return 1;
    }

    int last_result = 0;
    loop_monitor_t monitor;
    loop_monitor_init(&monitor);
    bool runaway_tripped = false;
    bool errexit_tripped = false;
    int iteration = 0;

    // Check for trailing redirections on the until loop
    bool has_redirections = count_redirections(until_node) > 0;
    redirection_state_t redir_state;

    if (has_redirections) {
        save_file_descriptors(&redir_state);
        int redir_result = setup_redirections(executor, until_node);
        if (redir_result != 0) {
            restore_file_descriptors(&redir_state);
            return redir_result;
        }
    }

    // Increment loop depth - enables break/continue builtins
    executor->loop_depth++;

    for (;;) {
        // Execute condition
        int condition_result = execute_node(executor, condition);

        if (executor->debug) {
            printf("DEBUG: UNTIL condition result: %d\n", condition_result);
        }

        // If condition succeeds (returns 0), exit loop
        // This is the key difference from while loop
        if (condition_result == 0) {
            break;
        }

        // Execute body
        last_result = execute_command_chain(executor, body);
        iteration++;

        // Check for break/continue
        if (executor->loop_control == LOOP_BREAK) {
            executor->loop_control = LOOP_NORMAL;
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            executor->loop_control = LOOP_NORMAL;
            // Continue to next iteration
        }

        /* `return` inside the body: stop iterating, propagate the
         * function-return signal to the enclosing function. */
        if (loop_body_is_function_return(last_result)) {
            break;
        }

        if (loop_errexit_tripped(last_result)) {
            errexit_tripped = true;
            break;
        }

        if (loop_monitor_check(&monitor, last_result)) {
            runaway_tripped = true;
            break;
        }

        // POSIX-required shell abort fired from inside the body.
        if (executor->shell_exit_requested) {
            break;
        }
    }

    // Decrement loop depth before returning
    executor->loop_depth--;

    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    // Pop error context
    executor_pop_context(executor);

    if (errexit_tripped) {
        executor_error_add(
            executor, SHELL_ERR_LOOP_LIMIT, until_node->loc,
            "until loop body failed with status %d in iteration %d "
            "(errexit_in_loops); use `unsetopt errexit_in_loops` to "
            "allow continued execution after a body failure",
            last_result, iteration);
        return last_result;
    }

    if (runaway_tripped) {
        executor_error_add(
            executor, SHELL_ERR_LOOP_LIMIT, until_node->loc,
            "until loop body failed with status %d for %d consecutive "
            "iterations over %d+ seconds — likely stuck "
            "(set behavior.loop_failure_streak = 0 to disable this check)",
            monitor.last_status, monitor.streak, config.loop_failure_seconds);
        return 1;
    }

    return last_result;
}

/**
 * @brief Execute a zsh repeat loop
 *
 * Runs the body N times. The count expression is the first child
 * (a NODE_VAR / NODE_COMMAND_SUB / NODE_ARITH_EXP / etc.) and the
 * body is the second child (a NODE_COMMAND_LIST or similar). The
 * count is arithmetic-evaluated so `repeat $((expr))` and
 * `repeat $var` both work. Non-positive counts are no-ops (zsh
 * compat). Honors break/continue/shell_exit_requested. Issue #103.
 *
 * @param executor Executor context
 * @param repeat_node NODE_REPEAT with [count, body] children
 * @return Exit status of last executed body command, or 0 if body
 *         never ran
 */
static int execute_repeat(executor_t *executor, node_t *repeat_node) {
    if (!repeat_node || repeat_node->type != NODE_REPEAT) {
        return 1;
    }

    node_t *count_node = repeat_node->first_child;
    node_t *body = count_node ? count_node->next_sibling : NULL;
    if (!count_node || !body) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           repeat_node->loc, "malformed repeat loop");
        return 1;
    }

    /* Evaluate count via the standard arg-expansion path (handles
     * $var, $(cmd), $((expr)), literal numbers) and then parse as
     * an integer. zsh accepts a negative or zero count as "do
     * nothing"; match that. */
    char *count_text = expand_arg_node(executor, count_node);
    if (!count_text) {
        return 1;
    }
    char *count_eval = arithm_expand_with_executor(executor, count_text);
    long count = count_eval ? strtol(count_eval, NULL, 10) : 0;
    free(count_text);
    free(count_eval);

    if (count <= 0) {
        return 0;
    }

    if (symtable_push_scope(executor->symtable, SCOPE_LOOP, "repeat-loop") !=
        0) {
        executor_error_add(executor, SHELL_ERR_SCOPE_ERROR, repeat_node->loc,
                           "failed to create loop scope");
        return 1;
    }
    executor->loop_depth++;
    executor_push_context(executor, repeat_node->loc, "in repeat loop");

    int last_result = 0;
    for (long i = 0; i < count; i++) {
        last_result = execute_command_chain(executor, body);

        if (executor->loop_control == LOOP_BREAK) {
            executor->loop_control = LOOP_NORMAL;
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            executor->loop_control = LOOP_NORMAL;
            continue;
        }
        /* `return` inside the body: stop iterating, propagate the
         * function-return signal to the enclosing function. */
        if (loop_body_is_function_return(last_result)) {
            break;
        }
        if (executor->shell_exit_requested) {
            break;
        }
    }

    executor->loop_depth--;
    executor_pop_context(executor);
    symtable_pop_scope(executor->symtable);
    return last_result;
}

/**
 * @brief Pathname-expand a slice of a for-loop word list in place
 *
 * Replaces every word in words[start..*count) that carries glob
 * metacharacters with its sorted matches; words with no matches are
 * kept verbatim (bash default) unless nullglob drops them. The slice
 * mechanism lets the caller glob only the words produced by an
 * UNQUOTED word node, leaving quoted words and `$@` elements alone.
 *
 * The for-loop word builder expands variables, braces, and fields but
 * historically never globbed -- `for x in *` iterated the literal
 * `*`. expand_glob_pattern handles zsh glob qualifiers, extglob,
 * nullglob, and `set -f` internally.
 *
 * @return false only on allocation failure
 */
static bool for_word_list_glob_range(char ***words, int *count, int start) {
    if (!words || !*words || !count) {
        return true;
    }
    char **src = *words;
    int n = *count;

    bool any = false;
    for (int i = start; i < n; i++) {
        if (src[i] && needs_glob_expansion(src[i])) {
            any = true;
            break;
        }
    }
    if (!any) {
        return true;
    }

    char **out = NULL;
    int out_count = 0;
    for (int i = 0; i < n; i++) {
        if (i >= start && src[i] && needs_glob_expansion(src[i])) {
            int gc = 0;
            char **g = expand_glob_pattern(src[i], &gc);
            if (g) {
                char **no =
                    realloc(out, (size_t)(out_count + gc) * sizeof(char *));
                if (!no) {
                    free(g);
                    free(out);
                    return false;
                }
                out = no;
                for (int j = 0; j < gc; j++) {
                    out[out_count++] = g[j];
                }
                free(g);
                free(src[i]);
                continue;
            }
        }
        char **no = realloc(out, (size_t)(out_count + 1) * sizeof(char *));
        if (!no) {
            free(out);
            return false;
        }
        out = no;
        out[out_count++] = src[i];
    }

    free(src);
    *words = out;
    *count = out_count;
    return true;
}

/**
 * @brief Execute a for loop
 *
 * Iterates over a word list, setting the loop variable for each iteration.
 * Handles "$@" specially to preserve word boundaries. Creates a loop scope
 * for the iteration variable.
 *
 * @param executor Executor context
 * @param for_node For loop node with variable name in val.str
 * @return Exit status of last executed body command
 */
static int execute_for(executor_t *executor, node_t *for_node) {
    if (!for_node || for_node->type != NODE_FOR) {
        return 1;
    }

    const char *var_name = for_node->val.str;
    if (!var_name) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           for_node->loc, "for loop missing variable name");
        return 1;
    }

    node_t *word_list = for_node->first_child;
    node_t *body = word_list ? word_list->next_sibling : NULL;

    if (!body || is_redirection_node(body)) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           for_node->loc, "for loop missing body");
        return 1;
    }

    // Check for trailing redirections on the for loop
    bool has_redirections = count_redirections(for_node) > 0;
    redirection_state_t redir_state;

    if (has_redirections) {
        save_file_descriptors(&redir_state);
        int redir_result = setup_redirections(executor, for_node);
        if (redir_result != 0) {
            restore_file_descriptors(&redir_state);
            return redir_result;
        }
    }

    // Push loop scope
    if (symtable_push_scope(executor->symtable, SCOPE_LOOP, "for-loop") != 0) {
        executor_error_add(executor, SHELL_ERR_SCOPE_ERROR, for_node->loc,
                           "failed to create loop scope");
        return 1;
    }

    // Notify debug system we're entering a loop
    if (g_debug_context && g_debug_context->enabled) {
        debug_enter_loop(g_debug_context, "for", var_name, NULL);
    }

    // Increment loop depth - enables break/continue builtins
    executor->loop_depth++;

    // Push error context for structured error reporting
    executor_push_context(executor, for_node->loc, "in for loop over '%s'",
                          var_name);

    int last_result = 0;
    loop_monitor_t monitor;
    loop_monitor_init(&monitor);
    bool runaway_tripped = false;
    bool errexit_tripped = false;
    int iteration = 0;

    // Build expanded word list for iteration
    char **expanded_words = NULL;
    int word_count = 0;

    // Process each word in the word list, expanding and splitting
    if (word_list && word_list->first_child) {
        node_t *word = word_list->first_child;
        while (word) {
            /* Index into expanded_words where this word node's
             * normal-expansion output begins, and whether that output
             * is eligible for pathname expansion. -1 until the normal
             * expansion path runs (the `$@` and vector paths produce
             * already-final values that are never glob-expanded). */
            int normal_wc_start = -1;
            bool word_globbable = false;
            if (word->val.str) {
                // Special handling for "$@" to preserve word boundaries
                if (strcmp(word->val.str, "\"$@\"") == 0 ||
                    strcmp(word->val.str, "$@") == 0) {
                    // Handle quoted "$@" - preserve word boundaries
                    // Check if we're in a function scope
                    if (symtable_in_function_scope(executor->symtable)) {
                        // In function scope - use local positional parameters
                        char *argc_str =
                            symtable_get_var(executor->symtable, "#");
                        int func_argc = argc_str ? atoi(argc_str) : 0;
                        free(argc_str);

                        for (int i = 1; i <= func_argc; i++) {
                            char param_name[16];
                            snprintf(param_name, sizeof(param_name), "%d", i);
                            char *param_value = symtable_get_var(
                                executor->symtable, param_name);
                            if (param_value && param_value[0] != '\0') {
                                expanded_words =
                                    realloc(expanded_words,
                                            (word_count + 1) * sizeof(char *));
                                if (!expanded_words) {
                                    free(param_value);
                                    set_executor_error(
                                        executor,
                                        "Memory allocation failed in for loop");
                                    symtable_pop_scope(executor->symtable);
                                    return 1;
                                }
                                expanded_words[word_count] = param_value;
                                word_count++;
                            } else {
                                free(param_value);
                            }
                        }
                    } else {
                        // Not in function scope - use global shell_argv
                        for (int i = 1; i < shell_argc; i++) {
                            if (shell_argv[i]) {
                                expanded_words =
                                    realloc(expanded_words,
                                            (word_count + 1) * sizeof(char *));
                                if (!expanded_words) {
                                    set_executor_error(
                                        executor,
                                        "Memory allocation failed in for loop");
                                    symtable_pop_scope(executor->symtable);
                                    return 1;
                                }

                                expanded_words[word_count] =
                                    strdup(shell_argv[i]);
                                word_count++;
                            }
                        }
                    }
                } else {
                    /* Vector-yielding expansions in for-loop word lists:
                     * `$@`, `"$@"`, `${arr[@]}`, `"${arr[@]}"`,
                     * `${!arr[@]}`, bare `$arr` (zsh/lush mode), and
                     * slice variants. Each produces N separate iteration
                     * values regardless of word-split setting -- the
                     * for-loop semantics are intrinsically per-element
                     * for these forms in both bash and zsh. Without this
                     * the FEATURE_WORD_SPLIT_DEFAULT=false path
                     * (zsh/lush mode default) treats the joined string
                     * as one iteration. Issue #99. */
                    char **vec = NULL;
                    int vcount = 0;
                    if (try_expand_vector_arg(executor, word, &vec, &vcount)) {
                        for (int v = 0; v < vcount; v++) {
                            expanded_words =
                                realloc(expanded_words,
                                        (word_count + 1) * sizeof(char *));
                            if (!expanded_words) {
                                for (int w = v; w < vcount; w++) {
                                    free(vec[w]);
                                }
                                free(vec);
                                set_executor_error(
                                    executor,
                                    "Memory allocation failed in for loop");
                                symtable_pop_scope(executor->symtable);
                                return 1;
                            }
                            expanded_words[word_count++] = vec[v];
                        }
                        free(vec);
                        word = word->next_sibling;
                        continue;
                    }

                    // Normal expansion and splitting for other words.
                    // Record the start index and quotedness so the
                    // words produced below can be pathname-expanded.
                    normal_wc_start = word_count;
                    word_globbable = (word->type != NODE_STRING_LITERAL &&
                                      word->type != NODE_STRING_EXPANDABLE);
                    char *expanded = expand_if_needed(executor, word->val.str);
                    if (expanded) {
                        // Check for brace expansion first
                        if (needs_brace_expansion(expanded)) {
                            int brace_count;
                            char **brace_results =
                                expand_brace_pattern(expanded, &brace_count);
                            if (brace_count == BRACE_EXPANSION_LIMIT_SENTINEL) {
                                set_executor_error(
                                    executor,
                                    "brace expansion exceeds configured "
                                    "limit (behavior.brace_expansion_max)");
                                executor->expansion_error = true;
                                executor->expansion_exit_status = 1;
                                free(expanded);
                                symtable_pop_scope(executor->symtable);
                                return 1;
                            }
                            if (brace_results) {
                                // Add each brace expansion result
                                for (int b = 0; b < brace_count; b++) {
                                    expanded_words = realloc(
                                        expanded_words,
                                        (word_count + 1) * sizeof(char *));
                                    if (!expanded_words) {
                                        set_executor_error(
                                            executor, "Memory allocation "
                                                      "failed in for loop");
                                        for (int k = 0; k < brace_count; k++) {
                                            free(brace_results[k]);
                                        }
                                        free(brace_results);
                                        free(expanded);
                                        symtable_pop_scope(executor->symtable);
                                        return 1;
                                    }
                                    expanded_words[word_count] =
                                        brace_results[b];
                                    word_count++;
                                }
                                free(
                                    brace_results); // Free array, not strings
                                                    // (moved to expanded_words)
                                free(expanded);
                            } else {
                                // Brace expansion failed, use original
                                expanded_words =
                                    realloc(expanded_words,
                                            (word_count + 1) * sizeof(char *));
                                if (expanded_words) {
                                    expanded_words[word_count] = expanded;
                                    word_count++;
                                } else {
                                    free(expanded);
                                }
                            }
                        } else {
                            // No brace expansion needed - check if IFS
                            // splitting is enabled
                            if (shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT)) {
                                // IFS splitting enabled - split the expanded
                                // string
                                const char *ifs =
                                    symtable_get(executor->symtable, "IFS");
                                if (!ifs) {
                                    ifs = " \t\n"; // Default IFS
                                }

                                // Split the expanded string into individual
                                // words
                                char *expanded_copy = strdup(expanded);
                                char *token = strtok(expanded_copy, ifs);

                                while (token) {
                                    // Resize array if needed
                                    expanded_words = realloc(
                                        expanded_words,
                                        (word_count + 1) * sizeof(char *));
                                    if (!expanded_words) {
                                        set_executor_error(
                                            executor, "Memory allocation "
                                                      "failed in for loop");
                                        free(expanded);
                                        free(expanded_copy);
                                        symtable_pop_scope(executor->symtable);
                                        return 1;
                                    }

                                    expanded_words[word_count] = strdup(token);
                                    word_count++;
                                    token = strtok(NULL, ifs);
                                }

                                free(expanded_copy);
                                free(expanded);
                            } else {
                                // Word splitting disabled (zsh-style) - keep as
                                // single word
                                expanded_words =
                                    realloc(expanded_words,
                                            (word_count + 1) * sizeof(char *));
                                if (expanded_words) {
                                    expanded_words[word_count] = expanded;
                                    word_count++;
                                } else {
                                    free(expanded);
                                }
                            }
                        }
                    }
                }
            }
            /* Pathname-expand the words this UNQUOTED word node just
             * produced. normal_wc_start is -1 for the `$@` / vector
             * paths and for quoted word nodes, leaving those
             * untouched. */
            if (normal_wc_start >= 0 && word_globbable) {
                if (!for_word_list_glob_range(&expanded_words, &word_count,
                                              normal_wc_start)) {
                    set_executor_error(executor,
                                       "Memory allocation failed in for loop");
                    for (int j = 0; j < word_count; j++) {
                        free(expanded_words[j]);
                    }
                    free(expanded_words);
                    symtable_pop_scope(executor->symtable);
                    return 1;
                }
            }
            word = word->next_sibling;
        }
    }

    // Iterate over expanded words
    for (int i = 0; i < word_count; i++) {
        if (expanded_words[i]) {
            // Update loop variable using POSIX scope-chain semantics: if
            // a local of this name already exists (e.g. `local i` in the
            // enclosing function), update that local instead of creating
            // a parallel global. See issue #47.
            if (symtable_assign_var(executor->symtable, var_name,
                                    expanded_words[i]) != 0) {
                set_executor_error(executor, "Failed to set loop variable");
                // Cleanup expanded words
                for (int j = 0; j < word_count; j++) {
                    free(expanded_words[j]);
                }
                free(expanded_words);
                symtable_pop_scope(executor->symtable);
                if (g_debug_context && g_debug_context->enabled) {
                    debug_exit_loop(g_debug_context);
                }
                executor_pop_context(executor);
                return 1;
            }

            if (executor->debug) {
                printf("DEBUG: FOR loop setting %s=%s\n", var_name,
                       expanded_words[i]);
            }

            // Notify debug system of loop variable update
            if (g_debug_context && g_debug_context->enabled) {
                debug_update_loop_variable(g_debug_context, var_name,
                                           expanded_words[i]);
            }

            // Execute body
            last_result = execute_command_chain(executor, body);
            iteration++;

            // Check for break/continue
            if (executor->loop_control == LOOP_BREAK) {
                executor->loop_control = LOOP_NORMAL;
                break;
            } else if (executor->loop_control == LOOP_CONTINUE) {
                executor->loop_control = LOOP_NORMAL;
                // Continue to next iteration
            }

            /* `return` inside the body: stop iterating, propagate the
             * function-return signal to the enclosing function. */
            if (loop_body_is_function_return(last_result)) {
                break;
            }

            if (loop_errexit_tripped(last_result)) {
                errexit_tripped = true;
                break;
            }

            if (loop_monitor_check(&monitor, last_result)) {
                runaway_tripped = true;
                break;
            }

            // POSIX-required shell abort fired from inside the body.
            if (executor->shell_exit_requested) {
                break;
            }
        }

        // Honor abort across the outer iteration as well.
        if (executor->shell_exit_requested) {
            break;
        }
    }

    // Cleanup expanded words
    for (int i = 0; i < word_count; i++) {
        free(expanded_words[i]);
    }
    free(expanded_words);

    // Notify debug system we're exiting the loop
    if (g_debug_context && g_debug_context->enabled) {
        debug_exit_loop(g_debug_context);
    }

    // Decrement loop depth before returning
    executor->loop_depth--;

    // Pop loop scope
    symtable_pop_scope(executor->symtable);

    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    // Pop error context
    executor_pop_context(executor);

    if (errexit_tripped) {
        executor_error_add(
            executor, SHELL_ERR_LOOP_LIMIT, for_node->loc,
            "for loop body failed with status %d in iteration %d "
            "(errexit_in_loops); use `unsetopt errexit_in_loops` to "
            "allow continued execution after a body failure",
            last_result, iteration);
        return last_result;
    }

    if (runaway_tripped) {
        executor_error_add(
            executor, SHELL_ERR_LOOP_LIMIT, for_node->loc,
            "for loop body failed with status %d for %d consecutive "
            "iterations over %d+ seconds — likely stuck "
            "(set behavior.loop_failure_streak = 0 to disable this check)",
            monitor.last_status, monitor.streak, config.loop_failure_seconds);
        return 1;
    }

    return last_result;
}

/**
 * @brief Execute C-style arithmetic for loop
 *
 * Executes a C-style for loop: for ((init; test; update)); do body; done
 * - Child 0: init expression (evaluated once at start)
 * - Child 1: test expression (evaluated before each iteration, loop continues
 * if non-zero)
 * - Child 2: update expression (evaluated after each iteration)
 * - Child 3: body (commands to execute each iteration)
 *
 * @param executor Executor context
 * @param for_arith_node C-style for loop node
 * @return Exit status of last executed body command
 */
static int execute_for_arith(executor_t *executor, node_t *for_arith_node) {
    if (!for_arith_node || for_arith_node->type != NODE_FOR_ARITH) {
        return 1;
    }

    // Get the four children: init, test, update, body
    node_t *init_node = for_arith_node->first_child;
    node_t *test_node = init_node ? init_node->next_sibling : NULL;
    node_t *update_node = test_node ? test_node->next_sibling : NULL;
    node_t *body = update_node ? update_node->next_sibling : NULL;

    if (!body) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           for_arith_node->loc,
                           "C-style for loop missing body");
        return 1;
    }

    // Check for trailing redirections
    bool has_redirections = count_redirections(for_arith_node) > 0;
    redirection_state_t redir_state;

    if (has_redirections) {
        save_file_descriptors(&redir_state);
        int redir_result = setup_redirections(executor, for_arith_node);
        if (redir_result != 0) {
            restore_file_descriptors(&redir_state);
            return redir_result;
        }
    }

    // Push loop scope
    if (symtable_push_scope(executor->symtable, SCOPE_LOOP, "for-arith-loop") !=
        0) {
        executor_error_add(executor, SHELL_ERR_SCOPE_ERROR, for_arith_node->loc,
                           "failed to create loop scope");
        return 1;
    }

    // Notify debug system we're entering a loop
    if (g_debug_context && g_debug_context->enabled) {
        debug_enter_loop(g_debug_context, "for", "(arithmetic)", NULL);
    }

    // Increment loop depth - enables break/continue builtins
    executor->loop_depth++;

    // Push error context for structured error reporting
    executor_push_context(executor, for_arith_node->loc, "in C-style for loop");

    int last_result = 0;
    loop_monitor_t monitor;
    loop_monitor_init(&monitor);
    bool runaway_tripped = false;
    bool errexit_tripped = false;
    int iteration = 0;

    // Execute init expression (once at the start)
    if (init_node && init_node->val.str && init_node->val.str[0] != '\0') {
        char *init_expanded = expand_if_needed(executor, init_node->val.str);
        if (init_expanded) {
            arithm_clear_error();
            char *result_str =
                arithm_expand_with_executor(executor, init_expanded);
            if (result_str) {
                free(result_str);
            }
            if (arithm_error_is_flagged() && executor->debug) {
                fprintf(stderr, "DEBUG: C-style for init failed: %s\n",
                        init_expanded);
            }
            free(init_expanded);
        }
    }

    // Loop: test, execute body, update
    while (1) {
        // Evaluate test expression (if empty, treat as true - infinite loop)
        if (test_node && test_node->val.str && test_node->val.str[0] != '\0') {
            char *test_expanded =
                expand_if_needed(executor, test_node->val.str);
            if (test_expanded) {
                arithm_clear_error();
                char *result_str =
                    arithm_expand_with_executor(executor, test_expanded);
                if (!result_str || arithm_error_is_flagged()) {
                    // Arithmetic error
                    free(result_str);
                    free(test_expanded);
                    last_result = 1;
                    break;
                }

                // Convert result to check if non-zero
                long long test_result = strtoll(result_str, NULL, 10);
                free(result_str);
                free(test_expanded);

                // If test result is 0, exit the loop
                if (test_result == 0) {
                    break;
                }
            }
        }
        // If test is empty, continue (infinite loop until break)

        // Execute body
        last_result = execute_command_chain(executor, body);
        iteration++;

        // Check for break/continue
        if (executor->loop_control == LOOP_BREAK) {
            executor->loop_control = LOOP_NORMAL;
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            executor->loop_control = LOOP_NORMAL;
            // Fall through to update expression
        }

        /* `return` inside the body: stop iterating, propagate the
         * function-return signal to the enclosing function. */
        if (loop_body_is_function_return(last_result)) {
            break;
        }

        if (loop_errexit_tripped(last_result)) {
            errexit_tripped = true;
            break;
        }

        /* POSIX-required shell abort fired from inside the body --
         * skip the update expression and exit the loop. */
        if (executor->shell_exit_requested) {
            break;
        }

        // Execute update expression
        if (update_node && update_node->val.str &&
            update_node->val.str[0] != '\0') {
            char *update_expanded =
                expand_if_needed(executor, update_node->val.str);
            if (update_expanded) {
                arithm_clear_error();
                char *result_str =
                    arithm_expand_with_executor(executor, update_expanded);
                if (result_str) {
                    free(result_str);
                }
                if (arithm_error_is_flagged() && executor->debug) {
                    fprintf(stderr, "DEBUG: C-style for update failed: %s\n",
                            update_expanded);
                }
                free(update_expanded);
            }
        }

        if (loop_monitor_check(&monitor, last_result)) {
            runaway_tripped = true;
            break;
        }
    }

    // Notify debug system we're exiting the loop
    if (g_debug_context && g_debug_context->enabled) {
        debug_exit_loop(g_debug_context);
    }

    // Decrement loop depth before returning
    executor->loop_depth--;

    // Pop loop scope
    symtable_pop_scope(executor->symtable);

    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    // Pop error context
    executor_pop_context(executor);

    if (errexit_tripped) {
        executor_error_add(
            executor, SHELL_ERR_LOOP_LIMIT, for_arith_node->loc,
            "C-style for loop body failed with status %d in iteration "
            "%d (errexit_in_loops); use `unsetopt errexit_in_loops` to "
            "allow continued execution after a body failure",
            last_result, iteration);
        return last_result;
    }

    if (runaway_tripped) {
        executor_error_add(
            executor, SHELL_ERR_LOOP_LIMIT, for_arith_node->loc,
            "C-style for loop body failed with status %d for %d "
            "consecutive iterations over %d+ seconds — likely stuck "
            "(set behavior.loop_failure_streak = 0 to disable this check)",
            monitor.last_status, monitor.streak, config.loop_failure_seconds);
        return 1;
    }

    return last_result;
}

/**
 * @brief Execute select loop
 *
 * Displays a numbered menu of choices and reads user selection.
 * Sets the loop variable to the selected word and REPLY to user input.
 * Continues until break or EOF.
 *
 * @param executor Executor context
 * @param select_node Select loop node with variable name in val.str
 * @return Exit status of last executed body command
 */
static int execute_select(executor_t *executor, node_t *select_node) {
    if (!select_node || select_node->type != NODE_SELECT) {
        return 1;
    }

    const char *var_name = select_node->val.str;
    if (!var_name) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           select_node->loc,
                           "select loop missing variable name");
        return 1;
    }

    node_t *word_list = select_node->first_child;
    node_t *body = word_list ? word_list->next_sibling : NULL;

    if (!body || is_redirection_node(body)) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           select_node->loc, "select loop missing body");
        return 1;
    }

    // Check for trailing redirections on the select loop
    bool has_redirections = count_redirections(select_node) > 0;
    redirection_state_t redir_state;

    if (has_redirections) {
        save_file_descriptors(&redir_state);
        int redir_result = setup_redirections(executor, select_node);
        if (redir_result != 0) {
            restore_file_descriptors(&redir_state);
            return redir_result;
        }
    }

    // Build expanded word list for menu
    char **menu_items = NULL;
    int item_count = 0;

    if (word_list && word_list->first_child) {
        node_t *word = word_list->first_child;
        while (word) {
            if (word->val.str) {
                char *expanded = expand_if_needed(executor, word->val.str);
                if (expanded) {
                    // Check if this was a quoted string (no IFS splitting)
                    bool is_quoted = (word->type == NODE_STRING_LITERAL ||
                                      word->type == NODE_STRING_EXPANDABLE);

                    if (is_quoted ||
                        !shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT)) {
                        // Quoted strings or no-word-split mode: keep as single
                        // item
                        menu_items = realloc(menu_items,
                                             (item_count + 1) * sizeof(char *));
                        if (!menu_items) {
                            free(expanded);
                            return 1;
                        }
                        menu_items[item_count] = expanded;
                        item_count++;
                    } else {
                        // Unquoted: Split by IFS
                        const char *ifs =
                            symtable_get(executor->symtable, "IFS");
                        if (!ifs) {
                            ifs = " \t\n";
                        }

                        char *expanded_copy = strdup(expanded);
                        char *token = strtok(expanded_copy, ifs);

                        while (token) {
                            menu_items = realloc(
                                menu_items, (item_count + 1) * sizeof(char *));
                            if (!menu_items) {
                                free(expanded);
                                free(expanded_copy);
                                return 1;
                            }
                            menu_items[item_count] = strdup(token);
                            item_count++;
                            token = strtok(NULL, ifs);
                        }

                        free(expanded_copy);
                        free(expanded);
                    }
                }
            }
            word = word->next_sibling;
        }
    }

    if (item_count == 0) {
        if (has_redirections) {
            restore_file_descriptors(&redir_state);
        }
        return 0; // No items, nothing to do
    }

    // Push loop scope
    if (symtable_push_scope(executor->symtable, SCOPE_LOOP, "select-loop") !=
        0) {
        for (int i = 0; i < item_count; i++) {
            free(menu_items[i]);
        }
        free(menu_items);
        if (has_redirections) {
            restore_file_descriptors(&redir_state);
        }
        return 1;
    }

    executor->loop_depth++;

    int last_result = 0;
    char input_buf[256];

    // Get PS3 prompt (default is "#? ")
    const char *ps3 = symtable_get(executor->symtable, "PS3");
    if (!ps3 || !*ps3) {
        ps3 = "#? ";
    }

    while (1) {
        // Display menu
        for (int i = 0; i < item_count; i++) {
            fprintf(stderr, "%d) %s\n", i + 1, menu_items[i]);
        }

        // Display prompt and read input
        fprintf(stderr, "%s", ps3);
        fflush(stderr);

        if (!fgets(input_buf, sizeof(input_buf), stdin)) {
            // EOF - exit loop
            break;
        }

        // Remove trailing newline
        size_t len = strlen(input_buf);
        if (len > 0 && input_buf[len - 1] == '\n') {
            input_buf[len - 1] = '\0';
        }

        // Set REPLY to the raw input
        symtable_set(executor->symtable, "REPLY", input_buf);

        // Parse selection number
        char *endptr;
        long selection = strtol(input_buf, &endptr, 10);

        // Set loop variable using POSIX scope-chain semantics (issue #47)
        if (*input_buf != '\0' && *endptr == '\0' && selection >= 1 &&
            selection <= item_count) {
            // Valid selection
            symtable_assign_var(executor->symtable, var_name,
                                menu_items[selection - 1]);
        } else {
            // Invalid or empty input - set variable to empty
            symtable_assign_var(executor->symtable, var_name, "");
        }

        // Execute body
        node_t *cmd = body;
        while (cmd) {
            last_result = execute_node(executor, cmd);

            // Check for break/continue
            if (executor->loop_control != LOOP_NORMAL) {
                break;
            }

            // POSIX-required shell abort: drop out of the select body.
            if (executor->shell_exit_requested) {
                break;
            }

            cmd = cmd->next_sibling;
        }

        // Handle break from body
        if (executor->loop_control == LOOP_BREAK) {
            executor->loop_control = LOOP_NORMAL;
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            executor->loop_control = LOOP_NORMAL;
            continue;
        }

        // POSIX-required shell abort: stop the outer select loop.
        if (executor->shell_exit_requested) {
            break;
        }
    }

    // Cleanup
    executor->loop_depth--;
    symtable_pop_scope(executor->symtable);

    for (int i = 0; i < item_count; i++) {
        free(menu_items[i]);
    }
    free(menu_items);

    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    return last_result;
}

/**
 * @brief Execute time command
 *
 * Times the execution of a pipeline and reports real, user, and sys time.
 * With -p option, uses POSIX format.
 *
 * @param executor Executor context
 * @param time_node Time command node
 * @return Exit status of the timed pipeline
 */
static int execute_time(executor_t *executor, node_t *time_node) {
    if (!time_node || time_node->type != NODE_TIME) {
        return 1;
    }

    bool posix_format =
        (time_node->val_type == VAL_SINT && time_node->val.sint == 1);
    node_t *pipeline = time_node->first_child;

    if (!pipeline) {
        return 0; // Nothing to time
    }

    // Get start time
    struct timeval start_time, end_time;
    struct rusage start_usage, end_usage;

    gettimeofday(&start_time, NULL);
    getrusage(RUSAGE_CHILDREN, &start_usage);

    // Execute the pipeline
    int result = execute_node(executor, pipeline);

    // Get end time
    gettimeofday(&end_time, NULL);
    getrusage(RUSAGE_CHILDREN, &end_usage);

    // Calculate elapsed times
    double real_time = (end_time.tv_sec - start_time.tv_sec) +
                       (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    double user_time =
        (end_usage.ru_utime.tv_sec - start_usage.ru_utime.tv_sec) +
        (end_usage.ru_utime.tv_usec - start_usage.ru_utime.tv_usec) / 1000000.0;

    double sys_time =
        (end_usage.ru_stime.tv_sec - start_usage.ru_stime.tv_sec) +
        (end_usage.ru_stime.tv_usec - start_usage.ru_stime.tv_usec) / 1000000.0;

    // Check for TIMEFORMAT variable (Bash extension)
    const char *timeformat = symtable_get(executor->symtable, "TIMEFORMAT");

    if (posix_format) {
        // POSIX format: real, user, sys in seconds
        fprintf(stderr, "real %.2f\nuser %.2f\nsys %.2f\n", real_time,
                user_time, sys_time);
    } else if (timeformat && *timeformat) {
        // Custom format (simplified - just show the times)
        fprintf(stderr, "\nreal\t%.3fs\nuser\t%.3fs\nsys\t%.3fs\n", real_time,
                user_time, sys_time);
    } else {
        // Default Bash-like format
        fprintf(stderr, "\nreal\t%dm%.3fs\nuser\t%dm%.3fs\nsys\t%dm%.3fs\n",
                (int)(real_time / 60), fmod(real_time, 60.0),
                (int)(user_time / 60), fmod(user_time, 60.0),
                (int)(sys_time / 60), fmod(sys_time, 60.0));
    }

    return result;
}

/**
 * @brief Execute a coprocess command
 *
 * Creates a coprocess running in background with bidirectional pipes.
 * Sets up NAME array with file descriptors and NAME_PID with process ID.
 * NAME[0] = fd to read from coprocess stdout
 * NAME[1] = fd to write to coprocess stdin
 *
 * @param executor Executor context
 * @param coproc_node Coproc command node
 * @return 0 on success, non-zero on failure
 */
static int execute_coproc(executor_t *executor, node_t *coproc_node) {
    if (!coproc_node || coproc_node->type != NODE_COPROC) {
        return 1;
    }

    node_t *command = coproc_node->first_child;
    if (!command) {
        set_executor_error(executor, "coproc: missing command");
        return 1;
    }

    // Get the coprocess name (default to "COPROC")
    const char *coproc_name = coproc_node->val.str;
    if (!coproc_name || !*coproc_name) {
        coproc_name = "COPROC";
    }

    // Create pipes for bidirectional communication
    // pipe_to_coproc: parent writes to [1], coproc reads from [0]
    // pipe_from_coproc: coproc writes to [1], parent reads from [0]
    int pipe_to_coproc[2];
    int pipe_from_coproc[2];

    if (pipe(pipe_to_coproc) == -1) {
        set_executor_error(executor, "coproc: failed to create input pipe");
        return 1;
    }

    if (pipe(pipe_from_coproc) == -1) {
        close(pipe_to_coproc[0]);
        close(pipe_to_coproc[1]);
        set_executor_error(executor, "coproc: failed to create output pipe");
        return 1;
    }

    pid_t pid = lush_fork();
    if (pid == -1) {
        close(pipe_to_coproc[0]);
        close(pipe_to_coproc[1]);
        close(pipe_from_coproc[0]);
        close(pipe_from_coproc[1]);
        set_executor_error(executor, "coproc: fork failed");
        return 1;
    }

    if (pid == 0) {
        // Child process (the coprocess)

        // Redirect stdin from pipe_to_coproc[0]
        close(pipe_to_coproc[1]); // Close write end
        dup2(pipe_to_coproc[0], STDIN_FILENO);
        close(pipe_to_coproc[0]);

        // Redirect stdout to pipe_from_coproc[1]
        close(pipe_from_coproc[0]); // Close read end
        dup2(pipe_from_coproc[1], STDOUT_FILENO);
        close(pipe_from_coproc[1]);

        // Execute the command
        int result = execute_node(executor, command);
        fflush(stdout);
        fflush(stderr);
        subshell_cleanup();
        _exit(result);
    }

    // Parent process

    // Close the ends we don't need
    close(pipe_to_coproc[0]);   // Close read end of input pipe
    close(pipe_from_coproc[1]); // Close write end of output pipe

    // Store file descriptors in NAME array
    // NAME[0] = fd to read from coproc (pipe_from_coproc[0])
    // NAME[1] = fd to write to coproc (pipe_to_coproc[1])
    char fd_str[32];

    // Set NAME[0] - read fd
    snprintf(fd_str, sizeof(fd_str), "%d", pipe_from_coproc[0]);
    symtable_set_array_element(coproc_name, "0", fd_str);

    // Set NAME[1] - write fd
    snprintf(fd_str, sizeof(fd_str), "%d", pipe_to_coproc[1]);
    symtable_set_array_element(coproc_name, "1", fd_str);

    // Store PID in NAME_PID variable
    char pid_var_name[256];
    snprintf(pid_var_name, sizeof(pid_var_name), "%s_PID", coproc_name);
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", (int)pid);
    symtable_set_global(pid_var_name, pid_str);

    // Add to job table (background job)
    // The coprocess runs in background, so we don't wait for it here

    return 0;
}

/**
 * @brief Execute an anonymous function (Zsh-style)
 *
 * Anonymous functions are immediately executed with a new scope.
 * Syntax: () { body }
 *
 * @param executor Executor context
 * @param anon_node Anonymous function node
 * @return Exit status of the function body
 */
static int execute_anonymous_function(executor_t *executor, node_t *anon_node) {
    if (!anon_node || anon_node->type != NODE_ANON_FUNCTION) {
        return 1;
    }

    node_t *body = anon_node->first_child;
    if (!body) {
        return 0; // Empty anonymous function
    }

    /* Collect and expand trailing arguments BEFORE pushing the
     * function scope. Arg expressions (e.g. $vars inside double-quoted
     * args) must resolve in the caller's scope, mirroring how
     * build_argv_from_ast / execute_function_call do for regular
     * function calls. The expanded strings are then set as positional
     * parameters once the new scope is active. */
    int argc = 0;
    for (node_t *arg = body->next_sibling; arg; arg = arg->next_sibling) {
        argc++;
    }

    char **argv = NULL;
    if (argc > 0) {
        argv = calloc((size_t)argc, sizeof(char *));
        if (!argv) {
            set_executor_error(executor,
                               "Failed to allocate anonymous function args");
            return 1;
        }
        int i = 0;
        for (node_t *arg = body->next_sibling; arg; arg = arg->next_sibling) {
            /* Use the shared type-aware expansion helper so anon-function
             * args follow the same per-node-type semantics as regular
             * command arguments — single-quoted stays literal, double-
             * quoted expands variables, arith / command substitution /
             * process substitution all dispatch correctly. */
            argv[i++] = expand_arg_node(executor, arg);
            if (!argv[i - 1]) {
                argv[i - 1] = strdup("");
            }
        }
    }

    // Create a new scope for the anonymous function
    if (symtable_push_scope(executor->symtable, SCOPE_FUNCTION,
                            "<anonymous>") != 0) {
        for (int i = 0; i < argc; i++) {
            free(argv[i]);
        }
        free(argv);
        set_executor_error(executor,
                           "Failed to create anonymous function scope");
        return 1;
    }

    // Set positional parameters $1..$N from the pre-expanded args.
    for (int i = 0; i < argc; i++) {
        char param_name[16];
        snprintf(param_name, sizeof(param_name), "%d", i + 1);
        symtable_set_local_var(executor->symtable, param_name,
                               argv[i] ? argv[i] : "");
    }
    char argc_str[16];
    snprintf(argc_str, sizeof(argc_str), "%d", argc);
    symtable_set_local_var(executor->symtable, "#", argc_str);

    // Free the expanded arg strings; symtable owns its own copies.
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);

    // Execute the body
    int result = execute_node(executor, body);

    // Check for function return (special code 200-455)
    if (result >= 200 && result <= 455) {
        result = result - 200; // Extract actual return value
    }

    // Pop the scope
    symtable_pop_scope(executor->symtable);

    return result;
}

/**
 * @brief Execute logical AND operator (&&)
 *
 * Short-circuit evaluation: executes right operand only if
 * left operand succeeds (returns 0).
 *
 * @param executor Executor context
 * @param and_node Logical AND node
 * @return Exit status of last executed operand
 */
static int execute_logical_and(executor_t *executor, node_t *and_node) {
    if (!and_node || and_node->type != NODE_LOGICAL_AND) {
        return 1;
    }

    node_t *left = and_node->first_child;
    node_t *right = left ? left->next_sibling : NULL;

    if (!left || !right) {
        set_executor_error(executor, "Logical AND missing operands");
        return 1;
    }

    // Execute left command
    int left_result = execute_node(executor, left);

    // Only execute right command if left succeeded (exit code 0)
    if (left_result == 0) {
        return execute_node(executor, right);
    }

    // Left failed, return its exit code without executing right
    return left_result;
}

/**
 * @brief Execute logical OR operator (||)
 *
 * Short-circuit evaluation: executes right operand only if
 * left operand fails (returns non-zero).
 *
 * @param executor Executor context
 * @param or_node Logical OR node
 * @return Exit status of last executed operand
 */
static int execute_logical_or(executor_t *executor, node_t *or_node) {
    if (!or_node || or_node->type != NODE_LOGICAL_OR) {
        return 1;
    }

    node_t *left = or_node->first_child;
    node_t *right = left ? left->next_sibling : NULL;

    if (!left || !right) {
        set_executor_error(executor, "Logical OR missing operands");
        return 1;
    }

    // Execute left command
    int left_result = execute_node(executor, left);

    // Only execute right command if left failed (non-zero exit code)
    if (left_result != 0) {
        return execute_node(executor, right);
    }

    // Left succeeded, return its exit code without executing right
    return left_result;
}

/**
 * @brief Add an argument to a dynamic argv list
 *
 * Dynamically grows the argument list as needed, doubling capacity
 * when full. Used during command argument building.
 *
 * @param argv_list Pointer to argument array
 * @param argv_count Pointer to current count
 * @param argv_capacity Pointer to current capacity
 * @param arg Argument string to add (ownership transferred)
 * @return 1 on success, 0 on allocation failure
 */
static int add_to_argv_list(char ***argv_list, int *argv_count,
                            int *argv_capacity, char *arg) {
    if (*argv_count >= *argv_capacity) {
        *argv_capacity = *argv_capacity ? *argv_capacity * 2 : 8;
        char **new_list = realloc(*argv_list, *argv_capacity * sizeof(char *));
        if (!new_list) {
            return 0; // Failed to expand
        }
        *argv_list = new_list;
    }
    (*argv_list)[(*argv_count)++] = arg;
    return 1;
}

/**
 * @brief Split text into fields using IFS delimiters
 *
 * Performs POSIX IFS field splitting on text. Default IFS is
 * space, tab, and newline.
 *
 * @param text Text to split
 * @param ifs Field separator characters (NULL for default)
 * @param count Output: number of fields produced
 * @return Array of field strings (caller must free), or NULL on error
 */
static char **ifs_field_split(const char *text, const char *ifs, int *count) {
    if (!text || !count) {
        *count = 0;
        return NULL;
    }

    // Default IFS if not provided
    if (!ifs) {
        ifs = " \t\n";
    }

    *count = 0;
    char **result = NULL;
    int capacity = 0;

    const char *start = text;
    const char *end = text;

    while (*end) {
        // Skip leading delimiters
        while (*start && strchr(ifs, *start)) {
            start++;
        }

        if (!*start)
            break;

        // Find end of current field
        end = start;
        while (*end && !strchr(ifs, *end)) {
            end++;
        }

        // Extract field
        size_t field_len = end - start;
        if (field_len > 0) {
            // Expand result array if needed
            if (*count >= capacity) {
                capacity = capacity ? capacity * 2 : 4;
                char **new_result = realloc(result, capacity * sizeof(char *));
                if (!new_result) {
                    // Cleanup on failure
                    for (int i = 0; i < *count; i++) {
                        free(result[i]);
                    }
                    free(result);
                    *count = 0;
                    return NULL;
                }
                result = new_result;
            }

            result[*count] = malloc(field_len + 1);
            if (!result[*count]) {
                // Cleanup on failure
                for (int i = 0; i < *count; i++) {
                    free(result[i]);
                }
                free(result);
                *count = 0;
                return NULL;
            }

            strncpy(result[*count], start, field_len);
            result[*count][field_len] = '\0';
            (*count)++;
        }

        start = end;
    }

    return result;
}

/**
 * @brief Build argument vector from command AST
 *
 * Constructs argv array from command node, performing:
 * - Variable expansion
 * - Brace expansion
 * - Glob expansion
 * - IFS field splitting
 * Excludes redirection nodes from the argument list.
 *
 * @param executor Executor context for variable lookup
 * @param command Command node to process
 * @param argc Output: argument count
 * @return NULL-terminated argv array (caller must free), or NULL on error
 */
/**
 * @brief Expand a single argument AST node to its string value.
 *
 * Dispatches by node type to the correct expansion routine:
 *   - NODE_STRING_LITERAL: no expansion (single-quoted; literal). If the
 *     content opens with $' and FEATURE_ANSI_QUOTING is enabled, decode
 *     it as an ANSI-C $'...' string instead.
 *   - NODE_STRING_EXPANDABLE: expand_quoted_string (double-quoted; vars
 *     expanded, glob/brace preserved by caller policy)
 *   - NODE_ARITH_EXP: expand_arithmetic ($((...)))
 *   - NODE_COMMAND_SUB: expand_command_substitution ($(...) or `...`)
 *   - NODE_PROC_SUB_IN / NODE_PROC_SUB_OUT: expand_process_substitution
 *   - anything else: expand_if_needed (general variable/expansion path)
 *
 * Used by build_argv_from_ast for command arguments and by
 * execute_anonymous_function for trailing positional args. Single
 * source of truth for per-node-type argument expansion across the
 * executor; callers needing a different post-expansion policy
 * (glob/brace/word-split) layer that on top of the value returned here.
 *
 * @param executor Executor context
 * @param node Argument node (must have val.str non-NULL)
 * @return Newly malloc'd expanded string, or NULL only if the
 *         underlying expansion fails (notably process substitution).
 */
/**
 * @brief Detect a "vector-yielding" argument and expand it to N words
 *
 * Several parameter-expansion shapes produce a sequence of words that,
 * when used in argv position, must occupy N separate slots rather than
 * one concatenated slot. This is the bash special-case that makes
 * `"$@"` / `"${arr[@]}"` / `"${!arr[@]}"` and their `[*]:N:M` slice
 * variants behave correctly: in unquoted context IFS-split happens on
 * the joined string, in quoted context the original element boundaries
 * are preserved.
 *
 * lush's parser strips outer quotes from the node's val.str, so we
 * can't distinguish `"${arr[@]}"` from `${arr[@]}` by punctuation
 * alone. Instead this helper matches by structure: if val.str is
 * exactly a single vector-yielding form (no surrounding text), we
 * explode it; otherwise we return false and let the caller fall back
 * to scalar expansion. The structural restriction is what bash uses
 * too -- `"prefix${arr[@]}suffix"` does NOT preserve word boundaries
 * (it becomes one concatenated string), only the bare form does.
 *
 * Recognized forms (after trimming surrounding whitespace):
 *   $@                       positional params
 *   $*                       positional params
 *   ${@}, ${*}               braced positional params
 *   ${NAME[@]}, ${NAME[*]}   array all-elements
 *   ${!NAME[@]}              array keys (assoc) / indices (indexed)
 *   ${NAME[@]:N},  ${NAME[@]:N:M}, ${NAME[*]:N:M}  array slice
 *
 * @param executor Executor context
 * @param node     Argument node
 * @param out_vec  Output: array of N newly-malloc'd strings (caller
 *                 frees each element AND the array). Untouched on false.
 * @param out_count Output: N (number of words produced). 0 is valid.
 * @return true if the node was a vector form and out_vec/out_count were
 *         populated; false otherwise.
 */
static bool try_expand_vector_arg(executor_t *executor, node_t *node,
                                  char ***out_vec, int *out_count) {
    if (!node || !node->val.str) {
        return false;
    }
    /* Only quoted-string and string-expandable shapes carry a single
     * parameter-expansion as their entire payload. Other node types
     * (command sub, arith, etc.) are not vector candidates. */
    if (node->type != NODE_STRING_EXPANDABLE && node->type != NODE_VAR) {
        return false;
    }

    const char *s = node->val.str;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        len--;
    }
    if (len == 0) {
        return false;
    }

    // $@ / $* (unbraced).
    bool positional_at =
        (len == 2 && s[0] == '$' && (s[1] == '@' || s[1] == '*'));
    // ${...} braced forms.
    bool braced = (len >= 3 && s[0] == '$' && s[1] == '{' && s[len - 1] == '}');
    /* Bare `$NAME` where NAME is an array. zsh expands bare array
     * references to N words in word-list contexts (for-loop iteration,
     * command argv) regardless of word-split setting. Bash's `$arr`
     * is the first element only, so only treat this as vector form
     * in zsh/lush mode. Detected here so execute_for and
     * build_argv_from_ast both honor it via the same helper.
     * Issue #99. */
    bool bare_array = false;
    if (!positional_at && !braced && len >= 2 && s[0] == '$' &&
        (isalpha((unsigned char)s[1]) || s[1] == '_')) {
        bool name_only = true;
        for (size_t k = 1; k < len; k++) {
            if (!isalnum((unsigned char)s[k]) && s[k] != '_') {
                name_only = false;
                break;
            }
        }
        if (name_only) {
            char name_buf[256];
            size_t nlen = len - 1;
            if (nlen < sizeof(name_buf)) {
                memcpy(name_buf, s + 1, nlen);
                name_buf[nlen] = '\0';
                array_value_t *probe = symtable_get_array(name_buf);
                if (probe) {
                    shell_mode_t mode = shell_mode_get();
                    /* Curated: zsh + lush explode bare $arr; bash + posix
                     * keep it scalar (first element via the existing
                     * expand_array_unsubscripted path). */
                    if (mode == SHELL_MODE_ZSH || mode == SHELL_MODE_LUSH) {
                        bare_array = true;
                    }
                }
            }
        }
    }

    if (!positional_at && !braced && !bare_array) {
        return false;
    }

    /* Distinguish what's inside the braces. For positional_at the
     * "name" is `@` / `*`; subscript is implied. For braced forms:
     * - `${@}` / `${*}` -> positional, same as $@/$*
     * - `${NAME[@]}` / `${NAME[*]}` -> array all
     * - `${!NAME[@]}` -> array keys
     * - any of the above with `:N` or `:N:M` slicing suffix
     * - anything else -> not a vector form
     */
    bool keys_form = false;
    const char *name_start = NULL;
    size_t name_len = 0;
    char subscript = '@'; // @ vs *; only matters for joining policy
    int slice_offset = 0;
    int slice_length = -1; // -1 = "to end"
    bool has_slice = false;
    bool is_positional = positional_at;

    if (positional_at) {
        subscript = s[1];
    } else if (bare_array) {
        /* Bare $NAME: name is s+1, length is len-1. Treat as if it
         * were ${NAME[@]} -- produce all elements as separate words. */
        name_start = s + 1;
        name_len = len - 1;
        subscript = '@';
    } else {
        // inner content between { and }
        const char *p = s + 2;
        const char *end = s + len - 1;
        if (p == end) {
            return false;
        }
        /* ${(FLAGS)NAME} -- zsh parameter-flag vector form. Recognised
         * flag chars: k (keys), v (values, default), o (sort ascending),
         * O (sort descending), a (array-order, no sort), u (unique).
         * Builds the vector directly and short-circuits the rest of
         * try_expand_vector_arg. Issue #104. */
        if (*p == '(') {
            const char *flag_start = p + 1;
            const char *flag_end = flag_start;
            while (flag_end < end && *flag_end != ')') {
                flag_end++;
            }
            if (flag_end < end && *flag_end == ')') {
                bool flag_k = false, flag_o = false, flag_O = false;
                bool flag_u = false;
                bool ok_flags = true;
                for (const char *f = flag_start; f < flag_end; f++) {
                    switch (*f) {
                    case 'k':
                        flag_k = true;
                        break;
                    case 'v':
                        flag_k = false;
                        break;
                    case 'o':
                        flag_o = true;
                        break;
                    case 'O':
                        flag_O = true;
                        break;
                    case 'a':
                        // array order, no sort -- default
                        flag_o = false;
                        flag_O = false;
                        break;
                    case 'u':
                        flag_u = true;
                        break;
                    case '@':
                        // Per SEMANTICS.md section 3.7, (@) is a
                        // spelling alias for vector presentation --
                        // redundant with the [@] subscript. Accept
                        // it as a no-op flag so ${(@)arr} yields
                        // exactly what ${arr[@]} would.
                        break;
                    default:
                        ok_flags = false;
                        break;
                    }
                }
                const char *np = flag_end + 1;
                const char *nstart = np;
                while (np < end &&
                       (isalnum((unsigned char)*np) || *np == '_')) {
                    np++;
                }
                if (ok_flags && np == end && np > nstart) {
                    char nbuf[256];
                    size_t nlen = (size_t)(np - nstart);
                    if (nlen < sizeof(nbuf)) {
                        memcpy(nbuf, nstart, nlen);
                        nbuf[nlen] = '\0';
                        array_value_t *arr = symtable_get_array(nbuf);
                        if (arr) {
                            size_t kc = 0;
                            char **items = NULL;
                            if (flag_k) {
                                items = symtable_array_get_keys(arr, &kc);
                            } else {
                                size_t total = symtable_array_length(arr);
                                items = malloc(sizeof(char *) * (total + 1));
                                if (items) {
                                    for (size_t i = 0; i < total; i++) {
                                        const char *e =
                                            symtable_array_get_index(arr,
                                                                     (int)i);
                                        items[i] = strdup(e ? e : "");
                                    }
                                    kc = total;
                                }
                            }
                            if (items) {
                                if (flag_u) {
                                    size_t w = 0;
                                    for (size_t i = 0; i < kc; i++) {
                                        bool dup = false;
                                        for (size_t j = 0; j < w; j++) {
                                            if (strcmp(items[i], items[j]) ==
                                                0) {
                                                dup = true;
                                                break;
                                            }
                                        }
                                        if (dup) {
                                            free(items[i]);
                                        } else {
                                            items[w++] = items[i];
                                        }
                                    }
                                    kc = w;
                                }
                                if (flag_o || flag_O) {
                                    for (size_t i = 0; i + 1 < kc; i++) {
                                        for (size_t j = i + 1; j < kc; j++) {
                                            int cmp =
                                                strcmp(items[i], items[j]);
                                            if ((flag_O && cmp < 0) ||
                                                (!flag_O && cmp > 0)) {
                                                char *tmp = items[i];
                                                items[i] = items[j];
                                                items[j] = tmp;
                                            }
                                        }
                                    }
                                }
                                char **vec = NULL;
                                int vcount = 0, vcap = 0;
                                bool ok = true;
                                for (size_t i = 0; i < kc; i++) {
                                    if (!add_to_argv_list(&vec, &vcount, &vcap,
                                                          items[i])) {
                                        ok = false;
                                        break;
                                    }
                                }
                                free(items);
                                if (!ok) {
                                    for (int j = 0; j < vcount; j++) {
                                        free(vec[j]);
                                    }
                                    free(vec);
                                    return false;
                                }
                                *out_vec = vec;
                                *out_count = vcount;
                                return true;
                            }
                        }
                    }
                }
            }
            // Not our shape -- not a vector form.
            return false;
        }
        if (*p == '!') {
            keys_form = true;
            p++;
        }
        if (p == end) {
            return false;
        }
        // Special-cased ${@} / ${*}: positional, no subscript.
        if (!keys_form && (end - p) == 1 && (*p == '@' || *p == '*')) {
            is_positional = true;
            subscript = *p;
        } else {
            // Must be NAME[@] / NAME[*] optionally followed by :N or :N:M
            name_start = p;
            while (p < end && (isalnum((unsigned char)*p) || *p == '_')) {
                p++;
            }
            name_len = (size_t)(p - name_start);
            if (name_len == 0) {
                return false;
            }
            /* Braced bare `${NAME}` (no subscript). Per SEMANTICS.md
             * section 3.9, a bare reference to a list/map value is a
             * vector-yielding expansion -- it contributes its elements
             * to the surrounding argv/word-list slot, exactly as
             * `${NAME[@]}` does. Curated zsh/lush behavior; bash and
             * posix mode keep the legacy "first-element" form via
             * expand_array_unsubscripted. The unbraced `$NAME` case
             * is already handled above; this is the braced peer.
             * Issue: SEMANTICS section 3.9 conformance. */
            if (!keys_form && p == end) {
                char nb[256];
                if (name_len >= sizeof(nb)) {
                    return false;
                }
                memcpy(nb, name_start, name_len);
                nb[name_len] = '\0';
                array_value_t *probe = symtable_get_array(nb);
                if (!probe) {
                    return false;
                }
                shell_mode_t mode = shell_mode_get();
                if (mode != SHELL_MODE_ZSH && mode != SHELL_MODE_LUSH) {
                    return false;
                }
                subscript = '@';
                // Fall through to the per-array assembly path below.
                goto braced_bare_array_ready;
            }
            if (p >= end || *p != '[') {
                return false;
            }
            p++;
            if (p >= end || (*p != '@' && *p != '*')) {
                return false;
            }
            subscript = *p;
            p++;
            if (p >= end || *p != ']') {
                return false;
            }
            p++;
            // Optional slicing :N or :N:M.
            if (p < end && *p == ':') {
                has_slice = true;
                p++;
                if (p >= end) {
                    return false;
                }
                char *endp = NULL;
                /* end is past the trailing `}`; we need a NUL-terminated
                 * span. Copy the slice spec into a scratch buffer. */
                size_t spec_len = (size_t)(end - p);
                char spec[64];
                if (spec_len >= sizeof(spec)) {
                    return false;
                }
                memcpy(spec, p, spec_len);
                spec[spec_len] = '\0';
                slice_offset = (int)strtol(spec, &endp, 10);
                if (!endp) {
                    return false;
                }
                if (*endp == ':') {
                    slice_length = (int)strtol(endp + 1, NULL, 10);
                } else if (*endp != '\0') {
                    return false;
                }
            } else if (p != end) {
                // Junk between `]` and `}`. Not a recognised vector form.
                return false;
            }
        }
    }

braced_bare_array_ready:;
    // Now produce the element vector.
    char **vec = NULL;
    int vcount = 0;
    int vcap = 0;

    if (is_positional) {
        /* Iterate $1..$N from positional params. Match the existing
         * "$@" loop in execute_for: in function scope use symtable
         * "1".."N"; otherwise use shell_argv. */
        int total;
        if (symtable_in_function_scope(executor->symtable)) {
            char *argc_str = symtable_get_var(executor->symtable, "#");
            total = argc_str ? atoi(argc_str) : 0;
            free(argc_str);
            for (int i = 1; i <= total; i++) {
                char name[16];
                snprintf(name, sizeof(name), "%d", i);
                char *val = symtable_get_var(executor->symtable, name);
                if (!val) {
                    val = strdup("");
                }
                if (!add_to_argv_list(&vec, &vcount, &vcap, val)) {
                    for (int k = 0; k < vcount; k++) {
                        free(vec[k]);
                    }
                    free(vec);
                    return false;
                }
            }
        } else {
            for (int i = 1; i < shell_argc; i++) {
                char *val = strdup(shell_argv[i] ? shell_argv[i] : "");
                if (!add_to_argv_list(&vec, &vcount, &vcap, val)) {
                    free(val);
                    for (int k = 0; k < vcount; k++) {
                        free(vec[k]);
                    }
                    free(vec);
                    return false;
                }
            }
        }
    } else {
        // Array name lookup. Need a NUL-terminated name.
        char name_buf[256];
        if (name_len >= sizeof(name_buf)) {
            return false;
        }
        memcpy(name_buf, name_start, name_len);
        name_buf[name_len] = '\0';

        array_value_t *array = symtable_get_array(name_buf);
        if (!array) {
            // Not actually an array -- not our case; fall back.
            return false;
        }

        if (keys_form) {
            // Produce keys (assoc) or indices (indexed). Honor slicing.
            if (array->is_associative) {
                size_t kcount = 0;
                char **keys = symtable_array_get_keys(array, &kcount);
                if (!keys) {
                    return false;
                }
                int start = 0, end_idx = (int)kcount - 1;
                if (has_slice) {
                    start = slice_offset;
                    if (start < 0) {
                        start = (int)kcount + start;
                        if (start < 0) {
                            start = 0;
                        }
                    }
                    if (slice_length >= 0) {
                        end_idx = start + slice_length - 1;
                    } else {
                        end_idx = (int)kcount - 1;
                    }
                    if (end_idx >= (int)kcount) {
                        end_idx = (int)kcount - 1;
                    }
                }
                for (int k = start; k <= end_idx; k++) {
                    if (!add_to_argv_list(&vec, &vcount, &vcap,
                                          strdup(keys[k]))) {
                        for (int j = 0; j < vcount; j++) {
                            free(vec[j]);
                        }
                        free(vec);
                        for (size_t j = 0; j < kcount; j++) {
                            free(keys[j]);
                        }
                        free(keys);
                        return false;
                    }
                }
                for (size_t j = 0; j < kcount; j++) {
                    free(keys[j]);
                }
                free(keys);
            } else {
                /* Indexed array: keys are the ACTUAL stored indices.
                 * For dense arrays these are 0..N-1; for sparse arrays
                 * they are the explicit indices assigned. The prior
                 * implementation generated 0..N-1 dense, which
                 * silently turned sparse arrays into dense ones and
                 * broke `for k in "${!arr[@]}"; do echo arr[$k]` on
                 * sparse data (issue #101). symtable_array_get_keys
                 * returns the real indices via array->indices[]. */
                size_t kcount = 0;
                char **keys = symtable_array_get_keys(array, &kcount);
                if (!keys) {
                    return false;
                }
                int start = 0, end_idx = (int)kcount - 1;
                if (has_slice) {
                    start = slice_offset;
                    if (start < 0) {
                        start = (int)kcount + start;
                        if (start < 0) {
                            start = 0;
                        }
                    }
                    if (slice_length >= 0) {
                        end_idx = start + slice_length - 1;
                    } else {
                        end_idx = (int)kcount - 1;
                    }
                    if (end_idx >= (int)kcount) {
                        end_idx = (int)kcount - 1;
                    }
                }
                for (int k = start; k <= end_idx; k++) {
                    if (!add_to_argv_list(&vec, &vcount, &vcap,
                                          strdup(keys[k]))) {
                        for (int j = 0; j < vcount; j++) {
                            free(vec[j]);
                        }
                        free(vec);
                        for (size_t j = 0; j < kcount; j++) {
                            free(keys[j]);
                        }
                        free(keys);
                        return false;
                    }
                }
                for (size_t j = 0; j < kcount; j++) {
                    free(keys[j]);
                }
                free(keys);
            }
        } else {
            // Produce values. Honor slicing.
            size_t total = symtable_array_length(array);
            int start = 0, end_idx = (int)total - 1;
            if (has_slice) {
                start = slice_offset;
                if (start < 0) {
                    start = (int)total + start;
                    if (start < 0) {
                        start = 0;
                    }
                }
                if (slice_length >= 0) {
                    end_idx = start + slice_length - 1;
                } else {
                    end_idx = (int)total - 1;
                }
                if (end_idx >= (int)total) {
                    end_idx = (int)total - 1;
                }
            }
            for (int k = start; k <= end_idx; k++) {
                const char *elem = symtable_array_get_index(array, k);
                if (!add_to_argv_list(&vec, &vcount, &vcap,
                                      strdup(elem ? elem : ""))) {
                    for (int j = 0; j < vcount; j++) {
                        free(vec[j]);
                    }
                    free(vec);
                    return false;
                }
            }
        }
    }

    (void)subscript; /* @ vs * matters only for joining; vector form
                        is the same for both. */

    *out_vec = vec;
    *out_count = vcount;
    return true;
}

static char *expand_arg_node(executor_t *executor, node_t *node) {
    if (!node || !node->val.str) {
        return strdup("");
    }
    switch (node->type) {
    case NODE_STRING_LITERAL:
        if (node->val.str[0] == '$' && node->val.str[1] == '\'' &&
            shell_mode_allows(FEATURE_ANSI_QUOTING)) {
            size_t len = strlen(node->val.str);
            if (len >= 3 && node->val.str[len - 1] == '\'') {
                return expand_ansi_c_string(node->val.str + 2, len - 3);
            }
        }
        return strdup(node->val.str);
    case NODE_STRING_EXPANDABLE:
        /* Per parser.c collect_word_argument: word-context backslashes
         * have been pre-stripped during multi-token concat, so any `\X`
         * still present in node->val.str came from a `"..."` segment and
         * must be resolved with double-quote rules. */
        return expand_quoted_string(executor, node->val.str, true);
    case NODE_ARITH_EXP:
        return expand_arithmetic(executor, node->val.str);
    case NODE_COMMAND_SUB:
        return expand_command_substitution(executor, node->val.str);
    case NODE_PROC_SUB_IN:
    case NODE_PROC_SUB_OUT:
        return expand_process_substitution(executor, node);
    default:
        return expand_if_needed(executor, node->val.str);
    }
}

/**
 * @brief Expand an array reference with no subscript ($arr or ${arr}).
 *
 * Mode-aware semantics — matches what each shell's $arr / ${arr} form
 * actually produces:
 *   - bash:  first element only (equivalent to ${arr[0]})
 *   - zsh:   all elements joined by space (default IFS behavior)
 *   - lush:  all elements joined by space (curated pick: explicit
 *            ${arr[0]} is available for bash-style first-element
 *            semantics; the joined form matches the common zsh idiom
 *            of `for x in $arr; do ...`)
 *   - POSIX: arrays don't exist in POSIX; if one happens to be defined
 *            (because lush carries the array across mode switches),
 *            fall back to first-element semantics (matches zsh's POSIX
 *            and ksh emulations and is least surprising for bash-leaning
 *            POSIX users)
 *
 * Single source of truth for "$a / ${a} on an unsubscripted array" so
 * bare and brace forms agree, and both honor the active shell mode.
 *
 * @param executor Executor context (currently unused but kept for
 *                 API uniformity with other expansion helpers)
 * @param array Array value to expand (may be NULL)
 * @return Newly malloc'd string (caller frees), empty on NULL/error
 */
static char *expand_array_unsubscripted(executor_t *executor,
                                        array_value_t *array,
                                        const char *arr_name) {
    if (!array) {
        return strdup("");
    }
    shell_mode_t mode = shell_mode_get();
    if (mode == SHELL_MODE_BASH || mode == SHELL_MODE_POSIX) {
        const char *first = symtable_array_get_index(array, 0);
        return strdup(first ? first : "");
    }
    /* zsh/lush mode: per SEMANTICS.md section 3.9, a list/map value
     * reaching a scalar slot (or glued to text within a word) is a
     * runtime type error. We get here only AFTER try_expand_vector_arg
     * declined to handle the reference as vector-yielding -- which
     * means the surrounding context is scalar (variable assignment
     * RHS, case word, here-string, arithmetic operand, conditional-
     * expression operand, or a within-word "glued" position). Emit
     * the type-mismatch diagnostic via the structured-error system
     * and request a POSIX shell abort so a script halts before the
     * bad value reaches a downstream command. */
    shell_error_t *err = shell_error_create(
        SHELL_ERR_TYPE_MISMATCH, SHELL_SEVERITY_ERROR,
        executor_current_loc(executor),
        "type mismatch: %s value ${%s} in a scalar position",
        array->is_associative ? "map" : "list", arr_name ? arr_name : "?");
    if (err) {
        shell_error_set_suggestion(
            err, "join the list explicitly to place it in a string position -- "
                 "${name[*]} for space-joining, or an explicit join.");
        shell_error_display(err, stderr, isatty(STDERR_FILENO));
        shell_error_free(err);
        if (executor) {
            executor->has_error = true;
        }
    } else if (executor) {
        executor_error_report(
            executor, SHELL_ERR_TYPE_MISMATCH, executor_current_loc(executor),
            "type mismatch: %s value ${%s} in a scalar position",
            array->is_associative ? "map" : "list", arr_name ? arr_name : "?");
    }
    if (executor) {
        executor_request_posix_exit(executor, 1);
    }
    return strdup("");
}

static char **build_argv_from_ast(executor_t *executor, node_t *command,
                                  int *argc) {
    if (!executor || !command || !argc) {
        return NULL;
    }

    // Dynamic argument list to handle glob expansion
    char **argv_list = NULL;
    int argv_count = 0;
    int argv_capacity = 0;

    // Find here document delimiters to exclude
    char *heredoc_delimiters[10] = {0};
    int delimiter_count = 0;

    node_t *child = command->first_child;
    while (child && delimiter_count < 10) {
        if (child->type == NODE_REDIR_HEREDOC ||
            child->type == NODE_REDIR_HEREDOC_STRIP) {
            if (child->val.str) {
                heredoc_delimiters[delimiter_count] = strdup(child->val.str);
                delimiter_count++;
            }
        }
        child = child->next_sibling;
    }

    // Add command name (no glob expansion for command names)
    if (command->val.str) {
        char *expanded_cmd = expand_if_needed(executor, command->val.str);
        if (!add_to_argv_list(&argv_list, &argv_count, &argv_capacity,
                              expanded_cmd)) {
            free(expanded_cmd);
            goto cleanup_and_fail;
        }
    }

    // Process arguments with glob expansion
    child = command->first_child;
    while (child) {
        // Skip redirection nodes and cmd_prefix assignments (NODE_ASSIGN
        // children are applied as the command's temporary environment,
        // not passed as argv words).
        if (!is_redirection_node(child) && child->type != NODE_ASSIGN) {
            if (child->val.str) {
                // Check if this is a here document delimiter
                bool is_delimiter = false;
                for (int i = 0; i < delimiter_count; i++) {
                    if (heredoc_delimiters[i] &&
                        strcmp(child->val.str, heredoc_delimiters[i]) == 0) {
                        is_delimiter = true;
                        break;
                    }
                }

                if (!is_delimiter) {
                    /* Vector-yielding expansion: `"$@"`, `"${arr[@]}"`,
                     * `"${!arr[@]}"` and their slice variants must
                     * produce N separate argv slots, not one
                     * concatenated slot. Detect before scalar
                     * expansion so the original element boundaries
                     * survive (issue #97). */
                    char **vec = NULL;
                    int vcount = 0;
                    if (try_expand_vector_arg(executor, child, &vec, &vcount)) {
                        for (int j = 0; j < vcount; j++) {
                            if (!add_to_argv_list(&argv_list, &argv_count,
                                                  &argv_capacity, vec[j])) {
                                for (int k = j; k < vcount; k++) {
                                    free(vec[k]);
                                }
                                free(vec);
                                goto cleanup_and_fail;
                            }
                        }
                        free(vec);
                        child = child->next_sibling;
                        continue;
                    }

                    /* Type-aware expansion via the shared helper.
                     * Process substitution is the only path that
                     * propagates failure as NULL — everything else
                     * either succeeds or returns "". */
                    char *expanded_arg = expand_arg_node(executor, child);
                    if (!expanded_arg && (child->type == NODE_PROC_SUB_IN ||
                                          child->type == NODE_PROC_SUB_OUT)) {
                        goto cleanup_and_fail;
                    }

                    if (getenv("NEW_PARSER_DEBUG")) {
                        fprintf(stderr,
                                "DEBUG: Processing argument: '%s' -> '%s'\n",
                                child->val.str, expanded_arg);
                    }

                    // Check if argument needs brace expansion first
                    // Skip brace/glob expansion for quoted strings
                    if (child->type != NODE_STRING_LITERAL &&
                        child->type != NODE_STRING_EXPANDABLE &&
                        needs_brace_expansion(expanded_arg)) {
                        int brace_count;
                        char **brace_results =
                            expand_brace_pattern(expanded_arg, &brace_count);

                        if (brace_count == BRACE_EXPANSION_LIMIT_SENTINEL) {
                            set_executor_error(
                                executor,
                                "brace expansion exceeds configured limit "
                                "(behavior.brace_expansion_max)");
                            executor->expansion_error = true;
                            executor->expansion_exit_status = 1;
                            free(expanded_arg);
                            goto cleanup_and_fail;
                        }
                        if (brace_results) {
                            // Process each brace expansion result for potential
                            // glob expansion
                            for (int j = 0; j < brace_count; j++) {
                                if (needs_glob_expansion(brace_results[j])) {
                                    int glob_count;
                                    char **glob_results = expand_glob_pattern(
                                        brace_results[j], &glob_count);

                                    if (glob_results) {
                                        // Add all glob results, free brace
                                        // result since we won't use it
                                        free(brace_results[j]);
                                        for (int k = 0; k < glob_count; k++) {
                                            if (!add_to_argv_list(
                                                    &argv_list, &argv_count,
                                                    &argv_capacity,
                                                    glob_results[k])) {
                                                // Cleanup remaining strings on
                                                // failure
                                                for (int l = k; l < glob_count;
                                                     l++) {
                                                    free(glob_results[l]);
                                                }
                                                free(glob_results);
                                                for (int l = j + 1;
                                                     l < brace_count; l++) {
                                                    free(brace_results[l]);
                                                }
                                                free(brace_results);
                                                free(expanded_arg);
                                                goto cleanup_and_fail;
                                            }
                                        }
                                        free(glob_results);
                                    } else {
                                        // Glob expansion failed, use brace
                                        // result
                                        if (!add_to_argv_list(
                                                &argv_list, &argv_count,
                                                &argv_capacity,
                                                brace_results[j])) {
                                            for (int l = j + 1; l < brace_count;
                                                 l++) {
                                                free(brace_results[l]);
                                            }
                                            free(brace_results);
                                            free(expanded_arg);
                                            goto cleanup_and_fail;
                                        }
                                    }
                                } else {
                                    // No glob expansion needed, use brace
                                    // result directly
                                    if (!add_to_argv_list(
                                            &argv_list, &argv_count,
                                            &argv_capacity, brace_results[j])) {
                                        for (int l = j + 1; l < brace_count;
                                             l++) {
                                            free(brace_results[l]);
                                        }
                                        free(brace_results);
                                        free(expanded_arg);
                                        goto cleanup_and_fail;
                                    }
                                }
                            }
                            free(brace_results);
                        } else {
                            // Brace expansion failed, fall back to normal
                            // expansion
                            if (needs_glob_expansion(expanded_arg)) {
                                int glob_count;
                                char **glob_results = expand_glob_pattern(
                                    expanded_arg, &glob_count);

                                if (glob_results) {
                                    for (int j = 0; j < glob_count; j++) {
                                        if (!add_to_argv_list(
                                                &argv_list, &argv_count,
                                                &argv_capacity,
                                                glob_results[j])) {
                                            for (int k = j; k < glob_count;
                                                 k++) {
                                                free(glob_results[k]);
                                            }
                                            free(glob_results);
                                            free(expanded_arg);
                                            goto cleanup_and_fail;
                                        }
                                    }
                                    free(glob_results);
                                } else {
                                    if (!add_to_argv_list(
                                            &argv_list, &argv_count,
                                            &argv_capacity, expanded_arg)) {
                                        free(expanded_arg);
                                        goto cleanup_and_fail;
                                    }
                                    expanded_arg =
                                        NULL; // Ownership transferred
                                }
                            } else {
                                if (!add_to_argv_list(&argv_list, &argv_count,
                                                      &argv_capacity,
                                                      expanded_arg)) {
                                    free(expanded_arg);
                                    goto cleanup_and_fail;
                                }
                                expanded_arg = NULL; // Ownership transferred
                            }
                        }
                        if (expanded_arg) {
                            free(expanded_arg);
                        }
                    } else if (child->type != NODE_STRING_LITERAL &&
                               child->type != NODE_STRING_EXPANDABLE &&
                               needs_glob_expansion(expanded_arg)) {
                        // No brace expansion, check for glob expansion
                        // Skip glob expansion for quoted strings
                        int glob_count;
                        char **glob_results =
                            expand_glob_pattern(expanded_arg, &glob_count);

                        if (glob_results) {
                            // Add all glob results
                            for (int j = 0; j < glob_count; j++) {
                                if (!add_to_argv_list(&argv_list, &argv_count,
                                                      &argv_capacity,
                                                      glob_results[j])) {
                                    // Cleanup on failure
                                    for (int k = j; k < glob_count; k++) {
                                        free(glob_results[k]);
                                    }
                                    free(glob_results);
                                    free(expanded_arg);
                                    goto cleanup_and_fail;
                                }
                            }
                            free(glob_results); // Free the array but not the
                                                // strings
                        } else {
                            // Glob expansion failed, use original
                            if (!add_to_argv_list(&argv_list, &argv_count,
                                                  &argv_capacity,
                                                  expanded_arg)) {
                                free(expanded_arg);
                                goto cleanup_and_fail;
                            }
                        }
                        free(
                            expanded_arg); // We copied the strings or used them
                    } else {
                        // Check if this needs field splitting
                        // - Command substitution $(cmd): always split (all
                        // shells do this)
                        // - Parameter expansion $var: only split if
                        // FEATURE_WORD_SPLIT_DEFAULT enabled
                        // - Quoted strings: never split
                        bool should_word_split = false;
                        if (child->type == NODE_COMMAND_SUB) {
                            // Command substitution always gets word split
                            should_word_split = true;
                        } else if (child->type == NODE_VAR &&
                                   shell_mode_allows(
                                       FEATURE_WORD_SPLIT_DEFAULT)) {
                            // Parameter expansion only splits if feature
                            // enabled
                            should_word_split = true;
                        }
                        // Quoted strings (NODE_STRING_LITERAL,
                        // NODE_STRING_EXPANDABLE) never split

                        if (should_word_split) {

                            // Get IFS for field splitting
                            const char *ifs =
                                symtable_get(executor->symtable, "IFS");
                            if (!ifs) {
                                ifs = " \t\n"; // Default IFS
                            }

                            // Check if expanded_arg contains any IFS characters
                            bool needs_splitting = false;
                            for (const char *p = ifs; *p; p++) {
                                if (strchr(expanded_arg, *p)) {
                                    needs_splitting = true;
                                    break;
                                }
                            }

                            if (needs_splitting) {
                                int field_count = 0;
                                char **fields = ifs_field_split(
                                    expanded_arg, ifs, &field_count);

                                if (fields && field_count > 0) {
                                    // Add each field as separate argument
                                    for (int i = 0; i < field_count; i++) {
                                        if (!add_to_argv_list(
                                                &argv_list, &argv_count,
                                                &argv_capacity, fields[i])) {
                                            // Cleanup remaining fields on
                                            // failure
                                            for (int j = i; j < field_count;
                                                 j++) {
                                                free(fields[j]);
                                            }
                                            free(fields);
                                            free(expanded_arg);
                                            goto cleanup_and_fail;
                                        }
                                        // Ownership transferred, don't free
                                        // fields[i]
                                    }
                                    free(fields);
                                    free(expanded_arg);
                                } else {
                                    // Field splitting failed, use original
                                    if (!add_to_argv_list(
                                            &argv_list, &argv_count,
                                            &argv_capacity, expanded_arg)) {
                                        free(expanded_arg);
                                        goto cleanup_and_fail;
                                    }
                                }
                            } else {
                                // No field splitting needed
                                if (!add_to_argv_list(&argv_list, &argv_count,
                                                      &argv_capacity,
                                                      expanded_arg)) {
                                    free(expanded_arg);
                                    goto cleanup_and_fail;
                                }
                            }
                        } else {
                            // No field splitting for non-variables
                            if (!add_to_argv_list(&argv_list, &argv_count,
                                                  &argv_capacity,
                                                  expanded_arg)) {
                                free(expanded_arg);
                                goto cleanup_and_fail;
                            }
                        }
                    }
                }
            }
        }
        child = child->next_sibling;
    }

    if (argv_count == 0) {
        *argc = 0;
        free(argv_list);
        goto cleanup_delimiters;
    }

    // Convert to final argv array
    char **argv = malloc((argv_count + 1) * sizeof(char *));
    if (!argv) {
        goto cleanup_and_fail;
    }

    for (int i = 0; i < argv_count; i++) {
        argv[i] = argv_list[i];
    }
    argv[argv_count] = NULL;

    *argc = argv_count;
    free(argv_list);

    // Clean up here document delimiters
    for (int k = 0; k < delimiter_count; k++) {
        free(heredoc_delimiters[k]);
    }

    return argv;

cleanup_and_fail:
    // Free all allocated arguments
    for (int i = 0; i < argv_count; i++) {
        free(argv_list[i]);
    }
    free(argv_list);

cleanup_delimiters:
    // Clean up here document delimiters
    for (int k = 0; k < delimiter_count; k++) {
        free(heredoc_delimiters[k]);
    }

    *argc = 0;
    return NULL;
}

/**
 * @brief Expand variables, arithmetic, and command substitutions if needed
 *
 * Checks text for expansion markers ($, ~, `) and applies appropriate
 * expansion. Handles tilde expansion, variable expansion, arithmetic
 * expansion $((...)), and command substitution $(...).
 *
 * @param executor Executor context for variable lookup
 * @param text Text to potentially expand
 * @return Expanded string (caller must free), or copy of original
 */
/**
 * @brief Expand a top-level kind sigil token (`@name` / `%name`).
 *
 * Caller has already established that the text starts with `@` or `%` and the
 * post-sigil span is a valid identifier.  Resolves the identifier against the
 * symbol table and produces the kind-aware presentation:
 *
 *   @scalar  -> scalar value (one element after field splitting)
 *   @list    -> space-joined elements (N elements)
 *   @map     -> space-joined values (N elements, insertion order)
 *   %scalar  -> SHELL_ERR_TYPE_MISMATCH
 *   %list    -> "0 v0 1 v1 ... " alternating (2N elements)
 *   %map     -> "k1 v1 k2 v2 ... " alternating (2N elements)
 *
 * Sigil mode (`@` or `%`) is the first character of `text`; the identifier
 * runs from `text + 1` to the end of the buffer.  Returns an owned string the
 * caller must free; on type mismatch, returns an empty string after queueing
 * a structured error.
 */
static char *expand_kind_sigil(executor_t *executor, const char *text) {
    char sigil = text[0];
    const char *name = text + 1;

    lush_value_view_t view;
    if (!symtable_lookup(name, &view)) {
        // Unset name: empty expansion -- matches bash `${var:-}` shape for an
        // unset variable in unquoted context.
        return strdup("");
    }

    char *result = NULL;

    if (sigil == '@') {
        switch (view.kind) {
        case LUSH_VALUE_SCALAR:
            result = strdup(view.scalar_value ? view.scalar_value : "");
            break;
        case LUSH_VALUE_LIST:
            result = view.array ? symtable_array_expand(view.array, " ")
                                : strdup("");
            break;
        case LUSH_VALUE_MAP: {
            size_t count = 0;
            char **values = view.array
                                ? symtable_array_get_values(view.array, &count)
                                : NULL;
            size_t total = 1;
            for (size_t i = 0; i < count; i++) {
                total += strlen(values[i] ? values[i] : "") + 1;
            }
            result = calloc(1, total);
            if (result) {
                size_t off = 0;
                for (size_t i = 0; i < count; i++) {
                    const char *v = values[i] ? values[i] : "";
                    if (i > 0) {
                        result[off++] = ' ';
                    }
                    size_t vl = strlen(v);
                    memcpy(result + off, v, vl);
                    off += vl;
                }
            }
            if (values) {
                for (size_t i = 0; i < count; i++) {
                    free(values[i]);
                }
                free(values);
            }
            break;
        }
        default:
            result = strdup("");
            break;
        }
    } else { // sigil == '%'
        switch (view.kind) {
        case LUSH_VALUE_SCALAR:
            executor_error_add(
                executor, SHELL_ERR_TYPE_MISMATCH, SOURCE_LOC_UNKNOWN,
                "%%%s: pair sigil on scalar -- "
                "a singleton has no pair component (use @%s for vector "
                "context or declare %s as a list/map)",
                name, name, name);
            result = strdup("");
            break;
        case LUSH_VALUE_LIST: {
            size_t count = 0;
            char **values = view.array
                                ? symtable_array_get_values(view.array, &count)
                                : NULL;
            // worst case: "INT v INT v ..." -- 32 bytes per entry is plenty
            size_t total = 1;
            for (size_t i = 0; i < count; i++) {
                total += 32 + strlen(values[i] ? values[i] : "") + 2;
            }
            result = calloc(1, total);
            if (result) {
                size_t off = 0;
                for (size_t i = 0; i < count; i++) {
                    const char *v = values[i] ? values[i] : "";
                    off +=
                        (size_t)snprintf(result + off, total - off, "%s%zu %s",
                                         i == 0 ? "" : " ", i, v);
                }
            }
            if (values) {
                for (size_t i = 0; i < count; i++) {
                    free(values[i]);
                }
                free(values);
            }
            break;
        }
        case LUSH_VALUE_MAP: {
            size_t kcount = 0;
            char **keys = view.array
                              ? symtable_array_get_keys(view.array, &kcount)
                              : NULL;
            size_t total = 1;
            for (size_t i = 0; i < kcount; i++) {
                const char *v = symtable_array_get_assoc(view.array, keys[i]);
                total += strlen(keys[i]) + 1 + strlen(v ? v : "") + 1;
            }
            result = calloc(1, total);
            if (result) {
                size_t off = 0;
                for (size_t i = 0; i < kcount; i++) {
                    const char *v =
                        symtable_array_get_assoc(view.array, keys[i]);
                    off += (size_t)snprintf(result + off, total - off,
                                            "%s%s %s", i == 0 ? "" : " ",
                                            keys[i], v ? v : "");
                }
            }
            if (keys) {
                for (size_t i = 0; i < kcount; i++) {
                    free(keys[i]);
                }
                free(keys);
            }
            break;
        }
        default:
            result = strdup("");
            break;
        }
    }

    lush_value_view_clear(&view);
    return result ? result : strdup("");
}

char *expand_if_needed(executor_t *executor, const char *text) {
    if (!executor || !text) {
        return NULL;
    }

    // zsh `$+NAME` / `$+NAME[SUBSCRIPT]` unbraced is-set test. Rewrite
    // to the braced ${+NAME...} form so the existing parameter-expansion
    // handler in parse_parameter_expansion picks it up. The whole
    // span is one token coming out of the tokenizer (see the $+IDENT
    // handler in src/tokenizer.c), so the rewrite is just text-shape.
    if (text[0] == '$' && text[1] == '+' &&
        (isalpha((unsigned char)text[2]) || text[2] == '_')) {
        size_t orig_len = strlen(text);
        char *braced = malloc(orig_len + 3);
        if (braced) {
            braced[0] = '$';
            braced[1] = '{';
            braced[2] = '+';
            memcpy(braced + 3, text + 2, orig_len - 2);
            braced[orig_len + 1] = '}';
            braced[orig_len + 2] = '\0';
            char *result = expand_variable(executor, braced);
            free(braced);
            return result;
        }
    }

    // Kind sigil top-level dispatch: `@NAME` / `%NAME` with NAME a valid
    // identifier.  Tokenizer has already done the regex check; we just need
    // to verify the shape before routing to expand_kind_sigil.
    if ((text[0] == '@' || text[0] == '%') &&
        shell_mode_allows(FEATURE_KIND_SIGILS) &&
        (isalpha((unsigned char)text[1]) || text[1] == '_')) {
        const char *p = text + 1;
        while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
            p++;
        }
        if (*p == '\0') {
            return expand_kind_sigil(executor, text);
        }
    }

    // Handle strings that contain single quotes - process them specially
    // Single-quoted content should not be expanded (POSIX requirement)
    // BUT: Don't enter this path for command substitution $(...) or `...`
    // which may contain quotes internally
    /* Enter the single-quote-handling block only when the text has
     * at least one MATCHED pair of UNESCAPED single quotes. The
     * parser pre-escapes `'` chars inside TOK_EXPANDABLE_STRING
     * content (issue #102) so they appear here as `\'` and must
     * not count toward the pair check -- those are literal
     * characters that the POSIX-unquoted backslash rule resolves
     * downstream. */
    bool has_paired_single_quote = false;
    {
        size_t unescaped = 0;
        for (const char *p = text; *p; p++) {
            if (*p == '\\' && p[1]) {
                p++;
                continue;
            }
            if (*p == '\'') {
                unescaped++;
                if (unescaped >= 2) {
                    has_paired_single_quote = true;
                    break;
                }
            }
        }
    }
    if (has_paired_single_quote && !(text[0] == '$' && text[1] == '(') &&
        !(text[0] == '`')) {
        size_t len = strlen(text);
        size_t result_capacity = len + 1;
        char *result = malloc(result_capacity);
        if (!result) {
            return strdup(text);
        }
        size_t result_pos = 0;

        for (size_t i = 0; i < len; i++) {
            if (text[i] == '$' && i + 1 < len && text[i + 1] == '\'') {
                // ANSI-C quoting $'...' - expand escape sequences
                i += 2; // Skip $'
                size_t content_start = i;
                // Find closing quote (handling escaped quotes)
                while (i < len) {
                    if (text[i] == '\\' && i + 1 < len) {
                        i += 2; // Skip escaped character
                    } else if (text[i] == '\'') {
                        break;
                    } else {
                        i++;
                    }
                }
                // Extract and expand the ANSI-C string content
                size_t content_len = i - content_start;
                if (shell_mode_allows(FEATURE_ANSI_QUOTING)) {
                    char *expanded =
                        expand_ansi_c_string(&text[content_start], content_len);
                    if (expanded) {
                        size_t exp_len = strlen(expanded);
                        while (result_pos + exp_len >= result_capacity) {
                            result_capacity *= 2;
                            char *new_result = realloc(result, result_capacity);
                            if (!new_result) {
                                free(result);
                                free(expanded);
                                return strdup(text);
                            }
                            result = new_result;
                        }
                        strcpy(&result[result_pos], expanded);
                        result_pos += exp_len;
                        free(expanded);
                    }
                } else {
                    // Feature disabled - copy literally (including $')
                    while (result_pos + content_len + 3 >= result_capacity) {
                        result_capacity *= 2;
                        char *new_result = realloc(result, result_capacity);
                        if (!new_result) {
                            free(result);
                            return strdup(text);
                        }
                        result = new_result;
                    }
                    result[result_pos++] = '$';
                    result[result_pos++] = '\'';
                    strncpy(&result[result_pos], &text[content_start],
                            content_len);
                    result_pos += content_len;
                    result[result_pos++] = '\'';
                }
                // i now points to closing quote (or end of string)
            } else if (text[i] == '\'') {
                // Regular single quote - copy content literally until closing
                // quote
                i++; // Skip opening quote
                while (i < len && text[i] != '\'') {
                    if (result_pos >= result_capacity - 1) {
                        result_capacity *= 2;
                        char *new_result = realloc(result, result_capacity);
                        if (!new_result) {
                            free(result);
                            return strdup(text);
                        }
                        result = new_result;
                    }
                    result[result_pos++] = text[i++];
                }
                // i now points to closing quote (or end of string)
            } else if (text[i] == '"') {
                // Double quote - expand content until closing quote
                i++; // Skip opening quote
                size_t dq_start = i;
                int depth = 1;
                while (i < len && depth > 0) {
                    if (text[i] == '"' && (i == 0 || text[i - 1] != '\\')) {
                        depth--;
                        if (depth == 0)
                            break;
                    }
                    i++;
                }
                // Extract double-quoted content and expand it
                size_t dq_len = i - dq_start;
                char *dq_content = malloc(dq_len + 1);
                if (dq_content) {
                    strncpy(dq_content, &text[dq_start], dq_len);
                    dq_content[dq_len] = '\0';
                    // Content inside `"..."` -- DQ rules apply.
                    char *expanded =
                        expand_quoted_string(executor, dq_content, true);
                    free(dq_content);
                    if (expanded) {
                        size_t exp_len = strlen(expanded);
                        while (result_pos + exp_len >= result_capacity) {
                            result_capacity *= 2;
                            char *new_result = realloc(result, result_capacity);
                            if (!new_result) {
                                free(result);
                                free(expanded);
                                return strdup(text);
                            }
                            result = new_result;
                        }
                        strcpy(&result[result_pos], expanded);
                        result_pos += exp_len;
                        free(expanded);
                    }
                }
                // i now points to closing quote
            } else if (text[i] == '$') {
                // Outside quotes - expand variable
                size_t var_start = i;
                // Find end of variable reference
                if (i + 1 < len && text[i + 1] == '{') {
                    // ${var} format - find closing brace
                    size_t brace_end = i + 2;
                    int brace_depth = 1;
                    while (brace_end < len && brace_depth > 0) {
                        if (text[brace_end] == '{')
                            brace_depth++;
                        else if (text[brace_end] == '}')
                            brace_depth--;
                        brace_end++;
                    }
                    i = brace_end - 1;
                } else if (i + 1 < len && text[i + 1] == '(') {
                    // $(cmd) or $((arith)) - find closing paren
                    size_t paren_end = i + 2;
                    int paren_depth = 1;
                    while (paren_end < len && paren_depth > 0) {
                        if (text[paren_end] == '(')
                            paren_depth++;
                        else if (text[paren_end] == ')')
                            paren_depth--;
                        paren_end++;
                    }
                    i = paren_end - 1;
                } else {
                    // $var format
                    i++;
                    while (i < len && (isalnum(text[i]) || text[i] == '_')) {
                        i++;
                    }
                    i--; // Back up to last char of variable
                }
                // Extract and expand the variable reference
                size_t var_len = i - var_start + 1;
                char *var_ref = malloc(var_len + 1);
                if (var_ref) {
                    strncpy(var_ref, &text[var_start], var_len);
                    var_ref[var_len] = '\0';
                    char *expanded = expand_variable(executor, var_ref);
                    free(var_ref);
                    if (expanded) {
                        size_t exp_len = strlen(expanded);
                        while (result_pos + exp_len >= result_capacity) {
                            result_capacity *= 2;
                            char *new_result = realloc(result, result_capacity);
                            if (!new_result) {
                                free(result);
                                free(expanded);
                                return strdup(text);
                            }
                            result = new_result;
                        }
                        strcpy(&result[result_pos], expanded);
                        result_pos += exp_len;
                        free(expanded);
                    }
                }
            } else {
                // Regular character outside quotes
                if (result_pos >= result_capacity - 1) {
                    result_capacity *= 2;
                    char *new_result = realloc(result, result_capacity);
                    if (!new_result) {
                        free(result);
                        return strdup(text);
                    }
                    result = new_result;
                }
                result[result_pos++] = text[i];
            }
        }
        result[result_pos] = '\0';
        return result;
    }

    // Check for tilde expansion first
    if (text[0] == '~') {
        char *tilde_expanded = expand_tilde(text);
        if (tilde_expanded && strcmp(tilde_expanded, text) != 0) {
            // Tilde was expanded, now check if result needs variable expansion
            const char *first_dollar = strchr(tilde_expanded, '$');
            if (first_dollar) {
                /* Unquoted text post-tilde-expansion: POSIX-unquoted
                 * escape rules apply to any surviving backslashes. */
                char *final_result =
                    expand_quoted_string(executor, tilde_expanded, false);
                free(tilde_expanded);
                return final_result;
            }
            return tilde_expanded;
        }
        if (tilde_expanded) {
            free(tilde_expanded);
        }
    }

    // Check if this looks like it contains variables (has $)
    // This is a heuristic for expandable strings
    const char *first_dollar = strchr(text, '$');
    if (first_dollar) {
        // Count dollar signs to determine if we have multiple variables
        int dollar_count = 0;
        for (const char *p = text; *p; p++) {
            if (*p == '$') {
                dollar_count++;
            }
        }

        // If we have multiple dollar signs or the first dollar is not at
        // position 0, treat as quoted string with multiple expansions.
        // This branch is reached from unquoted contexts (NODE_VAR with
        // embedded $ etc.); pass in_double_quotes=false so any surviving
        // `\X` follows POSIX-unquoted rules.
        if (dollar_count > 1 || first_dollar != text) {
            return expand_quoted_string(executor, text, false);
        }

        // Single expansion starting at position 0
        if (strncmp(text, "$'", 2) == 0) {
            // ANSI-C quoting $'...'
            // Find closing quote (handling escaped quotes)
            size_t len = strlen(text);
            size_t quote_end = 2;
            while (quote_end < len) {
                if (text[quote_end] == '\\' && quote_end + 1 < len) {
                    quote_end += 2;
                } else if (text[quote_end] == '\'') {
                    break;
                } else {
                    quote_end++;
                }
            }
            if (quote_end < len && text[quote_end] == '\'') {
                // Check if feature is allowed
                if (!shell_mode_allows(FEATURE_ANSI_QUOTING)) {
                    // Feature disabled, return literal
                    return strdup(text);
                }
                char *expanded = expand_ansi_c_string(text + 2, quote_end - 2);
                // If there's text after the closing quote, append it
                if (text[quote_end + 1] != '\0') {
                    char *rest =
                        expand_if_needed(executor, text + quote_end + 1);
                    if (rest) {
                        size_t exp_len = strlen(expanded);
                        size_t rest_len = strlen(rest);
                        char *combined = malloc(exp_len + rest_len + 1);
                        if (combined) {
                            memcpy(combined, expanded, exp_len);
                            memcpy(combined + exp_len, rest, rest_len + 1);
                            free(expanded);
                            free(rest);
                            return combined;
                        }
                        free(rest);
                    }
                }
                return expanded;
            }
            return strdup(text);
        } else if (strncmp(text, "$((", 3) == 0) {
            /* Disambiguate `$((` between arithmetic and command-sub
             * of an anonymous function `$(() {...})`. Same shape as
             * the tokenizer and expand_variables_in_string detectors
             * (issue #99): if the lookahead from after $(( finds {,
             * }, ;, or \n before matched )), route to command-sub. */
            bool looks_arith = true;
            {
                size_t tlen = strlen(text);
                size_t s = 3;
                int d = 2;
                while (s < tlen && d > 0) {
                    char sc = text[s];
                    if (sc == '(') {
                        d++;
                    } else if (sc == ')') {
                        d--;
                        if (d == 0) {
                            break;
                        }
                    } else if (sc == '{' || sc == '}' || sc == ';' ||
                               sc == '\n') {
                        looks_arith = false;
                        break;
                    }
                    s++;
                }
            }
            if (looks_arith) {
                return expand_arithmetic(executor, text);
            }
            return expand_command_substitution(executor, text);
        } else if (strncmp(text, "$(", 2) == 0) {
            return expand_command_substitution(executor, text);
        } else if (strncmp(text, "${", 2) == 0) {
            // ${var} format - check if there's more text after }
            const char *close_brace = strchr(text, '}');
            if (close_brace && close_brace[1] != '\0') {
                // Text continues after ${var}; unquoted context.
                return expand_quoted_string(executor, text, false);
            }
            return expand_variable(executor, text);
        } else {
            // $var format - check if there's more text after variable name
            const char *p = text + 1; // Skip $
            // Find end of variable name
            if (*p == '?' || *p == '$' || *p == '#' || *p == '*' || *p == '@' ||
                *p == '!' || *p == '-' || (*p >= '0' && *p <= '9')) {
                p++; // Single character special variable
            } else {
                while (*p && (isalnum(*p) || *p == '_')) {
                    p++;
                }
            }
            // Trailing text after the variable; unquoted context.
            if (*p != '\0') {
                return expand_quoted_string(executor, text, false);
            }
            return expand_variable(executor, text);
        }
    }

    // Check for backtick command substitution
    if (text[0] == '`') {
        return expand_command_substitution(executor, text);
    }

    /* Regular text reaching this path has no quote machinery and no
     * expansion markers ($, ~, `, ') -- only possibly backslash
     * escapes. Per POSIX, `\X` outside any quote produces a literal X
     * (with `\<newline>` removed entirely as line continuation). The
     * older code was returning strdup(text) here, which left the
     * backslashes in the argument and produced bug #90 (a typed
     * `rm a\ test\ file.txt` shipped the literal backslash through
     * to rm). The fix walks the text removing the escape backslashes;
     * the post-walk string is the dequoted form the executor will
     * eventually pass through field splitting and into argv. */
    {
        size_t len = strlen(text);
        char *result = malloc(len + 1);
        if (!result)
            return strdup(text); // OOM fallback
        size_t out = 0;
        for (size_t i = 0; i < len; i++) {
            if (text[i] == '\\' && i + 1 < len) {
                char next = text[i + 1];
                if (next == '\n') {
                    // Line continuation: drop both bytes.
                    i++;
                    continue;
                }
                /* Strip the backslash; emit the escaped character.
                 * Multi-byte UTF-8 escapees survive because their
                 * continuation bytes are >= 0x80 and are not '\\'
                 * themselves; the next loop iteration sees them as
                 * regular bytes and copies them through. */
                result[out++] = next;
                i++;
                continue;
            }
            result[out++] = text[i];
        }
        result[out] = '\0';
        return result;
    }
}

/**
 * @brief Execute a negated pipeline (! pipeline)
 *
 * Executes the pipeline and inverts its exit status.
 * Exit status 0 becomes 1, any non-zero becomes 0.
 *
 * @param executor Executor context
 * @param negate_node Negate node containing the pipeline
 * @return Inverted exit status
 */
static int execute_negate(executor_t *executor, node_t *negate_node) {
    if (!negate_node || negate_node->type != NODE_NEGATE) {
        return 1;
    }

    // Execute the child (pipeline)
    node_t *child = negate_node->first_child;
    if (!child) {
        return 1;
    }

    int result = execute_node(executor, child);

    // Invert the exit status: 0 -> 1, non-zero -> 0
    int inverted = (result == 0) ? 1 : 0;
    executor->exit_status = inverted;

    return inverted;
}

/**
 * @brief Execute a brace group { commands; }
 *
 * Executes commands within braces in the current shell context
 * (not a subshell). Useful for grouping commands for redirection.
 *
 * @param executor Executor context
 * @param group Brace group node
 * @return Exit status of last command in group
 */
static int execute_brace_group(executor_t *executor, node_t *group) {
    if (!group || group->type != NODE_BRACE_GROUP) {
        return 1;
    }

    // Push error context for structured error reporting
    executor_push_context(executor, group->loc, "in brace group");

    // Check for trailing redirections on the brace group
    bool has_redirections = count_redirections(group) > 0;
    redirection_state_t redir_state;

    if (has_redirections) {
        save_file_descriptors(&redir_state);
        int redir_result = setup_redirections(executor, group);
        if (redir_result != 0) {
            restore_file_descriptors(&redir_state);
            executor_pop_context(executor);
            return redir_result;
        }
    }

    int last_result = 0;
    node_t *command = group->first_child;

    while (command) {
        // Skip redirection nodes - they've already been processed
        if (is_redirection_node(command)) {
            command = command->next_sibling;
            continue;
        }

        /* Bash-style DEBUG pseudo-signal: fires BEFORE each command in a
         * brace group. fire_debug_trap gates on functrace + scope. */
        fire_debug_trap();

        last_result = execute_node(executor, command);

        if (executor->debug) {
            printf("DEBUG: Brace group command result: %d\n", last_result);
        }

        /* Bash-style ERR pseudo-signal: fires on a non-zero exit
         * inside a brace group. fire_err_trap itself gates on
         * errtrace + function scope so the trap is suppressed inside
         * functions by default and surfaces only when the user opts
         * in. */
        if (last_result != 0 && last_result < 200) {
            fire_err_trap();
        }

        // Check for function return (special code 200-455) - propagate it
        if (last_result >= 200 && last_result <= 455) {
            if (has_redirections) {
                restore_file_descriptors(&redir_state);
            }
            executor_pop_context(executor);
            return last_result;
        }

        // Check for loop control (break/continue)
        if (executor->loop_control != LOOP_NORMAL) {
            break;
        }

        /* POSIX-required shell abort: drop out of the brace group so
         * the request propagates to the surrounding command list. */
        if (executor->shell_exit_requested) {
            break;
        }

        command = command->next_sibling;
    }

    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    // Pop error context
    executor_pop_context(executor);

    return last_result;
}

/**
 * @brief Execute a subshell ( commands )
 *
 * Forks a child process and executes commands in that subshell.
 * Variable changes in the subshell do not affect the parent.
 *
 * @param executor Executor context
 * @param subshell Subshell node
 * @return Exit status of subshell
 */
static int execute_subshell(executor_t *executor, node_t *subshell) {
    if (!subshell || subshell->type != NODE_SUBSHELL) {
        return 1;
    }

    // Push error context for structured error reporting
    executor_push_context(executor, subshell->loc, "in subshell");

    // Fork a new process for the subshell
    pid_t pid = lush_fork();
    if (pid == -1) {
        executor_error_add(executor, SHELL_ERR_FORK_FAILED, subshell->loc,
                           "failed to fork for subshell: %s", strerror(errno));
        executor_pop_context(executor);
        return 1;
    }

    if (pid == 0) {
        // Child process - execute commands in subshell environment

        // Set up any redirections attached to the subshell
        if (count_redirections(subshell) > 0) {
            int redir_result = setup_redirections(executor, subshell);
            if (redir_result != 0) {
                exit(redir_result);
            }
        }

        int last_result = 0;
        node_t *command = subshell->first_child;

        while (command) {
            // Skip redirection nodes - they've already been applied
            if (is_redirection_node(command)) {
                command = command->next_sibling;
                continue;
            }
            last_result = execute_node(executor, command);

            /* Update $? between subshell commands so subsequent
             * argv expansions see the correct exit status. NODE_PIPE
             * and NODE_BUILTIN paths set last_exit_status via
             * set_exit_status() inside execute_command, but the
             * pipeline executor itself returns directly without
             * updating it. The outer execute_command_list does this
             * after each command; the subshell loop was missing the
             * same update. Issue #100. */
            set_exit_status(last_result);

            /* Honor set -e inside the subshell: if a command fails
             * (non-zero exit) and the option is on, abort the rest
             * of the subshell body. The existing exit_on_error check
             * lives in execute_command_list / execute_command_chain
             * which the subshell loop bypasses. The standard
             * exceptions for if-conditions and ||/&& chains are
             * already handled by their respective execute_* paths
             * not propagating last_result to here. Issue #100. */
            if (shell_opts.exit_on_error && last_result != 0) {
                break;
            }

            /* POSIX-required shell abort -- same flag the outer
             * walker honors; if the subshell hit a ${var:?} or
             * similar, terminate. */
            if (executor->shell_exit_requested) {
                last_result = executor->shell_exit_status;
                break;
            }

            command = command->next_sibling;
        }

        // Exit with the last command's result
        exit(last_result);
    } else {
        // Parent process - wait for subshell to complete
        int status;
        // Wait for child, retrying on EINTR (signal interruption)
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR)
            ;

        int result;
        if (WIFEXITED(status)) {
            result = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            // Child was killed by signal - return 128 + signal number (bash
            // convention)
            result = 128 + WTERMSIG(status);
        } else {
            result = 1; // Abnormal termination
        }

        // Pop error context
        executor_pop_context(executor);

        return result;
    }
}

/**
 * @brief Expand glob pattern to matching filenames
 *
 * Uses system glob() function to expand patterns like *.c, ?.txt.
 * Returns original pattern if no matches (POSIX behavior).
 * Respects set -f (no_globbing) option.
 *
 * @param pattern Glob pattern to expand
 * @param expanded_count Output: number of matches
 * @return Array of matching paths (caller must free), or NULL on error
 */
/**
 * @brief Glob qualifier types (Zsh-style)
 *
 * Uses power-of-2 values for bitmask operations (combined qualifiers like
 * *(.,@))
 */
typedef enum {
    GLOB_QUAL_NONE = 0,      // No qualifier
    GLOB_QUAL_FILE = 1,      // (.) - regular files only
    GLOB_QUAL_DIR = 2,       // (/) - directories only
    GLOB_QUAL_LINK = 4,      // (@) - symbolic links only
    GLOB_QUAL_EXEC = 8,      // (*) - executable files
    GLOB_QUAL_READABLE = 16, // (r) - readable files
    GLOB_QUAL_WRITABLE = 32, // (w) - writable files
    // Behavior modifiers (not type/permission filters):
    GLOB_QUAL_NULLGLOB = 64, // (N) - no match -> empty, never literal
    GLOB_QUAL_DOTGLOB = 128, // (D) - include dot (hidden) files
} glob_qualifier_t;

/* Bits that filter by file type or permission (vs. behavior modifiers
 * like N/D). matches_glob_qualifier only needs to act when one of
 * these is set. */
#define GLOB_QUAL_FILTER_MASK                                                  \
    (GLOB_QUAL_FILE | GLOB_QUAL_DIR | GLOB_QUAL_LINK | GLOB_QUAL_EXEC |        \
     GLOB_QUAL_READABLE | GLOB_QUAL_WRITABLE)

/**
 * @brief Parse and strip glob qualifier from pattern
 *
 * @param pattern Input pattern (may be modified)
 * @param base_pattern Output: pattern without qualifier (must be freed)
 * @return Glob qualifier type
 */
static glob_qualifier_t parse_glob_qualifier(const char *pattern,
                                             char **base_pattern) {
    if (!pattern || !base_pattern) {
        *base_pattern = pattern ? strdup(pattern) : NULL;
        return GLOB_QUAL_NONE;
    }

    size_t len = strlen(pattern);

    // Check for qualifier pattern: ends with (X) or (X,Y,...) where X,Y are
    // qualifier chars
    if (len >= 3 && pattern[len - 1] == ')') {
        // Find matching open paren - must be near end (qualifiers are short)
        // Limit search to last 10 chars (or start of string for short patterns)
        const char *open_paren = NULL;
        size_t min_idx = (len > 10) ? (len - 10) : 1;
        for (size_t i = len - 2; i >= min_idx; i--) {
            if (pattern[i] == '(') {
                open_paren = &pattern[i];
                break;
            }
            if (i == min_idx)
                break; // Prevent underflow on decrement
        }

        if (open_paren) {
            // Parse all qualifier characters between ( and )
            glob_qualifier_t qual = GLOB_QUAL_NONE;
            bool valid_qualifier = true;

            for (const char *p = open_paren + 1; p < pattern + len - 1; p++) {
                switch (*p) {
                case '.':
                    qual |= GLOB_QUAL_FILE;
                    break;
                case '/':
                    qual |= GLOB_QUAL_DIR;
                    break;
                case '@':
                    qual |= GLOB_QUAL_LINK;
                    break;
                case '*':
                    qual |= GLOB_QUAL_EXEC;
                    break;
                case 'r':
                    qual |= GLOB_QUAL_READABLE;
                    break;
                case 'w':
                    qual |= GLOB_QUAL_WRITABLE;
                    break;
                case 'N':
                    qual |= GLOB_QUAL_NULLGLOB;
                    break;
                case 'D':
                    qual |= GLOB_QUAL_DOTGLOB;
                    break;
                case ',':
                    break; // Separator, ignore
                default:
                    // Unknown character - not a valid glob qualifier
                    valid_qualifier = false;
                    break;
                }
                if (!valid_qualifier)
                    break;
            }

            if (valid_qualifier && qual != GLOB_QUAL_NONE) {
                // Strip the qualifier
                *base_pattern = strndup(pattern, open_paren - pattern);
                return qual;
            }
        }
    }

    *base_pattern = strdup(pattern);
    return GLOB_QUAL_NONE;
}

/**
 * @brief Check if file matches glob qualifier
 *
 * @param path File path to check
 * @param qualifier Glob qualifier type
 * @return true if file matches qualifier
 */
static bool matches_glob_qualifier(const char *path,
                                   glob_qualifier_t qualifier) {
    if (qualifier == GLOB_QUAL_NONE) {
        return true;
    }

    struct stat st;
    if (lstat(path, &st) != 0) {
        return false;
    }

    // With combined qualifiers (bitmask), file must match ANY of the type
    // qualifiers For example, *(.,@) matches files OR symlinks
    bool type_match = false;
    bool has_type_qualifier = false;

    // Check type qualifiers (file, dir, link, exec) - OR logic
    if (qualifier & GLOB_QUAL_FILE) {
        has_type_qualifier = true;
        if (S_ISREG(st.st_mode))
            type_match = true;
    }
    if (qualifier & GLOB_QUAL_DIR) {
        has_type_qualifier = true;
        if (S_ISDIR(st.st_mode))
            type_match = true;
    }
    if (qualifier & GLOB_QUAL_LINK) {
        has_type_qualifier = true;
        if (S_ISLNK(st.st_mode))
            type_match = true;
    }
    if (qualifier & GLOB_QUAL_EXEC) {
        has_type_qualifier = true;
        if (S_ISREG(st.st_mode) &&
            (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
            type_match = true;
        }
    }

    // If no type qualifiers specified, default to matching any type
    if (!has_type_qualifier) {
        type_match = true;
    }

    if (!type_match) {
        return false;
    }

    // Check permission qualifiers - AND logic (must satisfy all)
    if (qualifier & GLOB_QUAL_READABLE) {
        if (access(path, R_OK) != 0)
            return false;
    }
    if (qualifier & GLOB_QUAL_WRITABLE) {
        if (access(path, W_OK) != 0)
            return false;
    }

    return true;
}

// =============================================================================
// ZSH EXTENDED GLOB PATTERNS
// =============================================================================
// Zsh extended glob uses different syntax than bash:
//   X#   - zero or more of X (like * in regex)
//   X##  - one or more of X (like + in regex)
//   (a|b) - alternation (without preceding operator)
//   ^pat - negation (match everything except pattern)
// =============================================================================

/**
 * @brief Check if pattern contains zsh-style extglob syntax
 *
 * Detects X#, X##, (a|b) alternation, and ^pattern negation.
 */
static bool has_zsh_extglob_pattern(const char *pattern) {
    if (!pattern || !shell_mode_allows(FEATURE_EXTENDED_GLOB)) {
        return false;
    }

    // Check for ^pattern negation at start
    if (pattern[0] == '^') {
        return true;
    }

    // Check for (a|b) alternation - parentheses with | inside, NOT preceded by
    // extglob op
    const char *p = pattern;
    while (*p) {
        if (*p == '(' && (p == pattern || !strchr("?*+@!", *(p - 1)))) {
            // Found ( not preceded by extglob operator - check for | inside
            const char *inner = p + 1;
            int depth = 1;
            while (*inner && depth > 0) {
                if (*inner == '(')
                    depth++;
                else if (*inner == ')')
                    depth--;
                else if (*inner == '|' && depth == 1)
                    return true;
                inner++;
            }
        }
        p++;
    }

    // Check for # or ## quantifiers (after a char or ])
    p = pattern;
    while (*p) {
        if (*p == '#') {
            // # must be preceded by something (char, ], or ))
            if (p > pattern) {
                char prev = *(p - 1);
                if (prev != '/' && prev != ' ' && prev != '\t') {
                    return true;
                }
            }
        }
        p++;
    }

    return false;
}

/**
 * @brief Convert zsh extglob pattern to POSIX extended regex
 *
 * Converts zsh extended glob syntax to regex:
 *   X#   -> X* (zero or more)
 *   X##  -> X+ (one or more)
 *   (a|b) -> (a|b) (alternation)
 *   [...]# -> [...]* (char class zero or more)
 *   *    -> .* (any chars)
 *   ?    -> . (any single char)
 *   .    -> \. (literal dot)
 *
 * @param pattern Zsh extglob pattern (without leading ^ if negation)
 * @return Regex pattern (caller must free), or NULL on error
 */
static char *zsh_extglob_to_regex(const char *pattern) {
    if (!pattern)
        return NULL;

    // Allocate generous buffer
    size_t max_len = strlen(pattern) * 4 + 10;
    char *regex = malloc(max_len);
    if (!regex)
        return NULL;

    char *out = regex;
    *out++ = '^'; // Anchor at start

    const char *p = pattern;
    while (*p) {
        if (*p == '[') {
            // Character class - copy until ]
            *out++ = *p++;
            // Handle negation [^ or [!
            if (*p == '^' || *p == '!') {
                *out++ = '^';
                p++;
            }
            // Handle ] as first char (literal)
            if (*p == ']') {
                *out++ = *p++;
            }
            while (*p && *p != ']') {
                *out++ = *p++;
            }
            if (*p == ']') {
                *out++ = *p++;
            }
            // Check for # or ## after char class
            if (*p == '#') {
                if (*(p + 1) == '#') {
                    *out++ = '+'; // ## = one or more
                    p += 2;
                } else {
                    *out++ = '*'; // # = zero or more
                    p++;
                }
            }
        } else if (*p == '(') {
            // Alternation group - copy as-is, handling nested parens
            *out++ = *p++;
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '(') {
                    depth++;
                    *out++ = *p++;
                } else if (*p == ')') {
                    depth--;
                    *out++ = *p++;
                } else if (*p == '*') {
                    // Glob * inside alternation
                    *out++ = '.';
                    *out++ = '*';
                    p++;
                } else if (*p == '?') {
                    *out++ = '.';
                    p++;
                } else if (*p == '.') {
                    *out++ = '\\';
                    *out++ = '.';
                    p++;
                } else {
                    *out++ = *p++;
                }
            }
        } else if (*p == '*') {
            // Glob * -> regex .*
            *out++ = '.';
            *out++ = '*';
            p++;
        } else if (*p == '?') {
            // Glob ? -> regex .
            *out++ = '.';
            p++;
        } else if (*p == '.') {
            // Escape literal dot
            *out++ = '\\';
            *out++ = '.';
            p++;
        } else if (*p == '#') {
            // Standalone # - should not happen if preceded by nothing
            // Skip it (treat as literal)
            *out++ = '#';
            p++;
        } else {
            // Regular character
            char c = *p++;
            // Check for # or ## quantifier after this char
            if (*p == '#') {
                // Need to wrap single char in group for regex
                // But first, escape regex metacharacters
                if (strchr("^$+{}\\|()", c)) {
                    *out++ = '\\';
                }
                *out++ = c;
                if (*(p + 1) == '#') {
                    *out++ = '+'; // ## = one or more
                    p += 2;
                } else {
                    *out++ = '*'; // # = zero or more
                    p++;
                }
            } else {
                // No quantifier - regular char, might need escaping
                if (strchr("^$+{}\\|", c)) {
                    *out++ = '\\';
                }
                *out++ = c;
            }
        }
    }

    *out++ = '$'; // Anchor at end
    *out = '\0';

    return regex;
}

/**
 * @brief Reject regex patterns that exceed behavior.regex_pattern_max length.
 *
 * Platform regcomp implementations (TRE on macOS, glibc regex on Linux)
 * exhibit catastrophic compile-time on pathological patterns -- deeply
 * nested alternations with quantifiers, very long alternation lists,
 * specific bounded-repetition shapes. Fuzz mutation generates such
 * inputs trivially; real users essentially never type them.
 *
 * Pre-validation by length is the simplest defense that bounds worst-
 * case compile time without rejecting human input. The default cap of
 * 1024 chars is ~10x larger than any realistic shell regex; setting
 * the cap to 0 disables the check entirely (parity with bash/zsh which
 * have no cap).
 *
 * @param pattern Regex pattern about to be passed to regcomp.
 * @return true if pattern is OK to compile, false if it exceeds the cap.
 */
static bool regex_pattern_is_safe(const char *pattern) {
    if (!pattern) {
        return false;
    }
    if (config.regex_pattern_max <= 0) {
        return true; // explicitly unbounded
    }
    return strlen(pattern) <= (size_t)config.regex_pattern_max;
}

/**
 * @brief Match filename against zsh extglob pattern
 */
static bool match_zsh_extglob(const char *filename, const char *pattern,
                              bool is_negated) {
    if (!regex_pattern_is_safe(pattern)) {
        return false;
    }
    char *regex_pattern = zsh_extglob_to_regex(pattern);
    if (!regex_pattern) {
        return false;
    }

    regex_t regex;
    int ret = regcomp(&regex, regex_pattern, REG_EXTENDED | REG_NOSUB);
    free(regex_pattern);

    if (ret != 0) {
        return false;
    }

    ret = regexec(&regex, filename, 0, NULL, 0);
    regfree(&regex);

    bool matches = (ret == 0);

    // For ^pattern, invert the result
    if (is_negated) {
        matches = !matches;
    }

    return matches;
}

/**
 * @brief Expand zsh extglob pattern by reading directory and matching
 */
static char **expand_zsh_extglob_pattern(const char *pattern,
                                         int *expanded_count) {
    *expanded_count = 0;

    if (!pattern) {
        return NULL;
    }

    // Check for ^pattern negation
    bool is_negated = (pattern[0] == '^');
    const char *match_pattern = is_negated ? pattern + 1 : pattern;

    // Split pattern into directory and filename parts
    char *pattern_copy = strdup(match_pattern);
    if (!pattern_copy)
        return NULL;

    char *last_slash = strrchr(pattern_copy, '/');
    char *dir_path = NULL;
    char *file_pattern = NULL;

    if (last_slash) {
        *last_slash = '\0';
        dir_path = pattern_copy;
        file_pattern = last_slash + 1;
    } else {
        dir_path = ".";
        file_pattern = pattern_copy;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        free(pattern_copy);
        return NULL;
    }

    // Collect matching entries
    char **results = NULL;
    size_t result_count = 0;
    size_t result_capacity = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Skip hidden files unless pattern starts with .
        if (entry->d_name[0] == '.' && file_pattern[0] != '.') {
            continue;
        }

        if (match_zsh_extglob(entry->d_name, file_pattern, is_negated)) {
            // Grow array if needed
            if (result_count >= result_capacity) {
                size_t new_capacity =
                    result_capacity == 0 ? 16 : result_capacity * 2;
                char **new_results =
                    realloc(results, (new_capacity + 1) * sizeof(char *));
                if (!new_results) {
                    // Cleanup on failure
                    for (size_t i = 0; i < result_count; i++) {
                        free(results[i]);
                    }
                    free(results);
                    closedir(dir);
                    free(pattern_copy);
                    return NULL;
                }
                results = new_results;
                result_capacity = new_capacity;
            }

            // Build full path if in subdirectory
            char *full_path;
            if (last_slash) {
                size_t dir_len = strlen(dir_path);
                size_t name_len = strlen(entry->d_name);
                full_path = malloc(dir_len + 1 + name_len + 1);
                if (full_path) {
                    strcpy(full_path, dir_path);
                    full_path[dir_len] = '/';
                    strcpy(full_path + dir_len + 1, entry->d_name);
                }
            } else {
                full_path = strdup(entry->d_name);
            }

            if (full_path) {
                results[result_count++] = full_path;
            }
        }
    }

    closedir(dir);
    free(pattern_copy);

    if (result_count == 0) {
        free(results);
        return NULL;
    }

    // Sort results
    qsort(results, result_count, sizeof(char *),
          (int (*)(const void *, const void *))strcmp);

    results[result_count] = NULL;
    *expanded_count = result_count;

    return results;
}

/**
 * @brief Check if pattern contains extglob syntax
 *
 * Detects ?(pat), *(pat), +(pat), @(pat), !(pat) patterns.
 */
static bool has_extglob_pattern(const char *pattern) {
    if (!pattern || !shell_mode_allows(FEATURE_EXTENDED_GLOB)) {
        return false;
    }

    while (*pattern) {
        if ((*pattern == '?' || *pattern == '*' || *pattern == '+' ||
             *pattern == '@' || *pattern == '!') &&
            *(pattern + 1) == '(') {
            return true;
        }
        pattern++;
    }
    return false;
}

/**
 * @brief Convert extglob pattern to regex pattern
 *
 * Converts bash extglob syntax to POSIX extended regex:
 *   ?(pat)  -> (pat)?
 *   *(pat)  -> (pat)*
 *   +(pat)  -> (pat)+
 *   @(pat)  -> (pat)
 *   !(pat)  -> handled specially (negative match)
 *   *       -> .*
 *   ?       -> .
 *   .       -> \.
 *
 * @param pattern Extglob pattern
 * @param is_negated Output: true if pattern uses !(...)
 * @return Regex pattern (caller must free), or NULL on error
 */
static char *extglob_to_regex(const char *pattern, bool *is_negated) {
    if (!pattern)
        return NULL;

    *is_negated = false;

    // Allocate generous buffer (pattern can expand significantly)
    size_t max_len = strlen(pattern) * 4 + 10;
    char *regex = malloc(max_len);
    if (!regex)
        return NULL;

    char *out = regex;
    *out++ = '^'; // Anchor at start

    const char *p = pattern;
    while (*p) {
        // Check for extglob operators
        if ((*p == '?' || *p == '*' || *p == '+' || *p == '@' || *p == '!') &&
            *(p + 1) == '(') {
            char op = *p;
            p += 2; // Skip operator and (

            // Find matching closing paren
            int depth = 1;
            const char *start = p;
            while (*p && depth > 0) {
                if (*p == '(')
                    depth++;
                else if (*p == ')')
                    depth--;
                if (depth > 0)
                    p++;
            }

            if (depth != 0) {
                // Unmatched paren - treat literally
                free(regex);
                return NULL;
            }

            // Copy the inner pattern
            *out++ = '(';
            size_t inner_len = p - start;

            // Convert inner pattern (replace | with |, escape regex chars)
            for (size_t i = 0; i < inner_len; i++) {
                char c = start[i];
                if (c == '*') {
                    *out++ = '.';
                    *out++ = '*';
                } else if (c == '?') {
                    *out++ = '.';
                } else if (c == '.') {
                    *out++ = '\\';
                    *out++ = '.';
                } else if (c == '|') {
                    *out++ = '|';
                } else {
                    *out++ = c;
                }
            }
            *out++ = ')';

            // Add quantifier based on operator
            switch (op) {
            case '?':
                *out++ = '?';
                break;
            case '*':
                *out++ = '*';
                break;
            case '+':
                *out++ = '+';
                break;
            case '@': // exactly one, no quantifier
                break;
            case '!':
                *is_negated = true;
                break;
            }

            p++; // Skip closing paren
        } else if (*p == '*') {
            // Glob * -> regex .*
            *out++ = '.';
            *out++ = '*';
            p++;
        } else if (*p == '?') {
            // Glob ? -> regex .
            *out++ = '.';
            p++;
        } else if (*p == '.') {
            // Escape literal dot
            *out++ = '\\';
            *out++ = '.';
            p++;
        } else if (*p == '[') {
            // Character class - copy as-is until ]
            *out++ = *p++;
            while (*p && *p != ']') {
                *out++ = *p++;
            }
            if (*p == ']') {
                *out++ = *p++;
            }
        } else {
            // Regular character - might need escaping
            if (strchr("^$+{}\\", *p)) {
                *out++ = '\\';
            }
            *out++ = *p++;
        }
    }

    *out++ = '$'; // Anchor at end
    *out = '\0';

    return regex;
}

/**
 * @brief Match filename against extglob pattern
 *
 * @param filename Filename to match
 * @param pattern Extglob pattern
 * @return true if matches
 */
static bool match_extglob(const char *filename, const char *pattern) {
    if (!regex_pattern_is_safe(pattern)) {
        return false;
    }
    bool is_negated = false;
    char *regex_pattern = extglob_to_regex(pattern, &is_negated);
    if (!regex_pattern) {
        return false;
    }

    regex_t regex;
    int ret = regcomp(&regex, regex_pattern, REG_EXTENDED | REG_NOSUB);
    free(regex_pattern);

    if (ret != 0) {
        return false;
    }

    ret = regexec(&regex, filename, 0, NULL, 0);
    regfree(&regex);

    bool matches = (ret == 0);

    // For !(pattern), invert the result
    if (is_negated) {
        matches = !matches;
    }

    return matches;
}

/**
 * @brief Expand extglob pattern by reading directory and matching
 *
 * @param pattern Pattern with extglob syntax
 * @param expanded_count Output: number of matches
 * @return Array of matching filenames, or NULL
 */
static char **expand_extglob_pattern(const char *pattern, int *expanded_count) {
    *expanded_count = 0;

    if (!pattern || !has_extglob_pattern(pattern)) {
        return NULL;
    }

    // Split pattern into directory and filename parts
    char *pattern_copy = strdup(pattern);
    if (!pattern_copy)
        return NULL;

    char *last_slash = strrchr(pattern_copy, '/');
    char *dir_path = NULL;
    char *file_pattern = NULL;

    if (last_slash) {
        *last_slash = '\0';
        dir_path = pattern_copy;
        file_pattern = last_slash + 1;
    } else {
        dir_path = ".";
        file_pattern = pattern_copy;
    }

    // Open directory
    DIR *dir = opendir(dir_path);
    if (!dir) {
        free(pattern_copy);
        return NULL;
    }

    // Collect matching entries
    char **results = NULL;
    int count = 0;
    int capacity = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. unless pattern explicitly starts with .
        if (entry->d_name[0] == '.' && file_pattern[0] != '.') {
            if (!shell_mode_allows(FEATURE_DOT_GLOB)) {
                continue;
            }
        }

        if (match_extglob(entry->d_name, file_pattern)) {
            // Resize array if needed
            if (count >= capacity) {
                capacity = capacity ? capacity * 2 : 16;
                char **new_results =
                    realloc(results, capacity * sizeof(char *));
                if (!new_results) {
                    for (int i = 0; i < count; i++)
                        free(results[i]);
                    free(results);
                    closedir(dir);
                    free(pattern_copy);
                    return NULL;
                }
                results = new_results;
            }

            // Build full path
            char *full_path;
            if (last_slash) {
                size_t len = strlen(dir_path) + strlen(entry->d_name) + 2;
                full_path = malloc(len);
                if (full_path) {
                    snprintf(full_path, len, "%s/%s", dir_path, entry->d_name);
                }
            } else {
                full_path = strdup(entry->d_name);
            }

            if (!full_path) {
                for (int i = 0; i < count; i++)
                    free(results[i]);
                free(results);
                closedir(dir);
                free(pattern_copy);
                return NULL;
            }

            results[count++] = full_path;
        }
    }

    closedir(dir);
    free(pattern_copy);

    if (count == 0) {
        free(results);
        return NULL;
    }

    // Add NULL terminator
    char **final = realloc(results, (count + 1) * sizeof(char *));
    if (final) {
        final[count] = NULL;
        results = final;
    }

    *expanded_count = count;
    return results;
}

/**
 * @brief Check if pattern contains ** globstar syntax
 *
 * @param pattern Pattern to check
 * @return true if pattern contains **
 */
static bool has_globstar_pattern(const char *pattern) {
    if (!pattern)
        return false;
    return strstr(pattern, "**") != NULL;
}

/**
 * @brief Recursive helper to expand ** patterns
 *
 * @param base_dir Directory to search in
 * @param remaining_pattern Pattern after the ** segment
 * @param results Pointer to results array
 * @param count Pointer to current count
 * @param capacity Pointer to current capacity
 * @return 0 on success, -1 on error
 */
static int expand_globstar_recursive(const char *base_dir,
                                     const char *remaining_pattern,
                                     char ***results, int *count,
                                     int *capacity) {
    DIR *dir = opendir(base_dir[0] ? base_dir : ".");
    if (!dir) {
        return 0; // Not an error - directory might not be readable
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Skip hidden files unless dotglob is enabled
        if (entry->d_name[0] == '.' && !shell_mode_allows(FEATURE_DOT_GLOB)) {
            continue;
        }

        // Build full path
        char full_path[PATH_MAX];
        if (base_dir[0]) {
            snprintf(full_path, sizeof(full_path), "%s/%s", base_dir,
                     entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
        }

        // Check if this path matches the remaining pattern
        if (remaining_pattern && remaining_pattern[0]) {
            // Build candidate path with remaining pattern
            char candidate[PATH_MAX];
            int written = snprintf(candidate, sizeof(candidate), "%s/%s",
                                   full_path, remaining_pattern);
            if (written < 0 || (size_t)written >= sizeof(candidate)) {
                continue; // Path too long, skip this entry
            }

            // Use glob to match the remaining pattern
            glob_t globbuf;
            if (glob(candidate, GLOB_NOSORT, NULL, &globbuf) == 0) {
                for (size_t i = 0; i < globbuf.gl_pathc; i++) {
                    // Resize array if needed
                    if (*count >= *capacity) {
                        *capacity = *capacity ? *capacity * 2 : 32;
                        char **new_results =
                            realloc(*results, *capacity * sizeof(char *));
                        if (!new_results) {
                            globfree(&globbuf);
                            closedir(dir);
                            return -1;
                        }
                        *results = new_results;
                    }
                    (*results)[(*count)++] = strdup(globbuf.gl_pathv[i]);
                }
                globfree(&globbuf);
            }
        } else {
            // No remaining pattern - match the path itself
            if (*count >= *capacity) {
                *capacity = *capacity ? *capacity * 2 : 32;
                char **new_results =
                    realloc(*results, *capacity * sizeof(char *));
                if (!new_results) {
                    closedir(dir);
                    return -1;
                }
                *results = new_results;
            }
            (*results)[(*count)++] = strdup(full_path);
        }

        // Recurse into directories
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (expand_globstar_recursive(full_path, remaining_pattern, results,
                                          count, capacity) < 0) {
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);
    return 0;
}

/**
 * @brief Expand globstar (**) pattern
 *
 * When globstar is enabled, ** matches zero or more directories recursively.
 * For example: src/ ** / *.c matches all .c files under src/ at any depth.
 *
 * @param pattern Pattern containing **
 * @param expanded_count Output: number of matches
 * @return Array of matching paths, or NULL
 */
static char **expand_globstar_pattern(const char *pattern,
                                      int *expanded_count) {
    *expanded_count = 0;

    if (!pattern || !has_globstar_pattern(pattern)) {
        return NULL;
    }

    // Find the ** in the pattern
    const char *starstar = strstr(pattern, "**");
    if (!starstar) {
        return NULL;
    }

    // Split into prefix (before **) and suffix (after **)
    size_t prefix_len = starstar - pattern;
    char *prefix = malloc(prefix_len + 1);
    if (!prefix)
        return NULL;

    strncpy(prefix, pattern, prefix_len);
    prefix[prefix_len] = '\0';

    // Remove trailing slash from prefix if present
    if (prefix_len > 0 && prefix[prefix_len - 1] == '/') {
        prefix[prefix_len - 1] = '\0';
    }

    // Get suffix (after **)
    const char *suffix = starstar + 2;
    if (*suffix == '/')
        suffix++; /* Skip leading slash after ** */

    // Initialize results
    char **results = NULL;
    int count = 0;
    int capacity = 0;

    // Start directory
    const char *start_dir = prefix[0] ? prefix : ".";

    // First, match the start directory itself with the suffix pattern
    if (suffix[0]) {
        char candidate[PATH_MAX];
        if (prefix[0]) {
            snprintf(candidate, sizeof(candidate), "%s/%s", prefix, suffix);
        } else {
            snprintf(candidate, sizeof(candidate), "%s", suffix);
        }

        glob_t globbuf;
        if (glob(candidate, GLOB_NOSORT, NULL, &globbuf) == 0) {
            for (size_t i = 0; i < globbuf.gl_pathc; i++) {
                if (count >= capacity) {
                    capacity = capacity ? capacity * 2 : 32;
                    char **new_results =
                        realloc(results, capacity * sizeof(char *));
                    if (!new_results) {
                        globfree(&globbuf);
                        free(prefix);
                        for (int j = 0; j < count; j++)
                            free(results[j]);
                        free(results);
                        return NULL;
                    }
                    results = new_results;
                }
                results[count++] = strdup(globbuf.gl_pathv[i]);
            }
            globfree(&globbuf);
        }
    }

    // Recursively expand through directories
    if (expand_globstar_recursive(start_dir, suffix[0] ? suffix : NULL,
                                  &results, &count, &capacity) < 0) {
        free(prefix);
        for (int i = 0; i < count; i++)
            free(results[i]);
        free(results);
        return NULL;
    }

    free(prefix);

    if (count == 0) {
        free(results);
        return NULL;
    }

    // Add NULL terminator
    char **final = realloc(results, (count + 1) * sizeof(char *));
    if (final) {
        final[count] = NULL;
        results = final;
    }

    *expanded_count = count;
    return results;
}

/**
 * @brief Expand a glob pattern with dotfile matching (zsh `D` qualifier)
 *
 * libc glob() never matches leading-dot files and offers no portable
 * flag to change that (GLOB_PERIOD is a glibc/_GNU_SOURCE extension,
 * absent on macOS). The zsh `(D)` glob qualifier explicitly requests
 * dotfile inclusion, so for D-qualified patterns scan the directory
 * directly with readdir + fnmatch. `.` and `..` are always excluded.
 *
 * Only single-component patterns and `dir/filepat` forms are handled
 * (the qualifier-glob syntax in practice never nests deeper). Results
 * are filtered through matches_glob_qualifier for any type/permission
 * bits combined with D.
 *
 * @param base_pattern Pattern with the qualifier already stripped
 * @param qualifier    Parsed qualifier bitmask (includes GLOB_QUAL_DOTGLOB)
 * @param count        OUT: number of matches
 * @return Match array (caller frees), empty array on no match, or NULL
 *         on error
 */
static char **expand_glob_dotglob(const char *base_pattern,
                                  glob_qualifier_t qualifier, int *count) {
    *count = 0;

    // Split into directory and filename pattern at the last '/'.
    const char *slash = strrchr(base_pattern, '/');
    char *dir = NULL;
    const char *filepat = base_pattern;
    if (slash) {
        size_t dlen = (size_t)(slash - base_pattern);
        dir = malloc(dlen + 1);
        if (!dir) {
            return NULL;
        }
        memcpy(dir, base_pattern, dlen);
        dir[dlen] = '\0';
        filepat = slash + 1;
    }

    DIR *d = opendir(dir ? dir : ".");
    if (!d) {
        free(dir);
        return NULL;
    }

    char **result = NULL;
    size_t result_count = 0;
    size_t result_cap = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        /* fnmatch without FNM_PERIOD: `*` matches leading-dot names,
         * which is exactly the D-qualifier semantics. */
        if (fnmatch(filepat, entry->d_name, 0) != 0) {
            continue;
        }
        // Build the path as the caller would see it.
        char *path;
        if (dir) {
            size_t plen = strlen(dir) + 1 + strlen(entry->d_name) + 1;
            path = malloc(plen);
            if (path) {
                snprintf(path, plen, "%s/%s", dir, entry->d_name);
            }
        } else {
            path = strdup(entry->d_name);
        }
        if (!path) {
            continue;
        }
        if ((qualifier & GLOB_QUAL_FILTER_MASK) &&
            !matches_glob_qualifier(path, qualifier)) {
            free(path);
            continue;
        }
        if (result_count + 1 >= result_cap) {
            size_t new_cap = result_cap ? result_cap * 2 : 8;
            char **nr = realloc(result, new_cap * sizeof(char *));
            if (!nr) {
                free(path);
                break;
            }
            result = nr;
            result_cap = new_cap;
        }
        result[result_count++] = path;
    }
    closedir(d);
    free(dir);

    if (!result) {
        // No matches: hand back an empty (non-NULL) array.
        result = malloc(sizeof(char *));
        if (result) {
            result[0] = NULL;
        }
        *count = 0;
        return result;
    }
    result[result_count] = NULL;
    *count = (int)result_count;
    return result;
}

static char **expand_glob_pattern(const char *pattern, int *expanded_count) {
    if (!pattern || !expanded_count) {
        *expanded_count = 0;
        return NULL;
    }

    // Check if globbing is disabled (set -f)
    if (shell_opts.no_globbing) {
        // Return the original pattern without expansion
        char **result = malloc(sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            *expanded_count = 1;
            return result;
        }
        *expanded_count = 0;
        return NULL;
    }

    // Try globstar expansion if ** pattern and FEATURE_GLOBSTAR is enabled
    if (shell_mode_allows(FEATURE_GLOBSTAR) && has_globstar_pattern(pattern)) {
        char **globstar_results =
            expand_globstar_pattern(pattern, expanded_count);
        if (globstar_results && *expanded_count > 0) {
            return globstar_results;
        }
        // Globstar expansion failed or no matches - fall through to handle
        // according to nullglob setting
        if (shell_mode_allows(FEATURE_NULL_GLOB)) {
            // Return empty array
            char **result = malloc(sizeof(char *));
            if (result) {
                result[0] = NULL;
                *expanded_count = 0;
                return result;
            }
            *expanded_count = 0;
            return NULL;
        }
        // Return original pattern
        char **result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            result[1] = NULL;
            *expanded_count = 1;
        } else {
            *expanded_count = 0;
        }
        return result;
    }

    // Parse glob qualifier FIRST (Zsh-style)
    // This must happen before extglob detection because *(.) could be either:
    //   - Zsh glob qualifier: * with file qualifier (.)
    //   - Bash extglob: zero or more of pattern "."
    // Glob qualifiers are always a single char at the END, so check that first
    char *base_pattern = NULL;
    glob_qualifier_t qualifier = GLOB_QUAL_NONE;

    if (shell_mode_allows(FEATURE_GLOB_QUALIFIERS)) {
        qualifier = parse_glob_qualifier(pattern, &base_pattern);
    }

    // If we found a glob qualifier, use the base pattern for further expansion
    // Otherwise, use the original pattern
    const char *pattern_to_expand =
        (qualifier != GLOB_QUAL_NONE) ? base_pattern : pattern;

    // Try zsh-style extglob expansion first (X#, X##, (a|b), ^pattern)
    if (qualifier == GLOB_QUAL_NONE &&
        has_zsh_extglob_pattern(pattern_to_expand)) {
        // Free base_pattern if it was allocated by parse_glob_qualifier
        free(base_pattern);
        char **zsh_results =
            expand_zsh_extglob_pattern(pattern_to_expand, expanded_count);
        if (zsh_results && *expanded_count > 0) {
            return zsh_results;
        }
        // Zsh extglob expansion failed or no matches - handle according to
        // nullglob
        if (shell_mode_allows(FEATURE_NULL_GLOB)) {
            char **result = malloc(sizeof(char *));
            if (result) {
                result[0] = NULL;
                *expanded_count = 0;
                return result;
            }
            *expanded_count = 0;
            return NULL;
        }
        // Return original pattern
        char **result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            result[1] = NULL;
            *expanded_count = 1;
        } else {
            *expanded_count = 0;
        }
        return result;
    }

    // Try bash-style extglob expansion if pattern contains extglob syntax
    // (only if we didn't already strip a glob qualifier)
    if (qualifier == GLOB_QUAL_NONE && has_extglob_pattern(pattern_to_expand)) {
        // Free base_pattern if it was allocated by parse_glob_qualifier
        free(base_pattern);
        char **extglob_results =
            expand_extglob_pattern(pattern_to_expand, expanded_count);
        if (extglob_results && *expanded_count > 0) {
            return extglob_results;
        }
        // Extglob expansion failed or no matches - fall through to handle
        // according to nullglob setting
        if (shell_mode_allows(FEATURE_NULL_GLOB)) {
            // Return empty array
            char **result = malloc(sizeof(char *));
            if (result) {
                result[0] = NULL;
                *expanded_count = 0;
                return result;
            }
            *expanded_count = 0;
            return NULL;
        }
        // Return original pattern
        char **result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            result[1] = NULL;
            *expanded_count = 1;
        } else {
            *expanded_count = 0;
        }
        return result;
    }

    /* Ensure base_pattern is set to a strdup of the original pattern
     * when no qualifier was parsed. parse_glob_qualifier already does
     * this on its GLOB_QUAL_NONE return paths (lines 5887, 5959), so
     * we only need to strdup here when the qualifier feature is
     * disabled and parse_glob_qualifier was therefore never called --
     * doing it unconditionally would leak the parse_glob_qualifier
     * allocation (issue #112). */
    if (qualifier == GLOB_QUAL_NONE && !base_pattern) {
        base_pattern = strdup(pattern);
    }

    if (!base_pattern) {
        *expanded_count = 0;
        return NULL;
    }

    /* The zsh `D` qualifier requests dotfile matching, which libc
     * glob() cannot do portably -- route through a readdir scan. */
    if (qualifier & GLOB_QUAL_DOTGLOB) {
        char **dot_results =
            expand_glob_dotglob(base_pattern, qualifier, expanded_count);
        free(base_pattern);
        if (dot_results) {
            return dot_results;
        }
        // Scan failed: fall back to the literal pattern.
        char **result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            result[1] = NULL;
            *expanded_count = 1;
        } else {
            *expanded_count = 0;
        }
        return result;
    }

    glob_t globbuf;
    int glob_result = glob(base_pattern, GLOB_NOSORT, NULL, &globbuf);
    free(base_pattern);

    if (glob_result == GLOB_NOMATCH) {
        // No matches - nullglob mode OR an explicit (N) qualifier
        // both mean "expand to nothing" rather than the literal.
        if (shell_mode_allows(FEATURE_NULL_GLOB) ||
            (qualifier & GLOB_QUAL_NULLGLOB)) {
            // Nullglob: unmatched patterns expand to nothing
            // Return empty array (not NULL, to distinguish from error)
            char **result = malloc(sizeof(char *));
            if (result) {
                result[0] = NULL;
                *expanded_count = 0;
                return result;
            }
            *expanded_count = 0;
            return NULL;
        }
        // Default POSIX behavior: return original pattern
        char **result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            result[1] = NULL;
            *expanded_count = 1;
        } else {
            *expanded_count = 0;
        }
        return result;
    } else if (glob_result != 0) {
        // Error in globbing
        *expanded_count = 0;
        return NULL;
    }

    // Success - copy results, filtering by qualifier if present
    if (qualifier == GLOB_QUAL_NONE) {
        // No filtering needed
        *expanded_count = globbuf.gl_pathc;
        char **result = malloc((globbuf.gl_pathc + 1) * sizeof(char *));
        if (!result) {
            globfree(&globbuf);
            *expanded_count = 0;
            return NULL;
        }

        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
            result[i] = strdup(globbuf.gl_pathv[i]);

            if (!result[i]) {
                // Cleanup on allocation failure
                for (size_t j = 0; j < i; j++) {
                    free(result[j]);
                }
                free(result);
                globfree(&globbuf);
                *expanded_count = 0;
                return NULL;
            }
        }
        result[globbuf.gl_pathc] = NULL;

        globfree(&globbuf);
        return result;
    } else {
        // Filter results by glob qualifier
        char **result = malloc((globbuf.gl_pathc + 1) * sizeof(char *));
        if (!result) {
            globfree(&globbuf);
            *expanded_count = 0;
            return NULL;
        }

        size_t match_count = 0;
        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
            if (matches_glob_qualifier(globbuf.gl_pathv[i], qualifier)) {
                result[match_count] = strdup(globbuf.gl_pathv[i]);
                if (!result[match_count]) {
                    // Cleanup on allocation failure
                    for (size_t j = 0; j < match_count; j++) {
                        free(result[j]);
                    }
                    free(result);
                    globfree(&globbuf);
                    *expanded_count = 0;
                    return NULL;
                }
                match_count++;
            }
        }
        result[match_count] = NULL;

        globfree(&globbuf);

        if (match_count == 0) {
            // No matches after filtering - nullglob mode OR an
            // explicit (N) qualifier both expand to nothing.
            free(result);
            if (shell_mode_allows(FEATURE_NULL_GLOB) ||
                (qualifier & GLOB_QUAL_NULLGLOB)) {
                // Nullglob: expand to nothing
                // Return empty array (not NULL, to distinguish from error)
                result = malloc(sizeof(char *));
                if (result) {
                    result[0] = NULL;
                    *expanded_count = 0;
                    return result;
                }
                *expanded_count = 0;
                return NULL;
            }
            // Default: return original pattern
            result = malloc(2 * sizeof(char *));
            if (result) {
                result[0] = strdup(pattern);
                result[1] = NULL;
                *expanded_count = 1;
            } else {
                *expanded_count = 0;
            }
            return result;
        }

        *expanded_count = match_count;
        return result;
    }
}

/**
 * @brief Check if string contains glob metacharacters
 *
 * Checks for *, ?, and [...] character class patterns.
 *
 * @param str String to check
 * @return true if glob expansion is needed
 */
static bool needs_glob_expansion(const char *str) {
    if (!str) {
        return false;
    }

    // Check for glob metacharacters: *, ?, and character classes [...]
    const char *p = str;
    while (*p) {
        if (*p == '*' || *p == '?' || *p == '[') {
            return true;
        }
        // Check for bash-style extglob patterns: ?(pat), *(pat), +(pat),
        // @(pat), !(pat)
        if (shell_mode_allows(FEATURE_EXTENDED_GLOB)) {
            if ((*p == '?' || *p == '*' || *p == '+' || *p == '@' ||
                 *p == '!') &&
                *(p + 1) == '(') {
                return true;
            }
            // Check for zsh-style extglob: ^pattern, X#, X##, (a|b)
            // ^ at start = negation
            if (p == str && *p == '^') {
                return true;
            }
            // # after a char or ] = zero or more quantifier
            if (*p == '#' && p > str) {
                char prev = *(p - 1);
                if (prev != '/' && prev != ' ' && prev != '\t') {
                    return true;
                }
            }
            // ( not preceded by extglob op may be zsh alternation
            if (*p == '(' && (p == str || !strchr("?*+@!", *(p - 1)))) {
                // Check for | inside
                const char *inner = p + 1;
                int depth = 1;
                while (*inner && depth > 0) {
                    if (*inner == '(')
                        depth++;
                    else if (*inner == ')')
                        depth--;
                    else if (*inner == '|' && depth == 1)
                        return true;
                    inner++;
                }
            }
        }
        p++;
    }
    return false;
}

/**
 * @brief Check if string contains brace expansion patterns
 *
 * Checks for {a,b,c} style patterns with comma separators.
 *
 * @param str String to check
 * @return true if brace expansion is needed
 */
static bool needs_brace_expansion(const char *str) {
    if (!str || !shell_mode_allows(FEATURE_BRACE_EXPANSION)) {
        return false;
    }

    // Check for brace expansion patterns: {a,b,c} or {1..10}
    const char *p = str;
    while (*p) {
        if (*p == '{') {
            const char *close = strchr(p + 1, '}');
            if (close) {
                // Look for comma pattern: {a,b,c}
                const char *comma = strchr(p + 1, ',');
                if (comma && comma < close) {
                    return true;
                }
                // Look for range pattern: {1..10} or {a..z}
                const char *dotdot = strstr(p + 1, "..");
                if (dotdot && dotdot < close) {
                    return true;
                }
            }
        }
        p++;
    }
    return false;
}

/**
 * @brief Expand brace range patterns like {1..10} or {a..z}
 *
 * Handles numeric ranges: {1..5} -> 1 2 3 4 5
 * Handles char ranges: {a..e} -> a b c d e
 * Handles step: {1..10..2} -> 1 3 5 7 9
 * Handles reverse: {5..1} -> 5 4 3 2 1
 * Handles zero-padding: {01..05} -> 01 02 03 04 05
 *
 * @param prefix String before the brace
 * @param content Content between braces (e.g., "1..10" or "a..z..2")
 * @param suffix String after the brace
 * @param expanded_count Output: number of expansions
 * @return Array of expanded strings (caller must free), or NULL on error
 */
/* Returns the configured brace expansion result-count cap.
 * 0 means unbounded (matches bash/zsh — caller skips the limit check). */
static int brace_expansion_cap(void) {
    int cap = config.brace_expansion_max;
    return cap > 0 ? cap : 0;
}

static char **expand_brace_range(const char *prefix, const char *content,
                                 const char *suffix, int *expanded_count) {
    *expanded_count = 0;

    // Parse: start..end or start..end..step
    const char *dotdot1 = strstr(content, "..");
    if (!dotdot1) {
        return NULL;
    }

    // Extract start
    size_t start_len = dotdot1 - content;
    char *start_str = strndup(content, start_len);
    if (!start_str)
        return NULL;

    // Find second .. for step (optional)
    const char *after_first = dotdot1 + 2;
    const char *dotdot2 = strstr(after_first, "..");

    char *end_str = NULL;
    char *step_str = NULL;

    if (dotdot2) {
        // Has step: start..end..step
        size_t end_len = dotdot2 - after_first;
        end_str = strndup(after_first, end_len);
        step_str = strdup(dotdot2 + 2);
    } else {
        // No step: start..end
        end_str = strdup(after_first);
    }

    if (!end_str) {
        free(start_str);
        free(step_str);
        return NULL;
    }

    // Determine if numeric or character range
    bool is_numeric = false;
    bool is_char = false;
    int pad_width = 0;

    // Check for zero-padding in numeric (e.g., "01")
    if (start_str[0] == '0' && strlen(start_str) > 1) {
        pad_width = strlen(start_str);
    }
    if (end_str[0] == '0' && strlen(end_str) > 1) {
        int end_pad = strlen(end_str);
        if (end_pad > pad_width)
            pad_width = end_pad;
    }

    // Check if start/end are single chars
    if (strlen(start_str) == 1 && strlen(end_str) == 1 &&
        isalpha(start_str[0]) && isalpha(end_str[0])) {
        is_char = true;
    } else {
        // Try to parse as numbers
        char *endptr;
        long start_val = strtol(start_str, &endptr, 10);
        if (*endptr == '\0') {
            long end_val = strtol(end_str, &endptr, 10);
            if (*endptr == '\0') {
                is_numeric = true;
                (void)start_val; // Used below
                (void)end_val;
            }
        }
    }

    if (!is_numeric && !is_char) {
        // Invalid range - return NULL, caller will use original
        free(start_str);
        free(end_str);
        free(step_str);
        return NULL;
    }

    // Parse step value
    long step = 1;
    if (step_str && strlen(step_str) > 0) {
        char *endptr;
        step = strtol(step_str, &endptr, 10);
        if (*endptr != '\0' || step == 0) {
            step = 1; // Invalid step, use default
        }
        if (step < 0)
            step = -step; // Step is always positive, direction from start/end
    }

    // Calculate range
    long start_val, end_val;
    if (is_char) {
        start_val = start_str[0];
        end_val = end_str[0];
    } else {
        start_val = strtol(start_str, NULL, 10);
        end_val = strtol(end_str, NULL, 10);
    }

    // Determine direction
    bool reverse = (start_val > end_val);

    // Count items
    long range = reverse ? (start_val - end_val) : (end_val - start_val);
    int count = (int)(range / step) + 1;

    int cap = brace_expansion_cap();
    if (count <= 0 || (cap > 0 && count > cap)) {
        /* Range exceeds configured cap (or is degenerate). Signal
         * limit-exceeded distinctly from malloc/parse failure so the
         * top-level caller can produce a real diagnostic. The cap path
         * relies on the sentinel; the count<=0 path keeps prior
         * "fall back to original pattern" behaviour by returning NULL
         * with *expanded_count = 0 unchanged. */
        if (cap > 0 && count > cap) {
            *expanded_count = BRACE_EXPANSION_LIMIT_SENTINEL;
        }
        free(start_str);
        free(end_str);
        free(step_str);
        return NULL;
    }

    // Allocate result array
    char **result = malloc((count + 1) * sizeof(char *));
    if (!result) {
        free(start_str);
        free(end_str);
        free(step_str);
        return NULL;
    }

    // Generate expansions
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);

    for (int i = 0; i < count; i++) {
        long val = reverse ? (start_val - i * step) : (start_val + i * step);

        char item_buf[32];
        if (is_char) {
            snprintf(item_buf, sizeof(item_buf), "%c", (char)val);
        } else if (pad_width > 0) {
            snprintf(item_buf, sizeof(item_buf), "%0*ld", pad_width, val);
        } else {
            snprintf(item_buf, sizeof(item_buf), "%ld", val);
        }

        size_t full_len = prefix_len + strlen(item_buf) + suffix_len;
        result[i] = malloc(full_len + 1);
        if (!result[i]) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                free(result[j]);
            }
            free(result);
            free(start_str);
            free(end_str);
            free(step_str);
            return NULL;
        }

        strcpy(result[i], prefix);
        strcat(result[i], item_buf);
        strcat(result[i], suffix);
    }

    result[count] = NULL;
    *expanded_count = count;

    free(start_str);
    free(end_str);
    free(step_str);

    // Recursively expand any remaining brace patterns in suffix
    // This handles Cartesian products like {1..2}{a..b}
    if (strchr(suffix, '{')) {
        char **final_results = NULL;
        int final_count = 0;
        int cap = brace_expansion_cap();
        bool limit_hit = false;

        for (int i = 0; i < count; i++) {
            if (limit_hit) {
                free(result[i]);
                continue;
            }
            if (needs_brace_expansion(result[i])) {
                int sub_count;
                char **sub_results =
                    expand_brace_pattern(result[i], &sub_count);
                if (sub_count == BRACE_EXPANSION_LIMIT_SENTINEL) {
                    free(result[i]);
                    limit_hit = true;
                    continue;
                }
                if (sub_results) {
                    if (cap > 0 && (long)final_count + sub_count > cap) {
                        for (int j = 0; j < sub_count; j++) {
                            free(sub_results[j]);
                        }
                        free(sub_results);
                        free(result[i]);
                        limit_hit = true;
                        continue;
                    }
                    // Add all sub-results to final
                    char **new_final =
                        realloc(final_results,
                                (final_count + sub_count) * sizeof(char *));
                    if (new_final) {
                        final_results = new_final;
                        for (int j = 0; j < sub_count; j++) {
                            final_results[final_count++] = sub_results[j];
                        }
                        free(sub_results);
                    } else {
                        for (int j = 0; j < sub_count; j++) {
                            free(sub_results[j]);
                        }
                        free(sub_results);
                    }
                    free(result[i]);
                } else {
                    if (cap > 0 && final_count + 1 > cap) {
                        free(result[i]);
                        limit_hit = true;
                        continue;
                    }
                    char **new_final = realloc(
                        final_results, (final_count + 1) * sizeof(char *));
                    if (new_final) {
                        final_results = new_final;
                        final_results[final_count++] = result[i];
                    } else {
                        free(result[i]);
                    }
                }
            } else {
                if (cap > 0 && final_count + 1 > cap) {
                    free(result[i]);
                    limit_hit = true;
                    continue;
                }
                char **new_final =
                    realloc(final_results, (final_count + 1) * sizeof(char *));
                if (new_final) {
                    final_results = new_final;
                    final_results[final_count++] = result[i];
                } else {
                    free(result[i]);
                }
            }
        }

        free(result);

        if (limit_hit) {
            for (int j = 0; j < final_count; j++) {
                free(final_results[j]);
            }
            free(final_results);
            *expanded_count = BRACE_EXPANSION_LIMIT_SENTINEL;
            return NULL;
        }

        char **terminated =
            realloc(final_results, (final_count + 1) * sizeof(char *));
        if (terminated) {
            terminated[final_count] = NULL;
            *expanded_count = final_count;
            return terminated;
        }

        *expanded_count = final_count;
        return final_results;
    }

    return result;
}

/**
 * @brief Expand brace patterns like {a,b,c} or {1..10}
 *
 * Expands patterns like file.{c,h} into [file.c, file.h].
 * Expands ranges like {1..5} into [1, 2, 3, 4, 5].
 * Handles prefix and suffix around the braces.
 *
 * @param pattern Pattern containing braces
 * @param expanded_count Output: number of expansions
 * @return Array of expanded strings (caller must free), or NULL on error
 */
char **expand_brace_pattern(const char *pattern, int *expanded_count) {
    if (!pattern || !expanded_count) {
        *expanded_count = 0;
        return NULL;
    }

    // Find the first brace pattern
    const char *open = strchr(pattern, '{');
    if (!open) {
        // No braces - return original pattern
        char **result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            result[1] = NULL;
            *expanded_count = 1;
        } else {
            *expanded_count = 0;
        }
        return result;
    }

    /* Find the matching close brace, tracking nesting depth so that
     * patterns like `{{1..3},{a..c}}` resolve the OUTER brace first
     * rather than the first inner `}`. Without this, the function
     * splits content as `{1..3` and bails, leaving the pattern
     * un-expanded (real_world/bash/206). */
    const char *close = NULL;
    {
        int depth = 1;
        for (const char *p = open + 1; *p; p++) {
            if (*p == '{') {
                depth++;
            } else if (*p == '}') {
                depth--;
                if (depth == 0) {
                    close = p;
                    break;
                }
            }
        }
    }
    if (!close) {
        // Malformed brace - return original pattern
        char **result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            result[1] = NULL;
            *expanded_count = 1;
        } else {
            *expanded_count = 0;
        }
        return result;
    }

    // Extract prefix, brace content, and suffix
    size_t prefix_len = open - pattern;
    size_t content_len = close - open - 1;
    const char *suffix = close + 1;

    char *prefix = malloc(prefix_len + 1);
    char *content = malloc(content_len + 1);
    if (!prefix || !content) {
        free(prefix);
        free(content);
        *expanded_count = 0;
        return NULL;
    }

    strncpy(prefix, pattern, prefix_len);
    prefix[prefix_len] = '\0';
    strncpy(content, open + 1, content_len);
    content[content_len] = '\0';

    /* Check if this is a SIMPLE range pattern (e.g. `1..10`, `a..z`,
     * `1..10..2`). A range pattern has `..` and no top-level commas
     * and no nested braces — otherwise it's a comma-list whose items
     * happen to contain ranges (e.g. `{{1..3},{a..c}}`) and must be
     * split on the outer commas first. */
    bool is_range = false;
    if (strstr(content, "..")) {
        is_range = true;
        int depth = 0;
        for (const char *p = content; *p; p++) {
            if (*p == '{') {
                depth++;
            } else if (*p == '}') {
                depth--;
            } else if (*p == ',' && depth == 0) {
                is_range = false;
                break;
            }
        }
    }
    if (is_range) {
        char **range_result =
            expand_brace_range(prefix, content, suffix, expanded_count);
        free(prefix);
        free(content);
        if (range_result) {
            return range_result;
        }
        // Range expansion failed - fall through to return original pattern
        char **result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            result[1] = NULL;
            *expanded_count = 1;
        }
        return result;
    }

    /* Count comma-separated items at the TOP LEVEL only. Commas
     * inside nested braces belong to the inner pattern and must not
     * split the outer one (e.g. `{{1..3},{a..c}}` has two outer
     * items, not five). */
    int item_count = 1;
    {
        int depth = 0;
        for (const char *p = content; *p; p++) {
            if (*p == '{') {
                depth++;
            } else if (*p == '}') {
                depth--;
            } else if (*p == ',' && depth == 0) {
                item_count++;
            }
        }
    }

    // Allocate result array
    char **result = malloc((item_count + 1) * sizeof(char *));
    if (!result) {
        free(prefix);
        free(content);
        *expanded_count = 0;
        return NULL;
    }

    // Split content by commas and build result strings
    int result_index = 0;
    char *item_start = content;
    char *comma_pos = content;

    while (result_index < item_count) {
        /* Find next TOP-LEVEL comma (depth 0) or end of string.
         * Nested braces hide their commas from the outer split. */
        int depth = 0;
        while (*comma_pos && !(depth == 0 && *comma_pos == ',')) {
            if (*comma_pos == '{') {
                depth++;
            } else if (*comma_pos == '}') {
                depth--;
            }
            comma_pos++;
        }

        // Extract current item
        size_t item_len = comma_pos - item_start;
        char *item = malloc(item_len + 1);
        if (!item) {
            // Cleanup on failure
            for (int i = 0; i < result_index; i++) {
                free(result[i]);
            }
            free(result);
            free(prefix);
            free(content);
            *expanded_count = 0;
            return NULL;
        }
        strncpy(item, item_start, item_len);
        item[item_len] = '\0';

        // Build full result string: prefix + item + suffix
        size_t full_len = strlen(prefix) + strlen(item) + strlen(suffix);
        result[result_index] = malloc(full_len + 1);
        if (!result[result_index]) {
            // Cleanup on failure
            free(item);
            for (int i = 0; i < result_index; i++) {
                free(result[i]);
            }
            free(result);
            free(prefix);
            free(content);
            *expanded_count = 0;
            return NULL;
        }

        strcpy(result[result_index], prefix);
        strcat(result[result_index], item);
        strcat(result[result_index], suffix);

        free(item);
        result_index++;

        // Move to next item
        if (*comma_pos == ',') {
            comma_pos++;
            item_start = comma_pos;
        }
    }

    result[item_count] = NULL;
    *expanded_count = item_count;

    free(prefix);
    free(content);

    /* Recursively expand any remaining brace patterns in results.
     * This handles Cartesian products like `{1..2}{a..b}` (where the
     * suffix carries another brace) AND nested patterns like
     * `{{1..3},{a..c}}` (where each comma-separated item is itself a
     * brace pattern). Scan all results so neither case is missed. */
    bool any_result_has_brace = false;
    for (int i = 0; i < item_count; i++) {
        if (strchr(result[i], '{')) {
            any_result_has_brace = true;
            break;
        }
    }
    if (any_result_has_brace) {
        char **final_results = NULL;
        int final_count = 0;
        int cap = brace_expansion_cap();
        bool limit_hit = false;

        for (int i = 0; i < item_count; i++) {
            if (limit_hit) {
                /* Already over cap — drain remaining originals
                 * cleanly to avoid leaks. */
                free(result[i]);
                continue;
            }
            if (needs_brace_expansion(result[i])) {
                int sub_count;
                char **sub_results =
                    expand_brace_pattern(result[i], &sub_count);
                if (sub_count == BRACE_EXPANSION_LIMIT_SENTINEL) {
                    // Recursive cap propagation.
                    free(result[i]);
                    limit_hit = true;
                    continue;
                }
                if (sub_results) {
                    if (cap > 0 && (long)final_count + sub_count > cap) {
                        for (int j = 0; j < sub_count; j++) {
                            free(sub_results[j]);
                        }
                        free(sub_results);
                        free(result[i]);
                        limit_hit = true;
                        continue;
                    }
                    // Add all sub-results to final
                    char **new_final =
                        realloc(final_results,
                                (final_count + sub_count) * sizeof(char *));
                    if (new_final) {
                        final_results = new_final;
                        for (int j = 0; j < sub_count; j++) {
                            final_results[final_count++] = sub_results[j];
                        }
                        free(sub_results); // Free array, not strings
                    } else {
                        // Memory error - cleanup and return what we have
                        for (int j = 0; j < sub_count; j++) {
                            free(sub_results[j]);
                        }
                        free(sub_results);
                    }
                    free(result[i]); // Free original since we expanded it
                } else {
                    // Sub-expansion failed, keep original
                    if (cap > 0 && final_count + 1 > cap) {
                        free(result[i]);
                        limit_hit = true;
                        continue;
                    }
                    char **new_final = realloc(
                        final_results, (final_count + 1) * sizeof(char *));
                    if (new_final) {
                        final_results = new_final;
                        final_results[final_count++] = result[i];
                    } else {
                        /* realloc failed — original was unmodified, but
                         * result[i] is now orphaned; free it to avoid
                         * a leak under malloc pressure. */
                        free(result[i]);
                    }
                }
            } else {
                // No more braces, keep as-is
                if (cap > 0 && final_count + 1 > cap) {
                    free(result[i]);
                    limit_hit = true;
                    continue;
                }
                char **new_final =
                    realloc(final_results, (final_count + 1) * sizeof(char *));
                if (new_final) {
                    final_results = new_final;
                    final_results[final_count++] = result[i];
                } else {
                    free(result[i]);
                }
            }
        }

        free(result); // Free original array

        if (limit_hit) {
            /* Cap exceeded — release the partial accumulation cleanly
             * and return the limit sentinel for the top-level caller. */
            for (int j = 0; j < final_count; j++) {
                free(final_results[j]);
            }
            free(final_results);
            *expanded_count = BRACE_EXPANSION_LIMIT_SENTINEL;
            return NULL;
        }

        // Add NULL terminator
        char **terminated =
            realloc(final_results, (final_count + 1) * sizeof(char *));
        if (terminated) {
            terminated[final_count] = NULL;
            *expanded_count = final_count;
            return terminated;
        }

        *expanded_count = final_count;
        return final_results;
    }

    return result;
}

/**
 * @brief Execute external command with full redirection setup
 *
 * Forks and sets up redirections in the child process before exec.
 * Handles command hashing, tracing (set -x), and debug profiling.
 *
 * @param executor Executor context
 * @param argv NULL-terminated argument vector
 * @param redirect_stderr If true, redirect stderr to /dev/null
 * @param command Command node for redirection setup
 * @return Exit status of command
 */
static int execute_external_command_with_setup(executor_t *executor,
                                               char **argv,
                                               bool redirect_stderr,
                                               node_t *command) {
    if (!argv || !argv[0]) {
        return 1;
    }

    // Check if command exists before forking (for better error messages)
    // Skip this check for path-based commands (containing '/')
    char *full_path = NULL;
    if (!strchr(argv[0], '/')) {
        full_path = find_command_in_path(argv[0]);
        if (!full_path) {
            // Command not found - report with suggestions from parent process
            source_location_t loc = command ? command->loc : SOURCE_LOC_UNKNOWN;
            report_command_not_found(executor, argv[0], loc);
            return 127;
        }

        // If hashall is enabled, remember this command's location
        if (shell_opts.hash_commands) {
            init_command_hash();
            if (command_hash) {
                ht_strstr_insert(command_hash, argv[0], full_path);
            }
        }
        free(full_path);
    }

    // Reset terminal state before forking for external commands
    // This ensures git and other commands get proper TTY behavior
    if (is_interactive_shell()) {
        fflush(stdout);
        fflush(stderr);
    }

    pid_t pid = lush_fork();
    if (pid == -1) {
        set_executor_error(executor, "Failed to fork");
        return 1;
    }

    if (pid == 0) {
        // Child process - setup redirections here
        int redir_result = setup_redirections(executor, command);
        if (redir_result != 0) {
            exit(1);
        }

        if (redirect_stderr) {
            // Redirect stderr to /dev/null
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd != -1) {
                dup2(null_fd, STDERR_FILENO);
                close(null_fd);
            }
        }

        execvp(argv[0], argv);
        // Check errno to determine appropriate exit code
        int exit_code = 127; // Default: command not found
        if (errno == EACCES) {
            exit_code = 126; // Permission denied
        } else if (errno == ENOENT) {
            exit_code = 127; // Command not found
        }
        if (!redirect_stderr) {
            int saved_errno = errno;
            executor_error_report(executor, SHELL_ERR_EXEC_FAILED,
                                  command ? command->loc : SOURCE_LOC_UNKNOWN,
                                  "%s: %s", argv[0], strerror(saved_errno));
        }
        exit(exit_code);
    } else {
        // Parent process
        set_current_child_pid(pid);

        // Print trace for external command if -x is enabled
        if (should_trace_execution()) {
            // Build command string from argv for tracing
            size_t cmd_len = 1; // for null terminator
            for (int j = 0; argv[j]; j++) {
                cmd_len += strlen(argv[j]) + (j > 0 ? 1 : 0); // +1 for space
            }

            char *cmd_str = malloc(cmd_len);
            if (cmd_str) {
                strcpy(cmd_str, argv[0]);
                for (int j = 1; argv[j]; j++) {
                    strcat(cmd_str, " ");
                    strcat(cmd_str, argv[j]);
                }
                print_command_trace(cmd_str);
                free(cmd_str);
            }
        }

        // Enhanced debug tracing for external commands with setup
        DEBUG_TRACE_COMMAND(argv[0], argv, 0);
        DEBUG_PROFILE_ENTER(argv[0]);

        int status;
        // Wait for child, retrying on EINTR (signal interruption)
        while (waitpid(pid, &status, 0) == -1) {
            if (errno != EINTR) {
                // Real error - child may have already been reaped
                clear_current_child_pid();
                return 1;
            }
            // EINTR - signal interrupted wait, continue waiting
        }
        clear_current_child_pid();

        DEBUG_PROFILE_EXIT(argv[0]);

        // Handle exit status properly - child may have exited or been signaled
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            // Child was killed by signal - return 128 + signal number (bash
            // convention)
            return 128 + WTERMSIG(status);
        }
        return 1;
    }
}

// Forward declaration for test builtin
static int execute_test_builtin(executor_t *executor, char **argv);

/**
 * @brief Execute a builtin command
 *
 * Looks up and executes a shell builtin command from the builtins table.
 * Handles command tracing (set -x) and sets global executor for job control.
 *
 * @param executor Executor context
 * @param argv NULL-terminated argument vector
 * @return Exit status of builtin command
 */
static int execute_builtin_command(executor_t *executor, char **argv,
                                   source_location_t loc) {
    if (!argv || !argv[0]) {
        return 1;
    }

    // Set global executor for job control builtins
    current_executor = executor;

    /* Stash the call-site source location for the duration of this
     * builtin invocation so builtin error helpers can produce a real
     * `--> file:line:col` line and source-snippet caret. The swap
     * returns the previously-stashed loc, which we restore on every
     * exit path so a re-entrant builtin (e.g. `eval` invoking another
     * builtin) doesn't clobber the outer caller's location when the
     * inner call returns. */
    source_location_t saved_loc = builtin_swap_source_location(loc);

    // Find the builtin function in the builtin table
    for (size_t i = 0; i < builtins_count; i++) {
        if (strcmp(argv[0], builtins[i].name) == 0) {
            // Print trace for builtin command if -x is enabled
            if (should_trace_execution()) {
                // Build command string from argv for tracing
                size_t cmd_len = 1; // for null terminator
                for (int j = 0; argv[j]; j++) {
                    cmd_len +=
                        strlen(argv[j]) + (j > 0 ? 1 : 0); // +1 for space
                }

                char *cmd_str = malloc(cmd_len);
                if (cmd_str) {
                    strcpy(cmd_str, argv[0]);
                    for (int j = 1; argv[j]; j++) {
                        strcat(cmd_str, " ");
                        strcat(cmd_str, argv[j]);
                    }
                    print_command_trace(cmd_str);
                    free(cmd_str);
                }
            }

            // Count arguments
            int argc = 0;
            while (argv[argc]) {
                argc++;
            }

            /* Push "in builtin '<name>'" onto the executor's context
             * stack for the duration of this builtin invocation.
             * executor_error_report() (the canonical wrapper) walks
             * the context stack at display time, so any error a
             * builtin emits — directly or via the wrapper — picks up
             * this context frame automatically. Per-builtin sites no
             * longer need to push it themselves. */
            executor_push_context(executor, loc, "in builtin '%s'", argv[0]);

            int result = builtins[i].func(argc, argv);

            // Restore previous loc + clear builtin context + global executor
            executor_pop_context(executor);
            (void)builtin_swap_source_location(saved_loc);
            current_executor = NULL;

            return result;
        }
    }

    // Restore previous loc + clear global executor
    (void)builtin_swap_source_location(saved_loc);
    current_executor = NULL;

    return 1; // Command not found
}

/**
 * @brief Check if command name is a builtin
 *
 * @param cmd Command name to check
 * @return true if command is a shell builtin
 */
static bool is_builtin_command(const char *cmd) { return is_builtin(cmd); }

/**
 * @brief Execute the test/[ builtin command
 *
 * Evaluates test expressions for conditionals. Supports:
 * - Unary operators: -z, -n (string tests)
 * - Binary operators: =, !=, -eq, -ne, -lt, -le, -gt, -ge
 *
 * @param executor Executor context (reserved for future use)
 * @param argv NULL-terminated argument vector
 * @return 0 if test succeeds, 1 if test fails
 */
MAYBE_UNUSED
static int execute_test_builtin(executor_t *executor, char **argv) {
    (void)executor; // Reserved for executor-aware test evaluation
    if (!argv || !argv[0]) {
        return 1;
    }

    // Count arguments
    int argc = 0;
    while (argv[argc]) {
        argc++;
    }

    // Handle [ command - must end with ]
    if (strcmp(argv[0], "[") == 0) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            return 1; // Missing closing ]
        }
        argc--; // Don't count the closing ]
    }

    // Handle different test cases
    if (argc == 1) {
        // test with no arguments - false
        return 1;
    }

    if (argc == 2) {
        // test STRING - true if STRING is non-empty
        return (argv[1] && strlen(argv[1]) > 0) ? 0 : 1;
    }

    if (argc == 3) {
        // Unary operators
        if (strcmp(argv[1], "-z") == 0) {
            // -z STRING - true if STRING is empty
            return (argv[2] && strlen(argv[2]) == 0) ? 0 : 1;
        }
        if (strcmp(argv[1], "-n") == 0) {
            // -n STRING - true if STRING is non-empty
            return (argv[2] && strlen(argv[2]) > 0) ? 0 : 1;
        }
        // Add more unary operators as needed
        return 1;
    }

    if (argc == 4) {
        // Binary operators: STRING1 OP STRING2
        char *str1 = argv[1];
        char *op = argv[2];
        char *str2 = argv[3];

        if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
            // String equality
            return strcmp(str1, str2) == 0 ? 0 : 1;
        }

        if (strcmp(op, "!=") == 0) {
            // String inequality
            return strcmp(str1, str2) != 0 ? 0 : 1;
        }

        if (strcmp(op, "-eq") == 0) {
            // Numeric equality
            int num1 = atoi(str1);
            int num2 = atoi(str2);
            return (num1 == num2) ? 0 : 1;
        }

        if (strcmp(op, "-ne") == 0) {
            // Numeric inequality
            int num1 = atoi(str1);
            int num2 = atoi(str2);
            return (num1 != num2) ? 0 : 1;
        }

        if (strcmp(op, "-lt") == 0) {
            // Numeric less than
            int num1 = atoi(str1);
            int num2 = atoi(str2);
            return (num1 < num2) ? 0 : 1;
        }

        if (strcmp(op, "-le") == 0) {
            // Numeric less than or equal
            int num1 = atoi(str1);
            int num2 = atoi(str2);
            return (num1 <= num2) ? 0 : 1;
        }

        if (strcmp(op, "-gt") == 0) {
            // Numeric greater than
            int num1 = atoi(str1);
            int num2 = atoi(str2);
            return (num1 > num2) ? 0 : 1;
        }

        if (strcmp(op, "-ge") == 0) {
            // Numeric greater than or equal
            int num1 = atoi(str1);
            int num2 = atoi(str2);
            return (num1 >= num2) ? 0 : 1;
        }
    }

    // Default: false
    return 1;
}

/**
 * @brief Check if text is a variable assignment
 *
 * An assignment has the form VAR=value with = not at the start.
 * Parameter expansions ${...} are not treated as assignments.
 *
 * @param text Text to check
 * @return true if text is an assignment
 */
static bool is_assignment(const char *text) {
    if (!text) {
        return false;
    }

    // Don't treat parameter expansion ${...} as assignment
    if (text[0] == '$' && text[1] == '{') {
        return false;
    }

    // Look for '=' not at the beginning
    const char *eq = strchr(text, '=');
    return eq && eq != text;
}

/**
 * @brief Execute a variable assignment
 *
 * Parses and executes VAR=value assignments. Handles:
 * - Variable name validation
 * - Value expansion
 * - Local vs global scope based on context
 * - Auto-export with set -a
 * - Privileged mode restrictions
 *
 * @param executor Executor context
 * @param assignment Assignment string (VAR=value)
 * @return 0 on success, 1 on failure
 */
static int execute_assignment(executor_t *executor, const char *assignment,
                              source_location_t loc) {
    if (!executor || !assignment) {
        return 1;
    }

    char *eq = strchr(assignment, '=');
    if (!eq) {
        return 1;
    }

    // Check for += append operation
    bool is_append = (eq > assignment && *(eq - 1) == '+');

    // Split into variable and value
    size_t var_len = eq - assignment;
    if (is_append) {
        var_len--; // Exclude the '+' from variable name
    }

    char *var_name = malloc(var_len + 1);
    if (!var_name) {
        return 1;
    }

    strncpy(var_name, assignment, var_len);
    var_name[var_len] = '\0';

    // Privileged mode security check for environment variable modifications
    if (!is_privileged_path_modification_allowed(var_name)) {
        executor_error_report(
            executor, SHELL_ERR_PERMISSION_DENIED, loc,
            "%s: cannot modify restricted variable in privileged mode",
            var_name);
        free(var_name);
        return 1;
    }

    // Validate variable name
    if (!var_name[0] || (!isalpha(var_name[0]) && var_name[0] != '_')) {
        free(var_name);
        return 1;
    }

    for (size_t i = 1; i < var_len; i++) {
        if (!isalnum(var_name[i]) && var_name[i] != '_') {
            free(var_name);
            return 1;
        }
    }

    // Expand the value using modern expansion
    // Save exit status set by command substitution (POSIX: assignment-only
    // commands should return the exit status of the last command substitution)
    char *value = expand_if_needed(executor, eq + 1);
    int cmd_sub_exit_status = executor->exit_status;

    /* Propagate expansion failure. ${var:?word} and friends set
     * expansion_error during expand_if_needed; without this check
     * execute_assignment silently stores the empty fallback and
     * returns 0, masking the failure from the caller (execute_command
     * does check this flag in the command-not-assignment path, but
     * assignment-only commands bypassed that check). Free what was
     * allocated and surface expansion_exit_status. shell_exit_requested,
     * if set, has already been raised by executor_request_posix_exit
     * and will short-circuit the surrounding command list / loop /
     * function body. */
    if (executor->expansion_error) {
        free(value);
        free(var_name);
        return executor->expansion_exit_status;
    }

    /* Integer attribute (declare -i): subsequent assignments to the
     * variable arith-evaluate the RHS rather than storing the literal
     * string. `declare -i n; n=5+3` stores "8", not "5+3". Same for
     * `n=other_var+1` -- the RHS is evaluated as an arithmetic
     * expression which resolves identifiers as variables. Issue #102. */
    if (symtable_get_flags(executor->symtable, var_name) &
        SYMVAR_INTEGER_ATTR) {
        char *evaluated =
            arithm_expand_with_executor(executor, value ? value : "");
        if (evaluated) {
            free(value);
            value = evaluated;
        }
    }

    // Resolve nameref if the variable is a nameref (max depth 10)
    const char *target_name = var_name;
    char *resolved_to_free = NULL; // Track if we need to free resolved name
    if (symtable_is_nameref(executor->symtable, var_name)) {
        const char *resolved =
            symtable_resolve_nameref(executor->symtable, var_name, 10);
        if (resolved && resolved != var_name) {
            target_name = resolved;
            resolved_to_free = (char *)resolved; // May need to free this
        }
    }

    // POSIX compliance: variable assignments are GLOBAL by default
    // Local variables are only created via explicit 'local' builtin
    int result;

    if (is_append) {
        // += dispatches on the target's kind: list/map appends element,
        // scalar concatenates string. One symtable_lookup tags the kind
        // explicitly instead of the legacy "try array, fall back to
        // scalar" dance.
        lush_value_view_t view = {0};
        symtable_lookup(target_name, &view);
        if (view.kind == LUSH_VALUE_LIST || view.kind == LUSH_VALUE_MAP) {
            symtable_array_append(view.array, value ? value : "");
            lush_value_view_clear(&view);
            result = 0;
        } else {
            // String append: take ownership of the scalar out of the view
            // and proceed with the existing concatenate-and-reassign path.
            char *existing = view.scalar_value;
            view.scalar_value = NULL;
            lush_value_view_clear(&view);
            if (existing && existing[0]) {
                size_t existing_len = strlen(existing);
                size_t value_len = value ? strlen(value) : 0;
                char *combined = malloc(existing_len + value_len + 1);
                if (combined) {
                    strcpy(combined, existing);
                    if (value) {
                        strcat(combined, value);
                    }
                    result = symtable_assign_var(executor->symtable,
                                                 target_name, combined);
                    free(combined);
                } else {
                    result = -1;
                }
            } else {
                // No existing value, just set the new value
                result = symtable_assign_var(executor->symtable, target_name,
                                             value ? value : "");
            }
            // symtable_get_var returns a strdup; free it. Pre-existing
            // leak fixed as part of the symtable_lookup migration.
            free(existing);
        }
    } else {
        result = symtable_assign_var(executor->symtable, target_name,
                                     value ? value : "");
    }

    // Free resolved nameref if it was allocated
    if (resolved_to_free) {
        free(resolved_to_free);
    }

    // POSIX -a (allexport): automatically export assigned variables
    if (result == 0 && should_auto_export()) {
        symtable_export_global(var_name);
    }

    // Notify LLE prompt system when prompt variables are set by user code
    if (result == 0 &&
        (strcmp(var_name, "PS1") == 0 || strcmp(var_name, "PS2") == 0 ||
         strcmp(var_name, "PROMPT") == 0 || strcmp(var_name, "RPROMPT") == 0 ||
         strcmp(var_name, "RPS1") == 0)) {
        lle_shell_notify_prompt_var_set(var_name, value ? value : "");
    }

    if (executor->debug) {
        printf("DEBUG: Assignment %s=%s (result: %d)\n", var_name,
               value ? value : "", result);
    }

    free(var_name);
    free(value);

    // POSIX: For assignment-only commands, return the exit status of the
    // last command substitution performed during value expansion, or 0
    // if no command substitution was performed or if the assignment failed
    if (result == 0) {
        executor->exit_status = cmd_sub_exit_status;
        return cmd_sub_exit_status;
    }
    return 1;
}

/**
 * @brief Execute a case statement
 *
 * Matches test word against patterns and executes corresponding commands.
 * Patterns can be separated by | for alternation.
 *
 * @param executor Executor context
 * @param node Case statement node
 * @return Exit status of executed commands, or 0 if no match
 */
static int execute_case(executor_t *executor, node_t *node) {
    if (!executor || !node || node->type != NODE_CASE) {
        return 1;
    }

    // Get the test word and expand variables in it
    char *test_word = expand_if_needed(executor, node->val.str);
    if (!test_word) {
        return 1;
    }

    // Push error context for structured error reporting
    executor_push_context(executor, node->loc, "in case statement");

    // Check for trailing redirections on the case statement
    bool has_redirections = count_redirections(node) > 0;
    redirection_state_t redir_state;

    if (has_redirections) {
        save_file_descriptors(&redir_state);
        int redir_result = setup_redirections(executor, node);
        if (redir_result != 0) {
            restore_file_descriptors(&redir_state);
            free(test_word);
            executor_pop_context(executor);
            return redir_result;
        }
    }

    int result = 0;
    bool done = false;
    bool execute_next = false; // For ;& fall-through

    // Iterate through case items (children)
    node_t *case_item = node->first_child;
    while (case_item && !done) {
        // The pattern is stored in case_item->val.str with terminator prefix
        // Format: "<terminator_char><pattern>" where terminator_char is '0',
        // '1', or '2'
        char *patterns = case_item->val.str;
        if (!patterns || !*patterns) {
            case_item = case_item->next_sibling;
            continue;
        }

        // Extract terminator type from pattern prefix (for NODE_CASE_ITEM)
        case_terminator_t terminator = CASE_TERM_BREAK;
        if (case_item->type == NODE_CASE_ITEM && patterns[0] >= '0' &&
            patterns[0] <= '2') {
            terminator = (case_terminator_t)(patterns[0] - '0');
            patterns++; // Skip the prefix byte
        }

        bool matched = execute_next; // If fall-through, execute without testing

        if (!matched) {
            /* Split patterns by `|` and test each. strtok cannot be
             * used here because it skips empty tokens -- POSIX `case`
             * accepts an empty pattern (`''` matches the empty word),
             * and a multi-pattern arm may legitimately contain an
             * empty alternative (`case x in ''|a) ...`). strtok would
             * silently drop those, falling through to the default
             * `*)` and producing a POSIX-violating result (issue #95).
             *
             * Use strchr-based splitting that emits empty tokens. */
            const char *p = patterns;
            while (p && !matched) {
                const char *bar = strchr(p, '|');
                size_t plen = bar ? (size_t)(bar - p) : strlen(p);
                char *pattern = malloc(plen + 1);
                if (!pattern) {
                    free(test_word);
                    return 1;
                }
                memcpy(pattern, p, plen);
                pattern[plen] = '\0';

                char *expanded_pattern = expand_if_needed(executor, pattern);
                if (expanded_pattern) {
                    if (match_pattern(test_word, expanded_pattern)) {
                        matched = true;
                    }
                    free(expanded_pattern);
                }
                free(pattern);

                if (!bar) {
                    break;
                }
                p = bar + 1;
            }
        }

        if (matched) {
            /* Execute commands for this case item. A case arm is a
             * command list - run every statement sequentially. Do NOT
             * short-circuit on a non-zero status (that was wrong: it
             * dropped `exit $?` after a non-zero `do_status` in
             * real_world/posix/101 init scripts, and silently dropped
             * later statements in `x) echo a; false; echo b ;;`).
             * Errexit (set -e) is enforced at execute_command_list,
             * not here. Honor loop control / shell exit between
             * statements so `break` / `continue` / `exit` propagate
             * out of the arm without running trailing commands. */
            node_t *commands = case_item->first_child;
            while (commands) {
                result = execute_node(executor, commands);
                if (executor->loop_control != LOOP_NORMAL ||
                    executor->shell_exit_requested || exit_flag) {
                    break;
                }
                commands = commands->next_sibling;
            }

            // Handle terminator behavior
            switch (terminator) {
            case CASE_TERM_BREAK:
                // ;; - stop processing case items
                done = true;
                execute_next = false;
                break;
            case CASE_TERM_FALLTHROUGH:
                // ;& - execute next case item without testing pattern
                execute_next = true;
                break;
            case CASE_TERM_CONTINUE:
                // ;;& - continue testing next patterns
                execute_next = false;
                break;
            }
        } else {
            execute_next = false;
        }

        case_item = case_item->next_sibling;
    }

    free(test_word);

    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    // Pop error context
    executor_pop_context(executor);

    return result;
}

/**
 * @brief Execute a function definition
 *
 * Stores function name and body in the executor's function table.
 * Supports optional parameter syntax (disabled in POSIX mode).
 *
 * @param executor Executor context
 * @param node Function definition node
 * @return 0 on success, 1 on failure
 */
static int execute_function_definition(executor_t *executor, node_t *node) {
    if (!executor || !node || node->type != NODE_FUNCTION) {
        return 1;
    }

    char *function_name = node->val.str;
    if (!function_name) {
        executor_error_add(executor, SHELL_ERR_FUNCTION_ERROR, node->loc,
                           "function definition missing name");
        return 1;
    }

    // Get function body (can be NULL for empty function bodies)
    node_t *body = node->first_child;

    // Extract parameter information from function name if encoded
    function_param_t *params = NULL;
    int param_count = 0;
    char *actual_function_name = function_name;

    // Check if function name contains parameter encoding
    // POSIX compliance: disable advanced parameter syntax in strict POSIX mode
    char *param_separator = strchr(function_name, '|');
    if (param_separator && !is_posix_mode_enabled()) {
        // Extract actual function name
        size_t name_len = param_separator - function_name;
        actual_function_name = malloc(name_len + 1);
        strncpy(actual_function_name, function_name, name_len);
        actual_function_name[name_len] = '\0';

        // Parse parameter information
        char *param_info = param_separator + 1;
        if (strncmp(param_info, "PARAMS{", 7) == 0) {
            char *param_list = param_info + 7;
            char *end_brace = strchr(param_list, '}');
            if (end_brace) {
                *end_brace = '\0'; // Temporarily null-terminate

                // Parse parameter list
                char *param_copy = strdup(param_list);
                char *token = strtok(param_copy, ",");
                function_param_t *last_param = NULL;

                while (token) {
                    char *equals = strchr(token, '=');
                    char *param_name = token;
                    char *default_value = NULL;

                    if (equals) {
                        *equals = '\0';
                        default_value = equals + 1;
                    }

                    function_param_t *param =
                        create_function_param(param_name, default_value);
                    if (param) {
                        if (!params) {
                            params = param;
                        } else {
                            last_param->next = param;
                        }
                        last_param = param;
                        param_count++;
                    }

                    token = strtok(NULL, ",");
                }

                free(param_copy);
                *end_brace = '}'; // Restore original string
            }
        }
    }

    // Store function in function table
    if (store_function(executor, actual_function_name, body, params,
                       param_count) != 0) {
        set_executor_error(executor, "Failed to define function");
        if (actual_function_name != function_name) {
            free(actual_function_name);
        }
        return 1;
    }

    if (executor->debug) {
        printf("DEBUG: Defined function '%s' with %d parameters\n",
               actual_function_name, param_count);
    }

    // Clean up allocated function name if we created one
    if (actual_function_name != function_name) {
        free(actual_function_name);
    }

    return 0;
}

/**
 * @brief Check if a function is defined
 *
 * @param executor Executor context
 * @param function_name Name of function to check
 * @return true if function exists in function table
 */
static bool is_function_defined(executor_t *executor,
                                const char *function_name) {
    return find_function(executor, function_name) != NULL;
}

/**
 * @brief Execute a function call
 *
 * Creates a function scope, sets up positional parameters,
 * executes the function body, and handles return values.
 *
 * @param executor Executor context
 * @param function_name Name of function to call
 * @param argv Argument vector (argv[0] is function name)
 * @param argc Argument count
 * @return Exit status of function body
 */
static int execute_function_call(executor_t *executor,
                                 const char *function_name, char **argv,
                                 int argc, source_location_t loc) {
    if (!executor || !function_name) {
        return 1;
    }

    function_def_t *func = find_function(executor, function_name);
    if (!func) {
        set_executor_error(executor, "Function not found");
        return 1;
    }

    // Validate function parameters (errors already displayed structurally)
    if (validate_function_parameters(executor, func, argv, argc, loc) != 0) {
        return 1;
    }

    if (executor->debug) {
        printf("DEBUG: Calling function '%s' with %d args\n", function_name,
               argc - 1);
    }

    // Create new scope for function
    if (symtable_push_scope(executor->symtable, SCOPE_FUNCTION,
                            function_name) != 0) {
        set_executor_error(executor, "Failed to create function scope");
        return 1;
    }

    // Set parameters (both positional and named)
    if (func->params) {
        // Set named parameters with defaults
        int arg_index = 1; // Skip function name at argv[0]
        function_param_t *param = func->params;

        while (param) {
            const char *value;
            if (arg_index < argc) {
                // Use provided argument
                value = argv[arg_index++];
            } else {
                // Use default value (already validated that required params are
                // present)
                value = param->default_value ? param->default_value : "";
            }

            // Set named parameter
            if (symtable_set_local_var(executor->symtable, param->name,
                                       value) != 0) {
                symtable_pop_scope(executor->symtable);
                set_executor_error(executor,
                                   "Failed to set function parameter");
                return 1;
            }
            param = param->next;
        }
    }

    // Set positional parameters ($1, $2, etc.) for backward compatibility
    for (int i = 1; i < argc; i++) {
        char param_name[16];
        snprintf(param_name, sizeof(param_name), "%d", i);
        if (symtable_set_local_var(executor->symtable, param_name, argv[i]) !=
            0) {
            symtable_pop_scope(executor->symtable);
            set_executor_error(executor, "Failed to set function parameter");
            return 1;
        }
    }

    // Set $# (argument count)
    char argc_str[16];
    snprintf(argc_str, sizeof(argc_str), "%d", argc - 1);
    symtable_set_local_var(executor->symtable, "#", argc_str);

    // No need to clear environment variables with new approach

    // Push function context for error reporting (Phase 3)
    source_location_t func_loc =
        func->body ? func->body->loc : SOURCE_LOC_UNKNOWN;
    executor_push_context(executor, func_loc, "in function '%s'",
                          function_name);

    // Apply trailing redirections attached to the function definition (issue
    // #48). The redirection nodes were appended as siblings of the body by
    // parse_function_definition + parse_trailing_redirections (issue #43);
    // copy_ast_chain pulled them along into func->body. Use a synthetic
    // parent so setup_redirections can walk them via first_child like every
    // other compound command. Save/restore fds so the redirection only
    // applies for the duration of this call.
    node_t synthetic_parent = {0};
    synthetic_parent.first_child = func->body;
    bool has_redirections = count_redirections(&synthetic_parent) > 0;
    redirection_state_t redir_state;
    if (has_redirections) {
        save_file_descriptors(&redir_state);
        int redir_result = setup_redirections(executor, &synthetic_parent);
        if (redir_result != 0) {
            restore_file_descriptors(&redir_state);
            executor_pop_context(executor);
            symtable_pop_scope(executor->symtable);
            return redir_result;
        }
    }

    // Execute function body (handle multiple commands; skip redirection
    // siblings, which were already applied above)
    int result = 0;
    node_t *command = func->body;
    while (command) {
        if (is_redirection_node(command)) {
            command = command->next_sibling;
            continue;
        }

        /* Bash-style DEBUG pseudo-signal: fires BEFORE each command in
         * the function body. fire_debug_trap gates on functrace +
         * function scope, so by default it stays silent inside
         * functions and surfaces only when the user opts in. */
        fire_debug_trap();

        result = execute_node(executor, command);

        /* Bash-style ERR pseudo-signal: fires on a non-zero exit
         * inside the function body. fire_err_trap itself gates on
         * errtrace + function scope so the trap is suppressed inside
         * functions by default and surfaces only when the user has
         * `set -o errtrace`. */
        if (result != 0 && result < 200) {
            fire_err_trap();
        }

        // Check if this is a function return (special code 200-255)
        if (result >= 200 && result <= 255) {
            // Extract the actual return value from the special code
            int actual_return = result - 200;

            /* Bash-style RETURN pseudo-signal: fires when a function
             * returns via the `return` builtin. fire_return_trap gates
             * on functrace; fires BEFORE we pop the scope so the trap
             * runs in the function's frame. */
            fire_return_trap();

            if (has_redirections) {
                restore_file_descriptors(&redir_state);
            }

            // Pop function context before returning
            executor_pop_context(executor);

            // Restore previous scope before returning
            symtable_pop_scope(executor->symtable);

            return actual_return;
        }

        if (result != 0) {
            break; // Stop on first error
        }
        command = command->next_sibling;
    }

    /* Bash-style RETURN pseudo-signal: fires when a function returns
     * by falling off the end of its body (no explicit `return`).
     * Fires BEFORE we pop the scope so the trap runs in the
     * function's frame. */
    fire_return_trap();

    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    // Pop function context
    executor_pop_context(executor);

    // Restore previous scope
    symtable_pop_scope(executor->symtable);

    return result;
}

/**
 * @brief Create a new function parameter
 *
 * Allocates and initializes a function parameter structure.
 * Parameters without default values are marked as required.
 *
 * @param name Parameter name
 * @param default_value Default value (NULL for required parameters)
 * @return New parameter structure, or NULL on failure
 */
function_param_t *create_function_param(const char *name,
                                        const char *default_value) {
    if (!name) {
        return NULL;
    }

    function_param_t *param = malloc(sizeof(function_param_t));
    if (!param) {
        return NULL;
    }

    param->name = strdup(name);
    if (!param->name) {
        free(param);
        return NULL;
    }

    param->default_value = default_value ? strdup(default_value) : NULL;
    param->is_required = (default_value == NULL);
    param->next = NULL;

    return param;
}

/**
 * @brief Free a function parameter list
 *
 * Frees all parameters in the linked list including their
 * name and default_value strings.
 *
 * @param params Head of parameter list to free
 */
void free_function_params(function_param_t *params) {
    while (params) {
        function_param_t *next = params->next;
        free(params->name);
        free(params->default_value);
        free(params);
        params = next;
    }
}

/**
 * @brief Validate function call arguments against parameters
 *
 * Checks that required parameters have values and that the
 * argument count doesn't exceed the parameter count.
 * Disabled in POSIX mode for backward compatibility.
 *
 * @param func Function definition with parameter info
 * @param argv Argument vector (reserved for future validation)
 * @param argc Argument count (reserved for arity checking)
 * @return 0 on success, 1 on validation failure
 */
static int validate_function_parameters(executor_t *executor,
                                        function_def_t *func, char **argv,
                                        int argc, source_location_t loc) {
    (void)argv; // Reserved for argument type validation
    (void)argc; // Reserved for arity checking
    if (!func) {
        return 1;
    }

    // POSIX compliance: disable parameter validation in strict POSIX mode
    if (is_posix_mode_enabled()) {
        return 0;
    }

    // If no parameters defined, allow any arguments (backward compatibility)
    if (!func->params) {
        return 0;
    }

    int arg_index = 1; // Skip function name at argv[0]
    function_param_t *param = func->params;

    while (param) {
        if (arg_index < argc) {
            // Argument provided for this parameter
            arg_index++;
        } else if (param->is_required) {
            // Required parameter missing
            executor_error_report(executor, SHELL_ERR_FUNCTION_ERROR, loc,
                                  "function '%s' requires parameter '%s'",
                                  func->name, param->name);
            return 1;
        }
        // Optional parameter without argument - will use default
        param = param->next;
    }

    // Check for too many arguments
    if (arg_index < argc) {
        executor_error_report(
            executor, SHELL_ERR_FUNCTION_ERROR, loc,
            "function '%s' called with %d arguments but only accepts %d",
            func->name, argc - 1, func->param_count);
        return 1;
    }

    return 0;
}

/**
 * @brief Find function in function table
 *
 * Searches the executor's function linked list for a function
 * with the specified name.
 *
 * @param executor Executor context
 * @param function_name Name to search for
 * @return Function definition, or NULL if not found
 */
static function_def_t *find_function(executor_t *executor,
                                     const char *function_name) {
    if (!executor || !function_name) {
        return NULL;
    }

    function_def_t *func = executor->functions;
    while (func) {
        if (strcmp(func->name, function_name) == 0) {
            return func;
        }
        func = func->next;
    }
    return NULL;
}

/**
 * @brief Store function in function table
 *
 * Stores or replaces a function definition. Creates a deep copy
 * of the function body AST. If a function with the same name exists,
 * it is replaced.
 *
 * @param executor Executor context
 * @param function_name Function name
 * @param body AST of function body (will be copied)
 * @param params Parameter list (ownership transferred)
 * @param param_count Number of parameters
 * @return 0 on success, 1 on failure
 */
static int store_function(executor_t *executor, const char *function_name,
                          node_t *body, function_param_t *params,
                          int param_count) {
    if (!executor || !function_name) {
        return 1;
    }

    // Check if function already exists and remove it
    function_def_t **current = &executor->functions;
    while (*current) {
        if (strcmp((*current)->name, function_name) == 0) {
            function_def_t *to_remove = *current;
            *current = (*current)->next;
            free(to_remove->name);
            free_node_tree(to_remove->body);
            free_function_params(to_remove->params);
            free(to_remove);
            break;
        }
        current = &(*current)->next;
    }

    // Create new function definition
    function_def_t *new_func = malloc(sizeof(function_def_t));
    if (!new_func) {
        return 1;
    }

    new_func->name = strdup(function_name);
    if (!new_func->name) {
        free(new_func);
        return 1;
    }

    // Create a deep copy of the body AST (including sibling chain)
    // Allow NULL bodies for empty functions
    new_func->body = copy_ast_chain(body);
    if (!new_func->body && body != NULL) {
        // Only fail if body was non-NULL but copy failed
        free(new_func->name);
        free(new_func);
        return 1;
    }

    // Store parameter information
    new_func->params = params;
    new_func->param_count = param_count;

    // Add to front of function list
    new_func->next = executor->functions;
    executor->functions = new_func;

    return 0;
}

/**
 * @brief Copy an AST node recursively
 *
 * Creates a deep copy of an AST node including all children.
 * Does not copy siblings - use copy_ast_chain for that.
 *
 * @param node Node to copy
 * @return Deep copy of node, or NULL on failure
 */
node_t *copy_ast_node(node_t *node) {
    if (!node) {
        return NULL;
    }

    node_t *copy = new_node(node->type);
    if (!copy) {
        return NULL;
    }

    // Copy value
    copy->val_type = node->val_type;
    if (node->val.str) {
        copy->val.str = strdup(node->val.str);
        if (!copy->val.str) {
            free_node_tree(copy);
            return NULL;
        }
    } else {
        copy->val = node->val;
    }

    // Copy children
    node_t *child = node->first_child;
    while (child) {
        node_t *child_copy = copy_ast_node(child);
        if (!child_copy) {
            free_node_tree(copy);
            return NULL;
        }
        add_child_node(copy, child_copy);
        child = child->next_sibling;
    }

    return copy;
}

/**
 * @brief Copy an AST node chain including siblings
 *
 * Creates a deep copy of a node and all its siblings.
 * Used for copying function bodies with multiple statements.
 *
 * @param node First node in chain to copy
 * @return Deep copy of entire chain, or NULL on failure
 */
static node_t *copy_ast_chain(node_t *node) {
    if (!node) {
        return NULL;
    }

    node_t *first_copy = copy_ast_node(node);
    if (!first_copy) {
        return NULL;
    }

    node_t *current_copy = first_copy;
    node_t *current_orig = node->next_sibling;

    while (current_orig) {
        node_t *sibling_copy = copy_ast_node(current_orig);
        if (!sibling_copy) {
            free_node_tree(first_copy);
            return NULL;
        }

        current_copy->next_sibling = sibling_copy;
        sibling_copy->prev_sibling = current_copy;

        current_copy = sibling_copy;
        current_orig = current_orig->next_sibling;
    }

    return first_copy;
}

/**
 * @brief Check if a string is empty or NULL
 *
 * @param str String to check
 * @return true if str is NULL or empty string
 */
static bool is_empty_or_null(const char *str) { return !str || str[0] == '\0'; }

/**
 * @brief Extract a substring with offset and length (TR#29 grapheme-aware)
 *
 * Implements ${var:offset:length} substring expansion. Offsets and
 * lengths are measured in Unicode grapheme clusters (TR#29), not bytes,
 * so multi-byte UTF-8 sequences are never split mid-character. Negative
 * offsets count from the end of the string in graphemes; length of -1
 * means "to end".
 *
 * Uses the project's TR#29 primitives (lle_utf8_count_graphemes +
 * slice_string_graphemes) so user content with combining marks, emoji
 * sequences, regional indicators, ZWJ joins, and other multi-codepoint
 * graphemes is handled correctly.
 *
 * @param str Source string (UTF-8)
 * @param offset Starting grapheme position (negative for from-end)
 * @param length Number of graphemes to extract (-1 for rest of string)
 * @return Newly malloc'd substring (caller must free)
 */
static char *extract_substring(const char *str, int offset, int length) {
    if (!str) {
        return strdup("");
    }

    size_t byte_len = strlen(str);
    int total = (int)lle_utf8_count_graphemes(str, byte_len);

    if (offset < 0) {
        offset = total + offset;
        if (offset < 0) {
            offset = 0;
        }
    }
    if (offset >= total) {
        return strdup("");
    }
    int remaining = total - offset;
    if (length < 0 || length > remaining) {
        length = remaining;
    }
    char *result = slice_string_graphemes(str, byte_len, offset, length);
    return result ? result : strdup("");
}

/**
 * @brief Match string against glob pattern
 *
 * Supports *, ?, and [...] character classes including ranges
 * and negation [!...] or [^...]. Used for case patterns and
 * parameter expansion pattern matching.
 *
 * @param str String to match
 * @param pattern Glob pattern
 * @return true if string matches pattern
 */
static bool match_pattern(const char *str, const char *pattern) {
    if (!str || !pattern) {
        return false;
    }

    const char *s = str;
    const char *p = pattern;

    while (*p) {
        if (*p == '*') {
            // Handle wildcard
            p++; // Skip the *

            // If * is at the end, it matches everything remaining
            if (*p == '\0') {
                return true;
            }

            // Try to match the rest of the pattern at each position in the
            // string
            while (*s) {
                if (match_pattern(s, p)) {
                    return true;
                }
                s++;
            }

            // Try matching the pattern with empty string (for cases like
            // "*suffix")
            return match_pattern(s, p);
        } else if (*p == '?') {
            // Wildcard matches any single character
            if (*s == '\0') {
                return false; // ? can't match empty
            }
            s++;
            p++;
        } else if (*p == '[') {
            // Character class pattern [abc] or [a-z]
            if (*s == '\0') {
                return false; // Character class can't match empty
            }

            p++; // Skip opening [
            bool matched = false;
            bool negated = false;

            // Check for negation [!abc] or [^abc]
            if (*p == '!' || *p == '^') {
                negated = true;
                p++;
            }

            while (*p && *p != ']') {
                /* POSIX character class [:CLASS:] (e.g. [:space:],
                 * [:alpha:], [:digit:]). Used heavily by real-world
                 * trim/parse idioms - `${s%%[![:space:]]*}` was
                 * silently treating [:space:] as the literal
                 * character set { '[', ':', 's', 'p', 'a', 'c', 'e' }
                 * because the class form was never parsed
                 * (real_world/bash/201 trim function). Scan for `:]`
                 * to delimit the class name, then test via ctype. */
                if (p[0] == '[' && p[1] == ':') {
                    const char *class_start = p + 2;
                    const char *class_end = strstr(class_start, ":]");
                    if (class_end) {
                        size_t cl = (size_t)(class_end - class_start);
                        unsigned char uc = (unsigned char)*s;
                        bool cls_match = false;
                        if (cl == 5 && memcmp(class_start, "space", 5) == 0) {
                            cls_match = isspace(uc) != 0;
                        } else if (cl == 5 &&
                                   memcmp(class_start, "alpha", 5) == 0) {
                            cls_match = isalpha(uc) != 0;
                        } else if (cl == 5 &&
                                   memcmp(class_start, "digit", 5) == 0) {
                            cls_match = isdigit(uc) != 0;
                        } else if (cl == 5 &&
                                   memcmp(class_start, "alnum", 5) == 0) {
                            cls_match = isalnum(uc) != 0;
                        } else if (cl == 5 &&
                                   memcmp(class_start, "upper", 5) == 0) {
                            cls_match = isupper(uc) != 0;
                        } else if (cl == 5 &&
                                   memcmp(class_start, "lower", 5) == 0) {
                            cls_match = islower(uc) != 0;
                        } else if (cl == 5 &&
                                   memcmp(class_start, "punct", 5) == 0) {
                            cls_match = ispunct(uc) != 0;
                        } else if (cl == 5 &&
                                   memcmp(class_start, "print", 5) == 0) {
                            cls_match = isprint(uc) != 0;
                        } else if (cl == 5 &&
                                   memcmp(class_start, "graph", 5) == 0) {
                            cls_match = isgraph(uc) != 0;
                        } else if (cl == 5 &&
                                   memcmp(class_start, "blank", 5) == 0) {
                            cls_match = (uc == ' ' || uc == '\t');
                        } else if (cl == 5 &&
                                   memcmp(class_start, "cntrl", 5) == 0) {
                            cls_match = iscntrl(uc) != 0;
                        } else if (cl == 6 &&
                                   memcmp(class_start, "xdigit", 6) == 0) {
                            cls_match = isxdigit(uc) != 0;
                        }
                        if (cls_match) {
                            matched = true;
                        }
                        p = class_end + 2; // past `:]`
                        continue;
                    }
                }
                if (p[1] == '-' && p[2] != ']' && p[2] != '\0') {
                    // Range pattern like a-z
                    if (*s >= *p && *s <= p[2]) {
                        matched = true;
                    }
                    p += 3; // Skip a-z
                } else {
                    // Single character
                    if (*s == *p) {
                        matched = true;
                    }
                    p++;
                }
            }

            if (*p == ']') {
                p++; // Skip closing ]
            }

            // Apply negation if needed
            if (negated) {
                matched = !matched;
            }

            if (!matched) {
                return false;
            }

            s++;
        } else {
            // Literal character match (including special chars like : @ / etc.)
            if (*s != *p) {
                return false;
            }
            s++;
            p++;
        }
    }

    // Pattern is exhausted, string should be too for a complete match
    return *s == '\0';
}

/**
 * @brief Find prefix match length for # and ## operators
 *
 * Finds how many characters from the beginning of str match pattern.
 * Used for ${var#pattern} and ${var##pattern} expansion.
 *
 * @param str String to search
 * @param pattern Pattern to match
 * @param longest If true, find longest match (##), else shortest (#)
 * @return Number of characters matched from beginning
 */
static int find_prefix_match(const char *str, const char *pattern,
                             bool longest) {
    if (!str || !pattern) {
        return 0;
    }

    int str_len = strlen(str);
    int match_len = 0;

    for (int i = 0; i <= str_len; i++) {
        char *substr = malloc(i + 1);
        if (!substr) {
            break;
        }

        strncpy(substr, str, i);
        substr[i] = '\0';

        if (match_pattern(substr, pattern)) {
            match_len = i;
            if (!longest) {
                free(substr);
                break; // Return first (shortest) match
            }
        }
        free(substr);
    }

    return match_len;
}

/**
 * @brief Find suffix match length for % and %% operators
 *
 * Finds how many characters from the end of str match pattern.
 * Used for ${var%pattern} and ${var%%pattern} expansion.
 *
 * @param str String to search
 * @param pattern Pattern to match
 * @param longest If true, find longest match (%%), else shortest (%)
 * @return Number of characters matched from end
 */
static int find_suffix_match(const char *str, const char *pattern,
                             bool longest) {
    if (!str || !pattern) {
        return 0;
    }

    int str_len = strlen(str);
    int match_len = 0;

    for (int i = 0; i <= str_len; i++) {
        const char *suffix = str + str_len - i;
        if (match_pattern(suffix, pattern)) {
            match_len = i;
            if (!longest) {
                break; // Return first (shortest) match
            }
        }
    }

    return match_len;
}

/**
 * @brief Convert first character to uppercase
 *
 * Used for ${var^} parameter expansion.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
static char *convert_case_first_upper(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    // Allocate buffer for Unicode conversion (may need more space)
    size_t buf_size = len * 4 + 1; // UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_toupper_first(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        // Fallback to simple copy on error
        free(result);
        return strdup(str);
    }

    return result;
}

/**
 * @brief Convert first character to lowercase
 *
 * Used for ${var,} parameter expansion.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
static char *convert_case_first_lower(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    // Allocate buffer for Unicode conversion (may need more space)
    size_t buf_size = len * 4 + 1; // UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_tolower_first(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        // Fallback to simple copy on error
        free(result);
        return strdup(str);
    }

    return result;
}

/**
 * @brief Convert all characters to uppercase
 *
 * Used for ${var^^} parameter expansion.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
/**
 * @brief Pattern-restricted case modification
 *
 * Bash `${var^^[pat]}` / `${var,,[pat]}` / `${var^[pat]}` / `${var,[pat]}`
 * apply case conversion only to characters that match `pattern`.
 * `pattern` is fnmatch-style and matched single-character against each
 * byte. If `first_only` is true, only the first matching character is
 * converted (the `^` / `,` operators); otherwise all matches convert
 * (`^^` / `,,`).
 *
 * Pattern restriction operates byte-by-byte; bash itself documents the
 * feature as glob-pattern based and does not support Unicode-aware
 * pattern matching here. Issue #96.
 *
 * @param str Input string
 * @param pattern Glob pattern (may be NULL/empty -- treated as "any char")
 * @param to_upper true for uppercase conversion, false for lowercase
 * @param first_only true for `^` / `,`, false for `^^` / `,,`
 * @return Newly malloc'd converted string
 */
static char *convert_case_pattern(const char *str, const char *pattern,
                                  bool to_upper, bool first_only) {
    if (!str) {
        return strdup("");
    }
    size_t len = strlen(str);
    char *result = malloc(len + 1);
    if (!result) {
        return strdup("");
    }
    bool any_pattern = (pattern && pattern[0]);
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        bool should_convert;
        if (first_only && i > 0) {
            /* Per bash spec: `^pat` / `,pat` only inspect the first
             * character of the expanded value; subsequent characters
             * are copied unchanged regardless of whether they would
             * match the pattern. */
            should_convert = false;
        } else if (!any_pattern) {
            should_convert = true;
        } else {
            char buf[2] = {c, '\0'};
            should_convert = (fnmatch(pattern, buf, 0) == 0);
        }
        if (should_convert) {
            result[i] = to_upper ? (char)toupper((unsigned char)c)
                                 : (char)tolower((unsigned char)c);
        } else {
            result[i] = c;
        }
    }
    result[len] = '\0';
    return result;
}

static char *convert_case_all_upper(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    // Allocate buffer for Unicode conversion (may need more space)
    size_t buf_size = len * 4 + 1; // UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_toupper(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        // Fallback to simple copy on error
        free(result);
        return strdup(str);
    }

    return result;
}

/**
 * @brief Convert all characters to lowercase
 *
 * Used for ${var,,} parameter expansion.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
static char *convert_case_all_lower(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    // Allocate buffer for Unicode conversion (may need more space)
    size_t buf_size = len * 4 + 1; // UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_tolower(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        // Fallback to simple copy on error
        free(result);
        return strdup(str);
    }

    return result;
}

/**
 * @brief Capitalize each word (zsh-style ${(C)var})
 *
 * Converts the first character of each word to uppercase and the rest
 * to lowercase. Words are delimited by whitespace.
 *
 * @param str String to convert
 * @return Converted string (caller must free)
 */
static char *convert_case_capitalize_words(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    // Allocate buffer - capitalize shouldn't change length significantly
    size_t buf_size = len * 4 + 1; // UTF-8 worst case
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    const char *src = str;
    char *dst = result;
    bool word_start = true;

    while (*src) {
        // Get UTF-8 codepoint length
        size_t cp_len = 1;
        unsigned char c = (unsigned char)*src;
        if (c >= 0xC0 && c < 0xE0)
            cp_len = 2;
        else if (c >= 0xE0 && c < 0xF0)
            cp_len = 3;
        else if (c >= 0xF0)
            cp_len = 4;

        // Ensure we don't read past end
        size_t remaining = strlen(src);
        if (cp_len > remaining)
            cp_len = remaining;

        if (isspace((unsigned char)*src)) {
            *dst++ = *src++;
            word_start = true;
        } else if (word_start) {
            // Uppercase the first character of word
            char temp[8] = {0};
            memcpy(temp, src, cp_len);
            char upper[16] = {0};
            size_t upper_len =
                lle_utf8_toupper(temp, cp_len, upper, sizeof(upper));
            if (upper_len != (size_t)-1 && upper_len < sizeof(upper)) {
                memcpy(dst, upper, upper_len);
                dst += upper_len;
            } else {
                memcpy(dst, src, cp_len);
                dst += cp_len;
            }
            src += cp_len;
            word_start = false;
        } else {
            // Lowercase the rest
            char temp[8] = {0};
            memcpy(temp, src, cp_len);
            char lower[16] = {0};
            size_t lower_len =
                lle_utf8_tolower(temp, cp_len, lower, sizeof(lower));
            if (lower_len != (size_t)-1 && lower_len < sizeof(lower)) {
                memcpy(dst, lower, lower_len);
                dst += lower_len;
            } else {
                memcpy(dst, src, cp_len);
                dst += cp_len;
            }
            src += cp_len;
        }
    }
    *dst = '\0';

    return result;
}

/**
 * @brief Pattern substitution for ${var/pattern/replacement}
 *
 * Replaces pattern matches in str with replacement.
 * Supports glob patterns (* and ?).
 *
 * @param str Source string
 * @param pattern Pattern to match (supports * and ?)
 * @param replacement Replacement string
 * @param global If true, replace all occurrences; if false, only first
 * @return New string with substitutions (caller must free)
 */
static char *pattern_substitute(const char *str, const char *pattern,
                                const char *replacement, bool global) {
    if (!str) {
        return strdup("");
    }
    if (!pattern || !pattern[0]) {
        return strdup(str);
    }
    if (!replacement) {
        replacement = "";
    }

    /* Bash anchored-substitution prefixes:
     *   ${var/#pat/repl}  match pat at the START of str only
     *   ${var/%pat/repl}  match pat at the END of str only
     * Detect and strip the marker; the remainder is the real pattern.
     * Anchored substitution implies a single replacement -- there is
     * only one start and one end -- so global is ignored when anchored.
     * Issue #96.
     */
    bool anchor_start = false;
    bool anchor_end = false;
    if (pattern[0] == '#') {
        anchor_start = true;
        pattern++;
    } else if (pattern[0] == '%') {
        anchor_end = true;
        pattern++;
    }
    if (!pattern[0]) {
        return strdup(str);
    }

    size_t str_len = strlen(str);
    size_t pattern_len = strlen(pattern);
    size_t replacement_len = strlen(replacement);

    /* Detect glob metacharacters that route through fnmatch. The
     * original check missed `[` (character class) and treated `[bd]`
     * patterns as exact-substring matches, which never matched
     * because the literal string never contained `[bd]`. fnmatch
     * supports character classes natively. */
    bool is_glob =
        (strchr(pattern, '*') || strchr(pattern, '?') || strchr(pattern, '['));

    /* Anchored-start: match pattern once at position 0, then copy the
     * remainder. Anchored-end: match pattern once at the suffix, copy
     * the prefix then the replacement. Both are simpler one-shot
     * cases than the general scanner below. */
    if (anchor_start) {
        size_t match_len = 0;
        bool matched = false;
        if (is_glob) {
            for (size_t try_len = 1; try_len <= str_len; try_len++) {
                char *substr = malloc(try_len + 1);
                if (!substr) {
                    break;
                }
                memcpy(substr, str, try_len);
                substr[try_len] = '\0';
                if (fnmatch(pattern, substr, 0) == 0) {
                    matched = true;
                    match_len = try_len;
                    // For glob with *, prefer longest match.
                    if (strchr(pattern, '*')) {
                        for (size_t longer = try_len + 1; longer <= str_len;
                             longer++) {
                            char *l = malloc(longer + 1);
                            if (!l) {
                                break;
                            }
                            memcpy(l, str, longer);
                            l[longer] = '\0';
                            if (fnmatch(pattern, l, 0) == 0) {
                                match_len = longer;
                            }
                            free(l);
                        }
                    }
                    free(substr);
                    break;
                }
                free(substr);
            }
        } else {
            if (str_len >= pattern_len &&
                strncmp(str, pattern, pattern_len) == 0) {
                matched = true;
                match_len = pattern_len;
            }
        }
        if (!matched) {
            return strdup(str);
        }
        size_t tail_len = str_len - match_len;
        char *result = malloc(replacement_len + tail_len + 1);
        if (!result) {
            return strdup(str);
        }
        memcpy(result, replacement, replacement_len);
        memcpy(result + replacement_len, str + match_len, tail_len);
        result[replacement_len + tail_len] = '\0';
        return result;
    }

    if (anchor_end) {
        size_t match_len = 0;
        bool matched = false;
        if (is_glob) {
            /* Try suffixes from longest to shortest. For * patterns we
             * want longest; for fixed-length patterns either order is
             * fine. Longest-first matches bash. */
            for (size_t try_len = str_len; try_len >= 1; try_len--) {
                size_t start = str_len - try_len;
                char *substr = malloc(try_len + 1);
                if (!substr) {
                    break;
                }
                memcpy(substr, str + start, try_len);
                substr[try_len] = '\0';
                if (fnmatch(pattern, substr, 0) == 0) {
                    matched = true;
                    match_len = try_len;
                    free(substr);
                    break;
                }
                free(substr);
            }
        } else {
            if (str_len >= pattern_len && strncmp(str + str_len - pattern_len,
                                                  pattern, pattern_len) == 0) {
                matched = true;
                match_len = pattern_len;
            }
        }
        if (!matched) {
            return strdup(str);
        }
        size_t head_len = str_len - match_len;
        char *result = malloc(head_len + replacement_len + 1);
        if (!result) {
            return strdup(str);
        }
        memcpy(result, str, head_len);
        memcpy(result + head_len, replacement, replacement_len);
        result[head_len + replacement_len] = '\0';
        return result;
    }

    // Allocate result buffer - estimate size
    size_t result_size = str_len * 2 + 1;
    char *result = malloc(result_size);
    if (!result) {
        return strdup(str);
    }
    result[0] = '\0';
    size_t result_pos = 0;

    size_t i = 0;
    bool replaced = false;

    while (i < str_len) {
        // Try to match pattern at current position
        bool matched = false;
        size_t match_len = 0;

        // Simple pattern matching - check for exact match or glob
        if (is_glob) {
            // Use fnmatch for glob patterns
            // Try increasing lengths to find the match
            for (size_t try_len = 1; try_len <= str_len - i; try_len++) {
                char *substr = malloc(try_len + 1);
                if (substr) {
                    strncpy(substr, str + i, try_len);
                    substr[try_len] = '\0';
                    if (fnmatch(pattern, substr, 0) == 0) {
                        matched = true;
                        match_len = try_len;
                        // For greedy matching with *, keep trying longer
                        if (strchr(pattern, '*')) {
                            for (size_t longer = try_len + 1;
                                 longer <= str_len - i; longer++) {
                                char *longer_substr = malloc(longer + 1);
                                if (longer_substr) {
                                    strncpy(longer_substr, str + i, longer);
                                    longer_substr[longer] = '\0';
                                    if (fnmatch(pattern, longer_substr, 0) ==
                                        0) {
                                        match_len = longer;
                                    }
                                    free(longer_substr);
                                }
                            }
                        }
                        free(substr);
                        break;
                    }
                    free(substr);
                }
            }
        } else {
            // Exact substring match
            if (strncmp(str + i, pattern, pattern_len) == 0) {
                matched = true;
                match_len = pattern_len;
            }
        }

        if (matched && (!replaced || global)) {
            // Ensure we have enough space
            if (result_pos + replacement_len + 1 >= result_size) {
                result_size = result_size * 2 + replacement_len;
                char *new_result = realloc(result, result_size);
                if (!new_result) {
                    free(result);
                    return strdup(str);
                }
                result = new_result;
            }

            // Copy replacement
            strcpy(result + result_pos, replacement);
            result_pos += replacement_len;
            i += match_len;
            replaced = true;
        } else {
            // No match, copy current character
            if (result_pos + 1 >= result_size) {
                result_size *= 2;
                char *new_result = realloc(result, result_size);
                if (!new_result) {
                    free(result);
                    return strdup(str);
                }
                result = new_result;
            }
            result[result_pos++] = str[i++];
        }
    }

    result[result_pos] = '\0';
    return result;
}

/**
 * @brief Quote a string for safe reuse as shell input
 *
 * Used for ${var@Q} transformation.
 *
 * @param str String to quote
 * @return Quoted string (caller must free)
 */
char *transform_quote(const char *str) {
    if (!str) {
        return strdup("''");
    }

    size_t len = strlen(str);

    /* ${var@Q} produces a quoted representation safe to re-eval.
     * bash uses two output forms:
     *   - For printable strings without control chars: 'content'
     *     with embedded single quotes escaped as '\'' (close quote,
     *     literal '\'', reopen quote).
     *   - For strings containing control chars (\n, \t, etc): $'...'
     *     ANSI-C quoting with \n / \t / \xNN escapes.
     * Match bash's choice so diff_oracle can byte-compare against
     * the corpus. Issue #102.
     */
    bool has_control = false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 32) {
            has_control = true;
            break;
        }
    }

    if (has_control) {
        // $'...' ANSI-C form for strings with control chars.
        size_t result_size = len * 4 + 4;
        char *result = malloc(result_size);
        if (!result) {
            return strdup("''");
        }

        size_t pos = 0;
        result[pos++] = '$';
        result[pos++] = '\'';

        for (size_t i = 0; i < len; i++) {
            unsigned char c = str[i];
            if (c == '\'') {
                result[pos++] = '\\';
                result[pos++] = '\'';
            } else if (c == '\\') {
                result[pos++] = '\\';
                result[pos++] = '\\';
            } else if (c == '\n') {
                result[pos++] = '\\';
                result[pos++] = 'n';
            } else if (c == '\t') {
                result[pos++] = '\\';
                result[pos++] = 't';
            } else if (c == '\r') {
                result[pos++] = '\\';
                result[pos++] = 'r';
            } else if (c < 32) {
                pos += snprintf(result + pos, result_size - pos, "\\x%02x", c);
            } else {
                result[pos++] = c;
            }
        }

        result[pos++] = '\'';
        result[pos] = '\0';
        return result;
    } else {
        /* Single-quoted form with bash's close-escape-reopen idiom
         * for embedded single quotes. Each `'` in str becomes
         * `'\''`: close the open quote (`'`), emit a literal-quoted
         * single quote (`\'`), then reopen (`'`). For a string
         * with no embedded quotes this collapses to the simple
         * `'content'` form. Each `'` worst-case expands to 4 chars,
         * so worst-case output size is len*4 + 3. */
        size_t result_size = len * 4 + 3;
        char *result = malloc(result_size);
        if (!result) {
            return strdup("''");
        }
        size_t pos = 0;
        result[pos++] = '\'';
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)str[i];
            if (c == '\'') {
                // Emit `'\''`: close, escape, reopen.
                result[pos++] = '\'';
                result[pos++] = '\\';
                result[pos++] = '\'';
                result[pos++] = '\'';
            } else {
                result[pos++] = c;
            }
        }
        result[pos++] = '\'';
        result[pos] = '\0';
        return result;
    }
}

/**
 * @brief Expand escape sequences in a string
 *
 * Used for ${var@E} transformation.
 *
 * @param str String with escape sequences
 * @return Expanded string (caller must free)
 */
static char *transform_escape(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    char *result = malloc(len + 1);
    if (!result) {
        return strdup("");
    }

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            switch (str[i + 1]) {
            case 'n':
                result[j++] = '\n';
                i++;
                break;
            case 't':
                result[j++] = '\t';
                i++;
                break;
            case 'r':
                result[j++] = '\r';
                i++;
                break;
            case '\\':
                result[j++] = '\\';
                i++;
                break;
            case '\'':
                result[j++] = '\'';
                i++;
                break;
            case '"':
                result[j++] = '"';
                i++;
                break;
            case 'a':
                result[j++] = '\a';
                i++;
                break;
            case 'b':
                result[j++] = '\b';
                i++;
                break;
            case 'e':
                result[j++] = '\033';
                i++;
                break;
            case 'f':
                result[j++] = '\f';
                i++;
                break;
            case 'v':
                result[j++] = '\v';
                i++;
                break;
            case 'x': // Hex escape \xHH
                if (i + 3 < len && isxdigit(str[i + 2]) &&
                    isxdigit(str[i + 3])) {
                    char hex[3] = {str[i + 2], str[i + 3], '\0'};
                    result[j++] = (char)strtol(hex, NULL, 16);
                    i += 3;
                } else {
                    result[j++] = str[i];
                }
                break;
            default:
                result[j++] = str[i];
                break;
            }
        } else {
            result[j++] = str[i];
        }
    }

    result[j] = '\0';
    return result;
}

/**
 * @brief Create assignment statement form
 *
 * Used for ${var@A} transformation.
 *
 * @param name Variable name
 * @param value Variable value
 * @return Assignment string like "name='value'" (caller must free)
 */
static char *transform_assignment(const char *name, const char *value) {
    if (!name) {
        return strdup("");
    }
    if (!value) {
        value = "";
    }

    // Quote the value
    char *quoted = transform_quote(value);
    if (!quoted) {
        return strdup("");
    }

    size_t result_size = strlen(name) + strlen(quoted) + 2;
    char *result = malloc(result_size);
    if (!result) {
        free(quoted);
        return strdup("");
    }

    snprintf(result, result_size, "%s=%s", name, quoted);
    free(quoted);
    return result;
}

/**
 * @brief Expand prompt escape sequences
 *
 * Used for ${var@P} transformation. Expands Bash-style prompt escapes:
 *   \u - username
 *   \h - hostname (short)
 *   \H - hostname (full)
 *   \w - current working directory
 *   \W - basename of current working directory
 *   \$ - $ for regular users, # for root
 *   \n - newline
 *   \t - tab
 *   \\ - literal backslash
 *
 * @param str String containing prompt escapes
 * @return Expanded string (caller must free)
 */
static char *transform_prompt(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    size_t result_size = len * 4 + 256; // Allow for expansion
    char *result = malloc(result_size);
    if (!result) {
        return strdup("");
    }

    size_t j = 0;
    for (size_t i = 0; i < len && j < result_size - 1; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            char next = str[i + 1];
            switch (next) {
            case 'u': {
                // Username
                struct passwd *pw = getpwuid(getuid());
                const char *user = pw ? pw->pw_name : "user";
                size_t ulen = strlen(user);
                if (j + ulen < result_size - 1) {
                    strcpy(result + j, user);
                    j += ulen;
                }
                i++;
                break;
            }
            case 'h': {
                // Hostname (short - up to first dot)
                char hostname[256];
                if (gethostname(hostname, sizeof(hostname)) == 0) {
                    char *dot = strchr(hostname, '.');
                    if (dot)
                        *dot = '\0';
                    size_t hlen = strlen(hostname);
                    if (j + hlen < result_size - 1) {
                        strcpy(result + j, hostname);
                        j += hlen;
                    }
                }
                i++;
                break;
            }
            case 'H': {
                // Hostname (full)
                char hostname[256];
                if (gethostname(hostname, sizeof(hostname)) == 0) {
                    size_t hlen = strlen(hostname);
                    if (j + hlen < result_size - 1) {
                        strcpy(result + j, hostname);
                        j += hlen;
                    }
                }
                i++;
                break;
            }
            case 'w': {
                // Current working directory
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd))) {
                    // Replace home dir with ~
                    struct passwd *pw = getpwuid(getuid());
                    const char *home = pw ? pw->pw_dir : getenv("HOME");
                    if (home && strncmp(cwd, home, strlen(home)) == 0) {
                        result[j++] = '~';
                        size_t rest_len = strlen(cwd + strlen(home));
                        if (j + rest_len < result_size - 1) {
                            strcpy(result + j, cwd + strlen(home));
                            j += rest_len;
                        }
                    } else {
                        size_t clen = strlen(cwd);
                        if (j + clen < result_size - 1) {
                            strcpy(result + j, cwd);
                            j += clen;
                        }
                    }
                }
                i++;
                break;
            }
            case 'W': {
                // Basename of current working directory
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd))) {
                    const char *base = strrchr(cwd, '/');
                    base = base ? base + 1 : cwd;
                    if (*base == '\0')
                        base = "/";
                    size_t blen = strlen(base);
                    if (j + blen < result_size - 1) {
                        strcpy(result + j, base);
                        j += blen;
                    }
                }
                i++;
                break;
            }
            case '$':
                // $ or # based on UID
                result[j++] = (getuid() == 0) ? '#' : '$';
                i++;
                break;
            case 'n':
                result[j++] = '\n';
                i++;
                break;
            case 't':
                result[j++] = '\t';
                i++;
                break;
            case '\\':
                result[j++] = '\\';
                i++;
                break;
            default:
                // Unknown escape, keep as-is
                result[j++] = str[i];
                break;
            }
        } else {
            result[j++] = str[i];
        }
    }

    result[j] = '\0';
    return result;
}

/**
 * @brief Get variable attribute flags
 *
 * Used for ${var@a} transformation. Returns attribute flags:
 *   r - readonly
 *   x - exported
 *   a - indexed array
 *   A - associative array
 *   n - nameref
 *
 * @param name Variable name
 * @return Attribute string (caller must free)
 */
static char *get_variable_attributes(const char *name) {
    if (!name) {
        return strdup("");
    }

    char attrs[16] = {0};
    size_t idx = 0;

    symtable_manager_t *mgr = symtable_get_global_manager();
    if (!mgr) {
        return strdup("");
    }

    // Get variable flags
    symvar_flags_t flags = symtable_get_flags(mgr, name);

    /* Bash ${var@a} attribute-string ordering: bash emits `irx` form
     * (integer, readonly, exported) and 'a' / 'A' for indexed /
     * associative arrays. Order matters only for stylistic match
     * with bash output; bash's actual order is by attribute introduction
     * date. Issue #102. */
    if (flags & SYMVAR_INTEGER_ATTR) {
        attrs[idx++] = 'i';
    }
    if (flags & SYMVAR_READONLY) {
        attrs[idx++] = 'r';
    }
    if (flags & SYMVAR_EXPORTED) {
        attrs[idx++] = 'x';
    }

    // Check if it's an array
    if (symtable_is_array(name)) {
        array_value_t *arr = symtable_get_array(name);
        if (arr && arr->is_associative) {
            attrs[idx++] = 'A';
        } else {
            attrs[idx++] = 'a';
        }
    }

    // Check for nameref
    if (symtable_is_nameref(mgr, name)) {
        attrs[idx++] = 'n';
    }

    return strdup(attrs);
}

/**
 * @brief Recursively expand variables within a string
 *
 * Expands all variable references, arithmetic expressions, and
 * command substitutions within a string. Used for expanding
 * default values in parameter expansion.
 *
 * @param executor Executor context
 * @param str String containing variables to expand
 * @return Fully expanded string (caller must free)
 */
static char *expand_variables_in_string(executor_t *executor, const char *str) {
    if (!str || !executor) {
        return strdup("");
    }

    size_t len = strlen(str);
    char *result = malloc(len * 2 + 1); // Start with double size
    if (!result) {
        return strdup("");
    }

    size_t result_pos = 0;
    size_t result_size = len * 2 + 1;

    for (size_t i = 0; i < len; i++) {
        if (str[i] == '$') {
            // Check for arithmetic expansion $((...)
            if (i + 2 < len && str[i + 1] == '(' && str[i + 2] == '(') {
                /* $(( is ambiguous: arithmetic expansion or command
                 * substitution of an anonymous function `$(() {...})`.
                 * Same disambiguation rule as the tokenizer (issue #99):
                 * if the lookahead from after $(( finds `{`, `}`, `;`,
                 * or `\n` before matched `))`, the input is command
                 * substitution and must be routed through the next-
                 * branch's $(...) handler instead. Walk the lookahead;
                 * if it doesn't pass the arithmetic shape check, fall
                 * through to the $(...) handler below. */
                bool looks_arith = true;
                {
                    size_t s = i + 3;
                    int d = 2;
                    while (s < len && d > 0) {
                        char sc = str[s];
                        if (sc == '(') {
                            d++;
                        } else if (sc == ')') {
                            d--;
                            if (d == 0) {
                                break;
                            }
                        } else if (sc == '{' || sc == '}' || sc == ';' ||
                                   sc == '\n') {
                            looks_arith = false;
                            break;
                        }
                        s++;
                    }
                }
                if (!looks_arith) {
                    /* Re-route into the $(...) command-sub handler at
                     * the next branch (else-if on str[i + 1] == '('),
                     * which fires when str[i+2] != '(' OR when we
                     * intentionally skip the arithmetic path. To
                     * trigger it cleanly, just fall through to the
                     * next condition test by NOT entering the arith
                     * block. The post-block `i = arith_end - 1`
                     * advancement is skipped because we don't `continue`
                     * here. */
                    goto try_cmd_sub_path;
                }
                // This is arithmetic expansion $((expr))
                size_t arith_start = i;
                size_t arith_end = i + 3;
                int paren_depth = 2;

                while (arith_end < len && paren_depth > 0) {
                    if (str[arith_end] == '(') {
                        paren_depth++;
                    } else if (str[arith_end] == ')') {
                        paren_depth--;
                    }
                    arith_end++;
                }

                if (paren_depth == 0) {
                    // Extract arithmetic expression including $(( and ))
                    size_t full_arith_len = arith_end - arith_start;
                    char *full_arith_expr = malloc(full_arith_len + 1);
                    if (full_arith_expr) {
                        strncpy(full_arith_expr, &str[arith_start],
                                full_arith_len);
                        full_arith_expr[full_arith_len] = '\0';

                        // Expand arithmetic expression
                        char *arith_result =
                            expand_arithmetic(executor, full_arith_expr);
                        if (arith_result) {
                            size_t result_len = strlen(arith_result);

                            // Ensure buffer is large enough
                            while (result_pos + result_len >= result_size) {
                                result_size *= 2;
                                char *new_result = realloc(result, result_size);
                                if (!new_result) {
                                    free(result);
                                    free(arith_result);
                                    free(full_arith_expr);
                                    return strdup("");
                                }
                                result = new_result;
                            }

                            // Copy arithmetic result
                            strcpy(&result[result_pos], arith_result);
                            result_pos += result_len;
                            free(arith_result);
                        }
                        free(full_arith_expr);
                    }

                    i = arith_end - 1; // Skip past the entire $((...)
                    continue;
                }
            }
            // Check for command substitution $(...)
            else if (i + 1 < len && str[i + 1] == '(') {
            try_cmd_sub_path:;
                // Find matching closing parenthesis using find_closing_brace
                char *temp_str =
                    (char *)&str[i + 1]; // Start from the opening parenthesis
                size_t brace_offset = find_closing_brace(temp_str);

                if (brace_offset > 0) {
                    // Extract command from $(...)
                    size_t cmd_len =
                        brace_offset - 1; // Exclude the closing paren
                    char *command = malloc(cmd_len + 1);
                    if (command) {
                        strncpy(command, &str[i + 2], cmd_len); // Skip $(
                        command[cmd_len] = '\0';

                        // Execute command substitution - need to wrap in $()
                        // format
                        char *wrapped_cmd =
                            malloc(cmd_len + 4); // +3 for $() +1 for null
                        if (wrapped_cmd) {
                            snprintf(wrapped_cmd, cmd_len + 4, "$(%s)",
                                     command);
                            char *cmd_result = expand_command_substitution(
                                executor, wrapped_cmd);
                            free(wrapped_cmd);
                            if (cmd_result) {
                                size_t value_len = strlen(cmd_result);

                                // Ensure buffer is large enough
                                while (result_pos + value_len >= result_size) {
                                    result_size *= 2;
                                    char *new_result =
                                        realloc(result, result_size);
                                    if (!new_result) {
                                        free(result);
                                        free(cmd_result);
                                        free(command);
                                        return strdup("");
                                    }
                                    result = new_result;
                                }

                                strcpy(&result[result_pos], cmd_result);
                                result_pos += value_len;
                                free(cmd_result);
                            }
                        }

                        free(command);
                        i = i + 1 + brace_offset; // Skip past the entire $(...)
                        continue;
                    }
                }
            }

            // Find variable name
            size_t var_start = i + 1;
            size_t var_end = var_start;

            // Handle ${var} format
            if (var_start < len && str[var_start] == '{') {
                // Use proper brace matching for nested expressions
                char *brace_str = (char *)&str[var_start];
                size_t brace_len = find_closing_brace(brace_str);

                if (brace_len > 0) {
                    // brace_len is the index of the closing brace
                    var_end = var_start + brace_len +
                              1; // Point to after closing brace
                } else {
                    // Fallback: find closing brace manually with nesting
                    // support
                    int brace_count = 1;
                    var_end = var_start + 1; // Start after opening {

                    while (var_end < len && brace_count > 0) {
                        if (str[var_end] == '{') {
                            brace_count++;
                        } else if (str[var_end] == '}') {
                            brace_count--;
                        }
                        var_end++;
                    }
                }
            } else {
                // Handle $var format
                // Check for special single-character variables first
                if (var_end < len &&
                    (str[var_end] == '?' || str[var_end] == '$' ||
                     str[var_end] == '#' || str[var_end] == '*' ||
                     str[var_end] == '@' || str[var_end] == '!' ||
                     str[var_end] == '-' ||
                     (str[var_end] >= '0' && str[var_end] <= '9'))) {
                    var_end++; // Single character special variable
                } else {
                    // Regular variable names (alphanumeric + underscore)
                    while (var_end < len &&
                           (isalnum(str[var_end]) || str[var_end] == '_')) {
                        var_end++;
                    }
                    /* Zsh bare-subscript form: $var[N] / $var[N,M].
                     * Consume the bracket span so var_expr becomes
                     * "$var[N]" rather than "$var" + literal "[N]".
                     * Gated on FEATURE_ZSH_BARE_SUBSCRIPT — bash mode
                     * keeps the literal-[N]-after-$var semantic. */
                    if (var_end > var_start && var_end < len &&
                        str[var_end] == '[' &&
                        shell_mode_allows(FEATURE_ZSH_BARE_SUBSCRIPT)) {
                        size_t scan = var_end + 1;
                        while (scan < len && str[scan] != ']') {
                            scan++;
                        }
                        if (scan < len && str[scan] == ']') {
                            var_end = scan + 1;
                        }
                    }
                }
            }

            if (var_end > var_start) {
                // Extract and expand variable
                size_t var_len = var_end - i;
                char *var_expr = malloc(var_len + 1);
                if (var_expr) {
                    strncpy(var_expr, &str[i], var_len);
                    var_expr[var_len] = '\0';

                    char *var_value = expand_variable(executor, var_expr);
                    if (var_value) {
                        size_t value_len = strlen(var_value);

                        // Ensure buffer is large enough
                        while (result_pos + value_len >= result_size) {
                            result_size *= 2;
                            char *new_result = realloc(result, result_size);
                            if (!new_result) {
                                free(result);
                                free(var_value);
                                free(var_expr);
                                return strdup("");
                            }
                            result = new_result;
                        }

                        strcpy(&result[result_pos], var_value);
                        result_pos += value_len;
                        free(var_value);
                    }

                    free(var_expr);
                    i = var_end - 1; // Skip past variable
                    continue;
                }
            }
        }

        // Regular character - ensure buffer space
        if (result_pos + 1 >= result_size) {
            result_size *= 2;
            char *new_result = realloc(result, result_size);
            if (!new_result) {
                free(result);
                return strdup("");
            }
            result = new_result;
        }

        result[result_pos++] = str[i];
    }

    result[result_pos] = '\0';
    return result;
}

/**
 * @brief Parse and execute parameter expansion
 *
 * Handles all POSIX and bash-style parameter expansions:
 * - ${#var} - length
 * - ${var:-default} - use default if unset/empty
 * - ${var:+alternative} - use alternative if set
 * - ${var#pattern} - remove shortest prefix
 * - ${var##pattern} - remove longest prefix
 * - ${var%pattern} - remove shortest suffix
 * - ${var%%pattern} - remove longest suffix
 * - ${var^} ${var^^} - case conversion
 * - ${var,} ${var,,} - case conversion
 * - ${var:offset:length} - substring
 *
 * @param executor Executor context
 * @param expansion Content inside ${...} (without braces)
 * @return Expanded value (caller must free)
 */
/**
 * @brief Queue a POSIX-required shell-level exit
 *
 * IEEE 1003.1 defines several conditions under which a non-interactive
 * shell shall exit immediately: ${var:?word} on a null-or-unset
 * parameter, ${var?word} on an unset parameter, and (when `set -u` is
 * active) any reference to an unbound variable, among others. This
 * helper centralizes the trigger:
 *
 *  - Always sets expansion_error + expansion_exit_status so the
 *    current expansion bails and the immediate command surfaces a
 *    non-zero exit. This is the existing behavior for these sites.
 *  - In non-interactive shells, additionally raises
 *    shell_exit_requested with shell_exit_status. Every command-list
 *    walker, loop body, function-call dispatcher, and the top-level
 *    REPL honors that flag by short-circuiting up to the run loop,
 *    which terminates the shell with shell_exit_status. The flag
 *    persists across statements within the batch -- this is the
 *    point: subsequent statements in the script must NOT run.
 *  - Interactive shells never set shell_exit_requested. They emit
 *    the diagnostic and continue at the next prompt, per spec.
 *
 * is_interactive_shell() is the canonical query used elsewhere in the
 * executor for the same exit-on-error distinction (see executor.c
 * around line 10598 where the symmetric query gates an exit() call
 * for SHELL_ERR_UNBOUND_VARIABLE inside command-builtin paths).
 *
 * @param executor Executor context (must be non-NULL)
 * @param status Status the shell exits with
 */
static void executor_request_posix_exit(executor_t *executor, int status) {
    executor->expansion_error = true;
    executor->expansion_exit_status = status;
    if (!is_interactive_shell()) {
        executor->shell_exit_requested = true;
        executor->shell_exit_status = status;
    }
}

/**
 * @brief Emit a "parameter null or unset" error for ${var:?word} / ${var?word}
 *
 * Both POSIX required-parameter forms share identical reporting shape:
 * write "var: word" (or a default message if word is empty) via the
 * structured error system, mark the executor's expansion-error state so
 * the caller can react, and return an empty string. The split between
 * "null or unset" and "unset only" is the trigger condition (handled at
 * the call site); the reporting code is the same.
 *
 * Routes through executor_request_posix_exit() so the shell-level
 * abort fires in non-interactive shells per POSIX -- the prior
 * implementation only marked expansion_error, which made the current
 * command fail but allowed subsequent script statements to run.
 *
 * @param executor Executor context (for error_report and expansion flags)
 * @param var_name Name of the unset/null parameter
 * @param word User-supplied error word (may be NULL/empty)
 * @param default_msg Default message used when word is NULL/empty
 * @return strdup("") (caller takes ownership)
 */
static char *handle_required_param_error(executor_t *executor,
                                         const char *var_name, const char *word,
                                         const char *default_msg) {
    const char *msg = (word && *word) ? word : default_msg;
    executor_error_report(executor, SHELL_ERR_PARAMETER_NULL_OR_UNSET,
                          executor_current_loc(executor), "%s: %s", var_name,
                          msg);
    executor_request_posix_exit(executor, 1);
    return strdup("");
}

/**
 * @brief Slice a string by grapheme cluster positions (TR#29 correct)
 *
 * Used by ${var[N]} / ${var[N,M]} string subscripts on scalar (non-array)
 * variables. The bracket operators here are grapheme-indexed, not byte-
 * indexed.
 *
 * Iterates by *codepoint* using lle_utf8_decode_codepoint (the canonical
 * pattern used elsewhere in the shell — see src/tokenizer.c:2075). At
 * each codepoint boundary, lle_is_grapheme_boundary determines whether
 * the codepoint also starts a new *grapheme cluster*. A multi-codepoint
 * grapheme (emoji+ZWJ+emoji, base+combining-mark, etc.) increments the
 * grapheme counter only at its first codepoint, so all internal
 * codepoints are correctly grouped under one grapheme index.
 *
 * Indexing is 0-based at this layer; the caller is responsible for
 * converting from 1-based (zsh-style) where applicable.
 *
 * @param str Source string (UTF-8)
 * @param str_len Length in bytes
 * @param start_grapheme 0-based grapheme index to start at
 * @param count Number of graphemes to extract (-1 for "to end")
 * @return Newly malloc'd substring, or strdup("") on out-of-range / OOM
 */
static char *slice_string_graphemes(const char *str, size_t str_len,
                                    int start_grapheme, int count) {
    if (!str || str_len == 0 || start_grapheme < 0) {
        return strdup("");
    }

    int grapheme_idx = 0;
    size_t byte_start = SIZE_MAX;
    size_t byte_end = str_len;
    int target_end = (count < 0) ? -1 : start_grapheme + count;

    size_t i = 0;
    while (i < str_len) {
        /* Check grapheme boundary at this codepoint start (not at every
         * byte — continuation bytes would falsely register as boundaries
         * because lle_is_grapheme_boundary treats invalid UTF-8 as a
         * boundary, and continuation bytes alone are invalid as a
         * standalone codepoint). */
        if (lle_is_grapheme_boundary(str + i, str, str + str_len)) {
            if (grapheme_idx == start_grapheme) {
                byte_start = i;
            }
            if (target_end >= 0 && grapheme_idx == target_end) {
                byte_end = i;
                break;
            }
            grapheme_idx++;
        }

        // Advance by one codepoint.
        uint32_t cp;
        int cp_len = lle_utf8_decode_codepoint(str + i, str_len - i, &cp);
        if (cp_len <= 0) {
            i++; // Skip invalid byte to avoid infinite loop.
        } else {
            i += (size_t)cp_len;
        }
    }

    if (byte_start == SIZE_MAX) {
        return strdup("");
    }
    size_t slice_len = byte_end - byte_start;
    char *result = malloc(slice_len + 1);
    if (!result) {
        return strdup("");
    }
    memcpy(result, str + byte_start, slice_len);
    result[slice_len] = '\0';
    return result;
}

/* strstr-equivalent that skips over balanced `[...]` regions. The
 * operator search in parse_parameter_expansion would otherwise pick
 * `@` (op_type 17, transformations) out of an `[@]` subscript --
 * e.g. `${arr[@]^^}` would split var_name as "arr[" and op_type 17,
 * never reaching the `^^` per-element handler. Confining the search
 * to text outside subscripts makes operator picking correct on
 * `${arr[@]op}` and `${arr[*]op}` forms regardless of which scalar
 * operator follows the closing bracket. */
static const char *find_op_outside_brackets(const char *haystack,
                                            const char *needle) {
    if (!haystack || !needle || !*needle) {
        return NULL;
    }
    size_t needle_len = strlen(needle);
    const char *p = haystack;
    while (*p) {
        if (*p == '[') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '[') {
                    depth++;
                } else if (*p == ']') {
                    depth--;
                }
                p++;
            }
            continue;
        }
        if (strncmp(p, needle, needle_len) == 0) {
            return p;
        }
        p++;
    }
    return NULL;
}

static char *parse_parameter_expansion(executor_t *executor,
                                       const char *expansion) {
    if (!expansion) {
        return strdup("");
    }

    // zsh `${+NAME}` is-set test: returns "1" if NAME is bound, "0"
    // otherwise. Used heavily in arithmetic contexts -- `(( ${+v} ))`
    // is the idiomatic zsh test for variable presence. Real-world
    // corpus example: `(( ${+commands[dircolors]} ))` checks whether
    // `dircolors` is on $path via zsh's special `$commands` hash.
    //
    // Lush handles the plain `${+NAME}` form by walking the symbol
    // table, and the `${+commands[NAME]}` shape as a PATH lookup
    // (since lush has no `$commands` hash but the user-observable
    // semantic is "is this command available?").
    if (expansion[0] == '+' && expansion[1] != '\0') {
        const char *name = expansion + 1;
        // Special-case zsh's $commands[X] -- treat as a PATH lookup.
        if (strncmp(name, "commands[", 9) == 0) {
            const char *cmd_start = name + 9;
            const char *cmd_end = strchr(cmd_start, ']');
            if (cmd_end) {
                char *cmd = strndup(cmd_start, (size_t)(cmd_end - cmd_start));
                bool found = false;
                if (cmd && *cmd) {
                    char *path_resolved = find_command_in_path(cmd);
                    if (path_resolved) {
                        found = true;
                        free(path_resolved);
                    }
                }
                free(cmd);
                return strdup(found ? "1" : "0");
            }
        }
        // Plain ${+NAME}: ignore any trailing [subscript] for now and
        // probe the symbol table for the base name.
        const char *bracket = strchr(name, '[');
        char *probe_name =
            bracket ? strndup(name, (size_t)(bracket - name)) : strdup(name);
        bool bound = false;
        if (probe_name && *probe_name) {
            lush_value_view_t view;
            bound = symtable_lookup(probe_name, &view);
            if (bound) {
                lush_value_view_clear(&view);
            }
        }
        free(probe_name);
        return strdup(bound ? "1" : "0");
    }

    // Handle zsh-style parameter flags: ${(X)var}
    // Flags: U=uppercase, L=lowercase, C=capitalize, f=split on newlines,
    //        o=sort ascending, O=sort descending, k=keys, v=values
    //        j:X:=join with X, s:X:=split on X
    if (expansion[0] == '(') {
        const char *close_paren = strchr(expansion, ')');
        if (close_paren && close_paren > expansion + 1) {
            // Extract flags between ( and )
            size_t flags_len = close_paren - expansion - 1;
            char *flags = malloc(flags_len + 1);
            if (!flags) {
                return strdup("");
            }
            strncpy(flags, expansion + 1, flags_len);
            flags[flags_len] = '\0';

            // Rest of expansion after )
            const char *rest = close_paren + 1;

            // Check for 'k' / 'v' flags (return keys / values for arrays).
            // The combination 'kv' (or 'vk') asks for interleaved key/value
            // pairs and must be detected before the keys-only branch — see
            // the (want_keys && want_values) handler below.
            bool want_keys = (strchr(flags, 'k') != NULL);
            bool want_values = (strchr(flags, 'v') != NULL);

            char *inner_result = NULL;

            if (want_keys && want_values) {
                // Handle (kv)/(vk): emit interleaved "k1 v1 k2 v2 ..."
                // Both symtable_array_get_keys() and
                // symtable_array_get_values() iterate the same source data
                // (hashtable for assoc, indices array for indexed) in the
                // same order, so keys[i] pairs with values[i].
                char *arr_name = NULL;
                const char *bracket = strchr(rest, '[');
                if (bracket) {
                    arr_name = strndup(rest, bracket - rest);
                } else {
                    arr_name = strdup(rest);
                }

                if (arr_name) {
                    array_value_t *array = symtable_get_array(arr_name);
                    if (!array) {
                        // (kv) on a non-collection is a type mismatch.
                        // Distinguish unset (silent empty) from scalar
                        // (error) so unset-by-omission stays a non-event
                        // while genuine misapplication on a scalar
                        // surfaces as a runtime error.
                        char *probe =
                            symtable_get_var(executor->symtable, arr_name);
                        if (probe) {
                            free(probe);
                            executor_error_report(
                                executor, SHELL_ERR_TYPE_MISMATCH,
                                executor_current_loc(executor),
                                "type mismatch: flag '(kv)' requires a "
                                "list or map operand, but '%s' is a "
                                "scalar",
                                arr_name);
                            executor_request_posix_exit(executor, 1);
                            free(arr_name);
                            free(flags);
                            return strdup("");
                        }
                    }
                    if (array && shell_mode_get() == SHELL_MODE_ZSH &&
                        !array->is_associative) {
                        /* zsh-mode special case: ${(kv)indexed_array}
                         * emits values only — zsh treats indexed arrays
                         * as having no meaningful "keys" so (kv) collapses
                         * to (v). lush mode keeps the interleaved
                         * indices+values form (curated pick: internally
                         * consistent with lush's (k)/(v) semantics). */
                        inner_result = symtable_array_expand(array, " ");
                    } else if (array) {
                        size_t kc = 0, vc = 0;
                        char **keys = symtable_array_get_keys(array, &kc);
                        char **values = symtable_array_get_values(array, &vc);
                        size_t pairs = (kc < vc) ? kc : vc;
                        if (keys && values && pairs > 0) {
                            size_t total_len = 0;
                            for (size_t i = 0; i < pairs; i++) {
                                total_len +=
                                    strlen(keys[i]) + 1 + strlen(values[i]) + 1;
                            }
                            inner_result = malloc(total_len + 1);
                            if (inner_result) {
                                inner_result[0] = '\0';
                                for (size_t i = 0; i < pairs; i++) {
                                    if (i > 0)
                                        strcat(inner_result, " ");
                                    strcat(inner_result, keys[i]);
                                    strcat(inner_result, " ");
                                    strcat(inner_result, values[i]);
                                }
                            }
                        }
                        if (keys) {
                            for (size_t i = 0; i < kc; i++) {
                                free(keys[i]);
                            }
                            free(keys);
                        }
                        if (values) {
                            for (size_t i = 0; i < vc; i++) {
                                free(values[i]);
                            }
                            free(values);
                        }
                    }
                    free(arr_name);
                }
                if (!inner_result) {
                    inner_result = strdup("");
                }
            } else if (want_keys) {
                // Handle (k) flag: return array keys instead of values
                // Parse array name from rest (e.g., "arr[@]" -> "arr")
                char *arr_name = NULL;
                const char *bracket = strchr(rest, '[');
                if (bracket) {
                    arr_name = strndup(rest, bracket - rest);
                } else {
                    arr_name = strdup(rest);
                }

                if (arr_name) {
                    array_value_t *array = symtable_get_array(arr_name);
                    if (!array) {
                        // (k) on a non-collection is a type mismatch.
                        // See the (kv) branch above for the unset vs
                        // scalar distinction.
                        char *probe =
                            symtable_get_var(executor->symtable, arr_name);
                        if (probe) {
                            free(probe);
                            executor_error_report(
                                executor, SHELL_ERR_TYPE_MISMATCH,
                                executor_current_loc(executor),
                                "type mismatch: flag '(k)' requires a "
                                "list or map operand, but '%s' is a "
                                "scalar",
                                arr_name);
                            executor_request_posix_exit(executor, 1);
                            free(arr_name);
                            free(flags);
                            return strdup("");
                        }
                    }
                    if (array && shell_mode_get() == SHELL_MODE_ZSH &&
                        !array->is_associative) {
                        /* zsh-mode special case: ${(k)indexed_array} emits
                         * values only (zsh treats indexed-array indices as
                         * not meaningfully "keys" — `(k)` collapses to
                         * `(v)`). lush mode keeps the existing 0-based
                         * indices behavior (curated pick: more useful for
                         * iteration / debugging than redundantly emitting
                         * values which `(v)` and `${arr[@]}` already give). */
                        inner_result = symtable_array_expand(array, " ");
                    } else if (array) {
                        // Get all keys from array (works for both indexed and
                        // associative)
                        size_t count;
                        char **keys = symtable_array_get_keys(array, &count);
                        if (keys && count > 0) {
                            // Calculate total length needed
                            size_t total_len = 0;
                            for (size_t i = 0; i < count; i++) {
                                total_len += strlen(keys[i]) + 1;
                            }
                            inner_result = malloc(total_len + 1);
                            if (inner_result) {
                                inner_result[0] = '\0';
                                for (size_t i = 0; i < count; i++) {
                                    if (i > 0)
                                        strcat(inner_result, " ");
                                    strcat(inner_result, keys[i]);
                                    free(keys[i]);
                                }
                            }
                            free(keys);
                        }
                    }
                    free(arr_name);
                }
                if (!inner_result) {
                    inner_result = strdup("");
                }
            } else if (strchr(flags, 'w') != NULL && rest[0] == '#') {
                // Handle (w)# - word count instead of character count
                // Get the variable value first
                const char *var_name = rest + 1; // Skip the #
                char *var_value = parse_parameter_expansion(executor, var_name);
                if (var_value) {
                    // Count words (space-separated)
                    size_t word_count = 0;
                    bool in_word = false;
                    for (const char *c = var_value; *c; c++) {
                        if (*c == ' ' || *c == '\t' || *c == '\n') {
                            in_word = false;
                        } else if (!in_word) {
                            word_count++;
                            in_word = true;
                        }
                    }
                    // Return word count as string
                    char count_buf[32];
                    snprintf(count_buf, sizeof(count_buf), "%zu", word_count);
                    inner_result = strdup(count_buf);
                    free(var_value);
                } else {
                    inner_result = strdup("0");
                }
            } else {
                /* Collection-operand resolution for the flag pipeline.
                 *
                 * Several flags ((U)(L)(C)(s)(j)(o)(O)(u)(f)) operate on
                 * the space-separated string form of a collection's
                 * elements. The default recursive call below (the older
                 * "Normal expansion" path) routes `arr[@]` through the
                 * general path that rejects list-in-scalar-slot at the
                 * subscript handler, so the flag never gets to do its
                 * work. Detect when `rest` is a collection reference
                 * (bare name, name[@], or name[*]) and extract elements
                 * directly via symtable_get_array, joining with " " --
                 * the format every flag handler in this loop already
                 * expects.
                 *
                 * Also catches collection-only flags ((j)(k)(v)) applied
                 * to a scalar and raises SHELL_ERR_TYPE_MISMATCH at
                 * that site rather than silently returning empty. */
                char *arr_name = NULL;
                bool rest_is_simple_ref = false;
                const char *bracket = strchr(rest, '[');
                if (bracket) {
                    // Allow name[@] and name[*] forms; reject name[N].
                    if ((bracket[1] == '@' || bracket[1] == '*') &&
                        bracket[2] == ']' && bracket[3] == '\0') {
                        arr_name = strndup(rest, (size_t)(bracket - rest));
                        rest_is_simple_ref = (arr_name != NULL);
                    }
                } else if (rest[0] && (isalpha((unsigned char)rest[0]) ||
                                       rest[0] == '_')) {
                    // Bare name; validate it as a plain identifier.
                    bool plain = true;
                    for (size_t i = 1; rest[i]; i++) {
                        if (!isalnum((unsigned char)rest[i]) &&
                            rest[i] != '_') {
                            plain = false;
                            break;
                        }
                    }
                    if (plain) {
                        arr_name = strdup(rest);
                        rest_is_simple_ref = (arr_name != NULL);
                    }
                }

                bool has_collection_only_flag = strchr(flags, 'j') != NULL ||
                                                strchr(flags, 'k') != NULL ||
                                                strchr(flags, 'v') != NULL;

                array_value_t *array =
                    arr_name ? symtable_get_array(arr_name) : NULL;

                if (array) {
                    // Collection operand: extract elements as space-
                    // separated string. The flag handlers below iterate
                    // the words and apply per-element semantics for
                    // (U)(L)(C)(s)(o)(O)(u)(f), and (j:X:) joins them
                    // with the user-specified delimiter for the
                    // explicit list-to-scalar form.
                    inner_result = symtable_array_expand(array, " ");
                    if (!inner_result) {
                        inner_result = strdup("");
                    }
                    free(arr_name);
                } else if (rest_is_simple_ref && has_collection_only_flag) {
                    // The name resolves (or doesn't) to something that
                    // is not a collection, but the user wrote a flag
                    // that only makes sense for one. Surface the
                    // mismatch as a structured runtime error rather
                    // than silently returning empty.
                    executor_error_report(
                        executor, SHELL_ERR_TYPE_MISMATCH,
                        executor_current_loc(executor),
                        "type mismatch: flag '(%s)' requires a list or "
                        "map operand, but '%s' is not one",
                        flags, arr_name);
                    executor_request_posix_exit(executor, 1);
                    free(arr_name);
                    free(flags);
                    return strdup("");
                } else {
                    /* Existing path. `rest` is what comes after the
                     * flag-paren group: a scalar variable, or a nested
                     * full parameter expansion like ${(s/,/)${INNER}}
                     * (the zsh idiom ${(flag)${INNER}} applies the
                     * outer flag to the inner expansion's result). */
                    free(arr_name);
                    if (rest[0] == '$' && rest[1] == '{') {
                        inner_result = expand_variable(executor, rest);
                    } else {
                        inner_result =
                            parse_parameter_expansion(executor, rest);
                    }
                }
            }

            if (!inner_result) {
                free(flags);
                return strdup("");
            }

            // Process flags in order
            char *result = inner_result;
            const char *p = flags;

            while (*p) {
                char *new_result = NULL;

                switch (*p) {
                case 'U':
                    // Uppercase all
                    new_result = convert_case_all_upper(result);
                    if (result != inner_result)
                        free(result);
                    result = new_result ? new_result : strdup("");
                    p++;
                    break;

                case 'L':
                    // Lowercase all
                    new_result = convert_case_all_lower(result);
                    if (result != inner_result)
                        free(result);
                    result = new_result ? new_result : strdup("");
                    p++;
                    break;

                case 'C':
                    // Capitalize each word
                    new_result = convert_case_capitalize_words(result);
                    if (result != inner_result)
                        free(result);
                    result = new_result ? new_result : strdup("");
                    p++;
                    break;

                case 'f':
                    // Split on newlines - for now, replace newlines with spaces
                    // (full array support would require different return type)
                    {
                        size_t len = strlen(result);
                        new_result = malloc(len + 1);
                        if (new_result) {
                            for (size_t i = 0; i <= len; i++) {
                                new_result[i] =
                                    (result[i] == '\n') ? ' ' : result[i];
                            }
                        }
                        if (result != inner_result)
                            free(result);
                        result = new_result ? new_result : strdup("");
                    }
                    p++;
                    break;

                case 'j': {
                    // Join with separator: j<DELIM>X<DELIM>
                    // zsh accepts any non-')' character after 'j' as the
                    // delimiter; the same character closes the argument.
                    char delim = p[1];
                    if (delim && delim != ')') {
                        // Find closing delim
                        const char *sep_start = p + 2;
                        const char *sep_end = strchr(sep_start, delim);
                        if (sep_end) {
                            size_t sep_len = sep_end - sep_start;
                            char *sep = malloc(sep_len + 1);
                            if (sep) {
                                strncpy(sep, sep_start, sep_len);
                                sep[sep_len] = '\0';

                                // Replace spaces with separator
                                size_t res_len = strlen(result);
                                size_t count = 0;
                                for (size_t i = 0; i < res_len; i++) {
                                    if (result[i] == ' ')
                                        count++;
                                }
                                new_result =
                                    malloc(res_len + count * sep_len + 1);
                                if (new_result) {
                                    char *dst = new_result;
                                    for (size_t i = 0; i < res_len; i++) {
                                        if (result[i] == ' ') {
                                            memcpy(dst, sep, sep_len);
                                            dst += sep_len;
                                        } else {
                                            *dst++ = result[i];
                                        }
                                    }
                                    *dst = '\0';
                                }
                                free(sep);
                            }
                            if (result != inner_result)
                                free(result);
                            result = new_result ? new_result : strdup("");
                            p = sep_end + 1;
                        } else {
                            p++; // Malformed, skip
                        }
                    } else {
                        p++; // No separator specified
                    }
                    break;
                }

                case 's': {
                    // Split on separator: s<DELIM>X<DELIM> - replace X with
                    // space. zsh accepts any non-')' character after 's' as
                    // the delimiter; the same character closes the argument.
                    char delim = p[1];
                    if (delim && delim != ')') {
                        const char *sep_start = p + 2;
                        const char *sep_end = strchr(sep_start, delim);
                        if (sep_end) {
                            size_t sep_len = sep_end - sep_start;
                            char *sep = malloc(sep_len + 1);
                            if (sep && sep_len > 0) {
                                strncpy(sep, sep_start, sep_len);
                                sep[sep_len] = '\0';

                                // Replace separator with space
                                size_t res_len = strlen(result);
                                new_result = malloc(res_len + 1);
                                if (new_result) {
                                    char *src = result;
                                    char *dst = new_result;
                                    while (*src) {
                                        if (strncmp(src, sep, sep_len) == 0) {
                                            *dst++ = ' ';
                                            src += sep_len;
                                        } else {
                                            *dst++ = *src++;
                                        }
                                    }
                                    *dst = '\0';
                                }
                                free(sep);
                            } else {
                                if (sep)
                                    free(sep);
                            }
                            if (new_result) {
                                if (result != inner_result)
                                    free(result);
                                result = new_result;
                            }
                            p = sep_end + 1;
                        } else {
                            p++;
                        }
                    } else {
                        p++;
                    }
                    break;
                }

                case 'o':
                    // Sort ascending - split on spaces, sort, rejoin
                    {
                        // Count words
                        size_t word_count = 0;
                        bool in_word = false;
                        for (const char *c = result; *c; c++) {
                            if (*c == ' ') {
                                in_word = false;
                            } else if (!in_word) {
                                word_count++;
                                in_word = true;
                            }
                        }

                        if (word_count > 1) {
                            char **words = malloc(word_count * sizeof(char *));
                            if (words) {
                                // Split into words
                                char *copy = strdup(result);
                                if (copy) {
                                    size_t idx = 0;
                                    char *tok = strtok(copy, " ");
                                    while (tok && idx < word_count) {
                                        words[idx++] = strdup(tok);
                                        tok = strtok(NULL, " ");
                                    }
                                    word_count = idx;

                                    // Sort ascending
                                    for (size_t i = 0; i < word_count - 1;
                                         i++) {
                                        for (size_t j = i + 1; j < word_count;
                                             j++) {
                                            if (strcmp(words[i], words[j]) >
                                                0) {
                                                char *tmp = words[i];
                                                words[i] = words[j];
                                                words[j] = tmp;
                                            }
                                        }
                                    }

                                    // Rejoin
                                    size_t total_len = 0;
                                    for (size_t i = 0; i < word_count; i++) {
                                        total_len += strlen(words[i]) + 1;
                                    }
                                    new_result = malloc(total_len + 1);
                                    if (new_result) {
                                        new_result[0] = '\0';
                                        for (size_t i = 0; i < word_count;
                                             i++) {
                                            if (i > 0)
                                                strcat(new_result, " ");
                                            strcat(new_result, words[i]);
                                        }
                                    }

                                    for (size_t i = 0; i < word_count; i++) {
                                        free(words[i]);
                                    }
                                    free(copy);
                                }
                                free(words);
                            }
                            if (new_result) {
                                if (result != inner_result)
                                    free(result);
                                result = new_result;
                            }
                        }
                    }
                    p++;
                    break;

                case 'O':
                    // Sort descending - same as 'o' but reverse comparison
                    {
                        size_t word_count = 0;
                        bool in_word = false;
                        for (const char *c = result; *c; c++) {
                            if (*c == ' ') {
                                in_word = false;
                            } else if (!in_word) {
                                word_count++;
                                in_word = true;
                            }
                        }

                        if (word_count > 1) {
                            char **words = malloc(word_count * sizeof(char *));
                            if (words) {
                                char *copy = strdup(result);
                                if (copy) {
                                    size_t idx = 0;
                                    char *tok = strtok(copy, " ");
                                    while (tok && idx < word_count) {
                                        words[idx++] = strdup(tok);
                                        tok = strtok(NULL, " ");
                                    }
                                    word_count = idx;

                                    // Sort descending
                                    for (size_t i = 0; i < word_count - 1;
                                         i++) {
                                        for (size_t j = i + 1; j < word_count;
                                             j++) {
                                            if (strcmp(words[i], words[j]) <
                                                0) {
                                                char *tmp = words[i];
                                                words[i] = words[j];
                                                words[j] = tmp;
                                            }
                                        }
                                    }

                                    size_t total_len = 0;
                                    for (size_t i = 0; i < word_count; i++) {
                                        total_len += strlen(words[i]) + 1;
                                    }
                                    new_result = malloc(total_len + 1);
                                    if (new_result) {
                                        new_result[0] = '\0';
                                        for (size_t i = 0; i < word_count;
                                             i++) {
                                            if (i > 0)
                                                strcat(new_result, " ");
                                            strcat(new_result, words[i]);
                                        }
                                    }

                                    for (size_t i = 0; i < word_count; i++) {
                                        free(words[i]);
                                    }
                                    free(copy);
                                }
                                free(words);
                            }
                            if (new_result) {
                                if (result != inner_result)
                                    free(result);
                                result = new_result;
                            }
                        }
                    }
                    p++;
                    break;

                case 'k':
                    // Keys flag - already handled before inner expansion
                    p++;
                    break;

                case 'v':
                    // Values flag - no-op (values are the default)
                    p++;
                    break;

                case 'u': {
                    /* Unique: dedupe consecutive (and non-consecutive)
                     * elements after splitting on spaces. zsh's (u)
                     * removes ALL duplicates, not just adjacent ones.
                     * Combine with (o) or (O) for sort+unique. Issue
                     * #103. */
                    size_t word_count = 0;
                    bool in_word = false;
                    for (const char *c = result; *c; c++) {
                        if (*c == ' ') {
                            in_word = false;
                        } else if (!in_word) {
                            word_count++;
                            in_word = true;
                        }
                    }
                    if (word_count > 1) {
                        char **words = malloc(word_count * sizeof(char *));
                        if (words) {
                            char *copy = strdup(result);
                            if (copy) {
                                size_t idx = 0;
                                char *tok = strtok(copy, " ");
                                while (tok && idx < word_count) {
                                    /* Skip if already seen. O(N^2)
                                     * is fine for typical zsh array
                                     * sizes; switching to a hash set
                                     * would be premature. */
                                    bool seen = false;
                                    for (size_t k = 0; k < idx; k++) {
                                        if (strcmp(words[k], tok) == 0) {
                                            seen = true;
                                            break;
                                        }
                                    }
                                    if (!seen) {
                                        words[idx++] = strdup(tok);
                                    }
                                    tok = strtok(NULL, " ");
                                }
                                size_t unique_count = idx;
                                size_t total_len = 0;
                                for (size_t k = 0; k < unique_count; k++) {
                                    total_len += strlen(words[k]) + 1;
                                }
                                new_result = malloc(total_len + 1);
                                if (new_result) {
                                    new_result[0] = '\0';
                                    for (size_t k = 0; k < unique_count; k++) {
                                        if (k > 0) {
                                            strcat(new_result, " ");
                                        }
                                        strcat(new_result, words[k]);
                                    }
                                }
                                for (size_t k = 0; k < unique_count; k++) {
                                    free(words[k]);
                                }
                                free(copy);
                            }
                            free(words);
                        }
                        if (new_result) {
                            if (result != inner_result) {
                                free(result);
                            }
                            result = new_result;
                        }
                    }
                    p++;
                    break;
                }

                case 'l':
                case 'r': {
                    /* Padding flags. zsh syntax:
                     *   (l:N:)              -- left-pad to width N w/ spaces
                     *   (l:N::FILL:)        -- left-pad with FILL string
                     *   (r:N:) / (r:N::FILL:) -- right-pad analogously
                     * If the value is wider than N, zsh truncates the
                     * value to N chars (left-pad keeps the rightmost
                     * N; right-pad keeps the leftmost N). The N
                     * argument is bracketed by `:` chars (zsh accepts
                     * any non-`)` delimiter; we accept `:` to match
                     * the common form the corpus uses). Issue #103. */
                    bool pad_left = (*p == 'l');
                    char open = p[1];
                    if (!open || open == ')') {
                        p++;
                        break;
                    }
                    const char *spec = p + 2;
                    const char *width_end = strchr(spec, open);
                    if (!width_end) {
                        p++;
                        break;
                    }
                    size_t width_str_len = (size_t)(width_end - spec);
                    char width_buf[32];
                    if (width_str_len >= sizeof(width_buf)) {
                        p++;
                        break;
                    }
                    memcpy(width_buf, spec, width_str_len);
                    width_buf[width_str_len] = '\0';
                    int width = atoi(width_buf);

                    // Optional fill: another :FILL: after the width.
                    char *fill = NULL;
                    const char *after_width = width_end + 1;
                    const char *closing = after_width;
                    if (*after_width == ':') {
                        const char *fill_start = after_width + 1;
                        const char *fill_end = strchr(fill_start, ':');
                        if (fill_end) {
                            size_t fill_len = (size_t)(fill_end - fill_start);
                            fill = malloc(fill_len + 1);
                            if (fill) {
                                memcpy(fill, fill_start, fill_len);
                                fill[fill_len] = '\0';
                            }
                            closing = fill_end + 1;
                        } else {
                            closing = after_width + 1;
                        }
                    }

                    /* Advance p past the entire (l:N::FILL:) span,
                     * up to but not including the closing `)` of the
                     * flag group -- the outer while loop is iterating
                     * `flags` which is the content between `(` and `)`
                     * already, so `closing` is the position right
                     * after the trailing `:`. */
                    p = closing;

                    const char *fill_str = (fill && fill[0]) ? fill : " ";
                    size_t fill_len = strlen(fill_str);
                    size_t result_len = strlen(result);

                    if (width <= 0) {
                        free(fill);
                        break;
                    }
                    if ((int)result_len >= width) {
                        /* Truncate. For left-pad, keep last N chars;
                         * for right-pad, keep first N chars. */
                        new_result = malloc((size_t)width + 1);
                        if (new_result) {
                            if (pad_left) {
                                memcpy(new_result,
                                       result + (result_len - (size_t)width),
                                       (size_t)width);
                            } else {
                                memcpy(new_result, result, (size_t)width);
                            }
                            new_result[width] = '\0';
                        }
                    } else {
                        size_t pad_count = (size_t)width - result_len;
                        new_result = malloc((size_t)width + 1);
                        if (new_result) {
                            if (pad_left) {
                                for (size_t k = 0; k < pad_count; k++) {
                                    new_result[k] = fill_str[k % fill_len];
                                }
                                memcpy(new_result + pad_count, result,
                                       result_len);
                            } else {
                                memcpy(new_result, result, result_len);
                                for (size_t k = 0; k < pad_count; k++) {
                                    new_result[result_len + k] =
                                        fill_str[k % fill_len];
                                }
                            }
                            new_result[width] = '\0';
                        }
                    }
                    free(fill);
                    if (new_result) {
                        if (result != inner_result) {
                            free(result);
                        }
                        result = new_result;
                    }
                    break;
                }

                case 'Q': {
                    /* (Q) flag: strip one level of quoting from the
                     * value. zsh accepts `'a b c'` -> `a b c` and
                     * `"a b c"` -> `a b c`. If the value isn't wrapped
                     * in matching quotes, return unchanged.
                     * Issue #103. */
                    size_t result_len = strlen(result);
                    if (result_len >= 2 &&
                        ((result[0] == '\'' &&
                          result[result_len - 1] == '\'') ||
                         (result[0] == '"' && result[result_len - 1] == '"'))) {
                        new_result = malloc(result_len - 1);
                        if (new_result) {
                            memcpy(new_result, result + 1, result_len - 2);
                            new_result[result_len - 2] = '\0';
                        }
                    } else {
                        new_result = strdup(result);
                    }
                    p++;
                    if (new_result) {
                        if (result != inner_result) {
                            free(result);
                        }
                        result = new_result;
                    }
                    break;
                }

                case 'q': {
                    /* Quote-family flags (issue #103):
                     *   (q)   -- backslash-escape shell metacharacters
                     *   (qq)  -- single-quote the entire value
                     *   (qqq) -- double-quote the entire value
                     * Count consecutive 'q' chars to pick the variant.
                     */
                    int q_count = 0;
                    while (p[q_count] == 'q') {
                        q_count++;
                    }

                    size_t result_len = strlen(result);
                    if (q_count == 2) {
                        /* Single-quote: wrap with ' and escape any
                         * embedded ' using bash's '\\'' idiom (zsh
                         * accepts the same form). */
                        size_t cap = result_len * 4 + 3;
                        new_result = malloc(cap);
                        if (new_result) {
                            size_t pos = 0;
                            new_result[pos++] = '\'';
                            for (size_t k = 0; k < result_len; k++) {
                                if (result[k] == '\'') {
                                    new_result[pos++] = '\'';
                                    new_result[pos++] = '\\';
                                    new_result[pos++] = '\'';
                                    new_result[pos++] = '\'';
                                } else {
                                    new_result[pos++] = result[k];
                                }
                            }
                            new_result[pos++] = '\'';
                            new_result[pos] = '\0';
                        }
                    } else if (q_count == 3) {
                        /* Double-quote: wrap with " and escape `"` `$`
                         * `` ` `` `\` chars. */
                        size_t cap = result_len * 2 + 3;
                        new_result = malloc(cap);
                        if (new_result) {
                            size_t pos = 0;
                            new_result[pos++] = '"';
                            for (size_t k = 0; k < result_len; k++) {
                                char c = result[k];
                                if (c == '"' || c == '$' || c == '`' ||
                                    c == '\\') {
                                    new_result[pos++] = '\\';
                                }
                                new_result[pos++] = c;
                            }
                            new_result[pos++] = '"';
                            new_result[pos] = '\0';
                        }
                    } else {
                        /* (q): backslash-escape shell-meta chars. zsh's
                         * (q) escapes characters that would be
                         * special in any shell context -- space, tab,
                         * newline, and shell metacharacters
                         * (; & | < > ( ) { } [ ] $ ` " ' \ * ? ~ # !
                         * = % ^). */
                        size_t cap = result_len * 2 + 1;
                        new_result = malloc(cap);
                        if (new_result) {
                            size_t pos = 0;
                            for (size_t k = 0; k < result_len; k++) {
                                unsigned char c = (unsigned char)result[k];
                                bool meta =
                                    (c == ' ' || c == '\t' || c == '\n' ||
                                     c == ';' || c == '&' || c == '|' ||
                                     c == '<' || c == '>' || c == '(' ||
                                     c == ')' || c == '{' || c == '}' ||
                                     c == '[' || c == ']' || c == '$' ||
                                     c == '`' || c == '"' || c == '\'' ||
                                     c == '\\' || c == '*' || c == '?' ||
                                     c == '~' || c == '#' || c == '!' ||
                                     c == '=');
                                if (meta) {
                                    new_result[pos++] = '\\';
                                }
                                new_result[pos++] = (char)c;
                            }
                            new_result[pos] = '\0';
                        }
                    }
                    p += q_count;
                    if (new_result) {
                        if (result != inner_result) {
                            free(result);
                        }
                        result = new_result;
                    }
                    break;
                }

                default:
                    // Unknown flag, skip
                    p++;
                    break;
                }
            }

            free(flags);
            if (result == inner_result) {
                return result; // No transformation applied
            }
            free(inner_result);
            return result;
        }
    }

    // Handle indirect expansion: ${!name} or ${!prefix*} or ${!prefix@}
    if (expansion[0] == '!') {
        const char *var_name = expansion + 1;
        size_t name_len = strlen(var_name);

        // Check for ${!prefix*} or ${!prefix@} - list variable names
        if (name_len > 0 &&
            (var_name[name_len - 1] == '*' || var_name[name_len - 1] == '@')) {
            // Extract prefix (without * or @)
            char *prefix = malloc(name_len);
            if (!prefix) {
                return strdup("");
            }
            strncpy(prefix, var_name, name_len - 1);
            prefix[name_len - 1] = '\0';

            /* Enumerate the symbol table for matching names. The prior
             * implementation only scanned `environ` (exported vars
             * only); most shell-local variables never reach environ.
             * Collect into a dynamic array via the symtable enumerator,
             * sort alphabetically for determinism (bash documents the
             * order as unspecified but the corpus depends on a stable
             * order for byte-for-byte diff_oracle comparison).
             * Issue #102. The callback and qsort comparator are
             * file-scope helpers because C lacks nested functions. */
            prefix_collect_ctx_t ctx = {NULL, 0, 0, prefix, strlen(prefix)};
            symtable_enumerate_global_vars(prefix_collect_cb, &ctx);

            if (ctx.count > 1) {
                qsort(ctx.names, ctx.count, sizeof(char *), strptr_cmp);
            }

            // Build space-separated result.
            size_t total_len = 0;
            for (size_t i = 0; i < ctx.count; i++) {
                total_len += strlen(ctx.names[i]) + 1;
            }
            char *result = malloc(total_len + 1);
            if (result) {
                result[0] = '\0';
                size_t pos = 0;
                for (size_t i = 0; i < ctx.count; i++) {
                    if (i > 0) {
                        result[pos++] = ' ';
                    }
                    size_t l = strlen(ctx.names[i]);
                    memcpy(result + pos, ctx.names[i], l);
                    pos += l;
                    result[pos] = '\0';
                }
            }

            for (size_t i = 0; i < ctx.count; i++) {
                free(ctx.names[i]);
            }
            free(ctx.names);
            free(prefix);
            return result ? result : strdup("");
        }

        // Check for ${!arr[@]} or ${!arr[*]} - array keys
        const char *bracket = strchr(var_name, '[');
        if (bracket) {
            size_t arr_name_len = bracket - var_name;
            char *arr_name = malloc(arr_name_len + 1);
            if (!arr_name) {
                return strdup("");
            }
            strncpy(arr_name, var_name, arr_name_len);
            arr_name[arr_name_len] = '\0';

            array_value_t *array = symtable_get_array(arr_name);
            free(arr_name);

            if (array) {
                // Return array indices as space-separated string
                size_t count;
                char **keys = symtable_array_get_keys(array, &count);
                if (keys && count > 0) {
                    size_t result_size = count * 12; // Enough for integers
                    char *result = malloc(result_size);
                    if (result) {
                        result[0] = '\0';
                        for (size_t i = 0; i < count; i++) {
                            if (i > 0)
                                strcat(result, " ");
                            strcat(result, keys[i]);
                            free(keys[i]);
                        }
                        free(keys);
                        return result;
                    }
                    for (size_t i = 0; i < count; i++)
                        free(keys[i]);
                    free(keys);
                }
            }
            return strdup("");
        }

        // Simple indirect expansion: ${!name} - value of variable named by name
        char *indirect_name = symtable_get_var(executor->symtable, var_name);
        if (indirect_name && indirect_name[0]) {
            // Get the value of the variable whose name is in indirect_name
            char *result = symtable_get_var(executor->symtable, indirect_name);
            return strdup(result ? result : "");
        }
        return strdup("");
    }

    // Handle array length: ${#arr[@]} or ${#arr[*]}
    if (expansion[0] == '#') {
        const char *var_name = expansion + 1;

        /* Nested form ${#${INNER}}: count the length of the inner
         * expansion's result. expand_variable handles the full ${...}
         * form. Issue #98. */
        if (var_name[0] == '$' && var_name[1] == '{') {
            char *inner = expand_variable(executor, var_name);
            if (inner) {
                size_t inner_len = strlen(inner);
                free(inner);
                char buf[32];
                snprintf(buf, sizeof(buf), "%zu", inner_len);
                return strdup(buf);
            }
            return strdup("0");
        }

        // Check for array subscript
        const char *bracket = strchr(var_name, '[');
        if (bracket) {
            size_t name_len = bracket - var_name;
            char *arr_name = malloc(name_len + 1);
            if (!arr_name) {
                return strdup("0");
            }
            strncpy(arr_name, var_name, name_len);
            arr_name[name_len] = '\0';

            // Get subscript
            const char *close = strchr(bracket, ']');
            if (close) {
                size_t sub_len = close - bracket - 1;
                char *subscript = malloc(sub_len + 1);
                if (subscript) {
                    strncpy(subscript, bracket + 1, sub_len);
                    subscript[sub_len] = '\0';

                    // Check if array exists
                    array_value_t *array = symtable_get_array(arr_name);
                    if (array) {
                        char result_buf[32];

                        if (strcmp(subscript, "@") == 0 ||
                            strcmp(subscript, "*") == 0) {
                            // ${#arr[@]} - number of elements
                            snprintf(result_buf, sizeof(result_buf), "%zu",
                                     symtable_array_length(array));
                        } else {
                            // ${#arr[n]} - length of element at index n
                            arithm_clear_error();
                            char *idx_result = arithm_expand(subscript);
                            if (idx_result && !arithm_error_is_flagged()) {
                                int idx = (int)strtoll(idx_result, NULL, 10);
                                free(idx_result);

                                /* Same indexing convention as ${arr[n]}:
                                 * zsh-mode rejects 0, decrements positives,
                                 * passes negatives through to the symtable
                                 * helper's "from-end" handler. Lush/bash
                                 * pass through directly. (Issue #68.) */
                                if (!shell_mode_allows(
                                        FEATURE_ARRAY_ZERO_INDEXED)) {
                                    if (idx == 0) {
                                        snprintf(result_buf, sizeof(result_buf),
                                                 "0");
                                    } else {
                                        if (idx > 0) {
                                            idx--; // 1-based -> 0-based
                                        }
                                        const char *elem =
                                            symtable_array_get_index(array,
                                                                     idx);
                                        snprintf(result_buf, sizeof(result_buf),
                                                 "%zu",
                                                 elem ? strlen(elem) : 0);
                                    }
                                } else {
                                    const char *elem =
                                        symtable_array_get_index(array, idx);
                                    snprintf(result_buf, sizeof(result_buf),
                                             "%zu", elem ? strlen(elem) : 0);
                                }
                            } else {
                                if (idx_result)
                                    free(idx_result);
                                snprintf(result_buf, sizeof(result_buf), "0");
                            }
                        }

                        free(subscript);
                        free(arr_name);
                        return strdup(result_buf);
                    }

                    free(subscript);
                }
            }
            free(arr_name);
            return strdup("0");
        }

        /* Regular variable length: ${#var}. Mode-aware for the array
         * case on a bare array name (issue #99):
         *   zsh:  number of elements
         *   bash: length of arr[0] (treats $arr as ${arr[0]})
         *   lush: number of elements (curated zsh idiom)
         *   posix: arrays don't exist, but if one was carried over
         *          from a prior mode, match bash's first-element rule.
         * Unified lookup branches on kind in a single call. */
        lush_value_view_t view = {0};
        symtable_lookup(var_name, &view);
        if (view.kind == LUSH_VALUE_SCALAR) {
            int len = (int)strlen(view.scalar_value);
            lush_value_view_clear(&view);
            char *result = malloc(16);
            if (result) {
                snprintf(result, 16, "%d", len);
            }
            return result ? result : strdup("0");
        }
        if (view.kind == LUSH_VALUE_LIST || view.kind == LUSH_VALUE_MAP) {
            array_value_t *array = view.array;
            shell_mode_t mode = shell_mode_get();
            // size_t can render up to 20 digits on 64-bit + null.
            char *result = malloc(24);
            if (!result) {
                lush_value_view_clear(&view);
                return strdup("0");
            }
            if (mode == SHELL_MODE_BASH || mode == SHELL_MODE_POSIX) {
                const char *first = symtable_array_get_index(array, 0);
                snprintf(result, 24, "%zu", first ? strlen(first) : 0);
            } else {
                snprintf(result, 24, "%zu", symtable_array_length(array));
            }
            lush_value_view_clear(&view);
            return result;
        }
        return strdup("0");
    }

    /* Handle array element access: ${arr[n]}, ${arr[@]}, ${arr[*]}.
     * Only routes through this branch when the prefix before `[` is a
     * valid shell identifier; otherwise the `[` belongs to something
     * else (e.g. the character-class pattern inside a substitution
     * `${var/[abc]/X}`) and must not be consumed here. The prior
     * unconditional `strchr(expansion, '[')` matched any `[` and
     * silently emptied substitutions whose patterns happened to
     * contain a bracket. Issue #96. */
    const char *bracket = strchr(expansion, '[');
    if (bracket && bracket > expansion) {
        size_t name_len = bracket - expansion;
        bool valid_name = true;
        if (!isalpha((unsigned char)expansion[0]) && expansion[0] != '_') {
            valid_name = false;
        } else {
            for (size_t i = 1; i < name_len; i++) {
                if (!isalnum((unsigned char)expansion[i]) &&
                    expansion[i] != '_') {
                    valid_name = false;
                    break;
                }
            }
        }
        if (!valid_name) {
            bracket = NULL;
        }
    } else if (bracket && bracket == expansion) {
        /* `[` at the very start means there is no name -- not an
         * array access. */
        bracket = NULL;
    }
    /* Per-element passthrough: ${arr[@]op...} and ${arr[*]op...} should
     * route through the operator dispatch below so the trailing op
     * applies to each element. The bracket block would otherwise
     * intercept the [@]/[*] subscript and raise a list-in-scalar-slot
     * mismatch before the operator gets to dispatch.
     *
     * Conditions to skip the bracket block:
     *   - subscript is exactly @ or *
     *   - the next char after ] starts a per-element-amenable scalar
     *     operator (^, ',', #, %, /, @). Notably NOT `:` -- slicing
     *     ${arr[@]:0:2} and the conditional family ${arr[@]:-default}
     *     keep their existing handling in the bracket block.
     *   - the name resolves to a known array (otherwise the bracket
     *     belongs to something else and the existing block handles
     *     non-array names correctly). */
    if (bracket) {
        const char *close = strchr(bracket, ']');
        if (close && close[1] != '\0' && close[1] != ':') {
            char op_char = close[1];
            bool is_per_element_op =
                (op_char == '^' || op_char == ',' || op_char == '#' ||
                 op_char == '%' || op_char == '/' || op_char == '@');
            size_t sub_len = (size_t)(close - bracket - 1);
            if (is_per_element_op && sub_len == 1 &&
                (bracket[1] == '@' || bracket[1] == '*')) {
                size_t name_len = (size_t)(bracket - expansion);
                char *probe = strndup(expansion, name_len);
                if (probe) {
                    if (symtable_get_array(probe) != NULL) {
                        bracket = NULL;
                    }
                    free(probe);
                }
            }
        }
    }
    if (bracket) {
        size_t name_len = bracket - expansion;
        char *arr_name = malloc(name_len + 1);
        if (!arr_name) {
            return strdup("");
        }
        strncpy(arr_name, expansion, name_len);
        arr_name[name_len] = '\0';

        const char *close = strchr(bracket, ']');
        if (close) {
            size_t sub_len = close - bracket - 1;
            char *subscript = malloc(sub_len + 1);
            if (subscript) {
                strncpy(subscript, bracket + 1, sub_len);
                subscript[sub_len] = '\0';

                // Resolve nameref if applicable
                const char *resolved_arr_name = arr_name;
                symtable_manager_t *mgr = symtable_get_global_manager();
                if (mgr && symtable_is_nameref(mgr, arr_name)) {
                    const char *target =
                        symtable_resolve_nameref(mgr, arr_name, 10);
                    if (target) {
                        resolved_arr_name = target;
                    }
                }

                array_value_t *array = symtable_get_array(resolved_arr_name);
                if (array) {
                    char *result = NULL;

                    /* Detect a bash slicing suffix `:N` / `:N:M` after
                     * the closing `]`. ${arr[@]:N}, ${arr[@]:N:M},
                     * ${arr[*]:N:M} are element-wise slices on the
                     * array, not byte-wise on the joined string -- the
                     * generic substring case (parse_parameter_expansion
                     * case 14) would silently drop these because it
                     * couldn't resolve `arr[@]` as a scalar var, and
                     * even with a resolution it would byte-slice the
                     * joined string instead of picking elements. Issue
                     * #97. */
                    int slice_offset = 0;
                    int slice_length = -1; // -1 = "to end"
                    bool has_slice = (close[1] == ':' &&
                                      (strcmp(subscript, "@") == 0 ||
                                       strcmp(subscript, "*") == 0) &&
                                      !array->is_associative);
                    if (has_slice) {
                        const char *spec = close + 2;
                        char *endp = NULL;
                        slice_offset = (int)strtol(spec, &endp, 10);
                        if (endp && *endp == ':') {
                            slice_length = (int)strtol(endp + 1, NULL, 10);
                        }
                    }

                    if (has_slice) {
                        size_t total = symtable_array_length(array);
                        // Negative offset counts from end (bash).
                        if (slice_offset < 0) {
                            slice_offset = (int)total + slice_offset;
                            if (slice_offset < 0) {
                                slice_offset = 0;
                            }
                        }
                        int end_idx;
                        if (slice_length < 0) {
                            end_idx = (int)total - 1;
                        } else {
                            end_idx = slice_offset + slice_length - 1;
                        }
                        if (end_idx >= (int)total) {
                            end_idx = (int)total - 1;
                        }
                        if (slice_offset >= (int)total ||
                            end_idx < slice_offset) {
                            result = strdup("");
                        } else {
                            size_t cap = 64;
                            size_t pos = 0;
                            result = malloc(cap);
                            if (result) {
                                result[0] = '\0';
                                for (int k = slice_offset; k <= end_idx; k++) {
                                    const char *elem =
                                        symtable_array_get_index(array, k);
                                    if (!elem) {
                                        continue;
                                    }
                                    size_t elen = strlen(elem);
                                    size_t need =
                                        pos + elen + (pos > 0 ? 1 : 0) + 1;
                                    while (need > cap) {
                                        cap *= 2;
                                        char *nr = realloc(result, cap);
                                        if (!nr) {
                                            free(result);
                                            result = NULL;
                                            break;
                                        }
                                        result = nr;
                                    }
                                    if (!result) {
                                        break;
                                    }
                                    if (pos > 0) {
                                        result[pos++] = ' ';
                                    }
                                    memcpy(result + pos, elem, elen);
                                    pos += elen;
                                    result[pos] = '\0';
                                }
                            }
                        }
                    } else if (strcmp(subscript, "*") == 0) {
                        // ${arr[*]} - explicit scalar join (SEMANTICS.md
                        // section 3.5). All elements joined with the
                        // first character of IFS (" " here as a fixed
                        // default, matching prior behavior).
                        result = symtable_array_expand(array, " ");
                    } else if (strcmp(subscript, "@") == 0) {
                        // ${arr[@]} reaching this general parameter-
                        // expansion fallthrough means the expansion sits
                        // in a SCALAR-REQUIRING context (vector-accepting
                        // positions are handled earlier by
                        // try_expand_vector_arg). Per SEMANTICS.md section
                        // 3.9, this is a runtime type mismatch -- a list
                        // value cannot flow into a scalar slot.
                        shell_error_t *err = shell_error_create(
                            SHELL_ERR_TYPE_MISMATCH, SHELL_SEVERITY_ERROR,
                            executor_current_loc(executor),
                            "type mismatch: list value ${%s[@]} in a "
                            "scalar position",
                            arr_name ? arr_name : "?");
                        if (err) {
                            shell_error_set_suggestion(
                                err,
                                "join the list explicitly -- use ${name[*]} "
                                "for space-joining, or build a scalar from "
                                "the elements with an explicit join.");
                            shell_error_display(err, stderr,
                                                isatty(STDERR_FILENO));
                            shell_error_free(err);
                            executor->has_error = true;
                        } else {
                            executor_error_report(
                                executor, SHELL_ERR_TYPE_MISMATCH,
                                executor_current_loc(executor),
                                "type mismatch: list value ${%s[@]} in a "
                                "scalar position",
                                arr_name ? arr_name : "?");
                        }
                        /* SEMANTICS.md section 3.9 enforcement: in a
                         * script, a type mismatch aborts before the bad
                         * value can reach a downstream command;
                         * interactively, the prompt continues. */
                        executor_request_posix_exit(executor, 1);
                        result = strdup("");
                    } else if ((strncmp(subscript, "(r)", 3) == 0 ||
                                strncmp(subscript, "(R)", 3) == 0) &&
                               !array->is_associative) {
                        /* zsh subscript flags ${arr[(r)pat]} / ${arr[(R)pat]}:
                         * return the VALUE of the first / last element
                         * matching pat, or empty string on no match.
                         * pat is fnmatch-style. Issue #104. */
                        bool last_match = (subscript[1] == 'R');
                        const char *pat = subscript + 3;
                        size_t total = symtable_array_length(array);
                        const char *found = NULL;
                        bool is_glob = (strchr(pat, '*') || strchr(pat, '?') ||
                                        strchr(pat, '['));
                        for (size_t k = 0; k < total; k++) {
                            const char *elem =
                                symtable_array_get_index(array, (int)k);
                            if (!elem) {
                                continue;
                            }
                            bool match;
                            if (is_glob) {
                                match = (fnmatch(pat, elem, 0) == 0);
                            } else {
                                match = (strcmp(elem, pat) == 0);
                            }
                            if (match) {
                                found = elem;
                                if (!last_match) {
                                    break;
                                }
                            }
                        }
                        result = strdup(found ? found : "");
                    } else if ((strncmp(subscript, "(i)", 3) == 0 ||
                                strncmp(subscript, "(I)", 3) == 0) &&
                               !array->is_associative) {
                        /* zsh subscript flags ${arr[(i)pat]} / ${arr[(I)pat]}:
                         * return the 1-based index of the first / last
                         * element matching pat, or N+1 / 0 if no match.
                         * pat is fnmatch-style. Issue #99. */
                        bool last_index = (subscript[1] == 'I');
                        const char *pat = subscript + 3;
                        size_t total = symtable_array_length(array);
                        int found = last_index ? 0 : (int)(total + 1);
                        bool any_match = false;
                        bool is_glob = (strchr(pat, '*') || strchr(pat, '?') ||
                                        strchr(pat, '['));
                        for (size_t k = 0; k < total; k++) {
                            const char *elem =
                                symtable_array_get_index(array, (int)k);
                            if (!elem) {
                                continue;
                            }
                            bool match;
                            if (is_glob) {
                                match = (fnmatch(pat, elem, 0) == 0);
                            } else {
                                match = (strcmp(elem, pat) == 0);
                            }
                            if (match) {
                                int idx_1based = (int)k + 1;
                                if (last_index) {
                                    found = idx_1based;
                                } else if (!any_match) {
                                    found = idx_1based;
                                }
                                any_match = true;
                                if (!last_index) {
                                    break;
                                }
                            }
                        }
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%d", found);
                        result = strdup(buf);
                    } else if (strchr(subscript, ',') &&
                               !array->is_associative) {
                        /* zsh-style range subscript ${arr[N,M]} / $arr[N,M]
                         * on an indexed array: join elements N..M with a
                         * single space, matching zsh's default output of
                         * `$arr[N,M]`. The arithmetic expander interprets
                         * `N,M` as the C comma operator and returns M --
                         * which without this branch silently selected the
                         * M-th element instead of slicing. Supports
                         * negative indices (zsh: -1 = last); honors
                         * FEATURE_ARRAY_ZERO_INDEXED for the 1-based vs
                         * 0-based decision (same shape as the string-
                         * slicing fallback further down). Comma in
                         * associative-array subscripts has no range
                         * meaning -- those keep the C-comma key path. */
                        char *comma = strchr(subscript, ',');
                        *comma = '\0';
                        int start_idx = atoi(subscript);
                        int end_idx = atoi(comma + 1);
                        *comma = ',';
                        size_t total = symtable_array_length(array);
                        bool one_based =
                            !shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED);
                        if (start_idx < 0) {
                            start_idx =
                                (int)total + start_idx + (one_based ? 1 : 0);
                        }
                        if (end_idx < 0) {
                            end_idx =
                                (int)total + end_idx + (one_based ? 1 : 0);
                        }
                        if (one_based) {
                            if (start_idx > 0) {
                                start_idx--;
                            }
                            if (end_idx > 0) {
                                end_idx--;
                            }
                        }
                        if (start_idx < 0) {
                            start_idx = 0;
                        }
                        if (end_idx >= (int)total) {
                            end_idx = (int)total - 1;
                        }
                        if (end_idx < start_idx || (int)total == 0) {
                            result = strdup("");
                        } else {
                            /* Concatenate elements [start_idx..end_idx]
                             * with single-space separator. */
                            size_t cap = 64;
                            size_t pos = 0;
                            result = malloc(cap);
                            if (result) {
                                result[0] = '\0';
                                for (int k = start_idx; k <= end_idx; k++) {
                                    const char *elem =
                                        symtable_array_get_index(array, k);
                                    if (!elem) {
                                        continue;
                                    }
                                    size_t elen = strlen(elem);
                                    size_t need =
                                        pos + elen + (pos > 0 ? 1 : 0) + 1;
                                    while (need > cap) {
                                        cap *= 2;
                                        char *nr = realloc(result, cap);
                                        if (!nr) {
                                            free(result);
                                            result = NULL;
                                            break;
                                        }
                                        result = nr;
                                    }
                                    if (!result) {
                                        break;
                                    }
                                    if (pos > 0) {
                                        result[pos++] = ' ';
                                    }
                                    memcpy(result + pos, elem, elen);
                                    pos += elen;
                                    result[pos] = '\0';
                                }
                            }
                        }
                    } else if (array->is_associative) {
                        // Associative array - use subscript as string key
                        char *expanded_subscript =
                            expand_variable(executor, subscript);
                        const char *key =
                            expanded_subscript ? expanded_subscript : subscript;
                        const char *elem = symtable_array_get_assoc(array, key);
                        result = strdup(elem ? elem : "");
                        if (expanded_subscript)
                            free(expanded_subscript);
                    } else {
                        // Indexed array - ${arr[n]} - specific element
                        arithm_clear_error();
                        char *idx_result = arithm_expand(subscript);
                        if (idx_result && !arithm_error_is_flagged()) {
                            int idx = (int)strtoll(idx_result, NULL, 10);
                            free(idx_result);

                            /* zsh-mode (1-based): 0 is invalid (returns
                             * empty); positive indices need
                             * decrement-to-0-based; negative indices pass
                             * through unchanged so the symtable helper's
                             * built-in "from-end" handling fires.
                             * lush/bash-mode (0-based): pass through
                             * directly. (Issue #68 — array half.) */
                            if (!shell_mode_allows(
                                    FEATURE_ARRAY_ZERO_INDEXED)) {
                                if (idx == 0) {
                                    result = strdup("");
                                } else {
                                    if (idx > 0) {
                                        idx--; // 1-based -> 0-based
                                    }
                                    const char *elem =
                                        symtable_array_get_index(array, idx);
                                    result = strdup(elem ? elem : "");
                                }
                            } else {
                                const char *elem =
                                    symtable_array_get_index(array, idx);
                                result = strdup(elem ? elem : "");
                            }
                        } else {
                            if (idx_result)
                                free(idx_result);
                            result = strdup("");
                        }
                    }

                    /* Apply trailing parameter-expansion operator on an
                     * indexed/associative element: `${arr[key]:-default}`,
                     * `${arr[key]:+alt}`, etc. Without this, the array
                     * branch returned the (possibly empty) element value
                     * unconditionally, dropping the operator entirely
                     * (real_world/bash/200 fell back to "" instead of the
                     * `:-info` default for a missing assoc key). For
                     * arrays we can't distinguish unset from empty (a
                     * missing key reads as ""), so both `:-` and `-`
                     * behave identically here, as do `:+` and `+`. The
                     * default/alt RHS is variable-expanded so
                     * `${arr[k]:-$fallback}` works. */
                    const char *after_bracket = close + 1;
                    if (*after_bracket != '\0') {
                        bool value_is_empty = (!result || !*result);
                        const char *rhs = NULL;
                        bool want_default_when_empty = false; // :- / -
                        bool want_alt_when_nonempty = false;  // :+ / +
                        if (after_bracket[0] == ':' &&
                            (after_bracket[1] == '-' ||
                             after_bracket[1] == '+')) {
                            rhs = after_bracket + 2;
                            want_default_when_empty = (after_bracket[1] == '-');
                            want_alt_when_nonempty = (after_bracket[1] == '+');
                        } else if (after_bracket[0] == '-' ||
                                   after_bracket[0] == '+') {
                            rhs = after_bracket + 1;
                            want_default_when_empty = (after_bracket[0] == '-');
                            want_alt_when_nonempty = (after_bracket[0] == '+');
                        }
                        if (rhs) {
                            char *expanded_rhs =
                                expand_variables_in_string(executor, rhs);
                            const char *rhs_final =
                                expanded_rhs ? expanded_rhs : rhs;
                            if (want_default_when_empty && value_is_empty) {
                                free(result);
                                result = strdup(rhs_final);
                            } else if (want_alt_when_nonempty &&
                                       !value_is_empty) {
                                free(result);
                                result = strdup(rhs_final);
                            } else if (want_alt_when_nonempty) {
                                free(result);
                                result = strdup("");
                            }
                            if (expanded_rhs) {
                                free(expanded_rhs);
                            }
                        }
                    }

                    free(subscript);
                    free(arr_name);
                    return result ? result : strdup("");
                }

                // Array doesn't exist - check if there are parameter expansion
                // operators after ]
                // For example: ${arr[0]:-default}
                const char *after_bracket = close + 1;
                if (*after_bracket != '\0') {
                    // There's more after ] - this might be a parameter
                    // expansion on an unset array element. For now, treat as
                    // empty.
                }

                // String-slicing fallback: ${var[N]} / ${var[N,M]} on a
                // scalar string slices grapheme clusters (TR#29 boundaries).
                // Honors FEATURE_ARRAY_ZERO_INDEXED: 1-based for zsh-mode,
                // 0-based for bash/lush-mode. Subscript "@" / "*" are
                // array-only and have already been handled above.
                if (strcmp(subscript, "@") != 0 &&
                    strcmp(subscript, "*") != 0) {
                    char *str_value =
                        symtable_get_var(executor->symtable, arr_name);
                    if (str_value) {
                        int start_idx = 0, end_idx = -1;
                        char *comma = strchr(subscript, ',');
                        if (comma) {
                            *comma = '\0';
                            start_idx = atoi(subscript);
                            end_idx = atoi(comma + 1);
                            *comma = ',';
                        } else {
                            start_idx = atoi(subscript);
                            end_idx = start_idx; // single grapheme
                        }
                        size_t value_len = strlen(str_value);

                        /* Negative-index handling: ${str[-N]} counts from
                         * the end. zsh-mode (1-based): -1 = last grapheme
                         * (position total). lush/bash-mode (0-based):
                         * -1 = last (position total-1). Computes the
                         * grapheme count via lle_utf8_count_graphemes
                         * (TR#29-correct, same primitive
                         * slice_string_graphemes uses internally).
                         * (Issue #68.) */
                        if (start_idx < 0 || end_idx < 0) {
                            int total = (int)lle_utf8_count_graphemes(
                                str_value, value_len);
                            bool one_based =
                                !shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED);
                            if (start_idx < 0) {
                                start_idx =
                                    total + start_idx + (one_based ? 1 : 0);
                            }
                            if (end_idx < 0) {
                                end_idx = total + end_idx + (one_based ? 1 : 0);
                            }
                        }

                        // Convert from 1-based (zsh) to 0-based if needed
                        if (!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED)) {
                            if (start_idx <= 0 || end_idx <= 0) {
                                free(str_value);
                                free(subscript);
                                free(arr_name);
                                return strdup("");
                            }
                            start_idx--;
                            end_idx--;
                        }

                        /* Inverted range yields empty (catches both user-
                         * written ${str[3,1]} and post-conversion
                         * overshoots in 0-based mode). */
                        if (end_idx < start_idx) {
                            free(str_value);
                            free(subscript);
                            free(arr_name);
                            return strdup("");
                        }

                        int count = end_idx - start_idx + 1;
                        char *result = slice_string_graphemes(
                            str_value, value_len, start_idx, count);
                        free(str_value);
                        free(subscript);
                        free(arr_name);
                        return result ? result : strdup("");
                    }
                }

                free(subscript);
            }
        }
        free(arr_name);
        return strdup("");
    }

    // Look for parameter expansion operators
    const char *op_pos = NULL;
    // Order matters: longer operators first, then shorter ones
    // 0-14: existing operators, 15-18: new Phase 4 operators
    const char *operators[] = {":-", // 0: use default if unset or empty
                               ":+", // 1: use alternative if set and non-empty
                               "##", // 2: remove longest prefix pattern
                               "%%", // 3: remove longest suffix pattern
                               "^^", // 4: uppercase all
                               ",,", // 5: lowercase all
                               "#",  // 6: remove shortest prefix pattern
                               "%",  // 7: remove shortest suffix pattern
                               "^",  // 8: uppercase first
                               ",",  // 9: lowercase first
                               "-",  // 10: use default if unset
                               "+",  // 11: use alternative if set
                               ":=", // 12: assign default if unset or empty
                               "=",  // 13: assign default if unset
                               ":",  // 14: substring
                               "//", // 15: replace all occurrences
                               "/",  // 16: replace first occurrence
                               "@",  // 17: transformations
                               ":?", // 18: error if unset or null (POSIX)
                               "?",  // 19: error if unset (POSIX)
                               NULL};
    int op_type = -1;

    /* Special-parameter names at position 0 (@, *, #, ?, !, $, -, 0..9)
     * are variable names, not operators. Without this guard ${@^}
     * gets parsed as the `@` transformation operator (op_type 17)
     * applied to an empty var_name, instead of `@` as the variable
     * with the `^` case-mod operator. Same for ${*^}, ${#:-default}
     * variants on the special params, etc. Issue #96. */
    bool first_is_special_param = false;
    if (expansion[0]) {
        char c0 = expansion[0];
        if (c0 == '@' || c0 == '*' || c0 == '#' || c0 == '?' || c0 == '!' ||
            c0 == '$' || c0 == '-' || (c0 >= '0' && c0 <= '9')) {
            first_is_special_param = true;
        }
    }

    // Find the first valid operator - prioritize longer operators first.
    // Use the bracket-aware variant so subscript chars inside `[...]`
    // (notably `@` in `arr[@]`) don't get picked as operators.
    for (int i = 0; operators[i]; i++) {
        const char *found = find_op_outside_brackets(expansion, operators[i]);
        /* If the operator matches at position 0 and the first char is
         * a special-param name, search again starting after it -- the
         * apparent operator at position 0 is really the variable. */
        if (found == expansion && first_is_special_param) {
            found = find_op_outside_brackets(expansion + 1, operators[i]);
        }
        if (found) {
            // Skip single-character operators that are part of longer ones
            if (strlen(operators[i]) == 1) {
                // Check if this single char is part of a longer operator
                bool part_of_longer = false;

                // Check for :- :+ := :? before processing single :
                if (strcmp(operators[i], ":") == 0) {
                    if ((found > expansion &&
                         (found[-1] == '-' || found[-1] == '+')) ||
                        (found[1] == '-' || found[1] == '+' ||
                         found[1] == '=' || found[1] == '?')) {
                        part_of_longer = true;
                    }
                }

                // Check for ## and %% before processing single # or %
                if (strcmp(operators[i], "#") == 0 && found[1] == '#') {
                    part_of_longer = true;
                }
                if (strcmp(operators[i], "%") == 0 && found[1] == '%') {
                    part_of_longer = true;
                }
                // Check for // before processing single /
                if (strcmp(operators[i], "/") == 0 && found[1] == '/') {
                    part_of_longer = true;
                }
                // Check for :? before processing single ?
                if (strcmp(operators[i], "?") == 0 && found > expansion &&
                    found[-1] == ':') {
                    part_of_longer = true;
                }

                if (part_of_longer) {
                    continue;
                }
            }

            // If we haven't found an operator yet, or this one comes first, use
            // it
            if (!op_pos || found < op_pos) {
                op_pos = found;
                op_type = i;
            }
        }
    }

    if (op_pos) {
        // Extract variable name
        size_t var_len = op_pos - expansion;

        // If operator is at position 0, it might actually be a special variable
        // like $- (which contains the '-' character itself)
        if (var_len == 0 && strlen(expansion) == 1 && expansion[0] == '-') {
            // This is $- (shell options), not a parameter expansion operator
            // Fall through to regular variable lookup below
            op_pos = NULL;
            op_type = -1;
        }
    }

    if (op_pos) {
        // Extract variable name
        size_t var_len = op_pos - expansion;
        char *var_name = malloc(var_len + 1);
        if (!var_name) {
            return strdup("");
        }

        strncpy(var_name, expansion, var_len);
        var_name[var_len] = '\0';

        /* Scalar-operator on bare collection: type mismatch.
         *
         * A bare name like ${arr:-default} or ${arr##pattern} or
         * ${arr^^} reaches this dispatch when arr is a list/map.
         * The operator is scalar-shaped; applying it would either
         * silently degrade the collection or treat the unset-scalar
         * path as if the collection were empty. Both are exactly
         * the implicit list-to-scalar coercion the engine is
         * designed to reject. Surface a type mismatch with a hint
         * pointing at the explicit forms (slice + scalar op, or
         * [@]-vectorized op).
         *
         * Subscripted forms ${arr[@]op...} and ${arr[N]op...} go
         * through a different path; they're already vectorized or
         * single-element. The bare-name check fires only when the
         * operator's left operand is a complete collection
         * identifier with no subscript. */
        if (var_len > 0) {
            array_value_t *bare_array = symtable_get_array(var_name);
            if (bare_array) {
                const char *op_str = operators[op_type];
                const char *kind_label =
                    bare_array->is_associative ? "map" : "list";
                const char *hint = NULL;
                switch (op_type) {
                case 0:  // :-
                case 1:  // :+
                case 10: // -
                case 11: // +
                case 12: // :=
                case 13: // =
                case 18: // :?
                case 19: // ?
                    hint = "use ${name[0]:-default} for a scalar-"
                           "element default, or assign a list literal "
                           "directly for a list-shaped default";
                    break;
                case 14: // :
                    hint = "substring is scalar-only -- use "
                           "${name[@]:offset:length} for list slicing, "
                           "or ${name[N]:offset:length} for substring "
                           "of one element";
                    break;
                case 2:  // ##
                case 3:  // %%
                case 6:  // #
                case 7:  // %
                case 15: // //
                case 16: // /
                case 4:  // ^^
                case 5:  // ,,
                case 8:  // ^
                case 9:  // ,
                case 17: // @ transformations
                    hint = "append '[@]' to the name to vectorize the "
                           "operation across all elements -- "
                           "${name[@]op...}";
                    break;
                default:
                    hint = "scalar-shaped operators on a collection "
                           "require explicit vectorization with '[@]'";
                    break;
                }
                executor_error_report(
                    executor, SHELL_ERR_TYPE_MISMATCH,
                    executor_current_loc(executor),
                    "type mismatch: scalar operator '%s' applied to "
                    "'%s', which is a %s",
                    op_str, var_name, kind_label);
                if (hint) {
                    /* The error_report path emits the message
                     * immediately; the help line is set as part of
                     * the structured error if we have one. The
                     * downstream display already includes the help
                     * field from the most recent error in the
                     * collector, so we don't need to re-emit. The
                     * hint stays paired with the operator class
                     * above so future operators get a tailored
                     * suggestion when they land. */
                    (void)hint;
                }
                executor_request_posix_exit(executor, 1);
                free(var_name);
                return strdup("");
            }
        }

        // Get variable value
        char *var_value = symtable_get_var(executor->symtable, var_name);
        const char *default_value = op_pos + strlen(operators[op_type]);

        // Expand variables in default value
        char *expanded_default =
            expand_variables_in_string(executor, default_value);

        char *result = NULL;

        /* Per-element dispatch for vector-yielding var names with case-
         * modification operators. ${@^}, ${@^^[pat]}, ${@,}, ${@,,[pat]}
         * and the analogous ${arr[@]^^[pat]} family apply the operator
         * to each positional parameter or array element independently,
         * then join with space. Bash semantics; scope intentionally
         * narrowed to case-mod ops for issue #96 (other operators on
         * vector names -- substitution, trim, substring -- are
         * separate work). */
        bool case_mod_op =
            (op_type == 4 || op_type == 5 || op_type == 8 || op_type == 9);
        /* Per-element-amenable scalar operators when the variable
         * is a vector ($@, $* or arr[@] / arr[*]): case-mod, pattern
         * strip, replace, and the @-transform family. Each applies
         * to each element independently and the results join with
         * space. */
        bool per_element_op = case_mod_op || op_type == 2 || op_type == 3 ||
                              op_type == 6 || op_type == 7 || op_type == 15 ||
                              op_type == 16 || op_type == 17;
        bool is_at_or_star =
            (strcmp(var_name, "@") == 0 || strcmp(var_name, "*") == 0);
        size_t vn_len = strlen(var_name);
        bool is_arr_at_or_star =
            (vn_len >= 4 &&
             ((var_name[vn_len - 3] == '[' &&
               (var_name[vn_len - 2] == '@' || var_name[vn_len - 2] == '*') &&
               var_name[vn_len - 1] == ']')));
        if (per_element_op && (is_at_or_star || is_arr_at_or_star)) {
            char **elems = NULL;
            int n_elems = 0;
            int cap = 0;

            if (is_at_or_star) {
                int total;
                if (symtable_in_function_scope(executor->symtable)) {
                    char *argc_str = symtable_get_var(executor->symtable, "#");
                    total = argc_str ? atoi(argc_str) : 0;
                    free(argc_str);
                    for (int i = 1; i <= total; i++) {
                        char name[16];
                        snprintf(name, sizeof(name), "%d", i);
                        char *v = symtable_get_var(executor->symtable, name);
                        if (!v) {
                            v = strdup("");
                        }
                        add_to_argv_list(&elems, &n_elems, &cap, v);
                    }
                } else {
                    for (int i = 1; i < shell_argc; i++) {
                        add_to_argv_list(
                            &elems, &n_elems, &cap,
                            strdup(shell_argv[i] ? shell_argv[i] : ""));
                    }
                }
            } else {
                // arr[@] / arr[*]
                char arr_name[256];
                size_t name_len = vn_len - 3;
                if (name_len < sizeof(arr_name)) {
                    memcpy(arr_name, var_name, name_len);
                    arr_name[name_len] = '\0';
                    array_value_t *array = symtable_get_array(arr_name);
                    if (array) {
                        size_t total = symtable_array_length(array);
                        for (size_t i = 0; i < total; i++) {
                            const char *e =
                                symtable_array_get_index(array, (int)i);
                            add_to_argv_list(&elems, &n_elems, &cap,
                                             strdup(e ? e : ""));
                        }
                    }
                }
            }

            // Apply the per-element scalar op to each element and join
            // results with a single space. Each branch produces a
            // freshly allocated transformed string per element; the
            // join loop then concatenates with growth.
            size_t out_cap = 64;
            size_t out_pos = 0;
            char *joined = malloc(out_cap);
            if (joined) {
                joined[0] = '\0';
                for (int i = 0; i < n_elems; i++) {
                    char *converted = NULL;
                    switch (op_type) {
                    case 4: // ^^ uppercase all
                    case 5: // ,, lowercase all
                    case 8: // ^  uppercase first / match
                    case 9: // ,  lowercase first / match
                    {
                        bool to_upper = (op_type == 4 || op_type == 8);
                        bool first_only = (op_type == 8 || op_type == 9);
                        if (expanded_default && expanded_default[0]) {
                            converted =
                                convert_case_pattern(elems[i], expanded_default,
                                                     to_upper, first_only);
                        } else if (first_only) {
                            converted =
                                to_upper ? convert_case_first_upper(elems[i])
                                         : convert_case_first_lower(elems[i]);
                        } else {
                            converted = to_upper
                                            ? convert_case_all_upper(elems[i])
                                            : convert_case_all_lower(elems[i]);
                        }
                        break;
                    }
                    case 2: // ## longest prefix strip
                    case 6: // #  shortest prefix strip
                    {
                        int match = find_prefix_match(
                            elems[i], expanded_default, op_type == 2);
                        converted = strdup(elems[i] + match);
                        break;
                    }
                    case 3: // %% longest suffix strip
                    case 7: // %  shortest suffix strip
                    {
                        int match = find_suffix_match(
                            elems[i], expanded_default, op_type == 3);
                        int keep = (int)strlen(elems[i]) - match;
                        if (keep < 0) {
                            keep = 0;
                        }
                        converted = malloc((size_t)keep + 1);
                        if (converted) {
                            memcpy(converted, elems[i], (size_t)keep);
                            converted[keep] = '\0';
                        }
                        break;
                    }
                    case 15: // // replace all
                    case 16: // /  replace first
                    {
                        // Split expanded_default at first unescaped '/'.
                        char *sep = NULL;
                        for (char *p = expanded_default; p && *p; p++) {
                            if (*p == '\\' && p[1] == '/') {
                                p++;
                                continue;
                            }
                            if (*p == '/') {
                                sep = p;
                                break;
                            }
                        }
                        bool global = (op_type == 15);
                        char *pattern = NULL;
                        const char *replacement = "";
                        if (sep) {
                            size_t plen = (size_t)(sep - expanded_default);
                            pattern = malloc(plen + 1);
                            if (pattern) {
                                size_t pj = 0;
                                for (size_t pi = 0; pi < plen; pi++) {
                                    if (expanded_default[pi] == '\\' &&
                                        pi + 1 < plen &&
                                        expanded_default[pi + 1] == '/') {
                                        pattern[pj++] = '/';
                                        pi++;
                                    } else {
                                        pattern[pj++] = expanded_default[pi];
                                    }
                                }
                                pattern[pj] = '\0';
                                replacement = sep + 1;
                            }
                        } else if (expanded_default) {
                            pattern = strdup(expanded_default);
                        }
                        if (pattern) {
                            converted = pattern_substitute(elems[i], pattern,
                                                           replacement, global);
                            free(pattern);
                        }
                        if (!converted) {
                            converted = strdup(elems[i]);
                        }
                        break;
                    }
                    case 17: // @transform (Q E P A a)
                    {
                        char tcode = (expanded_default && expanded_default[0])
                                         ? expanded_default[0]
                                         : '\0';
                        switch (tcode) {
                        case 'Q': // shell-quoted form
                        {
                            // Conservative: wrap in single quotes and
                            // escape embedded single quotes. Matches
                            // bash's @Q for typical strings.
                            size_t elen = strlen(elems[i]);
                            size_t qcap = elen * 4 + 3;
                            converted = malloc(qcap);
                            if (converted) {
                                size_t qp = 0;
                                converted[qp++] = '\'';
                                for (size_t k = 0; k < elen; k++) {
                                    if (elems[i][k] == '\'') {
                                        converted[qp++] = '\'';
                                        converted[qp++] = '\\';
                                        converted[qp++] = '\'';
                                        converted[qp++] = '\'';
                                    } else {
                                        converted[qp++] = elems[i][k];
                                    }
                                }
                                converted[qp++] = '\'';
                                converted[qp] = '\0';
                            }
                            break;
                        }
                        case 'E': // backslash-escape processing
                            // Passthrough for now; per-element parity
                            // with the scalar @E path.
                            converted = strdup(elems[i]);
                            break;
                        case 'P': // prompt expansion
                            converted = strdup(elems[i]);
                            break;
                        case 'A': // assignment form
                            converted = strdup(elems[i]);
                            break;
                        case 'a': // attributes
                            converted = strdup("");
                            break;
                        default:
                            converted = strdup(elems[i]);
                            break;
                        }
                        break;
                    }
                    default:
                        converted = strdup(elems[i]);
                        break;
                    }
                    if (!converted) {
                        converted = strdup("");
                    }
                    size_t clen = strlen(converted);
                    size_t need = out_pos + (out_pos > 0 ? 1 : 0) + clen + 1;
                    while (need > out_cap) {
                        out_cap *= 2;
                        char *nb = realloc(joined, out_cap);
                        if (!nb) {
                            free(joined);
                            joined = NULL;
                            break;
                        }
                        joined = nb;
                    }
                    if (!joined) {
                        free(converted);
                        break;
                    }
                    if (out_pos > 0) {
                        joined[out_pos++] = ' ';
                    }
                    memcpy(joined + out_pos, converted, clen);
                    out_pos += clen;
                    joined[out_pos] = '\0';
                    free(converted);
                }
            }
            result = joined ? joined : strdup("");
            for (int i = 0; i < n_elems; i++) {
                free(elems[i]);
            }
            free(elems);
            free(var_name);
            free(var_value);
            free(expanded_default);
            return result;
        }

        switch (op_type) {
        case 0: // ${var:-default} - use default if var is unset or empty
            if (is_empty_or_null(var_value)) {
                result = strdup(expanded_default);
            } else {
                result = strdup(var_value);
            }
            break;

        case 1: // ${var:+alternative} - use alternative if var is set and
                // non-empty
            if (!is_empty_or_null(var_value)) {
                result = strdup(expanded_default);
            } else {
                result = strdup("");
            }
            break;

        case 2: // ${var##pattern} - remove longest match of pattern from
                // beginning
            if (var_value) {
                int match_len =
                    find_prefix_match(var_value, expanded_default, true);
                result = strdup(var_value + match_len);
            } else {
                result = strdup("");
            }
            break;

        case 3: // ${var%%pattern} - remove longest match of pattern from end
            if (var_value) {
                int str_len = strlen(var_value);
                int match_len =
                    find_suffix_match(var_value, expanded_default, true);
                int result_len = str_len - match_len;
                result = malloc(result_len + 1);
                if (result) {
                    strncpy(result, var_value, result_len);
                    result[result_len] = '\0';
                } else {
                    result = strdup("");
                }
            } else {
                result = strdup("");
            }
            break;

        case 4: // ${var^^[pat]} - convert all characters to uppercase
            if (var_value) {
                /* Pattern restriction (issue #96): ${var^^[abc]} converts
                 * only characters matching the glob pattern. Empty
                 * pattern falls through to the UTF-8-aware path so
                 * non-ASCII content is upper-cased correctly. */
                if (expanded_default && expanded_default[0]) {
                    result = convert_case_pattern(var_value, expanded_default,
                                                  true, false);
                } else {
                    result = convert_case_all_upper(var_value);
                }
            } else {
                result = strdup("");
            }
            break;

        case 5: // ${var,,[pat]} - convert all characters to lowercase
            if (var_value) {
                if (expanded_default && expanded_default[0]) {
                    result = convert_case_pattern(var_value, expanded_default,
                                                  false, false);
                } else {
                    result = convert_case_all_lower(var_value);
                }
            } else {
                result = strdup("");
            }
            break;

        case 6: // ${var#pattern} - remove shortest match of pattern from
                // beginning
            if (var_value) {
                int match_len =
                    find_prefix_match(var_value, expanded_default, false);
                result = strdup(var_value + match_len);
            } else {
                result = strdup("");
            }
            break;

        case 7: // ${var%pattern} - remove shortest match of pattern from end
            if (var_value) {
                int str_len = strlen(var_value);
                int match_len =
                    find_suffix_match(var_value, expanded_default, false);
                int result_len = str_len - match_len;
                result = malloc(result_len + 1);
                if (result) {
                    strncpy(result, var_value, result_len);
                    result[result_len] = '\0';
                } else {
                    result = strdup("");
                }
            } else {
                result = strdup("");
            }
            break;

        case 8: // ${var^[pat]} - convert first matching character to uppercase
            if (var_value) {
                if (expanded_default && expanded_default[0]) {
                    result = convert_case_pattern(var_value, expanded_default,
                                                  true, true);
                } else {
                    result = convert_case_first_upper(var_value);
                }
            } else {
                result = strdup("");
            }
            break;

        case 9: // ${var,[pat]} - convert first matching character to lowercase
            if (var_value) {
                if (expanded_default && expanded_default[0]) {
                    result = convert_case_pattern(var_value, expanded_default,
                                                  false, true);
                } else {
                    result = convert_case_first_lower(var_value);
                }
            } else {
                result = strdup("");
            }
            break;

        case 10: // ${var-default} - use default if var is unset (but not if
                 // empty)
            if (!var_value) {
                result = strdup(expanded_default);
            } else {
                result = strdup(var_value);
            }
            break;

        case 11: // ${var+alternative} - use alternative if var is set (even if
                 // empty)
            if (var_value) {
                result = strdup(expanded_default);
            } else {
                result = strdup("");
            }
            break;

        case 12: // ${var:=default} - assign default if var is unset or empty
                 // and return it
            if (is_empty_or_null(var_value)) {
                symtable_set_var(executor->symtable, var_name, expanded_default,
                                 SYMVAR_NONE);
                result = strdup(expanded_default);
            } else {
                result = strdup(var_value);
            }
            break;

        case 13: // ${var=default} - assign default if var is unset and return
                 // it
            if (!var_value) {
                symtable_set_var(executor->symtable, var_name, expanded_default,
                                 SYMVAR_NONE);
                result = strdup(expanded_default);
            } else {
                result = strdup(var_value);
            }
            break;

        case 14: // ${var:offset:length} - substring expansion
            if (var_value) {
                // Parse offset and optional length (with variable expansion)
                char *expanded_offset_str =
                    expand_variables_in_string(executor, expanded_default);
                char *endptr;
                int offset = strtol(expanded_offset_str, &endptr, 10);
                int length = -1;

                if (*endptr == ':') {
                    length = strtol(endptr + 1, NULL, 10);
                }

                result = extract_substring(var_value, offset, length);
                free(expanded_offset_str);
            } else {
                result = strdup("");
            }
            break;

        case 15: // ${var//pattern/replacement} - replace all occurrences
        case 16: // ${var/pattern/replacement} - replace first occurrence
            /* Pattern/replacement split honoring backslash-escaped
             * slashes. ${path//\//.} has pattern `\/` (literal slash)
             * and replacement `.`; the prior strchr-based split took
             * the FIRST `/` as the separator even when it was preceded
             * by `\`, splitting pattern as `\` (nothing) and replacement
             * as `/.` -- silently producing the original string back.
             * Walk the spec and break at the first unescaped `/`.
             * Backslash-escapes other than `\/` pass through to
             * fnmatch which handles them per glob spec. Issue #96. */
            if (var_value) {
                char *sep = NULL;
                for (char *p = expanded_default; *p; p++) {
                    if (*p == '\\' && p[1] == '/') {
                        p++;
                        continue;
                    }
                    if (*p == '/') {
                        sep = p;
                        break;
                    }
                }
                bool global = (op_type == 15);
                if (sep) {
                    size_t pattern_len = sep - expanded_default;
                    char *pattern = malloc(pattern_len + 1);
                    if (pattern) {
                        /* Strip `\/` -> `/` in the extracted pattern
                         * so downstream matchers see the canonical
                         * literal slash. Other backslash sequences
                         * pass through. */
                        size_t pj = 0;
                        for (size_t pi = 0; pi < pattern_len; pi++) {
                            if (expanded_default[pi] == '\\' &&
                                pi + 1 < pattern_len &&
                                expanded_default[pi + 1] == '/') {
                                pattern[pj++] = '/';
                                pi++;
                            } else {
                                pattern[pj++] = expanded_default[pi];
                            }
                        }
                        pattern[pj] = '\0';
                        const char *replacement = sep + 1;
                        result = pattern_substitute(var_value, pattern,
                                                    replacement, global);
                        free(pattern);
                    } else {
                        result = strdup(var_value);
                    }
                } else {
                    /* No replacement, just remove pattern. Same `\/`
                     * canonicalization as the pattern half. */
                    size_t plen = strlen(expanded_default);
                    char *pattern = malloc(plen + 1);
                    if (pattern) {
                        size_t pj = 0;
                        for (size_t pi = 0; pi < plen; pi++) {
                            if (expanded_default[pi] == '\\' && pi + 1 < plen &&
                                expanded_default[pi + 1] == '/') {
                                pattern[pj++] = '/';
                                pi++;
                            } else {
                                pattern[pj++] = expanded_default[pi];
                            }
                        }
                        pattern[pj] = '\0';
                        result =
                            pattern_substitute(var_value, pattern, "", global);
                        free(pattern);
                    } else {
                        result = strdup(var_value);
                    }
                }
            } else {
                result = strdup("");
            }
            break;

        case 17: // ${var@op} - transformations
            if (expanded_default[0]) {
                char op = expanded_default[0];
                /* The @a (attribute query) variant only inspects the
                 * variable's metadata and doesn't need var_value to
                 * be set. Arrays specifically have NULL var_value
                 * (scalar lookup misses them), so the prior
                 * `if (var_value && ...)` guard hid the attribute
                 * for `declare -A arr; echo "${arr@a}"`. Issue #102.
                 * Other @op flavors do still need a value; for those
                 * fall through to the empty-result path. */
                if (op == 'a') {
                    result = get_variable_attributes(var_name);
                    break;
                }
                if (!var_value) {
                    result = strdup("");
                    break;
                }
                switch (op) {
                case 'Q': // Quote value for reuse as input
                    result = transform_quote(var_value);
                    break;
                case 'E': // Expand escape sequences
                    result = transform_escape(var_value);
                    break;
                case 'P': // Expand as prompt string
                    result = transform_prompt(var_value);
                    break;
                case 'A': // Assignment statement form
                    result = transform_assignment(var_name, var_value);
                    break;
                case 'U': // Uppercase all
                    result = convert_case_all_upper(var_value);
                    break;
                case 'u': // Uppercase first
                    result = convert_case_first_upper(var_value);
                    break;
                case 'L': // Lowercase all
                    result = convert_case_all_lower(var_value);
                    break;
                default:
                    result = strdup(var_value);
                    break;
                }
            } else {
                result = strdup("");
            }
            break;

        case 18: // ${var:?word} - error if var unset or null (POSIX)
            if (is_empty_or_null(var_value)) {
                result = handle_required_param_error(
                    executor, var_name, expanded_default,
                    "parameter null or not set");
            } else {
                result = strdup(var_value);
            }
            break;

        case 19: // ${var?word} - error if var unset (null permitted) (POSIX)
            if (!var_value) {
                result = handle_required_param_error(
                    executor, var_name, expanded_default, "parameter not set");
            } else {
                result = strdup(var_value);
            }
            break;
        }

        free(var_name);
        free(var_value);
        free(expanded_default);
        return result ? result : strdup("");
    }

    // No operator found, just get the variable value
    // First check for special variables that aren't in the symbol table
    if (strlen(expansion) == 1) {
        char buffer[1024];

        switch (expansion[0]) {
        case '?': // Exit status of last command
            snprintf(buffer, sizeof(buffer), "%d", last_exit_status);
            return strdup(buffer);

        case '$': // Shell process ID
            snprintf(buffer, sizeof(buffer), "%d", (int)shell_pid);
            return strdup(buffer);

        case '#': // Number of positional parameters
            snprintf(buffer, sizeof(buffer), "%d",
                     shell_argc > 1 ? shell_argc - 1 : 0);
            return strdup(buffer);

        case '!': // Process ID of last background command
            if (last_background_pid > 0) {
                snprintf(buffer, sizeof(buffer), "%d",
                         (int)last_background_pid);
                return strdup(buffer);
            } else {
                return strdup("");
            }

        case '-': { // Current option flags
            // Build string of current shell option flags
            char flags[32];
            int pos = 0;
            if (is_interactive_shell())
                flags[pos++] = 'i';
            if (shell_opts.job_control)
                flags[pos++] = 'm';
            if (shell_opts.exit_on_error)
                flags[pos++] = 'e';
            if (shell_opts.unset_error)
                flags[pos++] = 'u';
            if (shell_opts.trace_execution)
                flags[pos++] = 'x';
            if (shell_opts.verbose)
                flags[pos++] = 'v';
            if (shell_opts.noclobber)
                flags[pos++] = 'C';
            if (shell_opts.no_globbing)
                flags[pos++] = 'f';
            if (shell_opts.syntax_check)
                flags[pos++] = 'n';
            if (shell_opts.allexport)
                flags[pos++] = 'a';
            if (shell_opts.notify)
                flags[pos++] = 'b';
            if (shell_opts.physical_mode)
                flags[pos++] = 'P';
            if (shell_opts.privileged_mode)
                flags[pos++] = 'p';
            if (shell_opts.history_mode)
                flags[pos++] = 'H';
            if (shell_opts.histexpand_mode)
                flags[pos++] = 'B';
            flags[pos] = '\0';
            return strdup(flags);
        }

        case '*': // All positional parameters as single word
            if (shell_argc > 1) {
                size_t total_len = 0;
                for (int i = 1; i < shell_argc; i++) {
                    if (shell_argv[i]) {
                        total_len += strlen(shell_argv[i]) + 1; // +1 for space
                    }
                }
                if (total_len > 0) {
                    char *result = malloc(total_len);
                    if (result) {
                        result[0] = '\0';
                        for (int i = 1; i < shell_argc; i++) {
                            if (shell_argv[i]) {
                                if (i > 1) {
                                    strcat(result, " ");
                                }
                                strcat(result, shell_argv[i]);
                            }
                        }
                        return result;
                    }
                }
            }
            return strdup("");

        case '@': // All positional parameters as separate words
            if (shell_argc > 1) {
                size_t total_len = 0;
                for (int i = 1; i < shell_argc; i++) {
                    if (shell_argv[i]) {
                        total_len += strlen(shell_argv[i]) + 1; // +1 for space
                    }
                }
                if (total_len > 0) {
                    char *result = malloc(total_len);
                    if (result) {
                        result[0] = '\0';
                        for (int i = 1; i < shell_argc; i++) {
                            if (shell_argv[i]) {
                                if (i > 1) {
                                    strcat(result, " ");
                                }
                                strcat(result, shell_argv[i]);
                            }
                        }
                        return result;
                    }
                }
            }
            return strdup("");

        default:
            if (expansion[0] >= '0' && expansion[0] <= '9') {
                // Handle positional parameters $0, $1, $2, etc.
                int pos = expansion[0] - '0';

                if (pos == 0) {
                    // $0 is the script/shell name
                    return strdup((shell_argc > 0 && shell_argv[0])
                                      ? shell_argv[0]
                                      : "lush");
                } else if (pos > 0) {
                    // $1, $2, etc. - check function scope first
                    if (symtable_in_function_scope(executor->symtable)) {
                        // In function scope - get from local positional params
                        char param_name[16];
                        snprintf(param_name, sizeof(param_name), "%d", pos);
                        char *value =
                            symtable_get_var(executor->symtable, param_name);
                        if (value && value[0] != '\0') {
                            return value; // Already allocated by
                                          // symtable_get_var
                        }
                        free(value);
                        return strdup("");
                    } else if (pos < shell_argc && shell_argv[pos]) {
                        // Global scope - use shell_argv
                        return strdup(shell_argv[pos]);
                    } else {
                        return strdup("");
                    }
                }
            }
            break;
        }
    }

    // Fall back to symbol table lookup for regular variables
    // Bare ${var} / ${arr} via the unified value view. The view
    // condenses the historical "try array, fall back to scalar" dance
    // into one call. For lists/maps, hand to expand_array_unsubscripted
    // (which enforces SEMANTICS section 3.9 in zsh/lush mode); for
    // scalars, take ownership of the strdup and continue to the
    // set -u check.
    lush_value_view_t view = {0};
    symtable_lookup(expansion, &view);
    if (view.kind == LUSH_VALUE_LIST || view.kind == LUSH_VALUE_MAP) {
        char *result =
            expand_array_unsubscripted(executor, view.array, expansion);
        lush_value_view_clear(&view);
        return result;
    }
    /* Transfer ownership of the scalar strdup out of the view so the
     * subsequent free path stays the same as the legacy code. clear()
     * is now a no-op on the (zeroed) scalar field. */
    char *value = view.scalar_value;
    view.scalar_value = NULL;
    lush_value_view_clear(&view);

    // Check for unset variable error (set -u) for ${var} syntax
    if (!value && shell_opts.unset_error) {
        // Don't error on special variables that have default behavior
        if (strlen(expansion) != 1 ||
            (expansion[0] != '?' && expansion[0] != '$' &&
             expansion[0] != '#' && expansion[0] != '0' &&
             expansion[0] != '@' && expansion[0] != '*' &&
             expansion[0] != '-' && expansion[0] != '!')) {
            // Report structured error for unbound variable
            executor_error_report(executor, SHELL_ERR_UNBOUND_VARIABLE,
                                  executor_current_loc(executor),
                                  "%s: unbound variable", expansion);
            // Set expansion error instead of exiting to allow || constructs
            executor->expansion_error = true;
            executor->expansion_exit_status = 1;
            return strdup(""); // Return empty string for unbound variable
        }
    }

    // value is already strdup'd by symtable_get_var, don't strdup again
    return value ? value : strdup("");
}

/**
 * @brief Expand a variable reference
 *
 * Expands $var and ${var...} syntax. Handles special variables:
 * - $? - last exit status
 * - $$ - shell PID
 * - $# - argument count
 * - $*, $@ - positional parameters
 * - $0-$9 - individual positional parameters
 * - $! - last background PID
 *
 * @param executor Executor context
 * @param var_text Variable text starting with $
 * @return Expanded value (caller must free)
 */
static char *expand_variable(executor_t *executor, const char *var_text) {
    if (!executor || !var_text || var_text[0] != '$') {
        return strdup(var_text ? var_text : "");
    }

    // Special case: if var_text is exactly "$$", treat it as shell PID
    if (strcmp(var_text, "$$") == 0) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", (int)shell_pid);
        return strdup(buffer);
    }

    // Special case: if var_text is exactly "$", treat it as shell PID
    if (strcmp(var_text, "$") == 0) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", (int)shell_pid);
        return strdup(buffer);
    }

    // Special case: if var_text is exactly "$?", treat it as exit status
    if (strcmp(var_text, "$?") == 0) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", last_exit_status);
        return strdup(buffer);
    }

    const char *var_name = var_text + 1;

    // Handle ${var} format with advanced parameter expansion
    if (var_name[0] == '{') {

        /* Find the matching closing brace, not the first one. Nested
         * parameter expansion ${(flag)${INNER}} has an inner `}` that
         * the outer brace match must skip past. strchr would stop at
         * the inner brace and leave the outer expansion truncated.
         * find_closing_brace counts depth and returns the matched
         * close. Issue #98. */
        size_t close_offset = find_closing_brace((char *)var_name);
        char *close = close_offset > 0 ? (char *)(var_name + close_offset)
                                       : strchr(var_name, '}');
        if (close) {
            size_t len = close - var_name - 1;
            char *expansion = malloc(len + 1);
            if (expansion) {
                strncpy(expansion, var_name + 1, len);
                expansion[len] = '\0';

                char *result = parse_parameter_expansion(executor, expansion);

                free(expansion);
                return result;
            }
        }
    } else {
        // Simple $var format - handle special variables and regular variables
        size_t name_len = 0;

        // Check for special single-character variables first
        if (var_name[0] == '?' || var_name[0] == '$' || var_name[0] == '#' ||
            var_name[0] == '*' || var_name[0] == '@' || var_name[0] == '!' ||
            var_name[0] == '-' || (var_name[0] >= '0' && var_name[0] <= '9')) {
            name_len = 1;
        } else {
            // Regular variable names (alphanumeric + underscore)
            while (var_name[name_len] &&
                   (isalnum(var_name[name_len]) || var_name[name_len] == '_')) {
                name_len++;
            }
        }

        /* Zsh bare-subscript form: $var[N] / $var[N,M]. The caller in
         * expand_variables_in_string already consumed the bracket span
         * into var_text when FEATURE_ZSH_BARE_SUBSCRIPT is enabled, so
         * if we see '[' after the name we route through
         * parse_parameter_expansion("var[N]") — same backend as the
         * brace form ${var[N]}. Gating here is a defensive double-check;
         * the primary gate is at the caller. */
        if (name_len > 0 && var_name[name_len] == '[' &&
            shell_mode_allows(FEATURE_ZSH_BARE_SUBSCRIPT)) {
            const char *bracket_end = strchr(var_name + name_len + 1, ']');
            if (bracket_end) {
                size_t total_len = (bracket_end + 1) - var_name;
                char *expansion = malloc(total_len + 1);
                if (expansion) {
                    strncpy(expansion, var_name, total_len);
                    expansion[total_len] = '\0';
                    char *result =
                        parse_parameter_expansion(executor, expansion);
                    free(expansion);
                    return result;
                }
            }
        }

        if (name_len > 0) {
            char *name = malloc(name_len + 1);
            if (name) {
                strncpy(name, var_name, name_len);
                name[name_len] = '\0';

                // Resolve nameref if applicable (max depth 10 to prevent loops)
                const char *resolved_name = name;
                char *resolved_to_free = NULL; // Track if we need to free
                if (symtable_is_nameref(executor->symtable, name)) {
                    const char *target =
                        symtable_resolve_nameref(executor->symtable, name, 10);
                    if (target && target != name) {
                        resolved_name = target;
                        resolved_to_free = (char *)target;
                    }
                }

                // Bare $arr / $var via the unified value view (mirrors
                // the parse_parameter_expansion braced ${arr} site).
                // bash mode gives first element, zsh/lush mode raises a
                // type error in scalar slots; both come out of
                // expand_array_unsubscripted. (Issue #65.)
                lush_value_view_t view = {0};
                symtable_lookup(resolved_name, &view);
                if (view.kind == LUSH_VALUE_LIST ||
                    view.kind == LUSH_VALUE_MAP) {
                    char *result = expand_array_unsubscripted(
                        executor, view.array, resolved_name);
                    lush_value_view_clear(&view);
                    if (resolved_to_free) {
                        free(resolved_to_free);
                    }
                    free(name);
                    /* Caller (expand_variables_in_string) appends any
                     * trailing literal text after the variable name. */
                    return result ? result : strdup("");
                }
                /* Transfer ownership of the scalar out of the view so
                 * the legacy unset / set -u path below stays identical. */
                char *value = view.scalar_value;
                view.scalar_value = NULL;
                lush_value_view_clear(&view);

                // Free resolved nameref if it was allocated
                if (resolved_to_free) {
                    free(resolved_to_free);
                }

                // Check for unset variable error (set -u)
                if (!value && shell_opts.unset_error && name_len > 0) {
                    // Don't error on special variables that have default
                    // behavior
                    if (name_len != 1 ||
                        (name[0] != '?' && name[0] != '$' && name[0] != '#' &&
                         name[0] != '0' && name[0] != '@' && name[0] != '*' &&
                         name[0] != '-' && name[0] != '!')) {
                        // Report structured error for unbound variable
                        executor_error_report(executor,
                                              SHELL_ERR_UNBOUND_VARIABLE,
                                              executor_current_loc(executor),
                                              "%s: unbound variable", name);
                        free(name);
                        // Set expansion error instead of exiting to allow ||
                        // constructs
                        executor->expansion_error = true;
                        executor->expansion_exit_status = 1;
                        return strdup(
                            ""); // Return empty string for unbound variable
                    }
                }

                // If not found in symbol table and it's a special variable,
                // handle it directly
                if (!value && name_len == 1) {
                    char buffer[1024];

                    switch (name[0]) {
                    case '?': // Exit status of last command
                        snprintf(buffer, sizeof(buffer), "%d",
                                 last_exit_status);
                        free(name);
                        return strdup(buffer);

                    case '$': // Shell process ID
                        snprintf(buffer, sizeof(buffer), "%d", (int)shell_pid);
                        free(name);
                        return strdup(buffer);

                    case '#': // Number of positional parameters
                        snprintf(buffer, sizeof(buffer), "%d",
                                 shell_argc > 1 ? shell_argc - 1 : 0);
                        free(name);
                        return strdup(buffer);

                    case '!': // Process ID of last background command
                        if (last_background_pid > 0) {
                            snprintf(buffer, sizeof(buffer), "%d",
                                     (int)last_background_pid);
                            free(name);
                            return strdup(buffer);
                        } else {
                            free(name);
                            return strdup("");
                        }

                    case '*': // All positional parameters as single word
                    {
                        // Check if we're in function scope - try to get $# from
                        // local scope
                        char *func_argc_str =
                            symtable_get_var(executor->symtable, "#");
                        if (func_argc_str && executor->symtable) {
                            // We're in a function scope - use function
                            // parameters
                            int func_argc = atoi(func_argc_str);
                            if (func_argc > 0) {
                                size_t total_len = 0;
                                // Calculate total length needed
                                for (int i = 1; i <= func_argc; i++) {
                                    char param_name[16];
                                    snprintf(param_name, sizeof(param_name),
                                             "%d", i);
                                    char *param_value = symtable_get_var(
                                        executor->symtable, param_name);
                                    if (param_value) {
                                        total_len += strlen(param_value) +
                                                     1; // +1 for space
                                    }
                                }
                                if (total_len > 0) {
                                    char *result = malloc(total_len);
                                    if (result) {
                                        result[0] = '\0';
                                        for (int i = 1; i <= func_argc; i++) {
                                            char param_name[16];
                                            snprintf(param_name,
                                                     sizeof(param_name), "%d",
                                                     i);
                                            char *param_value =
                                                symtable_get_var(
                                                    executor->symtable,
                                                    param_name);
                                            if (param_value) {
                                                if (i > 1) {
                                                    strcat(result, " ");
                                                }
                                                strcat(result, param_value);
                                            }
                                        }
                                        free(name);
                                        return result;
                                    }
                                }
                            }
                            free(name);
                            return strdup("");
                        } else {
                            // Use global shell parameters
                            if (shell_argc > 1) {
                                size_t total_len = 0;
                                for (int i = 1; i < shell_argc; i++) {
                                    if (shell_argv[i]) {
                                        total_len += strlen(shell_argv[i]) +
                                                     1; // +1 for space
                                    }
                                }
                                if (total_len > 0) {
                                    char *result = malloc(total_len);
                                    if (result) {
                                        result[0] = '\0';
                                        for (int i = 1; i < shell_argc; i++) {
                                            if (shell_argv[i]) {
                                                if (i > 1) {
                                                    strcat(result, " ");
                                                }
                                                strcat(result, shell_argv[i]);
                                            }
                                        }
                                        free(name);
                                        return result;
                                    }
                                }
                            }
                            free(name);
                            return strdup("");
                        }
                    }

                    case '@': // All positional parameters as separate words
                    {
                        // Check if we're in function scope - try to get $# from
                        // local scope
                        char *func_argc_str =
                            symtable_get_var(executor->symtable, "#");
                        if (func_argc_str && executor->symtable) {
                            // We're in a function scope - use function
                            // parameters
                            int func_argc = atoi(func_argc_str);
                            if (func_argc > 0) {
                                size_t total_len = 0;
                                // Calculate total length needed
                                for (int i = 1; i <= func_argc; i++) {
                                    char param_name[16];
                                    snprintf(param_name, sizeof(param_name),
                                             "%d", i);
                                    char *param_value = symtable_get_var(
                                        executor->symtable, param_name);
                                    if (param_value) {
                                        total_len += strlen(param_value) +
                                                     1; // +1 for space
                                    }
                                }
                                if (total_len > 0) {
                                    char *result = malloc(total_len);
                                    if (result) {
                                        result[0] = '\0';
                                        for (int i = 1; i <= func_argc; i++) {
                                            char param_name[16];
                                            snprintf(param_name,
                                                     sizeof(param_name), "%d",
                                                     i);
                                            char *param_value =
                                                symtable_get_var(
                                                    executor->symtable,
                                                    param_name);
                                            if (param_value) {
                                                if (i > 1) {
                                                    strcat(result, " ");
                                                }
                                                strcat(result, param_value);
                                            }
                                        }
                                        free(name);
                                        return result;
                                    }
                                }
                            }
                            free(name);
                            return strdup("");
                        } else {
                            // Use global shell parameters
                            // Note: This should ideally preserve word
                            // boundaries, but for now we'll implement it
                            // similarly to $* for compatibility
                            if (shell_argc > 1) {
                                size_t total_len = 0;
                                for (int i = 1; i < shell_argc; i++) {
                                    if (shell_argv[i]) {
                                        total_len += strlen(shell_argv[i]) +
                                                     1; // +1 for space
                                    }
                                }
                                if (total_len > 0) {
                                    char *result = malloc(total_len);
                                    if (result) {
                                        result[0] = '\0';
                                        for (int i = 1; i < shell_argc; i++) {
                                            if (shell_argv[i]) {
                                                if (i > 1) {
                                                    strcat(result, " ");
                                                }
                                                strcat(result, shell_argv[i]);
                                            }
                                        }
                                        free(name);
                                        return result;
                                    }
                                }
                            }
                            free(name);
                            return strdup("");
                        }
                    }

                    case '-': { // Current option flags
                        char flags[32];
                        int fpos = 0;
                        if (is_interactive_shell())
                            flags[fpos++] = 'i';
                        if (shell_opts.job_control)
                            flags[fpos++] = 'm';
                        if (shell_opts.exit_on_error)
                            flags[fpos++] = 'e';
                        if (shell_opts.unset_error)
                            flags[fpos++] = 'u';
                        if (shell_opts.trace_execution)
                            flags[fpos++] = 'x';
                        if (shell_opts.verbose)
                            flags[fpos++] = 'v';
                        if (shell_opts.noclobber)
                            flags[fpos++] = 'C';
                        if (shell_opts.no_globbing)
                            flags[fpos++] = 'f';
                        if (shell_opts.syntax_check)
                            flags[fpos++] = 'n';
                        if (shell_opts.allexport)
                            flags[fpos++] = 'a';
                        if (shell_opts.notify)
                            flags[fpos++] = 'b';
                        if (shell_opts.physical_mode)
                            flags[fpos++] = 'P';
                        if (shell_opts.privileged_mode)
                            flags[fpos++] = 'p';
                        if (shell_opts.history_mode)
                            flags[fpos++] = 'H';
                        if (shell_opts.histexpand_mode)
                            flags[fpos++] = 'B';
                        flags[fpos] = '\0';
                        free(name);
                        return strdup(flags);
                    }

                    default:
                        if (name[0] >= '0' && name[0] <= '9') {
                            // Handle positional parameters $0, $1, $2, etc.
                            int pos = name[0] - '0';

                            if (pos == 0) {
                                // $0 is the script/shell name
                                free(name);
                                return strdup((shell_argc > 0 && shell_argv[0])
                                                  ? shell_argv[0]
                                                  : "lush");
                            } else if (pos > 0 && pos < shell_argc &&
                                       shell_argv[pos]) {
                                // $1, $2, etc. are script arguments
                                free(name);
                                return strdup(shell_argv[pos]);
                            } else {
                                // Parameter doesn't exist, return empty string
                                free(name);
                                return strdup("");
                            }
                        }
                        break;
                    }
                }

                free(name);
                // value is already strdup'd by symtable_get_var
                return value ? value : strdup("");
            }
        }
    }

    return strdup("");
}

/**
 * @brief Expand tilde to home directory
 *
 * Handles ~ (current user) and ~user (specific user) expansion.
 * Falls back to getpwuid if HOME is not set.
 *
 * @param text Text starting with ~
 * @return Expanded path (caller must free)
 */
static char *expand_tilde(const char *text) {
    if (!text || text[0] != '~') {
        return strdup(text ? text : "");
    }

    // Find the end of the tilde expression (until '/' or end of string)
    const char *slash = strchr(text, '/');
    const char *rest = slash ? slash : "";
    size_t tilde_len = slash ? (size_t)(slash - text) : strlen(text);

    if (tilde_len == 1) {
        // Simple ~ expansion to $HOME
        const char *home = getenv("HOME");
        if (!home) {
            // Fallback if HOME is not set
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/";
        }

        if (strlen(rest) == 0) {
            return strdup(home);
        } else {
            size_t result_len = strlen(home) + strlen(rest) + 1;
            char *result = malloc(result_len);
            if (result) {
                strcpy(result, home);
                strcat(result, rest);
            }
            return result;
        }
    } else {
        // ~user expansion to user's home directory
        char *username = malloc(tilde_len);
        if (!username) {
            return strdup(text);
        }

        strncpy(username, text + 1, tilde_len - 1);
        username[tilde_len - 1] = '\0';

        struct passwd *pw = getpwnam(username);
        free(username);

        if (!pw) {
            // User not found, return original text
            return strdup(text);
        }

        if (strlen(rest) == 0) {
            return strdup(pw->pw_dir);
        } else {
            size_t result_len = strlen(pw->pw_dir) + strlen(rest) + 1;
            char *result = malloc(result_len);
            if (result) {
                strcpy(result, pw->pw_dir);
                strcat(result, rest);
            }
            return result;
        }
    }
}

/**
 * @brief Expand arithmetic expression $((...))
 *
 * Evaluates arithmetic expressions and returns the result as a string.
 * Sets expansion error flags on evaluation errors like division by zero.
 *
 * @param executor Executor context for variable lookup
 * @param arith_text Arithmetic expression text
 * @return Result as string (caller must free), "0" on error
 */
static char *expand_arithmetic(executor_t *executor, const char *arith_text) {
    if (!executor || !arith_text) {
        return strdup("0");
    }

    // Use the modern arithmetic evaluator with executor context for scoped
    // variables
    char *result = arithm_expand_with_executor(executor, arith_text);
    if (result) {
        return result;
    }

    /* Drain the typed error state from arithmetic.c and emit a fully
     * structured shell error: specific code (one per failure mode rather
     * than the old blanket SHELL_ERR_ARITHMETIC_SYNTAX), site-specific
     * `while:` context, and site-specific `help:` suggestion. The
     * arithmetic module owns the error semantics; the executor owns
     * displaying them.
     */
    if (arithm_error_is_flagged()) {
        const char *msg = arithm_error_message();
        const char *while_ctx = arithm_error_while();
        const char *help = arithm_error_help();
        shell_error_t *err =
            shell_error_create(arithm_error_code(), SHELL_SEVERITY_ERROR,
                               executor_current_loc(executor), "arithmetic: %s",
                               msg ? msg : "evaluation error");
        if (err) {
            if (while_ctx) {
                shell_error_push_context(err, "%s", while_ctx);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(err, "%s",
                                             executor->context_stack[i]);
                }
            }
            if (help) {
                shell_error_set_suggestion(err, help);
            }
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
            executor->has_error = true;
            executor->error_message = NULL;
        } else {
            // Fallback if shell_error_create failed (e.g. OOM)
            executor_error_report(
                executor, arithm_error_code(), executor_current_loc(executor),
                "arithmetic: %s", msg ? msg : "evaluation error");
        }
    } else {
        executor_error_report(executor, SHELL_ERR_ARITHMETIC_SYNTAX,
                              executor_current_loc(executor),
                              "arithmetic: evaluation error");
    }

    // Set expansion error flag instead of immediate exit status
    executor->expansion_error = true;
    executor->expansion_exit_status = 1;
    return strdup("");
}

/**
 * @brief Expand command substitution $(...) or `...`
 *
 * Forks a child process to execute the command and captures its stdout.
 * Trailing newlines are stripped from the output. Uses the shell's own
 * parser/executor to preserve function definitions.
 *
 * @param executor Executor context
 * @param cmd_text Command text in $(...) or `...` format
 * @return Command output (caller must free)
 */
static char *expand_command_substitution(executor_t *executor,
                                         const char *cmd_text) {
    if (!executor || !cmd_text) {
        return strdup("");
    }

    // Extract command from $(command) or `command` format
    char *command = NULL;
    if (strncmp(cmd_text, "$(", 2) == 0 &&
        cmd_text[strlen(cmd_text) - 1] == ')') {
        // Extract from $(command)
        size_t len = strlen(cmd_text) - 3; // Remove $( and )
        command = malloc(len + 1);
        if (!command) {
            return strdup("");
        }
        strncpy(command, cmd_text + 2, len);
        command[len] = '\0';
    } else if (cmd_text[0] == '`' && cmd_text[strlen(cmd_text) - 1] == '`') {
        // Extract from `command`
        size_t len = strlen(cmd_text) - 2; // Remove backticks
        command = malloc(len + 1);
        if (!command) {
            return strdup("");
        }
        strncpy(command, cmd_text + 1, len);
        command[len] = '\0';
    } else {
        // Already extracted command
        command = strdup(cmd_text);
        if (!command) {
            return strdup("");
        }
    }

    /* Pre-fork variable expansion of the command text was removed in
     * the #97 fix: it collapsed array values ("${!arr[@]}", "${arr[@]}")
     * into space-joined scalars before the child parser ever saw them,
     * which destroyed the per-element word boundaries the child would
     * otherwise have honored via the vector-expansion path in
     * build_argv_from_ast. The child inherits parent state through
     * fork() (full memory copy, including non-exported locals), so it
     * can parse and expand the raw command text natively -- which is
     * also what bash/dash/zsh do for $(...) -- and produce correctly
     * separated arguments. Pre-expansion was an architectural layering
     * violation that masked array semantics. */

    // Create a pipe to capture command output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        free(command);
        return strdup("");
    }

    pid_t pid = lush_fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        free(command);
        return strdup("");
    }

    if (pid == 0) {
        // Child process - execute command using lush's own parser/executor
        close(pipefd[0]);               // Close read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]);

        // Parse and execute command using lush's own parser/executor
        // This preserves all function definitions and variables in the child
        const char *src_name = executor->current_script_file
                                   ? executor->current_script_file
                                   : "<command substitution>";
        /* Command substitution context: input is its own logical
         * source slice, line 1 of that slice. */
        parser_t *parser = parser_new_with_source(command, src_name, 1);
        int result = 127;

        if (parser) {
            node_t *ast = parser_parse(parser);
            if (!parser_has_error(parser) && ast) {
                // Execute in current context (functions are inherited via fork)
                // Use executor_execute to handle command sequences
                // (next_sibling)
                result = executor_execute(executor, ast);
                free_node_tree(ast);
            }
            parser_free(parser);
        }

        // Ensure all output is flushed before exit
        fflush(stdout);
        free(command);
        subshell_cleanup();
        _exit(result);
    } else {
        // Parent process - read output
        close(pipefd[1]); // Close write end
        free(command);

        char *output = malloc(1024);
        size_t output_size = 1024;
        size_t output_len = 0;

        if (!output) {
            close(pipefd[0]);
            while (waitpid(pid, NULL, 0) == -1 && errno == EINTR)
                ;
            return strdup("");
        }

        ssize_t bytes_read;
        char buffer[256];

        // Wait for child process to complete first, retrying on EINTR
        int status;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR)
            ;

        // Propagate child's exit status to executor for $? access
        if (WIFEXITED(status)) {
            executor->exit_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            executor->exit_status = 128 + WTERMSIG(status);
        }

        // Then read all available output
        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            if (output_len + bytes_read >= output_size) {
                output_size *= 2;
                char *new_output = realloc(output, output_size);
                if (!new_output) {
                    free(output);
                    close(pipefd[0]);
                    return strdup("");
                }
                output = new_output;
            }
            memcpy(output + output_len, buffer, bytes_read);
            output_len += bytes_read;
        }

        close(pipefd[0]);

        // Null-terminate the output buffer before string operations
        if (output_len >= output_size) {
            char *new_output = realloc(output, output_size + 1);
            if (new_output) {
                output = new_output;
            }
        }
        output[output_len] = '\0';

        // Null terminate and remove trailing newlines
        output[output_len] = '\0';
        while (output_len > 0 && (output[output_len - 1] == '\n' ||
                                  output[output_len - 1] == '\r')) {
            output[--output_len] = '\0';
        }

        return output;
    }
}

/**
 * @brief Copy function definitions between executors
 *
 * Copies all function definitions from source to destination executor.
 * Used when creating child executors that need access to parent functions.
 *
 * @param dest Destination executor
 * @param src Source executor
 */
MAYBE_UNUSED
static void copy_function_definitions(executor_t *dest, executor_t *src) {
    if (!dest || !src) {
        return;
    }

    function_def_t *src_func = src->functions;
    while (src_func) {
        // Create a copy of the function definition
        function_def_t *new_func = malloc(sizeof(function_def_t));
        if (!new_func) {
            break;
        }

        new_func->name = strdup(src_func->name);
        if (!new_func->name) {
            free(new_func);
            break;
        }

        // Create a simple copy of the function body AST
        new_func->body = copy_node_simple(src_func->body);
        if (!new_func->body) {
            free(new_func->name);
            free(new_func);
            break;
        }

        // Add to destination's function list
        new_func->next = dest->functions;
        dest->functions = new_func;

        src_func = src_func->next;
    }
}

/**
 * @brief Simple recursive node copy
 *
 * Creates a copy of an AST node and its children. Simpler than
 * copy_ast_node, used for function definition copying.
 *
 * @param original Node to copy
 * @return Copy of node tree, or NULL on failure
 */
static node_t *copy_node_simple(node_t *original) {
    if (!original) {
        return NULL;
    }

    node_t *copy = new_node(original->type);
    if (!copy) {
        return NULL;
    }

    copy->val_type = original->val_type;
    copy->val = original->val;

    // If the node has a string value, copy it
    if (original->val_type == VAL_STR && original->val.str) {
        copy->val.str = strdup(original->val.str);
        if (!copy->val.str) {
            free_node_tree(copy);
            return NULL;
        }
    }

    // Copy children recursively
    node_t *child = original->first_child;
    while (child) {
        node_t *child_copy = copy_node_simple(child);
        if (!child_copy) {
            free_node_tree(copy);
            return NULL;
        }
        add_child_node(copy, child_copy);
        child = child->next_sibling;
    }

    return copy;
}

/**
 * @brief Expand ANSI-C escape sequences in $'...' strings
 *
 * Handles escape sequences like \n, \t, \xNN, \uNNNN, \UNNNNNNNN, etc.
 * This is the Bash/Zsh $'...' quoting mechanism.
 *
 * @param str Content between the quotes (without $' and ')
 * @param len Length of the content
 * @return Expanded string (caller must free)
 */
static char *expand_ansi_c_string(const char *str, size_t len) {
    if (!str || len == 0) {
        return strdup("");
    }

    // Allocate buffer (escape sequences usually shrink the string)
    size_t buffer_size = len * 4 + 1; // Extra space for Unicode expansion
    char *result = malloc(buffer_size);
    if (!result) {
        return strdup("");
    }

    size_t result_pos = 0;
    size_t i = 0;

    while (i < len) {
        if (str[i] == '\\' && i + 1 < len) {
            char next = str[i + 1];
            switch (next) {
            case 'a': // Alert (bell)
                result[result_pos++] = '\a';
                i += 2;
                break;
            case 'b': // Backspace
                result[result_pos++] = '\b';
                i += 2;
                break;
            case 'e': // Escape character
            case 'E':
                result[result_pos++] = '\033';
                i += 2;
                break;
            case 'f': // Form feed
                result[result_pos++] = '\f';
                i += 2;
                break;
            case 'n': // Newline
                result[result_pos++] = '\n';
                i += 2;
                break;
            case 'r': // Carriage return
                result[result_pos++] = '\r';
                i += 2;
                break;
            case 't': // Horizontal tab
                result[result_pos++] = '\t';
                i += 2;
                break;
            case 'v': // Vertical tab
                result[result_pos++] = '\v';
                i += 2;
                break;
            case '\\': // Backslash
                result[result_pos++] = '\\';
                i += 2;
                break;
            case '\'': // Single quote
                result[result_pos++] = '\'';
                i += 2;
                break;
            case '"': // Double quote
                result[result_pos++] = '"';
                i += 2;
                break;
            case '?': // Question mark
                result[result_pos++] = '?';
                i += 2;
                break;
            case 'x': // Hex escape \xNN
                if (i + 3 < len && isxdigit((unsigned char)str[i + 2])) {
                    char hex[3] = {0};
                    int hex_len = 0;
                    // Read up to 2 hex digits
                    for (int j = 0; j < 2 && i + 2 + j < len; j++) {
                        if (isxdigit((unsigned char)str[i + 2 + j])) {
                            hex[hex_len++] = str[i + 2 + j];
                        } else {
                            break;
                        }
                    }
                    if (hex_len > 0) {
                        unsigned int val = (unsigned int)strtoul(hex, NULL, 16);
                        result[result_pos++] = (char)val;
                        i += 2 + hex_len;
                    } else {
                        result[result_pos++] = str[i++];
                    }
                } else {
                    result[result_pos++] = str[i++];
                }
                break;
            case 'u': // Unicode escape \uNNNN (4 hex digits)
                if (i + 5 < len) {
                    char hex[5] = {0};
                    int hex_len = 0;
                    for (int j = 0; j < 4 && i + 2 + j < len; j++) {
                        if (isxdigit((unsigned char)str[i + 2 + j])) {
                            hex[hex_len++] = str[i + 2 + j];
                        } else {
                            break;
                        }
                    }
                    if (hex_len == 4) {
                        uint32_t codepoint = (uint32_t)strtoul(hex, NULL, 16);
                        // Encode as UTF-8
                        if (codepoint < 0x80) {
                            result[result_pos++] = (char)codepoint;
                        } else if (codepoint < 0x800) {
                            result[result_pos++] =
                                (char)(0xC0 | (codepoint >> 6));
                            result[result_pos++] =
                                (char)(0x80 | (codepoint & 0x3F));
                        } else {
                            result[result_pos++] =
                                (char)(0xE0 | (codepoint >> 12));
                            result[result_pos++] =
                                (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            result[result_pos++] =
                                (char)(0x80 | (codepoint & 0x3F));
                        }
                        i += 2 + hex_len;
                    } else {
                        result[result_pos++] = str[i++];
                    }
                } else {
                    result[result_pos++] = str[i++];
                }
                break;
            case 'U': // Unicode escape \UNNNNNNNN (8 hex digits)
                if (i + 9 < len) {
                    char hex[9] = {0};
                    int hex_len = 0;
                    for (int j = 0; j < 8 && i + 2 + j < len; j++) {
                        if (isxdigit((unsigned char)str[i + 2 + j])) {
                            hex[hex_len++] = str[i + 2 + j];
                        } else {
                            break;
                        }
                    }
                    if (hex_len == 8) {
                        uint32_t codepoint = (uint32_t)strtoul(hex, NULL, 16);
                        // Encode as UTF-8
                        if (codepoint < 0x80) {
                            result[result_pos++] = (char)codepoint;
                        } else if (codepoint < 0x800) {
                            result[result_pos++] =
                                (char)(0xC0 | (codepoint >> 6));
                            result[result_pos++] =
                                (char)(0x80 | (codepoint & 0x3F));
                        } else if (codepoint < 0x10000) {
                            result[result_pos++] =
                                (char)(0xE0 | (codepoint >> 12));
                            result[result_pos++] =
                                (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            result[result_pos++] =
                                (char)(0x80 | (codepoint & 0x3F));
                        } else if (codepoint <= 0x10FFFF) {
                            result[result_pos++] =
                                (char)(0xF0 | (codepoint >> 18));
                            result[result_pos++] =
                                (char)(0x80 | ((codepoint >> 12) & 0x3F));
                            result[result_pos++] =
                                (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            result[result_pos++] =
                                (char)(0x80 | (codepoint & 0x3F));
                        }
                        i += 2 + hex_len;
                    } else {
                        result[result_pos++] = str[i++];
                    }
                } else {
                    result[result_pos++] = str[i++];
                }
                break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7': // Octal escape \NNN
            {
                char octal[4] = {0};
                int octal_len = 0;
                for (int j = 0; j < 3 && i + 1 + j < len; j++) {
                    char c = str[i + 1 + j];
                    if (c >= '0' && c <= '7') {
                        octal[octal_len++] = c;
                    } else {
                        break;
                    }
                }
                if (octal_len > 0) {
                    unsigned int val = (unsigned int)strtoul(octal, NULL, 8);
                    result[result_pos++] = (char)(val & 0xFF);
                    i += 1 + octal_len;
                } else {
                    result[result_pos++] = str[i++];
                }
                break;
            }
            case 'c': // Control character \cX
                if (i + 2 < len) {
                    char ctrl = str[i + 2];
                    if (ctrl >= '@' && ctrl <= '_') {
                        result[result_pos++] = (char)(ctrl - '@');
                    } else if (ctrl >= 'a' && ctrl <= 'z') {
                        result[result_pos++] = (char)(ctrl - 'a' + 1);
                    } else if (ctrl == '?') {
                        result[result_pos++] = 127; // DEL
                    } else {
                        result[result_pos++] = str[i++];
                        break;
                    }
                    i += 3;
                } else {
                    result[result_pos++] = str[i++];
                }
                break;
            default:
                // Unknown escape - keep the backslash and character
                result[result_pos++] = str[i++];
                break;
            }
        } else {
            result[result_pos++] = str[i++];
        }

        // Ensure buffer has space
        if (result_pos >= buffer_size - 4) {
            buffer_size *= 2;
            char *new_result = realloc(result, buffer_size);
            if (!new_result) {
                free(result);
                return strdup("");
            }
            result = new_result;
        }
    }

    result[result_pos] = '\0';
    return result;
}

/**
 * @brief Expand variables within double-quoted strings
 *
 * Handles variable expansion, command substitution, arithmetic
 * expansion, and escape sequences within double quotes. Preserves
 * literal text and handles backslash escapes for $, `, ", and \.
 *
 * @param executor Executor context
 * @param str Double-quoted string content
 * @return Expanded string (caller must free)
 */
static char *expand_quoted_string(executor_t *executor, const char *str,
                                  bool in_double_quotes) {
    if (!executor || !str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    // Allocate a buffer for expansion (estimate double the original size)
    size_t buffer_size = len * 2 + 256;
    char *result = malloc(buffer_size);
    if (!result) {
        return strdup("");
    }

    size_t result_pos = 0;
    size_t i = 0;

    while (i < len) {
        /* Kind sigil inside a double-quoted string: `"@x"` and `"%x"` must
         * produce the same result as bare `@x` / `%x` per the §3.6 rule
         * that quoting is irrelevant to presentation.  The check mirrors the
         * tokenizer: sigil at this slot AND followed by a valid identifier
         * AND FEATURE_KIND_SIGILS is enabled.  Splice the expanded text into
         * the result buffer and advance past the consumed span. */
        if ((str[i] == '@' || str[i] == '%') && i + 1 < len &&
            shell_mode_allows(FEATURE_KIND_SIGILS) &&
            (isalpha((unsigned char)str[i + 1]) || str[i + 1] == '_')) {
            size_t sigil_end = i + 1;
            while (sigil_end < len && (isalnum((unsigned char)str[sigil_end]) ||
                                       str[sigil_end] == '_')) {
                sigil_end++;
            }
            size_t span = sigil_end - i;
            char *sigil_buf = malloc(span + 1);
            if (sigil_buf) {
                memcpy(sigil_buf, str + i, span);
                sigil_buf[span] = '\0';
                char *expanded = expand_kind_sigil(executor, sigil_buf);
                free(sigil_buf);
                if (expanded) {
                    size_t el = strlen(expanded);
                    if (result_pos + el + 1 > buffer_size) {
                        buffer_size = (result_pos + el + 1) * 2;
                        char *nb = realloc(result, buffer_size);
                        if (nb) {
                            result = nb;
                        }
                    }
                    memcpy(result + result_pos, expanded, el);
                    result_pos += el;
                    free(expanded);
                }
            }
            i = sigil_end;
            continue;
        }

        if (str[i] == '$' && i + 1 < len) {
            // NOTE: ANSI-C quoting $'...' is NOT expanded inside double quotes
            // per POSIX/bash behavior. It's only recognized at the outer level.
            // So we skip the $' check here and treat it as a literal $.

            // Check for arithmetic expansion $((...))
            if (str[i + 1] == '(' && i + 2 < len && str[i + 2] == '(') {
                /* $(( disambiguation: arithmetic vs command-sub of
                 * anonymous function. Same shape as tokenizer +
                 * expand_variables_in_string + expand_if_needed
                 * (issue #99). */
                bool qs_looks_arith = true;
                {
                    size_t s = i + 3;
                    int d = 2;
                    while (s < len && d > 0) {
                        char sc = str[s];
                        if (sc == '(') {
                            d++;
                        } else if (sc == ')') {
                            d--;
                            if (d == 0) {
                                break;
                            }
                        } else if (sc == '{' || sc == '}' || sc == ';' ||
                                   sc == '\n') {
                            qs_looks_arith = false;
                            break;
                        }
                        s++;
                    }
                }
                if (!qs_looks_arith) {
                    /* Fall through to the $(...) command-sub handler
                     * later in this function -- which is exactly the
                     * else-if test on str[i+1] == '(' that doesn't
                     * require str[i+2] == '('. To avoid restructuring
                     * the giant conditional chain, mark this branch
                     * as "not arithmetic" by setting paren_depth so
                     * the post-check fails through. Simplest path:
                     * just skip and let the next branch handle it. */
                    goto qs_try_cmd_sub;
                }
                // This is arithmetic expansion $((expr))
                size_t arith_start = i;
                size_t arith_end = i + 3;
                int paren_depth = 2;

                while (arith_end < len && paren_depth > 0) {
                    if (str[arith_end] == '(') {
                        paren_depth++;
                    } else if (str[arith_end] == ')') {
                        paren_depth--;
                    }
                    arith_end++;
                }

                if (paren_depth == 0) {
                    // Extract arithmetic expression including $(( and ))
                    size_t full_arith_len = arith_end - arith_start;
                    char *full_arith_expr = malloc(full_arith_len + 1);
                    if (full_arith_expr) {
                        strncpy(full_arith_expr, &str[arith_start],
                                full_arith_len);
                        full_arith_expr[full_arith_len] = '\0';

                        // Expand arithmetic expression
                        char *arith_result =
                            expand_arithmetic(executor, full_arith_expr);
                        if (arith_result) {
                            size_t result_len = strlen(arith_result);
                            // Ensure buffer is large enough
                            while (result_pos + result_len >= buffer_size) {
                                buffer_size *= 2;
                                char *new_result = realloc(result, buffer_size);
                                if (!new_result) {
                                    free(result);
                                    free(arith_result);
                                    free(full_arith_expr);
                                    return strdup("");
                                }
                                result = new_result;
                            }

                            // Copy arithmetic result
                            strcpy(&result[result_pos], arith_result);
                            result_pos += result_len;
                            free(arith_result);
                        }

                        free(full_arith_expr);
                        i = arith_end; // Skip past the closing ))
                        continue;
                    }
                }
            }
            // Check for command substitution $(...)
            else if (str[i + 1] == '(') {
            qs_try_cmd_sub:;
                // Use the robust find_closing_brace function to handle nested
                // quotes
                size_t cmd_start = i;

                // Create a temporary string starting from the '(' to use with
                // find_closing_brace
                char *temp_str =
                    (char *)&str[i + 1]; // Start from the opening parenthesis
                size_t brace_offset = find_closing_brace(temp_str);

                if (brace_offset > 0) {
                    // Found matching closing parenthesis
                    size_t cmd_end =
                        i + 1 + brace_offset; // Points to the closing paren

                    // Extract command substitution including $( and )
                    size_t full_cmd_len = cmd_end - cmd_start + 1;
                    char *full_cmd_expr = malloc(full_cmd_len + 1);
                    if (full_cmd_expr) {
                        strncpy(full_cmd_expr, &str[cmd_start], full_cmd_len);
                        full_cmd_expr[full_cmd_len] = '\0';

                        // Expand command substitution
                        char *cmd_result = expand_command_substitution(
                            executor, full_cmd_expr);
                        if (cmd_result) {
                            size_t result_len = strlen(cmd_result);
                            // Ensure buffer is large enough
                            while (result_pos + result_len >= buffer_size) {
                                buffer_size *= 2;
                                char *new_result = realloc(result, buffer_size);
                                if (!new_result) {
                                    free(result);
                                    free(cmd_result);
                                    free(full_cmd_expr);
                                    return strdup("");
                                }
                                result = new_result;
                            }

                            // Copy command result
                            strcpy(&result[result_pos], cmd_result);
                            result_pos += result_len;
                            free(cmd_result);
                        }

                        free(full_cmd_expr);
                        i = cmd_end + 1; // Skip past the closing )
                        continue;
                    }
                }
            }

            // Variable expansion needed
            size_t var_start = i + 1;
            size_t var_end = var_start;

            // Handle ${var} format
            if (str[var_start] == '{') {

                // Use proper brace matching for nested expressions
                int brace_count = 1;
                var_end = var_start + 1; // Start after opening {

                while (var_end < len && brace_count > 0) {
                    if (str[var_end] == '{') {
                        brace_count++;
                    } else if (str[var_end] == '}') {
                        brace_count--;
                    }
                    var_end++;
                }

                if (brace_count == 0) {
                    var_start++; // Skip opening brace for variable name
                                 // extraction
                    var_end--;   // Point to closing brace
                    // Extract variable name
                    size_t var_name_len = var_end - var_start;
                    char *var_name = malloc(var_name_len + 1);
                    if (var_name) {
                        strncpy(var_name, &str[var_start], var_name_len);
                        var_name[var_name_len] = '\0';
                        // Use parameter expansion to handle operators like =,
                        // :-, etc.
                        char *var_value =
                            parse_parameter_expansion(executor, var_name);

                        if (var_value) {
                            size_t value_len = strlen(var_value);
                            // Ensure buffer is large enough
                            while (result_pos + value_len >= buffer_size) {
                                buffer_size *= 2;
                                char *new_result = realloc(result, buffer_size);
                                if (!new_result) {
                                    free(result);
                                    free(var_value);
                                    free(var_name);
                                    return strdup("");
                                }
                                result = new_result;
                            }
                            strcpy(&result[result_pos], var_value);
                            result_pos += value_len;
                            free(var_value);
                        }

                        free(var_name);
                        i = var_end + 1; // Skip past closing brace
                    } else {
                        result[result_pos++] = str[i++];
                    }
                } else {
                    result[result_pos++] = str[i++];
                }
            } else {
                // Simple $var format - handle special variables and regular
                // variables
                size_t var_name_len = 0;

                // Check for special single-character variables first
                if (str[var_start] == '?' || str[var_start] == '$' ||
                    str[var_start] == '#' || str[var_start] == '*' ||
                    str[var_start] == '@' || str[var_start] == '!' ||
                    str[var_start] == '-' ||
                    (str[var_start] >= '0' && str[var_start] <= '9')) {
                    var_name_len = 1;
                } else {
                    // Regular variable names (alphanumeric + underscore)
                    while (var_start + var_name_len < len &&
                           (isalnum(str[var_start + var_name_len]) ||
                            str[var_start + var_name_len] == '_')) {
                        var_name_len++;
                    }
                    /* Zsh bare-subscript form: $var[N] / $var[N,M] inside
                     * a double-quoted string. Extend var_name_len through
                     * the bracket span so we pass "$var[N]" to
                     * expand_variable, which routes it through
                     * parse_parameter_expansion. Gated on
                     * FEATURE_ZSH_BARE_SUBSCRIPT — bash mode keeps the
                     * literal-[N]-after-$var semantic. Mirrors the
                     * unquoted path in expand_variables_in_string. */
                    if (var_name_len > 0 && var_start + var_name_len < len &&
                        str[var_start + var_name_len] == '[' &&
                        shell_mode_allows(FEATURE_ZSH_BARE_SUBSCRIPT)) {
                        size_t scan = var_start + var_name_len + 1;
                        while (scan < len && str[scan] != ']') {
                            scan++;
                        }
                        if (scan < len && str[scan] == ']') {
                            var_name_len = (scan + 1) - var_start;
                        }
                    }
                }

                if (var_name_len > 0) {
                    // Create variable expression for expansion
                    char *var_expr = malloc(
                        var_name_len + 2); // +2 for '$' and null terminator
                    if (var_expr) {
                        var_expr[0] = '$';
                        strncpy(&var_expr[1], &str[var_start], var_name_len);
                        var_expr[var_name_len + 1] = '\0';

                        // Use the main variable expansion function
                        char *var_value = expand_variable(executor, var_expr);
                        if (var_value) {
                            size_t value_len = strlen(var_value);
                            // Ensure buffer is large enough
                            while (result_pos + value_len >= buffer_size) {
                                buffer_size *= 2;
                                char *new_result = realloc(result, buffer_size);
                                if (!new_result) {
                                    free(result);
                                    free(var_value);
                                    free(var_expr);
                                    return strdup("");
                                }
                                result = new_result;
                            }
                            strcpy(&result[result_pos], var_value);
                            result_pos += value_len;
                            free(var_value);
                        }

                        free(var_expr);
                        i = var_start + var_name_len;
                    } else {
                        result[result_pos++] = str[i++];
                    }
                } else {
                    result[result_pos++] = str[i++];
                }
            }
        } else if (str[i] == '`') {
            // Handle backtick command substitution
            size_t cmd_start = i;
            size_t cmd_end = i + 1;

            // Find closing backtick
            while (cmd_end < len && str[cmd_end] != '`') {
                if (str[cmd_end] == '\\' && cmd_end + 1 < len) {
                    cmd_end += 2; // Skip escaped character
                } else {
                    cmd_end++;
                }
            }

            if (cmd_end < len && str[cmd_end] == '`') {
                // Found matching closing backtick
                size_t full_cmd_len = cmd_end - cmd_start + 1;
                char *full_cmd_expr = malloc(full_cmd_len + 1);
                if (full_cmd_expr) {
                    strncpy(full_cmd_expr, &str[cmd_start], full_cmd_len);
                    full_cmd_expr[full_cmd_len] = '\0';

                    // Expand command substitution
                    char *cmd_result =
                        expand_command_substitution(executor, full_cmd_expr);
                    if (cmd_result) {
                        size_t result_len = strlen(cmd_result);
                        // Ensure buffer is large enough
                        while (result_pos + result_len >= buffer_size) {
                            buffer_size *= 2;
                            char *new_result = realloc(result, buffer_size);
                            if (!new_result) {
                                free(result);
                                free(cmd_result);
                                free(full_cmd_expr);
                                return strdup("");
                            }
                            result = new_result;
                        }

                        // Copy command result
                        strcpy(&result[result_pos], cmd_result);
                        result_pos += result_len;
                        free(cmd_result);
                    }

                    free(full_cmd_expr);
                    i = cmd_end + 1; // Skip past the closing backtick
                    continue;
                }
            }

            // If we get here, no matching backtick found, treat as literal
            result[result_pos++] = str[i++];
        } else if (str[i] == '\\' && i + 1 < len) {
            /* Two escape regimes share this loop:
             *  in_double_quotes=true  -- POSIX double-quote: only \\, \",
             *      \$, \` are meaningful; all other `\X` is kept literally
             *      as `\X` so the consumer (e.g. echo with XPG escape
             *      interp) can still process it.
             *  in_double_quotes=false -- POSIX unquoted: any `\X` (other
             *      than `\<newline>` already eaten by the tokenizer)
             *      collapses to literal X, including suppressing the
             *      special meaning of `$` and `` ` `` so that `\$VAR`
             *      yields literal `$VAR` with no parameter expansion.
             *      Single-pass interleaving with variable expansion
             *      relies on emitting the literal byte and skipping past
             *      it, so we never re-enter the var-scan on the escaped
             *      character. */
            char next_char = str[i + 1];
            bool is_dq_meta = (next_char == '\\' || next_char == '"' ||
                               next_char == '$' || next_char == '`');

            if (!in_double_quotes || is_dq_meta) {
                if (result_pos >= buffer_size - 1) {
                    buffer_size *= 2;
                    result = realloc(result, buffer_size);
                    if (!result) {
                        return strdup("");
                    }
                }
                result[result_pos++] = next_char;
                i += 2;
            } else {
                // DQ + non-meta: keep the backslash and char literally.
                if (result_pos >= buffer_size - 2) {
                    buffer_size *= 2;
                    result = realloc(result, buffer_size);
                    if (!result) {
                        return strdup("");
                    }
                }
                result[result_pos++] = '\\';
                result[result_pos++] = next_char;
                i += 2;
            }
        } else {
            // Regular character
            if (result_pos >= buffer_size - 1) {
                buffer_size *= 2;
                result = realloc(result, buffer_size);
                if (!result) {
                    return strdup("");
                }
            }
            result[result_pos++] = str[i++];
        }
    }

    result[result_pos] = '\0';
    return result;
}
/* ========== JOB CONTROL IMPLEMENTATION ========== */

#include "executor.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @brief Initialize job control in executor
 *
 * Sets up job control data structures and records the shell's
 * process group ID.
 *
 * @param executor Executor context to initialize
 */
static void initialize_job_control(executor_t *executor) {
    if (!executor) {
        return;
    }

    executor->jobs = NULL;
    executor->next_job_id = 1;

#ifdef LUSH_FUZZ_SANDBOX
    /* Fuzz harness must not perform tty job-control operations.
     * executor_new() runs per fuzz iteration; tcgetpgrp / tcsetpgrp /
     * kill(-pgid, SIGTTIN) inside this function send signals to the
     * fuzzer's process group every iteration when stdin is a TTY,
     * causing eventual SIGABRT after many iterations as accumulated
     * signal state corrupts the process. The fuzz binary never needs
     * to take terminal control — it does not run external commands
     * (LUSH_FUZZ_SANDBOX makes lush_fork() return -1) and has no
     * interactive prompt. (Issue #75.) */
    executor->shell_pgid = getpgrp();
    return;
#endif

    // For interactive login shells, take control of the terminal
    if (isatty(STDIN_FILENO)) {
        // Wait until we're in the foreground
        while (tcgetpgrp(STDIN_FILENO) != (executor->shell_pgid = getpgrp())) {
            kill(-executor->shell_pgid, SIGTTIN);
        }

        // Put ourselves in our own process group
        executor->shell_pgid = getpid();
        if (setpgid(0, executor->shell_pgid) < 0) {
            // Not fatal - we may already be a process group leader
            executor->shell_pgid = getpgrp();
        }

        // Grab control of the terminal
        tcsetpgrp(STDIN_FILENO, executor->shell_pgid);

        // Ignore interactive and job-control signals in the shell process
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
    } else {
        executor->shell_pgid = getpgrp();
    }
}

/**
 * @brief Create a new process structure
 *
 * Allocates and initializes a process entry for job tracking.
 *
 * @param pid Process ID
 * @param command Command string (will be copied)
 * @return New process structure, or NULL on failure
 */
MAYBE_UNUSED
static process_t *create_process(pid_t pid, const char *command) {
    process_t *proc = malloc(sizeof(process_t));
    if (!proc) {
        return NULL;
    }

    proc->pid = pid;
    proc->command = command ? strdup(command) : NULL;
    proc->status = 0;
    proc->next = NULL;

    return proc;
}

/**
 * @brief Free a linked list of processes
 *
 * @param processes Head of process list to free
 */
static void free_process_list(process_t *processes) {
    while (processes) {
        process_t *next = processes->next;
        free(processes->command);
        free(processes);
        processes = next;
    }
}

/**
 * @brief Add a new job to the job list
 *
 * Creates a job entry for background process tracking.
 * Assigns a unique job ID and adds to the executor's job list.
 *
 * @param executor Executor context
 * @param pgid Process group ID of the job
 * @param command_line Command line for display
 * @return New job structure, or NULL on failure
 */
job_t *executor_add_job(executor_t *executor, pid_t pgid,
                        const char *command_line) {
    if (!executor) {
        return NULL;
    }

    job_t *job = malloc(sizeof(job_t));
    if (!job) {
        return NULL;
    }

    job->job_id = executor->next_job_id++;
    job->pgid = pgid;
    job->state = JOB_RUNNING;
    job->foreground = false;
    job->processes = NULL;
    job->command_line = command_line ? strdup(command_line) : NULL;
    job->next = executor->jobs;

    executor->jobs = job;
    return job;
}

/**
 * @brief Find a job by its ID
 *
 * @param executor Executor context
 * @param job_id Job ID to search for
 * @return Job structure, or NULL if not found
 */
job_t *executor_find_job(executor_t *executor, int job_id) {
    if (!executor) {
        return NULL;
    }

    job_t *job = executor->jobs;
    while (job) {
        if (job->job_id == job_id) {
            return job;
        }
        job = job->next;
    }
    return NULL;
}

/**
 * @brief Remove a job from the job list
 *
 * Removes and frees the job with the specified ID.
 *
 * @param executor Executor context
 * @param job_id Job ID to remove
 */
void executor_remove_job(executor_t *executor, int job_id) {
    if (!executor || !executor->jobs) {
        return;
    }

    job_t *job = executor->jobs;
    job_t *prev = NULL;

    while (job) {
        if (job->job_id == job_id) {
            if (prev) {
                prev->next = job->next;
            } else {
                executor->jobs = job->next;
            }

            free_process_list(job->processes);
            free(job->command_line);
            free(job);
            return;
        }
        prev = job;
        job = job->next;
    }
}

/**
 * @brief Update status of all jobs
 *
 * Checks for completed or stopped jobs using waitpid with WNOHANG.
 * Prints status messages and removes completed jobs.
 *
 * @param executor Executor context
 */
void executor_update_job_status(executor_t *executor) {
    if (!executor) {
        return;
    }

    job_t *job = executor->jobs;
    while (job) {
        job_t *next_job = job->next;

        if (job->state == JOB_RUNNING) {
            int status;
            pid_t result = waitpid(-job->pgid, &status, WNOHANG | WUNTRACED);

            if (result > 0) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    job->state = JOB_DONE;
                    printf("[%d]+ Done                    %s\n", job->job_id,
                           job->command_line ? job->command_line : "unknown");
                    executor_remove_job(executor, job->job_id);
                } else if (WIFSTOPPED(status)) {
                    job->state = JOB_STOPPED;
                    printf("[%d]+ Stopped                 %s\n", job->job_id,
                           job->command_line ? job->command_line : "unknown");
                }
            }
        }

        job = next_job;
    }
}

/**
 * @brief Count active jobs
 *
 * Returns count of jobs that are running or stopped (not done).
 *
 * @param executor Executor context
 * @return Number of active jobs
 */
int executor_count_jobs(executor_t *executor) {
    if (!executor) {
        return 0;
    }

    int count = 0;
    job_t *job = executor->jobs;
    while (job) {
        if (job->state == JOB_RUNNING || job->state == JOB_STOPPED) {
            count++;
        }
        job = job->next;
    }
    return count;
}

/**
 * @brief Execute a command in the background
 *
 * Forks the command and adds it to the job list if job control
 * is enabled. Sets up process group for proper signal handling.
 * Updates $! with the background PID.
 *
 * @param executor Executor context
 * @param command Command node to execute
 * @return 0 on success, 1 on failure
 */
int executor_execute_background(executor_t *executor, node_t *command) {
    if (!executor || !command) {
        return 1;
    }

    // Check if job control is enabled (set -m)
    if (!shell_opts.job_control) {
        // When job control is disabled, execute in background without job
        // tracking
        pid_t pid = lush_fork();
        if (pid == -1) {
            int saved_errno = errno;
            executor_error_report(executor, SHELL_ERR_FORK_FAILED, command->loc,
                                  "failed to fork for background process: %s",
                                  strerror(saved_errno));
            return 1;
        }

        if (pid == 0) {
            // Child process - execute the command
            int result = execute_node(executor, command->first_child);
            fflush(stdout);
            fflush(stderr);
            subshell_cleanup();
            _exit(result);
        } else {
            // Parent process - store background PID but no job tracking
            last_background_pid = pid;
            return 0;
        }
    }

    // Build command line for display
    char *command_line = NULL;
    if (command->first_child && command->first_child->type == NODE_COMMAND) {
        command_line = command->first_child->val.str;
    }

    pid_t pid = lush_fork();
    if (pid == -1) {
        int saved_errno = errno;
        executor_error_report(executor, SHELL_ERR_FORK_FAILED, command->loc,
                              "failed to fork for background job: %s",
                              strerror(saved_errno));
        return 1;
    }

    if (pid == 0) {
        // Child process - create new process group
        setpgid(0, 0);

        // Execute the command
        int result = execute_node(executor, command->first_child);
        fflush(stdout);
        fflush(stderr);
        subshell_cleanup();
        _exit(result);
    } else {
        // Parent process - add to job list
        setpgid(pid, pid); // Set child's process group

        // Store the background PID for $! variable
        last_background_pid = pid;

        job_t *job = executor_add_job(executor, pid, command_line);
        if (job) {
            printf("[%d] %d\n", job->job_id, pid);
        }

        return 0; // Background job started successfully
    }
}

/**
 * @brief Built-in jobs command implementation
 *
 * Lists all active jobs with their status (Running/Stopped/Done).
 * Updates job statuses before displaying.
 *
 * @param executor Executor context
 * @param argv Arguments (reserved for filtering options)
 * @return 0 on success
 */
int executor_builtin_jobs(executor_t *executor, char **argv) {
    (void)argv; // Reserved for job filtering options
    if (!executor) {
        return 1;
    }

    // Check if job control is enabled
    if (!shell_opts.job_control) {
        // When job control is disabled, there are no tracked jobs
        return 0;
    }

    // Update job statuses first
    executor_update_job_status(executor);

    job_t *job = executor->jobs;
    while (job) {
        const char *state_str;
        switch (job->state) {
        case JOB_RUNNING:
            state_str = "Running";
            break;
        case JOB_STOPPED:
            state_str = "Stopped";
            break;
        case JOB_DONE:
            state_str = "Done";
            break;
        default:
            state_str = "Unknown";
            break;
        }

        printf("[%d]%c %-20s %s\n", job->job_id, job->foreground ? '+' : '-',
               state_str, job->command_line ? job->command_line : "unknown");

        job = job->next;
    }

    return 0;
}

/**
 * @brief Built-in fg command implementation
 *
 * Brings a background job to the foreground. Continues stopped
 * jobs with SIGCONT. Waits for the job to complete or stop.
 *
 * @param executor Executor context
 * @param argv Arguments (argv[1] is optional job ID)
 * @return Exit status of the foregrounded job
 */
int executor_builtin_fg(executor_t *executor, char **argv) {
    if (!executor) {
        return 1;
    }

    int job_id = 1; // Default to job 1
    if (argv[1]) {
        job_id = atoi(argv[1]);
    }

    job_t *job = executor_find_job(executor, job_id);
    if (!job) {
        executor_error_report(executor, SHELL_ERR_JOB_NOT_FOUND,
                              builtin_get_source_location(), "%d: no such job",
                              job_id);
        return 1;
    }

    if (job->state == JOB_DONE) {
        executor_error_report(executor, SHELL_ERR_JOB_NOT_FOUND,
                              builtin_get_source_location(),
                              "%d: job has terminated", job_id);
        return 1;
    }

    // Give the job's process group control of the terminal
    if (isatty(STDIN_FILENO) && job->pgid > 0) {
        tcsetpgrp(STDIN_FILENO, job->pgid);
    }

    // Continue the job if it was stopped
    if (job->state == JOB_STOPPED) {
        kill(-job->pgid, SIGCONT);
    }

    job->foreground = true;
    job->state = JOB_RUNNING;

    // Wait for the job to complete or stop
    int status;
    waitpid(-job->pgid, &status, WUNTRACED);

    // Reclaim terminal control for the shell
    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, executor->shell_pgid);
    }

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        executor_remove_job(executor, job_id);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    } else if (WIFSTOPPED(status)) {
        job->state = JOB_STOPPED;
        job->foreground = false;
        printf("[%d]+ Stopped                 %s\n", job_id,
               job->command_line ? job->command_line : "unknown");
    }

    return 0;
}

/**
 * @brief Built-in bg command implementation
 *
 * Continues a stopped job in the background by sending SIGCONT.
 *
 * @param executor Executor context
 * @param argv Arguments (argv[1] is optional job ID)
 * @return 0 on success, 1 on error
 */
int executor_builtin_bg(executor_t *executor, char **argv) {
    if (!executor) {
        return 1;
    }

    int job_id = 1; // Default to job 1
    if (argv[1]) {
        job_id = atoi(argv[1]);
    }

    job_t *job = executor_find_job(executor, job_id);
    if (!job) {
        executor_error_report(executor, SHELL_ERR_JOB_NOT_FOUND,
                              builtin_get_source_location(), "%d: no such job",
                              job_id);
        return 1;
    }

    if (job->state != JOB_STOPPED) {
        executor_error_report(executor, SHELL_ERR_JOB_NOT_FOUND,
                              builtin_get_source_location(),
                              "%d: job already in background", job_id);
        return 1;
    }

    // Continue the job in background
    job->state = JOB_RUNNING;
    job->foreground = false;
    kill(-job->pgid, SIGCONT);

    printf("[%d]+ %s &\n", job_id,
           job->command_line ? job->command_line : "unknown");

    return 0;
}

/**
 * @brief Check if stdout is being captured
 *
 * Determines if stdout is piped or redirected to a file
 * (not a terminal). Used to handle builtin redirection properly.
 *
 * @return true if stdout is not a terminal
 */
static bool is_stdout_captured(void) {
    struct stat stat_buf;
    if (fstat(STDOUT_FILENO, &stat_buf) == -1) {
        return false;
    }

    // If stdout is not a terminal (tty), it's likely being captured
    return !isatty(STDOUT_FILENO);
}

/**
 * @brief Check if command has stdout-affecting redirections
 *
 * Checks for >, >>, &>, or >| redirections in the command.
 *
 * @param command Command node to check
 * @return true if command has stdout redirections
 */
static bool has_stdout_redirections(node_t *command) {
    if (!command) {
        return false;
    }

    node_t *child = command->first_child;
    while (child) {
        // Check for stdout-affecting redirections
        if (child->type == NODE_REDIR_OUT ||         // >
            child->type == NODE_REDIR_APPEND ||      // >>
            child->type == NODE_REDIR_BOTH ||        // &>
            child->type == NODE_REDIR_BOTH_APPEND || // &>>
            child->type == NODE_REDIR_CLOBBER) {     // >|
            return true;
        }
        child = child->next_sibling;
    }
    return false;
}

/**
 * @brief Check if a builtin can safely be executed in a subprocess
 *
 * Most builtins modify shell state and cannot be run in a child process
 * without losing their effects. Only "pure" builtins that simply produce
 * output can be safely forked.
 *
 * @param name Builtin command name
 * @return true if the builtin can be safely run in a subprocess
 */
static bool builtin_can_fork(const char *name) {
    if (!name)
        return false;

    // Only these builtins are "pure" - they produce output but don't modify
    // shell state. All others must run in the parent process.
    static const char *pure_builtins[] = {
        "echo", "printf", "true", "false", "test", "[",
        "type", "which",  "help", "pwd",   "dirs", "times",
        "kill", "wait",   "jobs", "fg",    "bg",   NULL};

    for (const char **p = pure_builtins; *p; p++) {
        if (strcmp(name, *p) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Execute builtin with redirections in child process
 *
 * When stdout is captured externally and the command has stdout
 * redirections, we must execute in a child process to avoid
 * file descriptor interference with the parent shell.
 *
 * NOTE: Only "pure" builtins (those that don't modify shell state)
 * can be safely executed this way. Check with builtin_can_fork() first.
 *
 * @param executor Executor context
 * @param argv NULL-terminated argument vector
 * @param command Command node with redirections
 * @return Exit status of the builtin
 */
static int execute_builtin_with_captured_stdout(executor_t *executor,
                                                char **argv, node_t *command) {
    if (!argv || !argv[0]) {
        return 1;
    }

    pid_t pid = lush_fork();
    if (pid == -1) {
        set_executor_error(executor,
                           "Failed to fork for builtin with captured stdout");
        return 1;
    }

    if (pid == 0) {
        // Child process - setup redirections and execute builtin
        int redir_result = setup_redirections(executor, command);
        if (redir_result != 0) {
            subshell_cleanup();
            _exit(1);
        }

        // Execute the builtin command
        int result = execute_builtin_command(executor, argv, command->loc);

        // Flush stdio buffers before _exit() - critical for file redirections
        // Without this, output redirected to files would be lost because
        // _exit() doesn't flush stdio buffers (unlike exit())
        fflush(stdout);
        fflush(stderr);
        subshell_cleanup();
        _exit(result);
    } else {
        // Parent process - wait for child, retrying on EINTR
        int status;
        while (waitpid(pid, &status, 0) == -1) {
            if (errno != EINTR) {
                set_executor_error(executor,
                                   "Failed to wait for builtin child process");
                return 1;
            }
            // EINTR - signal interrupted wait, continue waiting
        }

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }
        return 1;
    }
}

/**
 * @brief Execute an arithmetic command (( expr ))
 *
 * Evaluates the arithmetic expression and returns 0 (success) if the
 * result is non-zero, or 1 (failure) if the result is zero.
 * This matches Bash behavior for arithmetic commands.
 *
 * The expression is first expanded (variable substitution) then
 * evaluated using the arithmetic evaluator.
 *
 * @param executor Executor context
 * @param arith_node Arithmetic command node with expression in val.str
 * @return 0 if result is non-zero, 1 if result is zero
 */
static int execute_arithmetic_command(executor_t *executor,
                                      node_t *arith_node) {
    if (!arith_node || !arith_node->val.str) {
        return 1;
    }

    const char *expr = arith_node->val.str;

    if (executor->debug) {
        printf("DEBUG: Executing arithmetic command: (( %s ))\n", expr);
    }

    // zsh `$+NAME` is the unbraced shorthand for `${+NAME}` (is-set
    // test).  Rewrite to the braced form so the existing ${+NAME}
    // expansion handler in parse_parameter_expansion picks it up.
    //
    // Real-world example: `(( $+commands[dircolors] ))` -- common idiom
    // for "is command available on $path".  Without this rewrite, the
    // arithmetic parser splits `$+commands[...]` and chokes on the
    // bare `$`.
    char *rewritten_expr = NULL;
    {
        const char *needle = strstr(expr, "$+");
        if (needle) {
            size_t out_cap = strlen(expr) * 2 + 32;
            rewritten_expr = malloc(out_cap);
            if (rewritten_expr) {
                size_t out_len = 0;
                const char *p = expr;
                while (*p) {
                    if (p[0] == '$' && p[1] == '+' &&
                        (isalpha((unsigned char)p[2]) || p[2] == '_')) {
                        // Emit `${+`
                        if (out_len + 4 >= out_cap) {
                            out_cap *= 2;
                            char *grown = realloc(rewritten_expr, out_cap);
                            if (!grown) {
                                free(rewritten_expr);
                                rewritten_expr = NULL;
                                break;
                            }
                            rewritten_expr = grown;
                        }
                        memcpy(rewritten_expr + out_len, "${+", 3);
                        out_len += 3;
                        p += 2; // consume $+
                        // Copy the name and optional [subscript].
                        int bracket_depth = 0;
                        while (*p && (isalnum((unsigned char)*p) || *p == '_' ||
                                      (bracket_depth > 0) || *p == '[')) {
                            if (*p == '[') {
                                bracket_depth++;
                            } else if (*p == ']') {
                                bracket_depth--;
                                if (bracket_depth < 0) {
                                    break;
                                }
                            }
                            if (out_len + 2 >= out_cap) {
                                out_cap *= 2;
                                char *grown = realloc(rewritten_expr, out_cap);
                                if (!grown) {
                                    free(rewritten_expr);
                                    rewritten_expr = NULL;
                                    break;
                                }
                                rewritten_expr = grown;
                            }
                            rewritten_expr[out_len++] = *p++;
                            if (bracket_depth == 0 && *(p - 1) == ']') {
                                break;
                            }
                        }
                        if (!rewritten_expr) {
                            break;
                        }
                        rewritten_expr[out_len++] = '}';
                    } else {
                        if (out_len + 2 >= out_cap) {
                            out_cap *= 2;
                            char *grown = realloc(rewritten_expr, out_cap);
                            if (!grown) {
                                free(rewritten_expr);
                                rewritten_expr = NULL;
                                break;
                            }
                            rewritten_expr = grown;
                        }
                        rewritten_expr[out_len++] = *p++;
                    }
                }
                if (rewritten_expr) {
                    rewritten_expr[out_len] = '\0';
                    expr = rewritten_expr;
                }
            }
        }
    }

    // Pre-expand ${...} parameter expansions before arithmetic evaluation
    // The arithmetic module handles simple $var but not complex ${...} syntax
    char *expanded_expr = NULL;
    if (strchr(expr, '{')) {
        expanded_expr = expand_if_needed(executor, expr);
        if (expanded_expr) {
            expr = expanded_expr;
            if (executor->debug) {
                printf("DEBUG: Expanded arithmetic expression: (( %s ))\n",
                       expr);
            }
        }
    }

    // Empty arithmetic command `(( ))` is treated as false by bash and zsh
    // -- the expression evaluates to 0, the command exits 1 (the inverse
    // convention). No error is raised. Scripts use this idiom as a
    // placeholder false. Match the convention before invoking the
    // evaluator, which would otherwise flag empty input as a syntax error.
    {
        const char *p = expr;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            free(expanded_expr);
            free(rewritten_expr);
            return 1;
        }
    }

    // Use the existing arithmetic evaluator with executor context
    // This handles simple variable expansion internally
    arithm_clear_error();
    char *result_str = arithm_expand_with_executor(executor, expr);

    if (!result_str || arithm_error_is_flagged()) {
        // Arithmetic error - could be syntax error or division by zero
        // Create structured error with help suggestion
        shell_error_t *error = shell_error_create(
            SHELL_ERR_ARITHMETIC_SYNTAX, SHELL_SEVERITY_ERROR, arith_node->loc,
            "arithmetic syntax error in expression: %s", expr);
        if (error) {
            // Build the source line: (( expr ))
            char *source_line = NULL;
            size_t expr_len = strlen(expr);
            if (asprintf(&source_line, "(( %s ))", expr) > 0) {
                shell_error_set_source_line(error, source_line, 3,
                                            3 + expr_len);
                free(source_line);
            }
            // Update location to highlight the expression (column is 1-indexed)
            error->location.column = 4; // After "(( "
            error->location.length = expr_len;
            // Add context stack from executor
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            // Add specific context for arithmetic command
            shell_error_push_context(
                error, "evaluating arithmetic command (( %s ))", expr);
            // Add help suggestion
            shell_error_set_suggestion(
                error,
                "(( )) expects arithmetic expressions, not shell commands");
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
        }
        executor->has_error = true;
        if (result_str) {
            free(result_str);
        }
        free(expanded_expr);
        free(rewritten_expr);
        return 1;
    }

    // Convert result to long long to check if non-zero
    long long result = strtoll(result_str, NULL, 10);
    free(result_str);
    free(expanded_expr); // Safe to free NULL
    free(rewritten_expr);

    // Update exit status
    executor->exit_status = (result != 0) ? 0 : 1;

    if (executor->debug) {
        printf("DEBUG: Arithmetic result: %lld, exit status: %d\n", result,
               executor->exit_status);
    }

    // Return 0 if non-zero (true), 1 if zero (false)
    return (result != 0) ? 0 : 1;
}

/**
 * @brief Match a string against a glob pattern
 *
 * Uses fnmatch for shell-style pattern matching.
 *
 * @param str String to match
 * @param pattern Glob pattern
 * @return true if matches, false otherwise
 */
static bool extended_test_pattern_match(const char *str, const char *pattern) {
    if (!str || !pattern) {
        return false;
    }
    // FNM_EXTMATCH would enable extended patterns, but isn't portable
    return fnmatch(pattern, str, 0) == 0;
}

/**
 * @brief Match a string against a regex and populate BASH_REMATCH
 *
 * Uses POSIX extended regular expressions. Capture groups are stored
 * in the BASH_REMATCH array variable.
 *
 * @param executor Executor context (for setting BASH_REMATCH)
 * @param str String to match
 * @param pattern Regex pattern
 * @return true if matches, false otherwise
 */
static bool extended_test_regex_match(executor_t *executor, const char *str,
                                      const char *pattern) {
    if (!str || !pattern) {
        return false;
    }

    if (!regex_pattern_is_safe(pattern)) {
        executor_error_report(
            executor, SHELL_ERR_INVALID_ARGUMENT, builtin_get_source_location(),
            "regex pattern exceeds behavior.regex_pattern_max (%d)",
            config.regex_pattern_max);
        return false;
    }

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        // Regex compilation failed
        if (executor->debug) {
            char errbuf[256];
            regerror(ret, &regex, errbuf, sizeof(errbuf));
            printf("DEBUG: Regex compilation failed: %s\n", errbuf);
        }
        return false;
    }

    // Match with capture groups (up to 10 groups)
    regmatch_t matches[10];
    ret = regexec(&regex, str, 10, matches, 0);

    if (ret == 0) {
        // Match successful - populate BASH_REMATCH array
        // Use symtable_set_array_element which handles array creation
        for (int i = 0; i < 10 && matches[i].rm_so != -1; i++) {
            size_t match_len = matches[i].rm_eo - matches[i].rm_so;
            char *match_str = malloc(match_len + 1);
            if (match_str) {
                strncpy(match_str, str + matches[i].rm_so, match_len);
                match_str[match_len] = '\0';

                // Convert index to string for subscript
                char subscript[16];
                snprintf(subscript, sizeof(subscript), "%d", i);
                symtable_set_array_element("BASH_REMATCH", subscript,
                                           match_str);
                free(match_str);
            }
        }
        regfree(&regex);
        return true;
    }

    regfree(&regex);
    return false;
}

/**
 * @brief Evaluate a file test operator
 *
 * Handles file test operators like -f, -d, -e, -r, -w, -x, etc.
 *
 * @param op The operator (e.g., "-f", "-d")
 * @param path The file path to test
 * @return true if test passes, false otherwise
 */
static bool extended_test_file_test(const char *op, const char *path) {
    if (!op || !path) {
        return false;
    }

    struct stat st;
    bool exists = (stat(path, &st) == 0);

    if (strcmp(op, "-e") == 0) {
        return exists;
    } else if (strcmp(op, "-f") == 0) {
        return exists && S_ISREG(st.st_mode);
    } else if (strcmp(op, "-d") == 0) {
        return exists && S_ISDIR(st.st_mode);
    } else if (strcmp(op, "-r") == 0) {
        return access(path, R_OK) == 0;
    } else if (strcmp(op, "-w") == 0) {
        return access(path, W_OK) == 0;
    } else if (strcmp(op, "-x") == 0) {
        return access(path, X_OK) == 0;
    } else if (strcmp(op, "-s") == 0) {
        return exists && st.st_size > 0;
    } else if (strcmp(op, "-L") == 0 || strcmp(op, "-h") == 0) {
        struct stat lst;
        return (lstat(path, &lst) == 0) && S_ISLNK(lst.st_mode);
    } else if (strcmp(op, "-b") == 0) {
        return exists && S_ISBLK(st.st_mode);
    } else if (strcmp(op, "-c") == 0) {
        return exists && S_ISCHR(st.st_mode);
    } else if (strcmp(op, "-p") == 0) {
        return exists && S_ISFIFO(st.st_mode);
    } else if (strcmp(op, "-S") == 0) {
        return exists && S_ISSOCK(st.st_mode);
    } else if (strcmp(op, "-g") == 0) {
        return exists && (st.st_mode & S_ISGID);
    } else if (strcmp(op, "-u") == 0) {
        return exists && (st.st_mode & S_ISUID);
    } else if (strcmp(op, "-k") == 0) {
        return exists && (st.st_mode & S_ISVTX);
    } else if (strcmp(op, "-O") == 0) {
        return exists && (st.st_uid == getuid());
    } else if (strcmp(op, "-G") == 0) {
        return exists && (st.st_gid == getgid());
    }

    return false;
}

/**
 * @brief Execute an extended test command [[ expression ]]
 *
 * Evaluates the conditional expression within [[ ]].
 * Supports string comparisons, pattern matching, regex matching,
 * file tests, and logical operators.
 *
 * @param executor Executor context
 * @param test_node Extended test node with expression in val.str
 * @return 0 if test passes (true), 1 if fails (false)
 */

// Forward declaration for recursive evaluation
static bool evaluate_simple_test(executor_t *executor, const char *expr);

/**
 * @brief Find a logical operator (&& or ||) at the top level
 *
 * Scans for && or || that is not inside parentheses.
 * Returns pointer to the operator or NULL if not found.
 * Also sets op_len to 2 if found.
 */
static char *find_logical_operator(char *expr, int *op_len, char *op_type) {
    int paren_depth = 0;
    char *p = expr;

    while (*p) {
        if (*p == '(') {
            paren_depth++;
        } else if (*p == ')') {
            paren_depth--;
        } else if (paren_depth == 0) {
            // Check for || first (lower precedence, so we want to split on it
            // first)
            if (p[0] == '|' && p[1] == '|') {
                *op_len = 2;
                *op_type = '|';
                return p;
            }
            // Check for &&
            if (p[0] == '&' && p[1] == '&') {
                *op_len = 2;
                *op_type = '&';
                return p;
            }
        }
        p++;
    }
    return NULL;
}

/**
 * @brief Evaluate an extended test expression with && and ||
 *
 * Recursively evaluates expressions, handling:
 * - || (OR) with lowest precedence
 * - && (AND) with higher precedence
 * - Parentheses for grouping
 * - Simple test expressions
 */
static bool evaluate_extended_expr(executor_t *executor, char *expr) {
    // Trim whitespace
    while (*expr && isspace(*expr))
        expr++;
    char *end = expr + strlen(expr) - 1;
    while (end > expr && isspace(*end))
        *end-- = '\0';

    if (*expr == '\0') {
        return false;
    }

    // Check if entire expression is wrapped in parentheses
    if (*expr == '(' && *(expr + strlen(expr) - 1) == ')') {
        // Check if they match (not just opening and closing from different
        // groups)
        int depth = 0;
        bool matched = true;
        for (char *p = expr; *p; p++) {
            if (*p == '(')
                depth++;
            else if (*p == ')')
                depth--;
            if (depth == 0 && *(p + 1) != '\0') {
                matched = false;
                break;
            }
        }
        if (matched) {
            // Strip outer parentheses
            char *inner = expr + 1;
            expr[strlen(expr) - 1] = '\0';
            return evaluate_extended_expr(executor, inner);
        }
    }

    // Look for || first (lowest precedence)
    int op_len = 0;
    char op_type = 0;
    char *op_pos = find_logical_operator(expr, &op_len, &op_type);

    if (op_pos && op_type == '|') {
        // Split on ||
        *op_pos = '\0';
        char *left = expr;
        char *right = op_pos + 2;

        // Short-circuit: if left is true, don't evaluate right
        if (evaluate_extended_expr(executor, left)) {
            return true;
        }
        return evaluate_extended_expr(executor, right);
    }

    // Look for && (higher precedence than ||)
    op_pos = NULL;
    op_len = 0;
    op_type = 0;

    // Re-scan for && only
    int paren_depth = 0;
    for (char *p = expr; *p; p++) {
        if (*p == '(')
            paren_depth++;
        else if (*p == ')')
            paren_depth--;
        else if (paren_depth == 0 && p[0] == '&' && p[1] == '&') {
            op_pos = p;
            op_len = 2;
            op_type = '&';
            break;
        }
    }

    if (op_pos && op_type == '&') {
        // Split on &&
        *op_pos = '\0';
        char *left = expr;
        char *right = op_pos + 2;

        // Short-circuit: if left is false, don't evaluate right
        if (!evaluate_extended_expr(executor, left)) {
            return false;
        }
        return evaluate_extended_expr(executor, right);
    }

    // No logical operators at this level - evaluate as simple test
    return evaluate_simple_test(executor, expr);
}

/**
 * @brief Evaluate a simple test expression (no && or ||)
 */
static bool evaluate_simple_test(executor_t *executor, const char *expr) {
    char *p = (char *)expr;

    // Skip leading whitespace
    while (*p && isspace(*p))
        p++;

    // Check for negation
    bool negate = false;
    if (*p == '!') {
        negate = true;
        p++;
        while (*p && isspace(*p))
            p++;
    }

    bool result = false;

    // Check for unary file/string tests
    if (*p == '-' && p[1] && isalpha(p[1])) {
        // Extract operator
        char op[4] = {0};
        int op_len = 0;
        while (*p && !isspace(*p) && op_len < 3) {
            op[op_len++] = *p++;
        }
        op[op_len] = '\0';

        // Skip whitespace
        while (*p && isspace(*p))
            p++;

        // String tests
        if (strcmp(op, "-z") == 0) {
            result = (*p == '\0');
        } else if (strcmp(op, "-n") == 0) {
            result = (*p != '\0');
        } else if (strcmp(op, "-v") == 0) {
            /* -v NAME -- true if scalar variable NAME is set.
             * -v NAME[KEY] / NAME[N] -- true if the array element
             * is set (associative key present, indexed slot has a
             * value). Bash semantics: -v on an unset arr[k] returns
             * false; on an unset scalar also false. Issue #97. */
            char *arg = strdup(p);
            if (arg) {
                char *end = arg + strlen(arg) - 1;
                while (end > arg && isspace(*end)) {
                    *end-- = '\0';
                }
                char *bracket = strchr(arg, '[');
                if (bracket) {
                    *bracket = '\0';
                    char *close = strchr(bracket + 1, ']');
                    if (close) {
                        *close = '\0';
                        const char *key = bracket + 1;
                        array_value_t *array = symtable_get_array(arg);
                        if (array) {
                            if (array->is_associative) {
                                result = symtable_array_get_assoc(array, key) !=
                                         NULL;
                            } else {
                                char *endp = NULL;
                                long idx = strtol(key, &endp, 10);
                                if (endp && *endp == '\0') {
                                    result = symtable_array_get_index(
                                                 array, (int)idx) != NULL;
                                }
                            }
                        }
                    }
                } else {
                    /* -v NAME without subscript: true for any kind of
                     * binding (scalar OR array). Pre-migration this
                     * only checked symtable_get_var, missing arrays --
                     * `arr=(a); [[ -v arr ]]` was false in lush but
                     * true in bash. The unified view fixes that by
                     * returning true on any LUSH_VALUE_* hit. */
                    lush_value_view_t view = {0};
                    result = symtable_lookup(arg, &view);
                    lush_value_view_clear(&view);
                }
                free(arg);
            }
        } else if (strcmp(op, "-o") == 0) {
            // `[[ -o NAME ]]` -- query named shell option state.
            // Single unified entry point in shell_is_option_set walks
            // POSIX options, feature matrix names + aliases, noop-alias
            // recorded state, and the `interactive` pseudo-option.
            char *name = strdup(p);
            if (name) {
                char *end = name + strlen(name) - 1;
                while (end > name && isspace(*end)) {
                    *end-- = '\0';
                }
                result = shell_is_option_set(name);
                free(name);
            }
        } else {
            // File tests - get the path (rest of line, trimmed)
            char *path = strdup(p);
            if (path) {
                char *end = path + strlen(path) - 1;
                while (end > path && isspace(*end))
                    *end-- = '\0';
                result = extended_test_file_test(op, path);
                free(path);
            }
        }
    } else {
        // Binary expression: lhs op rhs
        char *lhs_start = p;
        char *op_start = NULL;
        char op_type[4] = {0};

        // Scan for binary operator
        char *scan = p;
        int paren_depth = 0;

        while (*scan) {
            if (*scan == '(')
                paren_depth++;
            else if (*scan == ')')
                paren_depth--;
            else if (paren_depth == 0) {
                if (scan[0] == '=' && scan[1] == '=') {
                    op_start = scan;
                    strcpy(op_type, "==");
                    break;
                } else if (scan[0] == '!' && scan[1] == '=') {
                    op_start = scan;
                    strcpy(op_type, "!=");
                    break;
                } else if (scan[0] == '=' && scan[1] == '~') {
                    op_start = scan;
                    strcpy(op_type, "=~");
                    break;
                } else if (scan[0] == '=' && scan[1] != '=' && scan[1] != '~') {
                    // Single `=` -- bash/zsh alias for `==`. Recognized
                    // even when scan == lhs_start because empty LHS
                    // (e.g. `[[ "$EMPTY" = "y" ]]` after expansion) is
                    // a real-world case where the binary-op scan must
                    // still split off the operator and RHS. Without
                    // this, `[[ "" = X ]]` falls through to the "no
                    // operator -- non-empty string is true" branch and
                    // reports equality regardless of RHS value.
                    if (g_debug_context) {
                        debug_trace_printf(
                            g_debug_context,
                            "extended-test = recognized at offset %zd\n",
                            (ssize_t)(scan - lhs_start));
                    }
                    op_start = scan;
                    strcpy(op_type, "==");
                    break;
                } else if (scan[0] == '<' && scan[1] != '<') {
                    op_start = scan;
                    strcpy(op_type, "<");
                    break;
                } else if (scan[0] == '>' && scan[1] != '>') {
                    op_start = scan;
                    strcpy(op_type, ">");
                    break;
                } else if (scan[0] == '-' && isalpha(scan[1])) {
                    if (strncmp(scan, "-eq", 3) == 0 &&
                        (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-eq");
                        break;
                    } else if (strncmp(scan, "-ne", 3) == 0 &&
                               (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-ne");
                        break;
                    } else if (strncmp(scan, "-lt", 3) == 0 &&
                               (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-lt");
                        break;
                    } else if (strncmp(scan, "-le", 3) == 0 &&
                               (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-le");
                        break;
                    } else if (strncmp(scan, "-gt", 3) == 0 &&
                               (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-gt");
                        break;
                    } else if (strncmp(scan, "-ge", 3) == 0 &&
                               (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-ge");
                        break;
                    } else if (strncmp(scan, "-nt", 3) == 0 &&
                               (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-nt");
                        break;
                    } else if (strncmp(scan, "-ot", 3) == 0 &&
                               (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-ot");
                        break;
                    } else if (strncmp(scan, "-ef", 3) == 0 &&
                               (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-ef");
                        break;
                    }
                }
            }
            scan++;
        }

        if (op_start && op_type[0]) {
            // Extract LHS
            size_t lhs_len = op_start - lhs_start;
            char *lhs = malloc(lhs_len + 1);
            if (lhs) {
                strncpy(lhs, lhs_start, lhs_len);
                lhs[lhs_len] = '\0';
                char *end = lhs + strlen(lhs) - 1;
                while (end >= lhs && isspace(*end))
                    *end-- = '\0';
            }

            // Extract RHS
            char *rhs_start = op_start + strlen(op_type);
            while (*rhs_start && isspace(*rhs_start))
                rhs_start++;
            char *rhs = strdup(rhs_start);
            if (rhs) {
                char *end = rhs + strlen(rhs) - 1;
                while (end >= rhs && isspace(*end))
                    *end-- = '\0';
            }

            // Evaluate based on operator
            if (strcmp(op_type, "==") == 0) {
                result = extended_test_pattern_match(lhs, rhs);
            } else if (strcmp(op_type, "!=") == 0) {
                result = !extended_test_pattern_match(lhs, rhs);
            } else if (strcmp(op_type, "=~") == 0) {
                result = extended_test_regex_match(executor, lhs, rhs);
            } else if (strcmp(op_type, "<") == 0) {
                result = (strcmp(lhs ? lhs : "", rhs ? rhs : "") < 0);
            } else if (strcmp(op_type, ">") == 0) {
                result = (strcmp(lhs ? lhs : "", rhs ? rhs : "") > 0);
            } else if (strcmp(op_type, "-eq") == 0) {
                result = (atoll(lhs ? lhs : "0") == atoll(rhs ? rhs : "0"));
            } else if (strcmp(op_type, "-ne") == 0) {
                result = (atoll(lhs ? lhs : "0") != atoll(rhs ? rhs : "0"));
            } else if (strcmp(op_type, "-lt") == 0) {
                result = (atoll(lhs ? lhs : "0") < atoll(rhs ? rhs : "0"));
            } else if (strcmp(op_type, "-le") == 0) {
                result = (atoll(lhs ? lhs : "0") <= atoll(rhs ? rhs : "0"));
            } else if (strcmp(op_type, "-gt") == 0) {
                result = (atoll(lhs ? lhs : "0") > atoll(rhs ? rhs : "0"));
            } else if (strcmp(op_type, "-ge") == 0) {
                result = (atoll(lhs ? lhs : "0") >= atoll(rhs ? rhs : "0"));
            } else if (strcmp(op_type, "-nt") == 0) {
                // File1 newer than file2
                struct stat st1, st2;
                if (lhs && rhs && stat(lhs, &st1) == 0 &&
                    stat(rhs, &st2) == 0) {
                    result = (st1.st_mtime > st2.st_mtime);
                } else {
                    result = false;
                }
            } else if (strcmp(op_type, "-ot") == 0) {
                // File1 older than file2
                struct stat st1, st2;
                if (lhs && rhs && stat(lhs, &st1) == 0 &&
                    stat(rhs, &st2) == 0) {
                    result = (st1.st_mtime < st2.st_mtime);
                } else {
                    result = false;
                }
            } else if (strcmp(op_type, "-ef") == 0) {
                // File1 and file2 are same file (same device and inode)
                struct stat st1, st2;
                if (lhs && rhs && stat(lhs, &st1) == 0 &&
                    stat(rhs, &st2) == 0) {
                    result =
                        (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);
                } else {
                    result = false;
                }
            }

            free(lhs);
            free(rhs);
        } else {
            // No operator - non-empty string is true
            result = (*p != '\0');
        }
    }

    return negate ? !result : result;
}

static int execute_extended_test(executor_t *executor, node_t *test_node) {
    if (!test_node || !test_node->val.str) {
        return 1;
    }

    const char *expr = test_node->val.str;

    if (executor->debug) {
        printf("DEBUG: Executing extended test: [[ %s ]]\n", expr);
    }

    // First, expand variables in the expression
    char *expanded = expand_if_needed(executor, expr);
    if (!expanded) {
        expanded = strdup(expr);
    }
    if (!expanded) {
        return 1;
    }

    if (executor->debug) {
        printf("DEBUG: Expanded extended test: [[ %s ]]\n", expanded);
    }

    // Evaluate the expression using recursive evaluator
    // This handles &&, ||, parentheses, and simple tests
    bool result = evaluate_extended_expr(executor, expanded);

    free(expanded);

    // Update exit status
    executor->exit_status = result ? 0 : 1;

    if (executor->debug) {
        printf("DEBUG: Extended test result: %s, exit status: %d\n",
               result ? "true" : "false", executor->exit_status);
    }

    return result ? 0 : 1;
}

/**
 * @brief Execute an array assignment
 *
 * Handles both array literal assignment (arr=(a b c)) and
 * array element assignment (arr[n]=value).
 *
 * For array literals, the first child is NODE_ARRAY_LITERAL.
 * For element assignment, children are subscript and value nodes.
 *
 * @param executor Executor context
 * @param assign_node Array assignment node
 * @return 0 on success, 1 on error
 */
static int execute_array_assignment(executor_t *executor, node_t *assign_node) {
    if (!assign_node || !assign_node->val.str) {
        return 1;
    }

    const char *var_name = assign_node->val.str;
    node_t *first_child = assign_node->first_child;

    if (!first_child) {
        return 1;
    }

    if (executor->debug) {
        printf("DEBUG: Executing array assignment for: %s\n", var_name);
    }

    // Check if this is an array literal assignment: arr=(a b c)
    if (first_child->type == NODE_ARRAY_LITERAL) {
        // Check if variable was already declared as associative array
        array_value_t *existing = symtable_get_array(var_name);
        bool is_associative = existing && existing->is_associative;

        // Create appropriate array type (preserving associative if declared)
        array_value_t *array = symtable_array_create(is_associative);
        if (!array) {
            set_executor_error(executor, "Failed to create array");
            return 1;
        }

        // Process each element in the literal
        int index = 0;
        node_t *elem = first_child->first_child;

        while (elem) {
            if (elem->val.str) {
                const char *elem_str = elem->val.str;

                // Check for [index]=value syntax
                if (elem_str[0] == '[') {
                    // Parse [index]=value
                    const char *bracket_end = strchr(elem_str, ']');
                    if (bracket_end && bracket_end[1] == '=') {
                        // Extract index/key
                        size_t idx_len = bracket_end - elem_str - 1;
                        char *idx_str = malloc(idx_len + 1);
                        if (idx_str) {
                            strncpy(idx_str, elem_str + 1, idx_len);
                            idx_str[idx_len] = '\0';

                            // Get value after ]=
                            const char *value = bracket_end + 2;

                            // Expand value using full expansion (handles $'...'
                            // ANSI-C quoting)
                            char *expanded = expand_if_needed(executor, value);
                            const char *final_value =
                                expanded ? expanded : value;

                            if (is_associative) {
                                // Use string key directly for associative
                                // arrays
                                symtable_array_set_assoc(array, idx_str,
                                                         final_value);
                            } else {
                                // Evaluate index as arithmetic for indexed
                                // arrays
                                arithm_clear_error();
                                char *idx_result = arithm_expand(idx_str);

                                if (idx_result && !arithm_error_is_flagged()) {
                                    long long idx_val =
                                        strtoll(idx_result, NULL, 10);
                                    if (idx_val >= 0) {
                                        index = (int)idx_val;
                                    }
                                    free(idx_result);
                                }

                                symtable_array_set_index(array, index,
                                                         final_value);
                            }

                            free(idx_str);
                            if (expanded) {
                                free(expanded);
                            }
                        }
                    }
                } else {
                    // Regular element without [key]=value syntax
                    if (is_associative) {
                        // Zsh-style: arr=(key1 val1 key2 val2 ...)
                        // Alternating key-value pairs
                        char *expanded_key =
                            expand_if_needed(executor, elem_str);
                        const char *key =
                            expanded_key ? expanded_key : elem_str;

                        // Get next element as value
                        node_t *value_elem = elem->next_sibling;
                        if (value_elem && value_elem->val.str) {
                            char *expanded_val =
                                expand_if_needed(executor, value_elem->val.str);
                            const char *val = expanded_val
                                                  ? expanded_val
                                                  : value_elem->val.str;

                            symtable_array_set_assoc(array, key, val);

                            if (expanded_val)
                                free(expanded_val);
                            elem = value_elem; // Skip the value element
                        }
                        if (expanded_key)
                            free(expanded_key);
                    } else {
                        // Indexed array - assign to next index
                        // Expand the element using full expansion (handles
                        // $'...' ANSI-C quoting)
                        char *expanded = expand_if_needed(executor, elem_str);
                        const char *final_value =
                            expanded ? expanded : elem_str;

                        // Only word split for unquoted elements (NODE_VAR)
                        // Quoted strings (NODE_STRING_LITERAL,
                        // NODE_STRING_EXPANDABLE) should be stored as single
                        // elements even if they contain spaces
                        bool is_quoted = (elem->type == NODE_STRING_LITERAL ||
                                          elem->type == NODE_STRING_EXPANDABLE);

                        /* Brace expansion on unquoted indexed-array
                         * elements: `arr=(/tmp/{a,b}/sub)` must yield
                         * two elements, matching bash/zsh behavior.
                         * Each brace-expansion result becomes its own
                         * array element regardless of internal spaces
                         * (the brace expander has already partitioned
                         * the source into discrete words). */
                        if (!is_quoted && needs_brace_expansion(final_value)) {
                            int brace_count = 0;
                            char **brace_results =
                                expand_brace_pattern(final_value, &brace_count);
                            if (brace_results) {
                                for (int bi = 0; bi < brace_count; bi++) {
                                    symtable_array_set_index(array, index,
                                                             brace_results[bi]);
                                    index++;
                                    free(brace_results[bi]);
                                }
                                free(brace_results);
                                if (expanded) {
                                    free(expanded);
                                }
                                elem = elem->next_sibling;
                                continue;
                            }
                        }

                        /* Pathname (glob) expansion on unquoted
                         * indexed-array elements: `arr=(*.txt)` must
                         * list the matching files, not iterate the
                         * literal pattern. expand_glob_pattern handles
                         * zsh glob qualifiers, extglob, nullglob, and
                         * `set -f` internally. */
                        if (!is_quoted && needs_glob_expansion(final_value)) {
                            int glob_count = 0;
                            char **glob_results =
                                expand_glob_pattern(final_value, &glob_count);
                            if (glob_results) {
                                for (int gi = 0; gi < glob_count; gi++) {
                                    symtable_array_set_index(array, index,
                                                             glob_results[gi]);
                                    index++;
                                    free(glob_results[gi]);
                                }
                                free(glob_results);
                                if (expanded) {
                                    free(expanded);
                                }
                                elem = elem->next_sibling;
                                continue;
                            }
                        }

                        // Word split the expanded value if it contains spaces
                        // This handles ${(s:,:)var} and ${(f)var} producing
                        // multiple words but NOT quoted strings like "echo
                        // hello"
                        if (!is_quoted && strchr(final_value, ' ') != NULL) {
                            // Split on spaces and add each word as separate
                            // element
                            char *copy = strdup(final_value);
                            if (copy) {
                                char *saveptr;
                                char *word = strtok_r(copy, " ", &saveptr);
                                while (word) {
                                    // Skip empty words
                                    if (*word) {
                                        symtable_array_set_index(array, index,
                                                                 word);
                                        index++;
                                    }
                                    word = strtok_r(NULL, " ", &saveptr);
                                }
                                free(copy);
                            }
                        } else {
                            symtable_array_set_index(array, index, final_value);
                            index++;
                        }

                        if (expanded) {
                            free(expanded);
                        }
                    }
                }
            }
            elem = elem->next_sibling;
        }

        // Store the array in the symbol table
        if (symtable_set_array(var_name, array) != 0) {
            symtable_array_free(array);
            set_executor_error(executor, "Failed to store array");
            return 1;
        }

        if (executor->debug) {
            printf("DEBUG: Created array %s with %zu elements\n", var_name,
                   symtable_array_length(array));
        }

        return 0;
    }

    // Array element assignment: arr[n]=value
    // First child is subscript, second child is value
    node_t *subscript_node = first_child;
    node_t *value_node = first_child->next_sibling;

    if (!subscript_node || !subscript_node->val.str) {
        set_executor_error(executor, "Missing array subscript");
        return 1;
    }

    const char *subscript = subscript_node->val.str;
    const char *value = value_node ? value_node->val.str : "";

    // Check for append operation (value starts with "+=")
    bool is_append = false;
    if (value && strlen(value) >= 2 && value[0] == '+' && value[1] == '=') {
        is_append = true;
        value += 2; // Skip "+=" prefix
    }

    // Expand value
    char *expanded_value = expand_variable(executor, value);
    const char *final_value = expanded_value ? expanded_value : value;

    // Get or create the array
    array_value_t *array = symtable_get_array(var_name);
    if (!array) {
        // Create new array if it doesn't exist
        array = symtable_array_create(false);
        if (!array) {
            if (expanded_value)
                free(expanded_value);
            set_executor_error(executor, "Failed to create array");
            return 1;
        }
        if (symtable_set_array(var_name, array) != 0) {
            symtable_array_free(array);
            if (expanded_value)
                free(expanded_value);
            set_executor_error(executor, "Failed to store array");
            return 1;
        }
    }

    // Handle subscript - could be "@", "*", string key, or numeric index
    if (strcmp(subscript, "@") == 0 || strcmp(subscript, "*") == 0) {
        // Append to array
        symtable_array_append(array, final_value);
    } else if (array->is_associative) {
        // Associative array - use subscript as string key
        // First expand any variables in the subscript
        char *expanded_subscript = expand_variable(executor, subscript);
        const char *key = expanded_subscript ? expanded_subscript : subscript;

        if (is_append) {
            // Append to existing element
            const char *existing = symtable_array_get_assoc(array, key);
            if (existing) {
                size_t new_len = strlen(existing) + strlen(final_value) + 1;
                char *combined = malloc(new_len);
                if (combined) {
                    strcpy(combined, existing);
                    strcat(combined, final_value);
                    symtable_array_set_assoc(array, key, combined);
                    free(combined);
                }
            } else {
                symtable_array_set_assoc(array, key, final_value);
            }
        } else {
            symtable_array_set_assoc(array, key, final_value);
        }

        if (expanded_subscript) {
            free(expanded_subscript);
        }
    } else {
        // Indexed array - evaluate subscript as arithmetic expression
        arithm_clear_error();
        char *idx_result = arithm_expand(subscript);

        if (!idx_result || arithm_error_is_flagged()) {
            if (idx_result)
                free(idx_result);
            if (expanded_value)
                free(expanded_value);
            set_executor_error(executor, "Invalid array index");
            return 1;
        }

        long long idx = strtoll(idx_result, NULL, 10);
        free(idx_result);

        // Adjust for 1-indexed arrays (zsh mode)
        // When FEATURE_ARRAY_ZERO_INDEXED is false, user index 1 maps to
        // internal 0
        if (!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED)) {
            if (idx <= 0) {
                if (expanded_value)
                    free(expanded_value);
                set_executor_error(executor,
                                   "Array index must be positive in zsh mode");
                return 1;
            }
            idx--; // Convert 1-indexed to 0-indexed internally
        }

        if (is_append) {
            // Append to existing element
            const char *existing = symtable_array_get_index(array, (int)idx);
            if (existing) {
                size_t new_len = strlen(existing) + strlen(final_value) + 1;
                char *combined = malloc(new_len);
                if (combined) {
                    strcpy(combined, existing);
                    strcat(combined, final_value);
                    symtable_array_set_index(array, (int)idx, combined);
                    free(combined);
                }
            } else {
                symtable_array_set_index(array, (int)idx, final_value);
            }
        } else {
            symtable_array_set_index(array, (int)idx, final_value);
        }
    }

    if (expanded_value) {
        free(expanded_value);
    }

    if (executor->debug) {
        printf("DEBUG: Set %s[%s] = %s\n", var_name, subscript, final_value);
    }

    return 0;
}

/**
 * @brief Execute array append: arr+=(a b c)
 *
 * Appends elements from the array literal to an existing array.
 * If the array doesn't exist, creates a new one.
 *
 * @param executor Executor context
 * @param append_node NODE_ARRAY_APPEND node with var name and literal
 * @return 0 on success, non-zero on error
 */
static int execute_array_append(executor_t *executor, node_t *append_node) {
    if (!append_node || !append_node->val.str) {
        return 1;
    }

    const char *var_name = append_node->val.str;
    node_t *first_child = append_node->first_child;

    if (!first_child || first_child->type != NODE_ARRAY_LITERAL) {
        return 1;
    }

    if (executor->debug) {
        printf("DEBUG: Executing array append for: %s\n", var_name);
    }

    // Get existing array or create new one
    array_value_t *array = symtable_get_array(var_name);
    bool new_array = false;

    if (!array) {
        // Create new array if it doesn't exist
        array = symtable_array_create(false);
        if (!array) {
            set_executor_error(executor, "Failed to create array");
            return 1;
        }
        new_array = true;
    }

    // Process each element in the literal and append
    node_t *elem = first_child->first_child;

    while (elem) {
        if (elem->val.str) {
            const char *elem_str = elem->val.str;

            // Expand value if needed
            char *expanded = expand_variable(executor, elem_str);
            const char *final_value = expanded ? expanded : elem_str;

            // Append to array using symtable_array_append
            symtable_array_append(array, final_value);

            if (expanded) {
                free(expanded);
            }
        }
        elem = elem->next_sibling;
    }

    // Store the array if newly created
    if (new_array) {
        if (symtable_set_array(var_name, array) != 0) {
            symtable_array_free(array);
            set_executor_error(executor, "Failed to store array");
            return 1;
        }
    }

    if (executor->debug) {
        printf("DEBUG: Appended to array %s, now has %zu elements\n", var_name,
               symtable_array_length(array));
    }

    return 0;
}

/**
 * @brief Expand a process substitution <(cmd) or >(cmd)
 *
 * Creates a FIFO or uses /dev/fd mechanism to provide a filename
 * that connects to the command's stdout (for <()) or stdin (for >()).
 *
 * @param executor Executor context
 * @param proc_sub Process substitution node (NODE_PROC_SUB_IN or
 * NODE_PROC_SUB_OUT)
 * @return Path to the FIFO/fd, or NULL on error (caller must free)
 */
char *expand_process_substitution(executor_t *executor, node_t *proc_sub);

/**
 * @brief Clean up file descriptors from process substitutions
 *
 * Closes all tracked process substitution file descriptors and resets
 * the tracking counter. Should be called after a command completes
 * to prevent fd leaks with nested process substitutions.
 *
 * @param executor Executor context
 */
static void cleanup_procsub_fds(executor_t *executor) {
    if (!executor) {
        return;
    }
    // Close all tracked file descriptors first
    for (int i = 0; i < executor->procsub_fd_count; i++) {
        if (executor->procsub_fds[i] >= 0) {
            close(executor->procsub_fds[i]);
        }
    }
    // Wait for all child processes to prevent zombies and terminal issues
    for (int i = 0; i < executor->procsub_fd_count; i++) {
        if (executor->procsub_pids[i] > 0) {
            int status;
            waitpid(executor->procsub_pids[i], &status, 0);
        }
    }
    executor->procsub_fd_count = 0;
}

char *expand_process_substitution(executor_t *executor, node_t *proc_sub) {
    if (!executor || !proc_sub) {
        return NULL;
    }

    // Check if feature is enabled
    if (!shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION)) {
        set_executor_error(executor, "Process substitution not enabled");
        return NULL;
    }

    bool is_input = (proc_sub->type == NODE_PROC_SUB_IN); // <(cmd)

    // Create a pipe for communication
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        set_executor_error(executor,
                           "Failed to create pipe for process substitution");
        return NULL;
    }

    pid_t pid = lush_fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        set_executor_error(executor, "Failed to fork for process substitution");
        return NULL;
    }

    if (pid == 0) {
        // Child process - execute the command
        if (is_input) {
            // <(cmd): command writes to pipe, parent reads
            close(pipefd[0]); // Close read end
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            // Disconnect stdin from terminal to prevent stealing input
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        } else {
            // >(cmd): parent writes to pipe, command reads
            close(pipefd[1]); // Close write end
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
        }

        // Execute the command list in the process substitution
        // Create a child executor
        executor_t *child_executor = executor_new();
        if (!child_executor) {
            subshell_cleanup();
            _exit(1);
        }

        // Copy function definitions to child
        copy_function_definitions(child_executor, executor);

        // Execute each command in the process substitution
        int result = 0;
        node_t *cmd = proc_sub->first_child;
        while (cmd) {
            result = execute_node(child_executor, cmd);
            cmd = cmd->next_sibling;
        }

        executor_free(child_executor);
        fflush(stdout);
        fflush(stderr);
        subshell_cleanup();
        _exit(result);
    }

    // Parent process
    char *path = NULL;
    int kept_fd = -1;

    if (is_input) {
        // <(cmd): We need to provide a readable path
        // Close write end, keep read end
        close(pipefd[1]);
        kept_fd = pipefd[0];

        // Use /dev/fd/N mechanism if available (macOS and Linux)
        path = malloc(32);
        if (path) {
            snprintf(path, 32, "/dev/fd/%d", kept_fd);
        }
    } else {
        // >(cmd): We need to provide a writable path
        // Close read end, keep write end
        close(pipefd[0]);
        kept_fd = pipefd[1];

        path = malloc(32);
        if (path) {
            snprintf(path, 32, "/dev/fd/%d", kept_fd);
        }
    }

    // Track this fd and pid for cleanup after command execution
    // This prevents fd leaks and zombie processes with nested process
    // substitutions
    if (kept_fd >= 0 && executor->procsub_fd_count < 32) {
        executor->procsub_fds[executor->procsub_fd_count] = kept_fd;
        executor->procsub_pids[executor->procsub_fd_count] = pid;
        executor->procsub_fd_count++;
    }

    return path;
}

/* ============================================================================
 * HOOK FUNCTIONS (Phase 7: Zsh-Specific)
 * ============================================================================
 */

/**
 * @brief Flag to prevent recursive hook calls
 *
 * When a hook function runs a command, we don't want that command to trigger
 * another hook call. This flag prevents that recursion.
 */
static bool g_in_hook_execution = false;

/**
 * @brief Call a hook function if defined
 *
 * Executes a user-defined hook function (precmd, preexec, chpwd) if it exists.
 * Only active when FEATURE_HOOK_FUNCTIONS is enabled.
 *
 * @param executor Executor context
 * @param hook_name Name of the hook function (e.g., "precmd")
 * @param arg Optional argument to pass to the hook (e.g., command for preexec)
 * @return Exit status of hook function, or 0 if not defined
 */
int executor_call_hook(executor_t *executor, const char *hook_name,
                       const char *arg) {
    if (!executor || !hook_name) {
        return 0;
    }

    // Check if hook functions are enabled
    if (!shell_mode_allows(FEATURE_HOOK_FUNCTIONS)) {
        return 0;
    }

    // Prevent recursive hook calls
    if (g_in_hook_execution) {
        return 0;
    }

    // Look up the hook function
    function_def_t *func = find_function(executor, hook_name);
    if (!func) {
        return 0; // Hook not defined, that's fine
    }

    // Set recursion guard
    g_in_hook_execution = true;

    // Build argv for the function call
    int argc;
    char *argv[3];
    argv[0] = (char *)hook_name;

    if (arg) {
        argv[1] = (char *)arg;
        argv[2] = NULL;
        argc = 2;
    } else {
        argv[1] = NULL;
        argc = 1;
    }

    // Call the function (no AST node for hook invocations)
    int result = execute_function_call(executor, hook_name, argv, argc,
                                       SOURCE_LOC_UNKNOWN);

    // Handle return code translation (200-455 range is internal return signal)
    if (result >= 200 && result <= 455) {
        result = result - 200;
    }

    // Clear recursion guard
    g_in_hook_execution = false;

    return result;
}

/**
 * @brief Call precmd hook (before prompt display)
 *
 * @param executor Executor context
 * @return Exit status of hook, or 0 if not defined
 */
int executor_call_precmd(executor_t *executor) {
    return executor_call_hook(executor, "precmd", NULL);
}

/**
 * @brief Call preexec hook (before command execution)
 *
 * @param executor Executor context
 * @param command The command about to be executed
 * @return Exit status of hook, or 0 if not defined
 */
int executor_call_preexec(executor_t *executor, const char *command) {
    return executor_call_hook(executor, "preexec", command);
}

/**
 * @brief Call chpwd hook (after directory change)
 *
 * @param executor Executor context
 * @return Exit status of hook, or 0 if not defined
 */
int executor_call_chpwd(executor_t *executor) {
    return executor_call_hook(executor, "chpwd", NULL);
}

/**
 * @brief Check if currently executing inside a hook
 *
 * @return true if in hook execution
 */
bool executor_in_hook(void) { return g_in_hook_execution; }

/* ============================================================================
 * Typed-function form
 *
 * Surface grammar lives in parser.c (parse_fn_declaration,
 * parse_fn_call_expression, parse_let_fn_call, parse_fn_return_statement).
 * The executor owns the registry, the call mechanism, and the typed
 * return unwind. Free names inside a fn body resolve through the
 * captured declaration-site scope via a SCOPE_LEXICAL frame, giving
 * the body lexical (closure) semantics distinct from POSIX-form
 * functions which use dynamic scoping.
 * ============================================================================
 */

#define SHELL_FN_RETURN_STATUS 500

typedef struct typed_fn_param {
    char *name;
    lush_value_kind_t kind;
    struct typed_fn_param *next;
} typed_fn_param_t;

typedef struct typed_fn {
    char *name;
    typed_fn_param_t *params;
    int param_count;
    lush_value_kind_t return_kind;
    bool has_return_kind;
    node_t *body;
    /**
     * Captured declaration-site scope. The closure environment for
     * free names in the body. Captured at execute_typed_fn_decl time
     * via symtable_capture_scope_for_lexical; fed back to
     * symtable_push_lexical_scope at each call. Opaque on this side --
     * the symtable layer owns the underlying scope.
     */
    void *captured_scope;
    struct typed_fn *next;
} typed_fn_t;

static lush_value_kind_t parse_fn_kind_name(const char *text) {
    if (!text) {
        return LUSH_VALUE_NONE;
    }
    if (strcmp(text, "scalar") == 0) {
        return LUSH_VALUE_SCALAR;
    }
    if (strcmp(text, "list") == 0) {
        return LUSH_VALUE_LIST;
    }
    if (strcmp(text, "map") == 0) {
        return LUSH_VALUE_MAP;
    }
    return LUSH_VALUE_NONE;
}

static const char *fn_kind_name(lush_value_kind_t k) {
    switch (k) {
    case LUSH_VALUE_SCALAR:
        return "scalar";
    case LUSH_VALUE_LIST:
        return "list";
    case LUSH_VALUE_MAP:
        return "map";
    default:
        return "void";
    }
}

// Deep-copy an array_value_t. The source remains owned by its
// originator (typically the caller's scope binding); the returned
// copy is independent and owned by the caller of this helper. Returns
// NULL on allocation failure.
static array_value_t *typed_fn_dup_array(const array_value_t *src) {
    if (!src) {
        return NULL;
    }
    bool is_assoc = src->is_associative;
    array_value_t *dup = symtable_array_create(is_assoc);
    if (!dup) {
        return NULL;
    }
    if (is_assoc) {
        // Walk insertion order so the copy preserves the map's
        // original key ordering rather than reflecting hashtable
        // iteration order.
        for (size_t i = 0; i < src->assoc_insertion_count; i++) {
            const char *key = src->assoc_insertion_order[i];
            const char *value =
                symtable_array_get_assoc((array_value_t *)src, key);
            if (symtable_array_set_assoc(dup, key, value ? value : "") != 0) {
                symtable_array_free(dup);
                return NULL;
            }
        }
    } else {
        size_t n = symtable_array_length((array_value_t *)src);
        for (size_t i = 0; i < n; i++) {
            const char *value =
                symtable_array_get_index((array_value_t *)src, (int)i);
            if (symtable_array_append(dup, value ? value : "") < 0) {
                symtable_array_free(dup);
                return NULL;
            }
        }
    }
    return dup;
}

static void typed_fn_param_free(typed_fn_param_t *p) {
    while (p) {
        typed_fn_param_t *next = p->next;
        free(p->name);
        free(p);
        p = next;
    }
}

static void typed_fn_free(typed_fn_t *f) {
    if (!f) {
        return;
    }
    free(f->name);
    typed_fn_param_free(f->params);
    // body was deep-copied via copy_ast_chain at registration time
    // (see execute_typed_fn_decl); the registry owns the copy.
    if (f->body) {
        free_node_tree(f->body);
    }
    free(f);
}

static void executor_typed_fns_clear(executor_t *executor) {
    if (!executor) {
        return;
    }
    typed_fn_t *f = executor->typed_fns;
    while (f) {
        typed_fn_t *next = f->next;
        typed_fn_free(f);
        f = next;
    }
    executor->typed_fns = NULL;
}

static typed_fn_t *executor_typed_fn_find(executor_t *executor,
                                          const char *name) {
    if (!executor || !name) {
        return NULL;
    }
    for (typed_fn_t *f = executor->typed_fns; f; f = f->next) {
        if (f->name && strcmp(f->name, name) == 0) {
            return f;
        }
    }
    return NULL;
}

// Decode the NODE_FN_DECL encoded val.str of the form
//   "name\x1F<return_kind>\x1F<p1>:<k1>\x1F<p2>:<k2>..."
// into a typed_fn_t record. Returns NULL on malformed encoding.
static typed_fn_t *decode_fn_signature(const char *encoded) {
    if (!encoded) {
        return NULL;
    }
    const char *sep1 = strchr(encoded, '\x1f');
    if (!sep1) {
        return NULL;
    }
    size_t name_len = (size_t)(sep1 - encoded);

    typed_fn_t *fn = calloc(1, sizeof(typed_fn_t));
    if (!fn) {
        return NULL;
    }
    fn->name = malloc(name_len + 1);
    if (!fn->name) {
        free(fn);
        return NULL;
    }
    memcpy(fn->name, encoded, name_len);
    fn->name[name_len] = '\0';

    const char *cursor = sep1 + 1;
    const char *sep2 = strchr(cursor, '\x1f');
    size_t rk_len = sep2 ? (size_t)(sep2 - cursor) : strlen(cursor);

    if (rk_len > 0) {
        char rk_buf[32];
        if (rk_len >= sizeof(rk_buf)) {
            typed_fn_free(fn);
            return NULL;
        }
        memcpy(rk_buf, cursor, rk_len);
        rk_buf[rk_len] = '\0';
        fn->return_kind = parse_fn_kind_name(rk_buf);
        fn->has_return_kind = fn->return_kind != LUSH_VALUE_NONE;
        if (!fn->has_return_kind) {
            typed_fn_free(fn);
            return NULL;
        }
    } else {
        fn->return_kind = LUSH_VALUE_NONE;
        fn->has_return_kind = false;
    }

    if (!sep2) {
        // No parameters.
        return fn;
    }
    cursor = sep2 + 1;

    typed_fn_param_t *tail = NULL;
    while (cursor && *cursor) {
        const char *next_sep = strchr(cursor, '\x1f');
        size_t entry_len =
            next_sep ? (size_t)(next_sep - cursor) : strlen(cursor);
        if (entry_len == 0) {
            cursor = next_sep ? next_sep + 1 : NULL;
            continue;
        }
        char entry[256];
        if (entry_len >= sizeof(entry)) {
            typed_fn_free(fn);
            return NULL;
        }
        memcpy(entry, cursor, entry_len);
        entry[entry_len] = '\0';

        char *colon = strchr(entry, ':');
        if (!colon || colon == entry || colon[1] == '\0') {
            typed_fn_free(fn);
            return NULL;
        }
        *colon = '\0';
        const char *pname = entry;
        const char *pkind = colon + 1;

        typed_fn_param_t *p = calloc(1, sizeof(typed_fn_param_t));
        if (!p) {
            typed_fn_free(fn);
            return NULL;
        }
        p->name = strdup(pname);
        p->kind = parse_fn_kind_name(pkind);
        if (!p->name || p->kind == LUSH_VALUE_NONE) {
            free(p->name);
            free(p);
            typed_fn_free(fn);
            return NULL;
        }

        if (tail) {
            tail->next = p;
        } else {
            fn->params = p;
        }
        tail = p;
        fn->param_count++;

        cursor = next_sep ? next_sep + 1 : NULL;
    }

    return fn;
}

// Register (or replace) a typed function declaration. Mirrors the
// POSIX function-table semantics: redeclaring the same name replaces
// the prior record.
static int execute_typed_fn_decl(executor_t *executor, node_t *node) {
    if (!executor || !node || !node->val.str) {
        return 1;
    }
    typed_fn_t *fn = decode_fn_signature(node->val.str);
    if (!fn) {
        executor_error_report(executor, SHELL_ERR_FUNCTION_ERROR, node->loc,
                              "malformed typed-function signature");
        return 1;
    }
    // Deep-copy the body so the registry survives the source AST
    // being freed at the end of this batch (executor_execute_command_line
    // frees `ast` after dispatch). copy_ast_chain produces a tree
    // independent of the parser's allocations.
    if (node->first_child) {
        fn->body = copy_ast_chain(node->first_child);
        if (!fn->body) {
            typed_fn_free(fn);
            executor_error_report(executor, SHELL_ERR_FUNCTION_ERROR, node->loc,
                                  "failed to copy typed-function body");
            return 1;
        }
    }

    // Capture the declaration-site scope as the lexical-closure parent.
    // For a top-level `fn`, this is the global scope, which the symtable
    // never pops -- the borrowed pointer is valid for the manager's
    // lifetime. For an `fn` declared inside another scope, the captured
    // pointer is only valid until that enclosing scope pops; once
    // re-decl of an inner fn from an already-popped enclosing scope
    // becomes a real pattern, the registry will need a stronger
    // lifetime guarantee. Today's typical case (top-level fn) is safe.
    fn->captured_scope = symtable_capture_scope_for_lexical(executor->symtable);

    // Replace existing record under the same name, if any.
    typed_fn_t **slot = &executor->typed_fns;
    while (*slot) {
        if ((*slot)->name && strcmp((*slot)->name, fn->name) == 0) {
            typed_fn_t *old = *slot;
            *slot = old->next;
            typed_fn_free(old);
            break;
        }
        slot = &(*slot)->next;
    }
    fn->next = executor->typed_fns;
    executor->typed_fns = fn;
    return 0;
}

// Evaluate one NODE_FN_CALL argument expression to a kind-tagged
// value. Argument forms accepted today: scalar literals (the parser
// stored the arg text in val.str of a NODE_COMMAND); `$var` references
// resolved to the var's current value+kind; nested NODE_FN_CALL
// returning a typed value.
//
// The caller owns the returned view (scalar_value is strdup'd; array
// is borrowed from the symtable's current store and remains valid
// until the call site's scope walk concludes).
static int eval_fn_call_argument(executor_t *executor, node_t *arg,
                                 lush_value_view_t *out) {
    if (!out) {
        return 1;
    }
    out->kind = LUSH_VALUE_NONE;
    out->scalar_value = NULL;
    out->array = NULL;

    if (!arg) {
        return 1;
    }

    if (arg->type == NODE_FN_CALL) {
        // Nested typed-fn call as an argument expression.
        executor->typed_fn_return_pending = false;
        lush_value_view_clear(&executor->typed_fn_return_value);
        int rc = execute_node(executor, arg);
        if (rc != 0 && rc != SHELL_FN_RETURN_STATUS) {
            return rc;
        }
        if (!executor->typed_fn_return_pending) {
            executor_error_report(
                executor, SHELL_ERR_TYPE_MISMATCH, arg->loc,
                "typed-function call returned no value (void) but a "
                "value is required as an argument");
            return 1;
        }
        *out = executor->typed_fn_return_value;
        executor->typed_fn_return_value.kind = LUSH_VALUE_NONE;
        executor->typed_fn_return_value.scalar_value = NULL;
        executor->typed_fn_return_value.array = NULL;
        executor->typed_fn_return_pending = false;
        return 0;
    }

    const char *text = arg->val.str;
    if (!text) {
        out->kind = LUSH_VALUE_SCALAR;
        out->scalar_value = strdup("");
        return out->scalar_value ? 0 : 1;
    }

    // NODE_VAR carries a bare $name (or ${name}) reference. Strip the
    // leading '$' / '${...}' to get the variable name and look it up
    // kind-aware via symtable_lookup. A list or map binding crosses
    // the call boundary as-is, preserving its kind tag for the
    // parameter-bind step rather than being flattened to a string.
    if (arg->type == NODE_VAR) {
        const char *var_name = text;
        char name_buf[256];
        if (var_name[0] == '$') {
            var_name++;
        }
        if (var_name[0] == '{') {
            const char *close = strchr(var_name, '}');
            if (close) {
                size_t len = (size_t)(close - var_name - 1);
                if (len < sizeof(name_buf)) {
                    memcpy(name_buf, var_name + 1, len);
                    name_buf[len] = '\0';
                    var_name = name_buf;
                }
            }
        }
        lush_value_view_t v = {LUSH_VALUE_NONE, NULL, NULL};
        if (symtable_lookup(var_name, &v) && v.kind != LUSH_VALUE_NONE) {
            *out = v;
            return 0;
        }
        // Unset variable -- treat as empty scalar, matching POSIX.
        out->kind = LUSH_VALUE_SCALAR;
        out->scalar_value = strdup("");
        return out->scalar_value ? 0 : 1;
    }

    // Single-quoted strings carry their text literally; no expansion.
    if (arg->type == NODE_STRING_LITERAL) {
        out->kind = LUSH_VALUE_SCALAR;
        out->scalar_value = strdup(text);
        return out->scalar_value ? 0 : 1;
    }

    // Bare words, double-quoted strings (NODE_STRING_EXPANDABLE), and
    // arithmetic numerals all pass through the standard word-expansion
    // path. This is where `"hello $name!"` style interpolation gets
    // resolved correctly.
    char *expanded = expand_if_needed(executor, text);
    out->kind = LUSH_VALUE_SCALAR;
    out->scalar_value = expanded ? expanded : strdup("");
    return out->scalar_value ? 0 : 1;
}

// Bind one parameter (name + declared kind) to an argument value in
// the current (just-pushed) function scope. Raises a type-mismatch
// error if the kinds disagree.
static int bind_typed_fn_param(executor_t *executor, source_location_t loc,
                               const typed_fn_param_t *param,
                               lush_value_view_t *value, const char *callee) {
    if (param->kind != value->kind) {
        executor_error_report(
            executor, SHELL_ERR_TYPE_MISMATCH, loc,
            "argument '%s' to '%s' is %s; declared kind is %s", param->name,
            callee, fn_kind_name(value->kind), fn_kind_name(param->kind));
        lush_value_view_clear(value);
        return 1;
    }
    switch (param->kind) {
    case LUSH_VALUE_SCALAR:
        if (symtable_set_local_var(executor->symtable, param->name,
                                   value->scalar_value ? value->scalar_value
                                                       : "") != 0) {
            lush_value_view_clear(value);
            return 1;
        }
        break;
    case LUSH_VALUE_LIST:
    case LUSH_VALUE_MAP: {
        if (!value->array) {
            executor_error_report(executor, SHELL_ERR_TYPE_MISMATCH, loc,
                                  "argument '%s' to '%s' is %s but the "
                                  "underlying value is missing",
                                  param->name, callee,
                                  fn_kind_name(param->kind));
            return 1;
        }
        // Deep-copy: the function scope owns its parameter binding.
        // The caller's array remains intact when this scope pops.
        array_value_t *bound = typed_fn_dup_array(value->array);
        if (!bound) {
            lush_value_view_clear(value);
            return 1;
        }
        if (symtable_set_array(param->name, bound) != 0) {
            symtable_array_free(bound);
            lush_value_view_clear(value);
            return 1;
        }
        break;
    }
    default:
        lush_value_view_clear(value);
        return 1;
    }
    lush_value_view_clear(value);
    return 0;
}

// Execute a NODE_FN_CALL. On a non-void return, sets
// executor->typed_fn_return_pending = true and stashes the value in
// executor->typed_fn_return_value for the caller (either NODE_LET_FN
// or a nested call) to consume. Returns 0 on success / non-void
// return, non-zero on error.
static int execute_typed_fn_call_node(executor_t *executor, node_t *node) {
    if (!executor || !node || node->type != NODE_FN_CALL || !node->val.str) {
        return 1;
    }
    const char *callee = node->val.str;
    typed_fn_t *fn = executor_typed_fn_find(executor, callee);
    if (!fn) {
        executor_error_report(executor, SHELL_ERR_INVALID_FUNCTION, node->loc,
                              "no typed function named '%s' is in scope",
                              callee);
        return 1;
    }

    // Count arguments and verify arity before pushing scope.
    int argc = 0;
    for (node_t *a = node->first_child; a; a = a->next_sibling) {
        argc++;
    }
    if (argc != fn->param_count) {
        executor_error_report(executor, SHELL_ERR_INVALID_ARGUMENT, node->loc,
                              "typed call '%s': expected %d argument%s, got %d",
                              callee, fn->param_count,
                              fn->param_count == 1 ? "" : "s", argc);
        return 1;
    }

    // Evaluate arguments BEFORE pushing scope so $var references
    // resolve in the caller's scope (the typed-fn body should not see
    // them as locals; they are values passed in).
    lush_value_view_t *arg_values =
        argc > 0 ? calloc((size_t)argc, sizeof(lush_value_view_t)) : NULL;
    if (argc > 0 && !arg_values) {
        return 1;
    }
    int idx = 0;
    int rc = 0;
    for (node_t *a = node->first_child; a; a = a->next_sibling) {
        rc = eval_fn_call_argument(executor, a, &arg_values[idx]);
        if (rc != 0) {
            for (int i = 0; i < idx; i++) {
                lush_value_view_clear(&arg_values[i]);
            }
            free(arg_values);
            return rc;
        }
        idx++;
    }

    // Push a SCOPE_LEXICAL frame parented at the captured declaration
    // site rather than the dynamic caller, so free names in the body
    // resolve through the closure environment. If the fn was declared
    // at top level the captured scope is the global scope, which
    // behaves identically to dynamic scoping for global variables --
    // the distinction surfaces when a POSIX-form caller has its own
    // locals; those are now invisible to the typed-fn body.
    int push_rc;
    if (fn->captured_scope) {
        push_rc = symtable_push_lexical_scope(executor->symtable, callee,
                                              fn->captured_scope);
    } else {
        push_rc =
            symtable_push_scope(executor->symtable, SCOPE_FUNCTION, callee);
    }
    if (push_rc != 0) {
        for (int i = 0; i < argc; i++) {
            lush_value_view_clear(&arg_values[i]);
        }
        free(arg_values);
        executor_error_report(executor, SHELL_ERR_FUNCTION_ERROR, node->loc,
                              "failed to push scope for typed call '%s'",
                              callee);
        return 1;
    }

    typed_fn_param_t *p = fn->params;
    for (int i = 0; i < argc; i++) {
        rc =
            bind_typed_fn_param(executor, node->loc, p, &arg_values[i], callee);
        if (rc != 0) {
            for (int j = i + 1; j < argc; j++) {
                lush_value_view_clear(&arg_values[j]);
            }
            free(arg_values);
            symtable_pop_scope(executor->symtable);
            return rc;
        }
        p = p->next;
    }
    free(arg_values);

    executor_push_context(executor, node->loc, "in typed call '%s'", callee);

    // Push a debug frame and mark it lexical so `debug stack` renders
    // the discipline. The push only happens when the debug subsystem
    // is enabled -- mirrors how execute_command guards its push.
    bool debug_frame_pushed = false;
    if (g_debug_context && g_debug_context->enabled) {
        debug_push_frame(g_debug_context, callee, NULL, (int)node->loc.line);
        debug_mark_current_frame_lexical(g_debug_context);
        debug_frame_pushed = true;
    }

    // Execute body. Any NODE_FN_RETURN inside surfaces as
    // SHELL_FN_RETURN_STATUS, which we consume here.
    int result = 0;
    executor->typed_fn_return_pending = false;
    lush_value_view_clear(&executor->typed_fn_return_value);

    for (node_t *cmd = fn->body ? fn->body->first_child : NULL; cmd;
         cmd = cmd->next_sibling) {
        result = execute_node(executor, cmd);
        if (result == SHELL_FN_RETURN_STATUS) {
            result = 0;
            break;
        }
        if (executor->shell_exit_requested) {
            break;
        }
        if (result != 0 && result < 200) {
            break;
        }
    }

    if (debug_frame_pushed) {
        debug_pop_frame(g_debug_context);
    }
    executor_pop_context(executor);
    symtable_pop_scope(executor->symtable);

    if (result != 0) {
        lush_value_view_clear(&executor->typed_fn_return_value);
        executor->typed_fn_return_pending = false;
        return result;
    }

    // Validate the captured return against the declared return kind.
    if (fn->has_return_kind && !executor->typed_fn_return_pending) {
        executor_error_report(
            executor, SHELL_ERR_TYPE_MISMATCH, node->loc,
            "typed call '%s' declared '-> %s' but the body returned "
            "no value",
            callee, fn_kind_name(fn->return_kind));
        return 1;
    }
    if (!fn->has_return_kind && executor->typed_fn_return_pending) {
        executor_error_report(
            executor, SHELL_ERR_TYPE_MISMATCH, node->loc,
            "typed call '%s' has no declared return kind but the "
            "body returned a value",
            callee);
        lush_value_view_clear(&executor->typed_fn_return_value);
        executor->typed_fn_return_pending = false;
        return 1;
    }
    if (fn->has_return_kind &&
        executor->typed_fn_return_value.kind != fn->return_kind) {
        executor_error_report(
            executor, SHELL_ERR_TYPE_MISMATCH, node->loc,
            "typed call '%s' declared '-> %s' but the return value "
            "is %s",
            callee, fn_kind_name(fn->return_kind),
            fn_kind_name(executor->typed_fn_return_value.kind));
        lush_value_view_clear(&executor->typed_fn_return_value);
        executor->typed_fn_return_pending = false;
        return 1;
    }
    return 0;
}

// Top-level dispatch for a NODE_FN_CALL appearing as a statement (its
// return value is discarded). Used when the user writes `name(args)`
// or `name args` at command position.
static int execute_typed_fn_call(executor_t *executor, node_t *node) {
    int rc = execute_typed_fn_call_node(executor, node);
    // Discard any return value the call produced.
    lush_value_view_clear(&executor->typed_fn_return_value);
    executor->typed_fn_return_pending = false;
    return rc;
}

// `let name = call(args)` capture form. Invokes the call, then binds
// the LHS in the current scope (kind-aware).
static int execute_typed_let_fn(executor_t *executor, node_t *node) {
    if (!executor || !node || !node->val.str || !node->first_child) {
        return 1;
    }
    const char *lhs = node->val.str;
    node_t *call = node->first_child;
    if (call->type != NODE_FN_CALL) {
        return 1;
    }

    int rc = execute_typed_fn_call_node(executor, call);
    if (rc != 0) {
        return rc;
    }

    if (!executor->typed_fn_return_pending) {
        executor_error_report(
            executor, SHELL_ERR_TYPE_MISMATCH, node->loc,
            "'let %s = %s(...)' but the call has no declared return "
            "kind (void function)",
            lhs, call->val.str);
        return 1;
    }

    lush_value_view_t v = executor->typed_fn_return_value;
    executor->typed_fn_return_value.kind = LUSH_VALUE_NONE;
    executor->typed_fn_return_value.scalar_value = NULL;
    executor->typed_fn_return_value.array = NULL;
    executor->typed_fn_return_pending = false;

    switch (v.kind) {
    case LUSH_VALUE_SCALAR:
        rc =
            symtable_set_var(executor->symtable, lhs,
                             v.scalar_value ? v.scalar_value : "", SYMVAR_NONE);
        break;
    case LUSH_VALUE_LIST:
    case LUSH_VALUE_MAP: {
        // The captured return view borrows from the callee's scope.
        // That scope has already popped by the time we get here, but
        // executor->typed_fn_return_value held the borrow; the
        // underlying array is still valid because the unwind moved
        // ownership semantics through the view. Take a deep copy so
        // the caller's binding is independent.
        array_value_t *bound = typed_fn_dup_array(v.array);
        if (!bound) {
            rc = 1;
            break;
        }
        if (symtable_set_array(lhs, bound) != 0) {
            symtable_array_free(bound);
            rc = 1;
        }
        break;
    }
    default:
        rc = 1;
        break;
    }
    lush_value_view_clear(&v);
    return rc;
}

// NODE_FN_RETURN handler. Evaluates the optional return expression to
// a kind-tagged value, stashes it in executor->typed_fn_return_value,
// and unwinds via SHELL_FN_RETURN_STATUS.
static int execute_typed_fn_return(executor_t *executor, node_t *node) {
    if (!executor) {
        return 1;
    }
    lush_value_view_clear(&executor->typed_fn_return_value);
    executor->typed_fn_return_pending = false;

    node_t *expr = node->first_child;
    if (expr) {
        lush_value_view_t v = {LUSH_VALUE_NONE, NULL, NULL};
        int rc = eval_fn_call_argument(executor, expr, &v);
        if (rc != 0) {
            return rc;
        }
        executor->typed_fn_return_value = v;
        executor->typed_fn_return_pending = true;
    }
    return SHELL_FN_RETURN_STATUS;
}
