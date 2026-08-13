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
#include "autoload.h"
#include "brace_match.h"
#include "builtins.h"
#include "config.h"
#include "debug.h"
#include "dequote.h"
#include "escape.h"
#include "field_split.h"
#include "ht.h"
#include "identifier.h"
#include "init.h"
#include "input_continuation.h"
#include "lle/lle_pager.h"
#include "lle/lle_shell_event_hub.h"
#include "lle/lle_shell_integration.h"
#include "lle/unicode_case.h"
#include "lle/unicode_compare.h"
#include "lle/unicode_grapheme.h"
#include "lle/utf8_support.h"
#include "lush.h"
#include "lush_fork.h"
#include "node.h"
#include "node_to_source.h"
#include "param_op.h"
#include "parser.h"
#include "pattern_match.h"
#include "redirection.h"
#include "restricted_mode.h"
#include "shell_mode.h"
#include "signals.h"
#include "subscript_key.h"
#include "symtable.h"
#include "tokenizer.h" /// QUOTE_PROV_* byte values for node_t.quote_prov (#498)
#include "word.h"      /// word_copy for node_t.word (Word CST integration)
#include "word_eval.h" /// word_eval: dual-route covered args on the CST backbone

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /// strcasecmp (LUSH_WORD_CST opt-out parsing)
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/// Global executor pointer for job control builtins
executor_t *current_executor = NULL;

/// Forward declarations
/// Forward declarations - updated for symtable
static int execute_node(executor_t *executor, node_t *node);
static int execute_command(executor_t *executor, node_t *command);
static int execute_pipeline(executor_t *executor, node_t *pipeline);
static void executor_publish_pipestatus(const int *exits, size_t count);
static int execute_function_definition(executor_t *executor, node_t *function);
/// Typed-function form -- declaration, call, return, let-capture.
static void executor_typed_fns_clear(executor_t *executor);
static void free_process_list(process_t *processes);
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
node_t *node_copy(node_t *node);
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
                                  char ***out_vec, int *out_count,
                                  bool positional_only);

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

static void cleanup_procsub_fds(executor_t *executor);

/// The parameter-expansion operator table. Longer operators precede the
/// shorter ones they contain so the scalar detection loop resolves e.g.
/// `##` before `#` and `:-` before `:`. Indexed by op_type throughout the
/// expansion engine; the single source of truth shared by the scalar
/// ${var op} detection, the array-element ${arr[k] op} detection, and the
/// operator applier so no path carries a private copy of this list.
/// The INDEX of each entry is a cross-module contract: param_op.c dispatches
/// the pure operators on these numbers (lush_param_op_apply), and word_parse.c
/// stores them in the Word CST. Reordering or inserting an entry silently
/// re-maps every operator -- append only, and update param_op.h with it.
static const char *const param_operators[] = {
    ":-", /// 0: use default if unset or empty
    ":+", /// 1: use alternative if set and non-empty
    "##", /// 2: remove longest prefix pattern
    "%%", /// 3: remove longest suffix pattern
    "^^", /// 4: uppercase all
    ",,", /// 5: lowercase all
    "#",  /// 6: remove shortest prefix pattern
    "%",  /// 7: remove shortest suffix pattern
    "^",  /// 8: uppercase first
    ",",  /// 9: lowercase first
    "-",  /// 10: use default if unset
    "+",  /// 11: use alternative if set
    ":=", /// 12: assign default if unset or empty
    "=",  /// 13: assign default if unset
    ":",  /// 14: substring / zsh modifiers
    "//", /// 15: replace all occurrences
    "/",  /// 16: replace first occurrence
    "@",  /// 17: transformations
    ":?", /// 18: error if unset or null (POSIX)
    "?",  /// 19: error if unset (POSIX)
    NULL};

/// Apply a resolved parameter-expansion operator to a scalar value.
/// PURE: never mutates the symbol table. For the assign operators
/// (:= / =) it returns the value to be assigned and sets *assign_back so
/// the caller writes it to the correct lvalue -- a scalar variable for
/// ${var:=x}, an array element for ${arr[k]:=x}. Shared by the scalar
/// ${var op} path and the single-element ${arr[k] op} path. Neither
/// var_value nor expanded_default is freed (caller owns them).
static char *apply_param_operator(executor_t *executor, const char *var_name,
                                  char *var_value, char *expanded_default,
                                  int op_type, bool *assign_back);

/// Identify the operator a trailing ${arr[k]OP...} suffix begins with.
/// The suffix starts exactly at the operator, so the longest operator in
/// param_operators[] that prefixes it wins (`:=` over `:`, `##` over `#`,
/// `//` over `/`). Returns the op_type and points *rhs_out at the operand
/// following the operator, or -1 when the suffix is not an operator.
static int detect_param_operator_suffix(const char *suffix,
                                        const char **rhs_out);

/// Forward declarations for POSIX compliance
bool is_posix_mode_enabled(void);
bool is_pipefail_enabled(void);
bool is_pipeline_diagnostic_enabled(void);

/// `$((` arithmetic-vs-command-sub disambiguation, shared with the
/// tokenizer (defined in tokenizer.c, declared in tokenizer.h which the
/// executor does not include).
bool lush_dollar_paren_is_arithmetic(const char *content, size_t remaining);
static int execute_external_command_with_setup(executor_t *executor,
                                               char **argv,
                                               bool redirect_stderr,
                                               node_t *command);
static int execute_builtin_command(executor_t *executor, char **argv,
                                   source_location_t loc);
static int execute_brace_group(executor_t *executor, node_t *group);
static int execute_subshell(executor_t *executor, node_t *subshell);
static int execute_negate(executor_t *executor, node_t *negate_node);

/// Forward declarations for Arrays and Arithmetic
static int execute_arithmetic_command(executor_t *executor, node_t *arith_node);
static int execute_array_assignment(executor_t *executor, node_t *assign_node);
static int execute_array_append(executor_t *executor, node_t *append_node);

/// Forward declarations for Extended Tests
static int execute_extended_test(executor_t *executor, node_t *test_node);

/// Forward declarations for Process Substitution
char *expand_process_substitution(executor_t *executor, node_t *proc_sub);
static bool is_builtin_command(const char *cmd);
static void set_executor_error(executor_t *executor, const char *message);
static char *expand_variable(executor_t *executor, const char *var_text);
static char *expand_tilde(const char *text);
static char *colon_segmented_tilde_expand(const char *value);
static char *magic_equal_tilde_expand(const char *word);
static char **expand_glob_pattern(const char *pattern, int *expanded_count);
static char **apply_glob_qualifier_to_literal(const char *value, int *count);
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
static void copy_function_definitions(executor_t *dest, executor_t *src);
char *expand_if_needed(executor_t *executor, const char *text);
static char *expand_quoted_string(executor_t *executor, const char *str,
                                  bool in_double_quotes);
/// Provenance-aware variant: `prov` is a per-character quote map parallel to
/// `str` (see node_t.quote_prov), or NULL. NULL is byte-identical to
/// expand_quoted_string; a map lets a fused mixed-quote word expand each
/// character by its own quote context (#498).
static char *expand_quoted_string_prov(executor_t *executor, const char *str,
                                       bool in_double_quotes, const char *prov,
                                       bool allow_tilde);
/// How a `${var OP operand}` operand is processed once the tokenizer hands the
/// whole `${...}` over verbatim: a VALUE operand (`:-`/`:=`/`:+`/`:?` ...) is a
/// word (quote-removed, expanded, tilde-expanded); a PATTERN operand
/// (`#`/`##`/`%`/`%%`, case-mod restrictors) is quote-removed + expanded, then
/// its quoted metacharacters are made glob-literal so they match as text.
typedef enum { PE_OPERAND_VALUE, PE_OPERAND_PATTERN } pe_operand_class_t;

/// True if op_type's operand is dequoted here, setting *out_class. False for
/// operators whose operand is handled elsewhere: substring (`:`) and the
/// transform (`@`) families. The replace operators (`/`/`//`) are dequoted
/// per-half after their separator split, not through this predicate.
static bool pe_operand_op(int op_type, pe_operand_class_t *out_class);

/// Quote-remove + expand a parameter-expansion operand carried verbatim inside
/// a `${...}`, composing lush_dequote_span with expand_quoted_string_prov (and,
/// for PATTERN operands, glob-suppressing the quoted parts). Returns an owned
/// string.
static char *pe_process_operand(executor_t *executor, const char *raw,
                                size_t len, pe_operand_class_t cls,
                                bool in_double_quotes);

/// Quote-remove + expand a REPLACE operand (`${v/pat/repl}`), which carries two
/// halves and so cannot go through pe_process_operand. Returns an owned string.
static char *pe_process_replace_operand(executor_t *executor, const char *raw,
                                        size_t len);

/// Remove one level of quoting from an INDEXED-array subscript before it is
/// evaluated as arithmetic. Returns an owned string.
static char *pe_dequote_subscript(const char *raw);

/// expand_arg_node is declared in executor.h -- it is the shared per-node-type
/// word expander, also consumed by the redirection/here-string target path.
static char *expand_array_unsubscripted(executor_t *executor,
                                        array_value_t *array,
                                        const char *arr_name);
static void executor_request_posix_exit(executor_t *executor, int status);
static bool is_assignment(const char *text);
static int execute_assignment(executor_t *executor, const char *assignment,
                              source_location_t loc);

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
        return true; /// Allow if not in privileged mode
    }

    /// Block commands containing '/' (absolute/relative paths)
    if (strchr(command, '/') != NULL) {
        return false;
    }

    /// Block dangerous built-in commands in privileged mode
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
        return true; /// Allow if not in privileged mode
    }

    /// Block absolute path redirections
    if (target[0] == '/') {
        return false;
    }

    /// Block redirection to parent directories
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
        return true; /// Allow if not in privileged mode
    }

    /// Block PATH modifications
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
 * @brief Terminate the current process, correctly for a forked child.
 *
 * A forked child (getpid() != shell_pid) must terminate with _exit(): exit()
 * runs stdio cleanup, and fclosing the inherited, seekable script FILE*
 * repositions the shared file offset, dragging the parent shell's read position
 * backward so it re-reads and re-executes the rest of the script (Issue #441 /
 * #444). The top-level shell (getpid() == shell_pid) uses exit() so its atexit
 * cleanup -- history flush, symbol-table free, memory-pool shutdown -- runs.
 * Output is flushed either way. Never returns.
 *
 * @param status Exit status to terminate with.
 */
_Noreturn void lush_process_terminate(int status) {
    fflush(stdout);
    fflush(stderr);
    if (getpid() == shell_pid) {
        exit(status);
    }
    /// Forked child: free what atexit would have, then _exit without stdio
    /// cleanup so the shared script input fd is left untouched.
    subshell_cleanup();
    _exit(status);
}

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

    /// Use global symbol table manager from modernized legacy interface
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
    executor->async_context = false;
    executor->expansion_error = false;
    executor->expansion_exit_status = 0;
    executor->shell_exit_requested = false;
    executor->shell_exit_status = 0;
    executor->command_abort = false;
    executor->pending_readonly_var = NULL;
    executor->loop_control = LOOP_NORMAL;
    executor->loop_control_level = 0;
    executor->loop_depth = 0;
    executor->source_depth = 0;
    executor->source_return = false;

    /// Initialize error context stack
    executor->context_depth = 0;
    for (size_t i = 0; i < EXECUTOR_CONTEXT_STACK_MAX; i++) {
        executor->context_stack[i] = NULL;
        executor->context_locations[i] = SOURCE_LOC_UNKNOWN;
    }
    executor->active_loc = SOURCE_LOC_UNKNOWN;
    executor->active_comp_result = NULL;
    executor->active_comp_prefix = NULL;

    /// Initialize process substitution fd tracking
    executor->procsub_fd_count = 0;
    memset(executor->procsub_fds, -1, sizeof(executor->procsub_fds));
    memset(executor->procsub_pids, 0, sizeof(executor->procsub_pids));

    /// Variable-allocated fd registry starts empty; grown on demand.
    executor->alloc_fds = NULL;
    executor->alloc_fd_count = 0;
    executor->alloc_fd_cap = 0;

    /// Source-text retention starts empty; populated per-batch by
    /// executor_execute_command_line.
    executor->source_text = NULL;
    executor->source_starting_line = 0;

    /// Typed-function state starts empty.
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

    /// Use provided symtable
    executor->symtable = symtable;

    executor->interactive = false;
    executor->debug = false;
    executor->exit_status = 0;
    executor->error_message = NULL;
    executor->has_error = false;
    executor->functions = NULL;
    executor->current_script_file = NULL;
    executor->in_script_execution = false;
    executor->async_context = false;
    executor->expansion_error = false;
    executor->expansion_exit_status = 0;
    executor->shell_exit_requested = false;
    executor->shell_exit_status = 0;
    executor->command_abort = false;
    executor->pending_readonly_var = NULL;
    executor->loop_control = LOOP_NORMAL;
    executor->loop_control_level = 0;
    executor->loop_depth = 0;
    executor->source_depth = 0;
    executor->source_return = false;

    /// Initialize error context stack
    executor->context_depth = 0;
    for (size_t i = 0; i < EXECUTOR_CONTEXT_STACK_MAX; i++) {
        executor->context_stack[i] = NULL;
        executor->context_locations[i] = SOURCE_LOC_UNKNOWN;
    }
    executor->active_loc = SOURCE_LOC_UNKNOWN;
    executor->active_comp_result = NULL;
    executor->active_comp_prefix = NULL;

    /// Initialize process substitution fd tracking
    executor->procsub_fd_count = 0;
    memset(executor->procsub_fds, -1, sizeof(executor->procsub_fds));
    memset(executor->procsub_pids, 0, sizeof(executor->procsub_pids));

    /// Variable-allocated fd registry starts empty; grown on demand.
    executor->alloc_fds = NULL;
    executor->alloc_fd_count = 0;
    executor->alloc_fd_cap = 0;

    /// Source-text retention starts empty; populated per-batch by
    /// executor_execute_command_line.
    executor->source_text = NULL;
    executor->source_starting_line = 0;

    /// Typed-function state starts empty.
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
        /// Don't free global symtable - it's managed globally

        /// Free function table
        function_def_t *func = executor->functions;
        while (func) {
            function_def_t *next = func->next;
            free(func->name);
            free_node_tree(func->body);
            free_function_params(func->params);
            free(func);
            func = next;
        }

        /// Free the job list (each job owns its command line and process
        /// list). Background jobs are tracked for the life of the shell, so an
        /// unpruned completed job would otherwise leak at teardown.
        job_t *job = executor->jobs;
        while (job) {
            job_t *next_job = job->next;
            free_process_list(job->processes);
            free(job->command_line);
            free(job);
            job = next_job;
        }
        executor->jobs = NULL;

        /// Free typed-function registry.
        executor_typed_fns_clear(executor);

        /// Free any pending typed-return value (defensive; cleared on
        /// unwind under normal flow).
        lush_value_view_clear(&executor->typed_fn_return_value);

        /// Free script context
        free(executor->current_script_file);

        /// Free any unconsumed readonly-abort diagnostic (defensive;
        /// the execute_command chokepoint clears it under normal flow).
        free(executor->pending_readonly_var);
        executor->pending_readonly_var = NULL;

        /// Free error context stack
        executor_clear_context(executor);

        /// Close any variable-allocated fds the script never closed. Done
        /// before freeing the array; errors here are intentionally silent
        /// (the kernel will reclaim on process exit regardless, and a
        /// script-side close that already raced us would return EBADF).
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
            /// OOM during tracking: fd is still allocated and usable; just
            /// not auto-cleaned at shell exit. The kernel reclaims on exit.
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
            /// Swap-with-last, then shrink count. Order doesn't matter; the
            /// registry is treated as a set.
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
        /// Don't free the old symtable if it exists - it might be external
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

    /// Free existing script file name
    free(executor->current_script_file);

    /// Set new script context
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
 * Error Context Stack
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
/// Shared core for executor_error_report and executor_error_report_with_help:
/// create the structured error, attach the optional `= help:` suggestion, the
/// rust-style source snippet, and the context stack, display it, and set the
/// legacy error state. `suggestion` may be NULL for an error that carries no
/// help line.
/// Diagnostic capture for word_cst_audit. While muted, executor_report_error_v
/// renders into g_error_capture instead of stderr, so the audit's SECOND
/// (legacy) expansion of a word does not print a diagnostic the real run never
/// produced. Only word_cst_audit sets these, only around its own
/// expand_arg_node call, and a covered word cannot run a command (command
/// substitution defers at parse), so nothing else can have its errors
/// swallowed.
static bool g_error_display_muted = false;
static char *g_error_capture = NULL;

static void executor_report_error_v(executor_t *executor,
                                    shell_error_code_t code,
                                    source_location_t loc,
                                    const char *suggestion, const char *fmt,
                                    va_list args) {
    if (!executor) {
        return;
    }

    shell_error_t *error =
        shell_error_createv(code, SHELL_SEVERITY_ERROR, loc, fmt, args);
    if (!error) {
        /// Fallback to legacy error system
        set_executor_error(executor, "runtime error");
        return;
    }

    /// Actionable `= help:` line (rust-style), when the caller supplies one.
    if (suggestion) {
        shell_error_set_suggestion(error, suggestion);
    }

    /// Attach source line for the rust-style snippet block (`N | ... / ^~~~~`).
    /// Skipped for SOURCE_LOC_UNKNOWN since there is no line to look up.
    if (SOURCE_LOC_VALID(loc)) {
        char *src_line = executor_get_source_line(executor, loc.line);
        if (src_line) {
            shell_error_set_source_line(error, src_line, loc.column,
                                        loc.column + loc.length);
            free(src_line);
        }
    }

    /// Add context stack to error
    for (size_t i = 0;
         i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
        if (executor->context_stack[i]) {
            shell_error_push_context(error, "%s", executor->context_stack[i]);
        }
    }

    /// Display the error immediately -- unless a diagnostic-capturing caller
    /// has muted the channel (word_cst_audit, which expands a word a SECOND
    /// time purely to compare routes and must not double-print the result).
    if (g_error_display_muted) {
        char *buf = NULL;
        size_t len = 0;
        FILE *ms = open_memstream(&buf, &len);
        if (ms) {
            shell_error_display(error, ms, false);
            fclose(ms);
            if (!g_error_capture) {
                g_error_capture = buf;
            } else {
                /// More than one diagnostic in a single muted span: keep them
                /// all, the report is more useful than the first line alone.
                size_t have = strlen(g_error_capture);
                char *joined = realloc(g_error_capture, have + len + 1);
                if (joined) {
                    memcpy(joined + have, buf, len + 1);
                    g_error_capture = joined;
                }
                free(buf);
            }
        }
    } else {
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
    }

    /// Set legacy error state for compatibility - use NULL since error was
    /// already displayed
    executor->has_error = true;
    executor->error_message = NULL; /// Already displayed via structured system
    shell_error_free(error);
}

void executor_error_report(executor_t *executor, shell_error_code_t code,
                           source_location_t loc, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    executor_report_error_v(executor, code, loc, NULL, fmt, args);
    va_end(args);
}

void executor_error_report_with_help(executor_t *executor,
                                     shell_error_code_t code,
                                     source_location_t loc,
                                     const char *suggestion, const char *fmt,
                                     ...) {
    va_list args;
    va_start(args, fmt);
    executor_report_error_v(executor, code, loc, suggestion, fmt, args);
    va_end(args);
}

bool executor_reject_mixed_script_ident(executor_t *executor, const char *name,
                                        source_location_t loc) {
    /// Opt-in only: the canonical modes stay permissive and surface
    /// mixed-script identifiers as a `debug analyze` advisory. This hard
    /// rejection fires solely under FEATURE_REJECT_MIXED_SCRIPT_IDENTS,
    /// at author-time definition boundaries (assignment, declare,
    /// function, alias) -- never on the environment-import path, where
    /// inherited names are external bytes rather than lush identifiers.
    if (!shell_mode_allows(FEATURE_REJECT_MIXED_SCRIPT_IDENTS)) {
        return false;
    }
    const char *script_a = NULL;
    const char *script_b = NULL;
    if (!lush_ident_mixes_scripts(name, &script_a, &script_b)) {
        return false;
    }
    executor_error_report(
        executor, SHELL_ERR_INVALID_ARGUMENT, loc,
        "identifier `%s' mixes %s and %s scripts -- homograph risk; "
        "rename to a single script or `unsetopt reject_mixed_script_idents'",
        name, script_a, script_b);
    return true;
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

    /// Create the error
    va_list args;
    va_start(args, fmt);
    shell_error_t *error =
        shell_error_createv(code, SHELL_SEVERITY_ERROR, loc, fmt, args);
    va_end(args);

    if (!error) {
        /// Fallback to legacy error system
        set_executor_error(executor, "runtime error");
        return;
    }

    /// Attach source line for the rust-style snippet block (`N | ... / ^~~~~`).
    /// Skipped for SOURCE_LOC_UNKNOWN since there is no line to look up.
    if (SOURCE_LOC_VALID(loc)) {
        char *src_line = executor_get_source_line(executor, loc.line);
        if (src_line) {
            shell_error_set_source_line(error, src_line, loc.column,
                                        loc.column + loc.length);
            free(src_line);
        }
    }

    /// Add context stack to error
    for (size_t i = 0;
         i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
        if (executor->context_stack[i]) {
            shell_error_push_context(error, "%s", executor->context_stack[i]);
        }
    }

    /// Display the error immediately
    shell_error_display(error, stderr, isatty(STDERR_FILENO));

    /// Set legacy error state for compatibility - use NULL since error was
    /// already displayed
    executor->has_error = true;
    executor->error_message = NULL; /// Already displayed via structured system

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

    /// Create the error
    shell_error_t *error =
        shell_error_create(SHELL_ERR_COMMAND_NOT_FOUND, SHELL_SEVERITY_ERROR,
                           loc, "%s: command not found", command);
    if (!error) {
        /// Fallback to simple error message
        fprintf(stderr, "lush: %s: command not found\n", command);
        return;
    }

    /// Attach source line for the rust-style snippet block
    if (SOURCE_LOC_VALID(loc)) {
        char *src_line = executor_get_source_line(executor, loc.line);
        if (src_line) {
            shell_error_set_source_line(error, src_line, loc.column,
                                        loc.column + loc.length);
            free(src_line);
        }
    }

    /// Add context stack to error
    for (size_t i = 0;
         i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
        if (executor->context_stack[i]) {
            shell_error_push_context(error, "%s", executor->context_stack[i]);
        }
    }

    /// Get suggestions from autocorrect (builtins + PATH with fast pre-filter)
    correction_results_t results;
    int num_suggestions =
        autocorrect_find_suggestions(executor, command, &results);

    if (num_suggestions > 0) {
        /// Build suggestion string
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

    /// Always free autocorrect results (original_command is allocated even with
    /// no suggestions)
    autocorrect_free_results(&results);

    /// Display the error
    shell_error_display(error, stderr, isatty(STDERR_FILENO));

    /// Set legacy error state
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

    /// Always route through execute_command_list. The historical
    /// single-command shortcut (call execute_node directly) bypassed
    /// the post-command bookkeeping that execute_command_chain runs
    /// after every node: ERR-trap firing on non-zero exit, errexit
    /// check, shell_exit_requested propagation. Those side-effects
    /// must fire for batch-of-one inputs too -- a script's first
    /// line, or any single-command -c invocation, has to behave the
    /// same as the same line inside a longer list. The chain walker
    /// already handles a 1-node chain correctly; collapsing onto it
    /// removes the divergence between the "single" and "list" paths.
    /// Surfaced while building the per-batch driver for issue #151.
    int result = execute_command_list(executor, ast);
    executor->exit_status = result;
    return result;
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
/// Find the end of the first syntactically-complete batch starting at
/// `input`. A "batch" is the same logical unit `get_input_complete_counted`
/// builds from a file: a single simple command, a multi-line construct
/// (if/case/for/function/heredoc), or the contents of a quoted string
/// spanning multiple lines. Returns the byte offset just past the last
/// consumed character (including the terminating newline, if any).
/// `*out_lines` receives the number of source lines this batch covers.
///
/// Reusing the canonical continuation analyzer (input_continuation.h)
/// means a -c string with `shopt -s extglob\ncase ... esac` is split
/// into the same two batches a file would produce -- the shopt batch
/// runs, flips FEATURE_EXTENDED_GLOB, and the case batch is then
/// tokenized with extglob in effect. Mode-accurate defaults stay
/// intact (`-c '...; ...'` on one line is still one batch and still
/// errors exactly as bash does -- bash doesn't fix the one-line form
/// either). Drives issue #151.
static size_t find_next_batch_end(const char *input, size_t input_len,
                                  size_t *out_lines) {
    continuation_state_t state;
    continuation_state_init(&state);

    size_t pos = 0;
    size_t lines = 0;
    bool produced_any = false;

    while (pos < input_len) {
        size_t line_start = pos;
        while (pos < input_len && input[pos] != '\n') {
            pos++;
        }
        size_t line_len = pos - line_start;

        char *line = malloc(line_len + 1);
        if (!line) {
            continuation_state_cleanup(&state);
            if (out_lines) {
                *out_lines = lines;
            }
            return pos;
        }
        memcpy(line, input + line_start, line_len);
        line[line_len] = '\0';

        continuation_analyze_line(line, &state);
        free(line);

        /// A blank/whitespace-only line on its own doesn't constitute a
        /// batch: skip past the newline and keep looking so we don't
        /// hand the parser an empty string. Only treat it as
        /// significant once a non-blank line has been seen.
        bool blank = true;
        for (size_t i = 0; i < line_len; i++) {
            if (!isspace((unsigned char)input[line_start + i])) {
                blank = false;
                break;
            }
        }
        if (!blank) {
            produced_any = true;
            lines++;
        } else if (produced_any) {
            lines++;
        }

        if (pos < input_len && input[pos] == '\n') {
            pos++;
        }

        if (!produced_any) {
            /// Don't end on a blank-prefix; keep scanning for the first
            /// real line.
            continue;
        }

        if (!continuation_needs_continuation(&state)) {
            break;
        }
    }

    continuation_state_cleanup(&state);
    if (out_lines) {
        *out_lines = lines;
    }
    return pos;
}

/// Parse + execute a single already-extracted batch. Owns the parser
/// lifecycle and the stashed source-text view for the structured-error
/// system. Factored out so executor_execute_command_line can iterate
/// over batches without duplicating the parser plumbing.
static int execute_one_batch(executor_t *executor, const char *batch,
                             const char *source_name, size_t starting_line) {
    parser_t *parser =
        parser_new_with_source(batch, source_name, starting_line);
    if (!parser) {
        set_executor_error(executor, "Failed to create parser");
        return 1;
    }

    node_t *ast = parser_parse(parser);

    if (shell_opts.syntax_check) {
        int rc = 0;
        if (parser_has_error(parser)) {
            parser_display_errors(parser, stderr, isatty(STDERR_FILENO));
            const char *legacy_err = parser_error(parser);
            if (legacy_err) {
                set_executor_error(executor, legacy_err);
            }
            rc = 2;
        }
        /// parser_parse may return a partial tree even on error; free it
        /// (the success path frees it below).
        free_node_tree(ast);
        parser_free(parser);
        return rc;
    }

    if (parser_has_error(parser)) {
        parser_display_errors(parser, stderr, isatty(STDERR_FILENO));
        const char *legacy_err = parser_error(parser);
        if (legacy_err) {
            set_executor_error(executor, legacy_err);
        }
        /// Free any partial tree the parser produced before erroring.
        free_node_tree(ast);
        parser_free(parser);
        return 1;
    }

    if (!ast) {
        parser_free(parser);
        return 0;
    }

    int rc = executor_execute(executor, ast);
    free_node_tree(ast);
    parser_free(parser);
    return rc;
}

int executor_execute_command_line(executor_t *executor, const char *input,
                                  size_t starting_line) {
    if (!executor || !input) {
        return 1;
    }
    if (starting_line == 0) {
        starting_line = 1;
    }

    /// Stash source text for the structured-error system. Any error
    /// site emitting via shell_error_create() can pull the actual
    /// source line via executor_get_source_line() and attach it via
    /// shell_error_set_source_line() to produce the full rust-style
    /// snippet block (`N | source line / ^~~~~`). The stash is set to
    /// the current batch we're about to parse and restored on exit so
    /// re-entrant dispatch (e.g. command substitution running its own
    /// batch recursively) doesn't leak text from one batch into
    /// another.
    const char *saved_source_text = executor->source_text;
    size_t saved_source_starting_line = executor->source_starting_line;

    /// Preprocess input to handle line continuation (backslash-newline)
    /// This is needed for -c option where the string comes directly without
    /// going through get_input_complete() which normally handles this
    char *processed_input = NULL;
    const char *parse_input = input;

    if (strchr(input, '\\') != NULL) {
        /// May contain line continuations - preprocess
        size_t len = strlen(input);
        processed_input = malloc(len + 1);
        if (processed_input) {
            size_t j = 0;
            for (size_t i = 0; i < len; i++) {
                if (input[i] == '\\' && i + 1 < len && input[i + 1] == '\n') {
                    /// Skip backslash-newline (line continuation)
                    i++; /// Skip the newline too (loop will increment past
                         /// backslash)
                } else {
                    processed_input[j++] = input[i];
                }
            }
            processed_input[j] = '\0';
            parse_input = processed_input;
        }
    }

    const char *source_name = executor->current_script_file
                                  ? executor->current_script_file
                                  : "<stdin>";

    /// Iterate over syntactically-complete batches. The file/stdin path
    /// already batches at the reader (get_input_complete_counted); -c
    /// and other in-process callers (eval-style, traps, autoload, fc,
    /// shell-hooks) used to bypass that batching and hand multi-batch
    /// strings to a single parse. Re-using the canonical continuation
    /// analyzer here closes that gap so every entry point sees the
    /// same batch granularity. Issue #151.
    size_t input_pos = 0;
    size_t input_len = strlen(parse_input);
    size_t batch_starting_line = starting_line;
    int result = 0;

    while (input_pos < input_len) {
        size_t batch_lines = 0;
        size_t batch_consumed = find_next_batch_end(
            parse_input + input_pos, input_len - input_pos, &batch_lines);
        if (batch_consumed == 0) {
            break;
        }

        /// Trim the trailing newline (if any) for the batch buffer the
        /// parser sees, but report the full consumed length to the
        /// caller-line tracker via batch_lines.
        size_t batch_text_len = batch_consumed;
        if (batch_text_len > 0 &&
            parse_input[input_pos + batch_text_len - 1] == '\n') {
            batch_text_len--;
        }

        /// Skip a pure-whitespace batch (no parser invocation needed).
        bool all_blank = true;
        for (size_t i = 0; i < batch_text_len; i++) {
            if (!isspace((unsigned char)parse_input[input_pos + i])) {
                all_blank = false;
                break;
            }
        }
        if (all_blank) {
            input_pos += batch_consumed;
            batch_starting_line += batch_lines;
            continue;
        }

        char *batch = malloc(batch_text_len + 1);
        if (!batch) {
            set_executor_error(executor, "Failed to allocate batch buffer");
            result = 1;
            break;
        }
        memcpy(batch, parse_input + input_pos, batch_text_len);
        batch[batch_text_len] = '\0';

        executor->source_text = batch;
        executor->source_starting_line = batch_starting_line;

        result = execute_one_batch(executor, batch, source_name,
                                   batch_starting_line);

        free(batch);

        input_pos += batch_consumed;
        batch_starting_line += batch_lines;

        /// Honor mid-stream shell-exit requests: ${var:?word}, builtin
        /// exit, errexit-style abort, a SIGHUP forwarded by the foreground
        /// wait, etc. The within-batch list checks exit_flag, but a
        /// multi-line -c string parses each line as a separate batch, so a
        /// hangup (or `exit`) on one line must also stop the batches after
        /// it -- without this an `exit 3` / hung-up `sleep` still ran the
        /// trailing lines and reported the wrong status.
        if (executor->shell_exit_requested || exit_flag) {
            break;
        }

        /// Parse / syntax-check failure in non-first batch: stop
        /// processing further batches and surface the error. This
        /// matches bash's behavior where a parse error in the middle
        /// of a -c string aborts the rest.
        if (result != 0 && shell_opts.syntax_check) {
            break;
        }

        /// `set -e` (errexit) at batch granularity: a non-zero result
        /// from a batch -- where the chain inside the batch already
        /// honored set -e's exemption rules (conditional context,
        /// pipeline LHS, etc.) and STILL surfaced non-zero -- aborts
        /// the remaining batches, matching bash's script-wide errexit
        /// semantics. Without this the historical "test passes / real
        /// script keeps running" divergence persisted because the
        /// chain returned to the batch loop but the loop kept feeding
        /// new batches.
        if (shell_opts.exit_on_error && result != 0) {
            break;
        }
    }

    free(processed_input);
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

    /// Translate file-relative line number into batch-relative line
    /// number. The batch's first line is executor->source_starting_line
    /// in the original file; that's batch line 1.
    size_t batch_line = file_line - executor->source_starting_line + 1;

    const char *src = executor->source_text;
    size_t current_line = 1;
    size_t line_start = 0;
    size_t i = 0;

    /// Walk to the start of the requested batch-relative line.
    while (src[i] != '\0' && current_line < batch_line) {
        if (src[i] == '\n') {
            current_line++;
            line_start = i + 1;
        }
        i++;
    }
    if (current_line != batch_line) {
        return NULL; /// Requested line is past end of batch text.
    }

    /// Find the end of the line (newline or end-of-string).
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

    /// Check syntax check mode (set -n) - don't execute any nodes
    if (shell_opts.syntax_check) {
        return 0; /// Syntax check mode - don't execute
    }

    /// Enhanced debug tracing
    if (executor->debug) {
        printf("DEBUG: Executing node type %d\n", node->type);
        if (node->val.str) {
            printf("DEBUG: Node value: '%s'\n", node->val.str);
        }
    }

    /// Advanced debug system integration
    DEBUG_TRACE_NODE(node, __FILE__, __LINE__);

    /// Check for a breakpoint at this node's source line. node->loc.line
    /// is the absolute 1-based source line (0 when the parser recorded no
    /// location); debug_check_breakpoint ignores line 0, so an unlocated
    /// node is skipped and its executable children carry the real line.
    if (executor->in_script_execution && executor->current_script_file) {
        DEBUG_BREAKPOINT_CHECK(executor->current_script_file,
                               (int)node->loc.line);
    }

    switch (node->type) {
    case NODE_COMMAND: {
        int result = execute_command(executor, node);
        /// Clean up any process substitution fds after command execution
        cleanup_procsub_fds(executor);
        /// A simple command is a one-element pipeline: refresh PIPESTATUS with
        /// its status (bash and zsh both do this for every command, not just
        /// multi-stage pipelines). A function call reaches here too, so its
        /// aggregate return status overwrites whatever its body's last pipeline
        /// left -- matching bash/zsh, which treat a function call as opaque to
        /// PIPESTATUS. Compound commands (if/for/brace) dispatch elsewhere and
        /// are left transparent (their inner pipeline's array survives).
        executor_publish_pipestatus(&result, 1);
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
    case NODE_SUBSHELL: {
        int result = execute_subshell(executor, node);
        /// A subshell runs in a forked child, so any PIPESTATUS its body set
        /// does not reach this shell. From here it is one command: publish its
        /// aggregate status as a one-element array (bash and zsh agree).
        executor_publish_pipestatus(&result, 1);
        return result;
    }
    case NODE_COMMAND_LIST:
        return execute_command_list(executor, node);
    case NODE_BACKGROUND:
        return executor_execute_background(executor, node);
    case NODE_NEGATE:
        return execute_negate(executor, node);
    case NODE_VAR:
        /// Variable nodes are typically handled by their parent
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
        /// Array literals are typically handled by NODE_ARRAY_ASSIGN
        return 0;
    case NODE_ARRAY_ACCESS:
        /// Array access is typically handled during variable expansion
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

bool run_pending_signals(executor_t *executor) {
    /// Re-entrancy guard: a trap body that itself blocks on a foreground
    /// command re-enters here; skip the nested call so the pending bit is
    /// dispatched at the next outer safe point rather than recursing.
    static bool dispatching = false;
    if (dispatching || !executor || !signal_traps_pending()) {
        return false;
    }
    dispatching = true;

    /// Snapshot every piece of state a nested trap body could corrupt for the
    /// dispatching point. State a trap is meant to change -- variables, cwd,
    /// $!, the job list -- is deliberately not bracketed; $? is bracketed
    /// inside execute_pending_traps.
    int saved_errno = errno;
    pid_t saved_child = get_current_child_pid();

    int saved_procsub_fds[32];
    pid_t saved_procsub_pids[32];
    int saved_procsub_count = executor->procsub_fd_count;
    memcpy(saved_procsub_fds, executor->procsub_fds, sizeof(saved_procsub_fds));
    memcpy(saved_procsub_pids, executor->procsub_pids,
           sizeof(saved_procsub_pids));

    loop_control_t saved_loop_control = executor->loop_control;
    int saved_loop_control_level = executor->loop_control_level;
    int saved_loop_depth = executor->loop_depth;
    bool saved_command_abort = executor->command_abort;
    bool saved_has_error = executor->has_error;
    const char *saved_error_message = executor->error_message;

    char *saved_script_file = executor->current_script_file
                                  ? strdup(executor->current_script_file)
                                  : NULL;
    bool saved_in_script = executor->in_script_execution;

    void *saved_comp_result = executor->active_comp_result;
    const char *saved_comp_prefix = executor->active_comp_prefix;

    bool have_tty = isatty(STDIN_FILENO);
    pid_t saved_pgrp = -1;
    struct termios saved_termios;
    bool have_termios = false;
    if (have_tty) {
        saved_pgrp = tcgetpgrp(STDIN_FILENO);
        have_termios = (tcgetattr(STDIN_FILENO, &saved_termios) == 0);
    }

    /// Give the trap an empty process-substitution set and completion
    /// accumulator so its own use of those touches only its own entries rather
    /// than closing/reaping the outer command's live process substitutions.
    executor->procsub_fd_count = 0;
    executor->active_comp_result = NULL;
    executor->active_comp_prefix = NULL;

    execute_pending_traps();

    bool exit_requested = exit_flag || executor->shell_exit_requested;

    /// A POSIX abort inside a trap body (${var:?word}, set -u unbound) records
    /// its status in shell_exit_status, not $?, and does not set exit_flag.
    /// Surface it so the boundary's `return last_exit_status` yields the abort
    /// status -- matching the direct POSIX-abort path (execute_command_list
    /// returns shell_exit_status), rather than the pre-trap value.
    if (executor->shell_exit_requested) {
        set_exit_status(executor->shell_exit_status);
    }

    /// Restore.
    set_current_child_pid(saved_child);
    memcpy(executor->procsub_fds, saved_procsub_fds, sizeof(saved_procsub_fds));
    memcpy(executor->procsub_pids, saved_procsub_pids,
           sizeof(saved_procsub_pids));
    executor->procsub_fd_count = saved_procsub_count;
    executor->loop_control = saved_loop_control;
    executor->loop_control_level = saved_loop_control_level;
    executor->loop_depth = saved_loop_depth;
    executor->command_abort = saved_command_abort;
    executor->has_error = saved_has_error;
    executor->error_message = saved_error_message;
    free(executor->current_script_file);
    executor->current_script_file = saved_script_file;
    /// Keep the script-context invariant even if the strdup above failed under
    /// OOM: in_script_execution stays true only while current_script_file holds
    /// a valid path.
    executor->in_script_execution =
        saved_in_script && (saved_script_file != NULL);
    executor->active_comp_result = saved_comp_result;
    executor->active_comp_prefix = saved_comp_prefix;
    if (have_tty) {
        if (saved_pgrp != -1) {
            tcsetpgrp(STDIN_FILENO, saved_pgrp);
        }
        if (have_termios) {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        }
    }
    errno = saved_errno;

    dispatching = false;
    return exit_requested;
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

    /// Handle NODE_COMMAND_LIST with children
    if (list->type == NODE_COMMAND_LIST) {
        current = list->first_child;
    } else {
        /// Handle legacy case where list is the first command in a sibling
        /// chain
        current = list;
    }

    while (current) {
        /// Check syntax check mode (set -n) - don't execute commands
        if (shell_opts.syntax_check) {
            return 0; /// Syntax check mode - don't execute
        }

        /// Bash-style DEBUG pseudo-signal: fires BEFORE every command.
        /// fire_debug_trap itself gates on functrace + function scope.
        fire_debug_trap();

        last_result = execute_node(executor, current);

        /// Check for loop control (break/continue) - stop executing list
        if (executor->loop_control != LOOP_NORMAL) {
            return last_result;
        }

        /// POSIX-required shell abort (set by executor_request_posix_exit
        /// from sites like ${var:?word}). Subsequent statements in this
        /// batch must not run; the REPL terminates the shell with
        /// shell_exit_status after we return.
        if (executor->shell_exit_requested) {
            return executor->shell_exit_status;
        }

        /// A hangup received while a prior statement ran (notably while an
        /// in-process builtin such as `read` was blocked, where no foreground
        /// wait was active to forward it) must stop the list here rather than
        /// run the next statement. This matches bash/zsh, which terminate at
        /// the hangup point; the alternative -- proceeding and forwarding the
        /// hangup to the next command -- races the child's fork/exec. Record
        /// 128 + SIGHUP and fall through to the exit_flag return below.
        if (sighup_was_received() && !exit_flag) {
            set_exit_status(128 + SIGHUP);
            exit_flag = true;
        }

        /// `exit` builtin requested shell termination. bin_exit sets the
        /// global exit_flag (the REPL polls it to leave its top-level
        /// loop) and stashes the chosen status in last_exit_status. The
        /// REPL alone is not enough: when a script is run as a single
        /// parsed AST, the whole tree executes inside ONE call to
        /// execute_command_list, so without this check every statement
        /// after `exit` still runs (real_world/posix/101 fell through
        /// `exit $?` and ran trailing `rm -f` / `exit 0`). Honor it
        /// here so `exit` inside any nested construct - case arm, if
        /// body, brace group, loop - immediately propagates up.
        if (exit_flag) {
            return last_exit_status;
        }

        /// Flush stdout to prevent pipeline from picking up residual output
        fflush(stdout);

        /// Update exit status after each command in the sequence
        set_exit_status(last_result);

        /// Dispatch any pending signal-trap bodies at this command boundary --
        /// the safe point that makes a `trap ... SIG` fire under -c, in a
        /// script, and mid-batch, not only between REPL iterations (issue
        /// #409). run_pending_signals brackets $? and the executor/process
        /// state a trap body can perturb; if a trap ran `exit`, stop the list
        /// here.
        if (run_pending_signals(executor)) {
            return last_exit_status;
        }

        if (executor->debug) {
            printf("DEBUG: Command result: %d\n", last_result);
        }

        /// Bash-style ERR pseudo-signal: fires after a non-zero command
        /// exit, before set -e gets a chance to abort.
        if (last_result != 0) {
            fire_err_trap();
        }

        /// Handle set -e (exit_on_error): exit if command failed
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
    char *old_value; ///< strdup of prior value when existed
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
        n--; /// += append
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
            saves[idx].name = name; /// ownership moves to saves
            saves[idx].existed = existed;
            saves[idx].old_value = old;
            saves[idx].was_exported = (fl & SYMVAR_EXPORTED) != 0;
        }

        int st = execute_assignment(executor, c->val.str, command->loc);
        if (st != 0) {
            if (!transient) {
                free(name);
            }
            /// On failure idx-th save (if transient) holds this name;
            /// include it so restore undoes any partial apply.
            if (transient) {
                idx++;
                *out_saves = saves;
                *out_n = idx;
            }
            return st;
        }
        symtable_export_global(name); /// setenv: child/env-readers see it

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

/// Undo a transient prefix_apply() and free the snapshot array.
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

    /// A readonly variable-assignment error was detected inside this
    /// command. Emit it now -- redirections set up by the assignment-only
    /// path have already been restored, so the shell-level diagnostic
    /// reaches the real stderr rather than a `2>/dev/null` the command
    /// requested (matching bash). command_abort stays set for the
    /// enclosing AND-OR handler to consume.
    if (executor->pending_readonly_var) {
        executor_error_report(
            executor, SHELL_ERR_READONLY_VAR, executor->pending_readonly_loc,
            "%s: readonly variable", executor->pending_readonly_var);
        free(executor->pending_readonly_var);
        executor->pending_readonly_var = NULL;
        set_exit_status(1);
        result = 1;
        /// POSIX 2.8.1: a variable-assignment error causes a
        /// non-interactive shell to exit. dash and zsh do this; bash
        /// continues, aborting only the current AND-OR list. Gated by
        /// FEATURE_ASSIGN_ERROR_EXITS (on in posix/zsh, off in
        /// bash/lush; per-script via `setopt assign_error_exits`).
        if (shell_mode_allows(FEATURE_ASSIGN_ERROR_EXITS)) {
            executor_request_posix_exit(executor, 1);
        }
    }

    executor->active_loc = saved_active_loc;
    return result;
}

static int execute_command_inner(executor_t *executor, node_t *command) {
    /// command non-null and NODE_COMMAND is guaranteed by the wrapper.

    /// Reset expansion error flags for this command
    executor->expansion_error = false;
    executor->expansion_exit_status = 0;
    /// Clear the per-command assignment-abort signal. A prior command's
    /// readonly abort has already been consumed by the enclosing AND-OR
    /// handler; the next command starts clean.
    executor->command_abort = false;

    /// Check for assignment (legacy lone-assignment shape: val.str is
    /// "var=value" with no NODE_ASSIGN children).
    if (command->val.str && is_assignment(command->val.str)) {
        return execute_assignment(executor, command->val.str, command->loc);
    }

    /// POSIX cmd_prefix: NODE_ASSIGN children precede the command word.
    int n_prefix = 0;
    for (node_t *c = command->first_child; c; c = c->next_sibling) {
        if (c->type == NODE_ASSIGN) {
            n_prefix++;
        }
    }

    if (n_prefix > 0 && command->val.str == NULL) {
        /// Pure prefix, no command word: `x=1`, `x=1 >file`. POSIX:
        /// redirections are performed, then assignments persist.
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
        /// Command word present: apply prefix transiently, dispatch, then
        /// restore. An external command forks (its child inherits the
        /// exported vars via environ); the parent-side restore reverts the
        /// shell's own view for all command kinds.
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

    /// Note: Parameter expansions like ${CMD} in command position are handled
    /// by build_argv_from_ast() which calls expand_if_needed() on the command
    /// name. The expanded result becomes the command to execute, matching
    /// bash/zsh behavior. Previously this code had an early-return that
    /// discarded the expansion result without executing - that was a bug.

    /// Check if command has redirections
    bool has_redirections = count_redirections(command) > 0;

    /// Build argument vector (excluding redirection nodes). Track whether a
    /// command substitution runs during this expansion so a resulting null
    /// command can tell "adopt the cmdsub status" from "no cmdsub -> 0".
    executor->word_cmdsub_ran = false;
    int argc;
    char **argv = build_argv_from_ast(executor, command, &argc);
    if (!argv) {
        return 1; /// allocation/expansion failure
    }
    if (argc == 0) {
        /// Null command: every word was removed by null-word removal (an
        /// unquoted empty expansion, e.g. `$x` with x="", or a command
        /// substitution `$(cmd)` whose output was empty). POSIX: perform any
        /// redirections, then the status is that of the LAST command
        /// substitution run while expanding the (now-empty) words -- `$(false)`
        /// yields 1 -- or 0 if no command substitution ran. word_cmdsub_ran
        /// distinguishes the two: executor->exit_status alone cannot, since a
        /// prior command may have left a non-zero status that no substitution
        /// in THIS command touched. Without this, `while $(false)` looped
        /// forever and `set -e; $(false)` did not exit, while `false; $x`
        /// (empty $x) must still be 0.
        int null_status = executor->word_cmdsub_ran ? executor->exit_status : 0;
        free(argv);
        if (has_redirections) {
            redirection_state_t rs;
            save_file_descriptors(&rs);
            if (setup_redirections(executor, command) != 0) {
                restore_file_descriptors(&rs);
                set_exit_status(1);
                return 1;
            }
            restore_file_descriptors(&rs);
        }
        set_exit_status(null_status);
        return null_status;
    }

    /// The parser only builds the `\x1F name=(...)` array-literal sentinel
    /// for an operand of an assignment-aware builtin (declare / local /
    /// typeset / export / readonly); an array literal as an argument to any
    /// other command is rejected at parse time. So a sentinel argv element
    /// only ever reaches those builtins, which consume it. Nothing needs to
    /// be stripped here for other commands -- and stripping a leading \x1F
    /// unconditionally would corrupt a legitimate value that merely begins
    /// with 0x1F (e.g. `x=$'\x1f'foo; echo "$x"`).

    /// Privileged mode security check
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

    /// Check for expansion errors (like arithmetic division by zero)
    if (executor->expansion_error) {
        /// Free argv before returning
        for (int i = 0; i < argc; i++) {
            free(argv[i]);
        }
        free(argv);
        return executor->expansion_exit_status;
    }

    /// Note: Parameter expansion arguments are already expanded by
    /// build_argv_from_ast() via expand_if_needed(). The expanded values
    /// are in argv and will be passed to the command. No need for
    /// special handling here - let the command execute normally.

    /// Check for stderr redirection pattern (2>/dev/null or 2> /dev/null)
    bool redirect_stderr = false;
    char **filtered_argv = NULL;
    int filtered_argc = 0;

    /// Look for 2>/dev/null pattern in arguments
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
        /// Create filtered argv without redirection tokens
        filtered_argv = malloc((argc + 1) * sizeof(char *));
        if (!filtered_argv) {
            /// Free original argv and return error
            for (int i = 0; i < argc; i++) {
                free(argv[i]);
            }
            free(argv);
            return 1;
        }

        int j = 0;
        for (int i = 0; i < argc; i++) {
            if (strcmp(argv[i], "2>/dev/null") == 0) {
                /// Skip this token
                continue;
            } else if (i + 2 < argc && strcmp(argv[i], "2") == 0 &&
                       strcmp(argv[i + 1], ">") == 0 &&
                       strcmp(argv[i + 2], "/dev/null") == 0) {
                /// Skip these three tokens
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

    /// zsh `alias -g NAME=value`: substitute non-command argv slots
    /// against the global-alias table before the command-position
    /// alias expansion below. Each slot's text is replaced verbatim
    /// (single-slot substitution) so `alias -g ...='../..'` works for
    /// directory shortcuts. Structural-operator substitutions
    /// (e.g. `alias -g G='| grep'`) are NOT supported because we don't
    /// re-tokenize after substitution; the value would be passed as a
    /// single argv slot rather than introducing a real pipe. Issue #204.
    for (int gi = 1; gi < filtered_argc; gi++) {
        char *gv = lookup_global_alias(filtered_argv[gi]);
        if (gv) {
            char *replacement = strdup(gv);
            if (replacement) {
                /// Only free the slot if filtered_argv is owned by us.
                /// build_argv_from_ast hands us an owned argv; the
                /// pre-existing free below at command-position
                /// expansion uses the same condition.
                if (filtered_argv != argv) {
                    free(filtered_argv[gi]);
                }
                filtered_argv[gi] = replacement;
            }
        }
    }

    /// Check for alias expansion and rebuild argv if needed
    char *alias_expanded = lookup_alias(filtered_argv[0]);
    if (alias_expanded) {
        /// Reconstruct original command for expansion
        char *original_command = NULL;
        size_t total_len = 1; /// for null terminator

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

            /// Expand the full command line with recursive expansion
            char *recursive_expanded =
                expand_aliases_recursive(filtered_argv[0], 10); /// max depth 10
            char *expanded_command = NULL;

            if (recursive_expanded) {
                /// If recursive expansion succeeded, use it to build full
                /// command
                if (filtered_argc > 1) {
                    /// Add original arguments to recursively expanded command
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
                /// Fall back to simple first-word expansion
                expanded_command = expand_first_word_alias(original_command);
            }
            if (expanded_command &&
                strcmp(expanded_command, original_command) != 0) {
                /// Create new argv array for expanded command
                char **new_argv =
                    malloc(256 * sizeof(char *)); /// reasonable limit
                if (new_argv) {
                    /// Tokenize expanded command into new argv
                    char *expanded_copy = strdup(expanded_command);
                    char *token = strtok(expanded_copy, " ");
                    int new_argc = 0;

                    while (token && new_argc < 255) {
                        new_argv[new_argc] = strdup(token);
                        new_argc++;
                        token = strtok(NULL, " ");
                    }
                    new_argv[new_argc] = NULL;

                    /// Only replace if we successfully created the new argv
                    if (new_argc > 0) {
                        /// Free old argv only if it's not the same as original
                        /// argv
                        if (filtered_argv != argv) {
                            for (int i = 0; i < filtered_argc; i++) {
                                free(filtered_argv[i]);
                            }
                            free(filtered_argv);
                        }

                        filtered_argv = new_argv;
                        filtered_argc = new_argc;
                    } else {
                        /// Failed to create new argv, clean up
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

    /// Get debug context for profiling and frame management
    const char *command_name = filtered_argv[0];

    /// Push debug frame and start profiling for this command
    if (g_debug_context && g_debug_context->enabled) {
        debug_push_frame(g_debug_context, command_name, NULL, 0);

        if (g_debug_context->profile_enabled) {
            g_debug_context->total_commands++;
            debug_profile_function_enter(g_debug_context, command_name);
        }
    }

    /// Zsh-style autoload: if the name was declared via `autoload NAME`
    /// and the function isn't yet defined, try to resolve it now via
    /// fpath. On success the function joins the live table and the
    /// subsequent is_function_defined check picks it up. Done BEFORE
    /// builtin lookup so an autoloadable name doesn't get shadowed by
    /// a same-named builtin the user explicitly chose to override.
    if (!is_function_defined(executor, filtered_argv[0])) {
        (void)autoload_try_resolve(executor, filtered_argv[0]);
    }

    if (is_function_defined(executor, filtered_argv[0])) {
        result =
            execute_function_call(executor, filtered_argv[0], filtered_argv,
                                  filtered_argc, command->loc);
    } else if (is_builtin_command(filtered_argv[0])) {
        /// For builtin commands with stdout redirections, check if stdout is
        /// captured. Only fork for "pure" builtins that don't modify shell
        /// state.
        if (has_redirections && has_stdout_redirections(command) &&
            is_stdout_captured() && builtin_can_fork(filtered_argv[0])) {
            /// When stdout is captured externally and command has stdout
            /// redirections, use child process to avoid file descriptor
            /// interference (only for pure builtins)
            result = execute_builtin_with_captured_stdout(
                executor, filtered_argv, command);
        } else {
            /// Normal case: handle redirections in parent process
            redirection_state_t redir_state;
            if (has_redirections) {
                save_file_descriptors(&redir_state);
                int redir_result = setup_redirections(executor, command);
                if (redir_result != 0) {
                    restore_file_descriptors(&redir_state);
                    /// Free argv
                    for (int i = 0; i < argc; i++) {
                        free(argv[i]);
                    }
                    free(argv);
                    /// Free filtered argv if separately allocated
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

            /// Flush output streams after builtin execution
            /// This ensures output appears immediately, especially under
            /// valgrind/piping
            fflush(stdout);
            fflush(stderr);

            /// Restore file descriptors after builtin execution
            /// EXCEPT for 'exec' builtin - its redirections are permanent
            if (has_redirections &&
                !(filtered_argv[0] && strcmp(filtered_argv[0], "exec") == 0)) {
                restore_file_descriptors(&redir_state);
            }
        }
    } else {
        /// Check auto_cd before attempting external command execution
        int auto_cd_enabled = symtable_get_global_int("AUTO_CD", 0);
        if (auto_cd_enabled && argc > 0 && argv[0]) {
            struct stat st;
            /// Check if the command is actually a directory
            if (stat(argv[0], &st) == 0 && S_ISDIR(st.st_mode)) {
                /// @brief Save old directory for event firing
                ///
                /// Required by Spec 26 shell event hub to notify handlers
                /// of directory change with both old and new paths.
                char *old_pwd = getcwd(NULL, 0);

                /// Auto-cd to the directory
                if (chdir(argv[0]) == 0) {
                    /// Successfully changed directory, update PWD
                    char *new_pwd = getcwd(NULL, 0);
                    if (new_pwd) {
                        symtable_set_global("PWD", new_pwd);

                        /// @brief Fire directory changed event for LLE shell
                        /// integration
                        ///
                        /// This notifies the prompt composer which:
                        /// - Refreshes context.cwd
                        /// - Invalidates all segment caches
                        /// - Sets needs_regeneration flag
                        /// - Triggers async git status refresh
                        lle_fire_directory_changed(old_pwd, new_pwd);

                        free(new_pwd);
                    }
                    result = 0; /// Success
                } else {
                    /// Failed to change directory, show error
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
                /// Not a directory, proceed with normal command execution
                goto normal_execution;
            }
        } else {
        /// Auto-cd disabled, proceed with normal command execution
        normal_execution:
            /// Check if command exists first, offer auto-correction if not
            /// Only do interactive autocorrect if:
            /// 1. spell_correction is enabled
            /// 2. autocorrect is enabled
            /// 3. interactive prompts are enabled (otherwise no point in
            /// searching)
            /// 4. stdin is a tty (user can actually respond)
            if (config.spell_correction && autocorrect_is_enabled() &&
                config.autocorrect_interactive && isatty(STDIN_FILENO)) {
                /// First, check if the command actually exists
                if (!autocorrect_command_exists(executor, filtered_argv[0])) {
                    /// Command doesn't exist, try auto-correction
                    correction_results_t correction_results;
                    int suggestions = autocorrect_find_suggestions(
                        executor, filtered_argv[0], &correction_results);

                    if (suggestions > 0) {
                        char selected_command[MAX_COMMAND_LENGTH];
                        if (autocorrect_prompt_user(&correction_results,
                                                    selected_command)) {
                            /// User selected a correction, replace the command
                            free(filtered_argv[0]);
                            filtered_argv[0] = strdup(selected_command);

                            /// Learn the corrected command
                            autocorrect_learn_command(selected_command);

                            /// Re-check if it's a builtin or function after
                            /// correction
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
                                /// Execute the corrected external command
                                result = execute_external_command_with_setup(
                                    executor, filtered_argv, redirect_stderr,
                                    command);
                            }
                        } else {
                            /// User declined correction, show original error
                            result = 127; /// Command not found
                        }
                    } else {
                        /// No suggestions or interactive prompts disabled
                        result = execute_external_command_with_setup(
                            executor, filtered_argv, redirect_stderr, command);
                    }

                    /// Clean up correction results
                    autocorrect_free_results(&correction_results);
                } else {
                    /// Command exists, execute normally
                    result = execute_external_command_with_setup(
                        executor, filtered_argv, redirect_stderr, command);
                }
            } else {
                /// Auto-correction disabled, execute normally
                result = execute_external_command_with_setup(
                    executor, filtered_argv, redirect_stderr, command);
            }
        }
    }

    /// Free argv
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);

    /// Free filtered argv if it was separately allocated (from redirect or
    /// alias expansion)
    if (filtered_argv != NULL && filtered_argv != argv) {
        for (int i = 0; i < filtered_argc; i++) {
            free(filtered_argv[i]);
        }
        free(filtered_argv);
    }

    /// End profiling and pop debug frame for this command
    if (g_debug_context && g_debug_context->enabled) {
        if (g_debug_context->profile_enabled) {
            debug_profile_function_exit(g_debug_context, command_name);
        }
        debug_pop_frame(g_debug_context);
    }

    /// Update exit status for $? variable
    set_exit_status(result);

    /// Profile function exit
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
 * @brief Wait for a foreground child, terminating the shell on a hangup.
 *
 * Retries past EINTR from incidental signals (SIGCHLD, SIGWINCH). On a SIGHUP
 * -- the controlling terminal hung up -- forwards the hangup to this child: a
 * real hangup reaches the whole foreground process group, but a SIGHUP
 * delivered only to the shell would otherwise leave the child running while the
 * wait merely retries. Forwarding makes the child exit, and setting the global
 * exit_flag stops any remaining statements and exits the shell (logout runs for
 * a login shell). A child that ignores SIGHUP is left to finish, which is
 * correct. A `trap ... HUP` replaces the SIGHUP handler, so
 * sighup_was_received() stays false here and the trap runs instead. Returns
 * waitpid's result (the pid, or -1 with errno set on a real error).
 *
 * The hangup is forwarded at the top of the loop, before waitpid blocks: the
 * async handler may have already run during a prior in-process builtin (`read`)
 * or between statements, so the flag can be set with nothing left to interrupt
 * the wait. SIGCONT accompanies the SIGHUP so a stopped child resumes and acts
 * on it rather than queueing the hangup while remaining stopped. The hung_up
 * guard forwards at most once.
 */
static pid_t executor_wait_foreground(pid_t pid, int *status) {
    bool hung_up = false;
    for (;;) {
        if (sighup_was_received() && !hung_up) {
            kill(pid, SIGHUP);
            kill(pid, SIGCONT);
            /// Record the hangup status (128 + SIGHUP) so paths that surface
            /// exit_flag without reaping through set_exit_status -- pipelines,
            /// command substitution, subshells -- report 129 rather than the
            /// previous command's stale status. The single-command path reaps
            /// below and overwrites this with the child's actual wait status.
            set_exit_status(128 + SIGHUP);
            exit_flag = true;
            hung_up = true;
        }
        pid_t r = waitpid(pid, status, 0);
        if (r != -1) {
            return r;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

/**
 * @brief Publish the per-stage exit codes of the most recent pipeline under
 *        both polyglot names (bash `PIPESTATUS`, zsh `pipestatus`).
 *
 * A simple command is a one-element pipeline, so this is called with count 1
 * from the statement-level simple-command and subshell dispatch as well as with
 * the full stage count from execute_pipeline -- matching bash and zsh, where
 * every executed pipeline (a simple command included) refreshes the array. Both
 * arrays are 0-indexed and hold identical data.
 *
 * @param exits Array of `count` stage exit statuses (pipeline order)
 * @param count Number of stages (>= 1)
 */
static void executor_publish_pipestatus(const int *exits, size_t count) {
    array_value_t *bash_arr = symtable_array_create(false);
    array_value_t *lush_arr = symtable_array_create(false);
    if (bash_arr && lush_arr) {
        for (size_t i = 0; i < count; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", exits[i]);
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
        if (bash_arr) {
            symtable_array_free(bash_arr);
        }
        if (lush_arr) {
            symtable_array_free(lush_arr);
        }
    }
}

pipestatus_snapshot_t executor_save_pipestatus(void) {
    pipestatus_snapshot_t snap = {NULL, 0, false};
    array_value_t *arr = symtable_get_array("PIPESTATUS");
    if (!arr) {
        return snap;
    }
    snap.present = true;
    size_t n = symtable_array_length(arr);
    if (n == 0) {
        return snap;
    }
    snap.exits = malloc(n * sizeof(int));
    if (!snap.exits) {
        /// Out of memory: drop the snapshot rather than abort. The trap body's
        /// PIPESTATUS will leak through, which is preferable to a crash.
        snap.present = false;
        return snap;
    }
    for (size_t i = 0; i < n; i++) {
        const char *v = symtable_array_get_index(arr, (int)i);
        snap.exits[i] = v ? (int)strtol(v, NULL, 10) : 0;
    }
    snap.count = n;
    return snap;
}

void executor_restore_pipestatus(pipestatus_snapshot_t *snap) {
    if (!snap) {
        return;
    }
    if (snap->present && snap->count > 0) {
        executor_publish_pipestatus(snap->exits, snap->count);
    }
    /// A present-but-empty snapshot is not reconstructed: an empty PIPESTATUS
    /// is indistinguishable from unset for every consumer, and the publish
    /// helper has no meaningful zero-element form.
    free(snap->exits);
    snap->exits = NULL;
    snap->count = 0;
    snap->present = false;
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

    /// N-1 pipes connect N stages. pipes[i] connects stage i (writer) to stage
    /// i+1 (reader).
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
            /// Close all pipes and reap any children already forked.
            for (size_t j = 0; j < npipes; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            for (size_t j = 0; j < i; j++) {
                executor_wait_foreground(pids[j], NULL);
            }
            free(pids);
            free(pipes);
            executor_pop_context(executor);
            return 1;
        }

        if (pid == 0) {
            /// Reset the inherited interactive SIGHUP/SIGSEGV handlers so a
            /// hangup or fault terminates this pipeline stage normally.
            reset_subshell_signals();
            /// Child: wire stdin from the previous pipe and stdout (plus stderr
            /// if |&) to the next.
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < npipes) {
                dup2(pipes[i][1], STDOUT_FILENO);
                if (stderr_to_next[i]) {
                    dup2(pipes[i][1], STDERR_FILENO);
                }
            }
            /// Close every pipe fd; dup2 already preserved what we need.
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

    /// Parent: close all pipes so children see EOF as their stages exit.
    for (size_t i = 0; i < npipes; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    free(pipes);

    int *stage_exit = calloc(nstages, sizeof(int));
    if (!stage_exit) {
        /// Reap children before giving up so we don't leak zombies.
        for (size_t i = 0; i < nstages; i++) {
            executor_wait_foreground(pids[i], NULL);
        }
        free(pids);
        executor_pop_context(executor);
        return 1;
    }

    for (size_t i = 0; i < nstages; i++) {
        int status = 0;
        executor_wait_foreground(pids[i], &status);
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

    /// pipeline-diagnostic: queue a structured error for each non-zero stage so
    /// tools can see exactly which junction failed without parsing exit codes
    /// out of the pipefail collapse.  The overall pipeline exit becomes strict
    /// (rightmost non-zero) under this mode regardless of pipefail.
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

    /// Publish per-stage exit codes under both polyglot names so callers can
    /// diagnose which stage failed.
    executor_publish_pipestatus(stage_exit, nstages);

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

        /// Check for loop control (break/continue) - stop executing chain
        if (executor->loop_control != LOOP_NORMAL) {
            return last_result;
        }

        /// POSIX-required shell abort: short-circuit the chain so the
        /// abort propagates up to execute_command_list and the REPL.
        if (executor->shell_exit_requested) {
            return executor->shell_exit_status;
        }

        /// Bash-style ERR pseudo-signal: fires after a non-zero command
        /// exit, before set -e gets a chance to abort. Matches bash's
        /// "ERR trap before errexit" ordering.
        if (last_result != 0) {
            fire_err_trap();
        }

        /// Handle set -e (exit_on_error): exit if command failed and not part
        /// of conditional
        if (shell_opts.exit_on_error && last_result != 0) {
            /// Don't exit on error for certain contexts (conditionals,
            /// pipelines, etc.) For now, implement basic exit-on-error behavior
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

    /// Check for trailing redirections on the if statement
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

    /// Traverse through all children of the if statement
    node_t *current = if_node->first_child;
    if (!current || is_redirection_node(current)) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           if_node->loc, "malformed if statement");
        result = 1;
        goto cleanup;
    }

    /// First child is always the if condition
    node_t *condition = current;
    current = current->next_sibling;

    /// Skip any redirection nodes to find then body
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

    /// Execute the initial if condition
    int condition_result = execute_node(executor, condition);

    if (condition_result == 0) { /// Success in shell terms
        /// Execute the then body (second child)
        result = execute_node(executor, current);
        goto cleanup;
    }

    /// Move to next child (elif condition or else body)
    current = current->next_sibling;

    /// Skip any redirection nodes
    while (current && is_redirection_node(current)) {
        current = current->next_sibling;
    }

    /// Process elif clauses - they come in pairs (condition, body)
    while (current && current->next_sibling) {
        /// Skip redirection nodes
        node_t *next = current->next_sibling;
        while (next && is_redirection_node(next)) {
            next = next->next_sibling;
        }
        if (!next)
            break;

        /// Execute elif condition
        condition_result = execute_node(executor, current);

        if (condition_result == 0) { /// Success in shell terms
            /// Execute elif body (next sibling)
            result = execute_node(executor, next);
            goto cleanup;
        }

        /// Move past the elif body to the next elif condition or else body
        current = next->next_sibling;
        while (current && is_redirection_node(current)) {
            current = current->next_sibling;
        }
    }

    /// Handle final else clause if present (no condition, just body)
    if (current && !is_redirection_node(current)) {
        result = execute_node(executor, current);
        goto cleanup;
    }

    result = 0;

cleanup:
    /// Restore file descriptors if we set up redirections
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
    int streak;            ///< consecutive non-zero body exits
    int last_status;       ///< last non-zero status seen
    struct timespec start; ///< monotonic clock at first non-zero of streak
    bool armed;            ///< streak start time captured
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

/* Consume one loop frame for a pending break/continue control signal.
 *
 * `break N` and `continue N` each cross N loop frames. This helper is
 * called at the unwind site of every loop body. If the level is > 1,
 * one frame is consumed (level--) and the helper returns true so the
 * caller propagates the signal outward; the loop_control state stays
 * armed for the enclosing frame. If the level is <= 1, the signal is
 * consumed here -- loop_control reverts to LOOP_NORMAL and the helper
 * returns false; the caller decides whether to break or continue this
 * frame based on the signal kind it just consumed.
 *
 * Returns true when this frame should also exit (signal still
 * propagating outward), false when this frame has fully consumed the
 * signal (loop_control is now LOOP_NORMAL). */
static bool consume_loop_control(executor_t *executor) {
    if (executor->loop_control_level > 1) {
        executor->loop_control_level--;
        return true;
    }
    executor->loop_control = LOOP_NORMAL;
    executor->loop_control_level = 0;
    return false;
}

/* Returns true when the streak satisfies both N and T thresholds.
 * Caller should report SHELL_ERR_LOOP_LIMIT and break out. */
static bool loop_monitor_check(loop_monitor_t *m, int body_status) {
    int n_threshold = config.loop_failure_streak;
    int t_threshold = config.loop_failure_seconds;
    if (n_threshold <= 0) {
        /// heuristic disabled
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

    /// Skip past body to find any non-redirection node issues
    /// Body might be followed by redirection nodes
    if (body && is_redirection_node(body)) {
        /// If what we think is body is a redirection, we have malformed
        /// structure
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

    /// Push loop context for error reporting
    executor_push_context(executor, while_node->loc, "in while loop");

    /// Check for trailing redirections on the while loop
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

    /// Increment loop depth - enables break/continue builtins
    executor->loop_depth++;

    for (;;) {
        /// Execute condition
        int condition_result = execute_node(executor, condition);

        if (executor->debug) {
            printf("DEBUG: WHILE condition result: %d\n", condition_result);
        }

        /// If condition fails, exit loop
        if (condition_result != 0) {
            break;
        }

        /// Execute body
        last_result = execute_command_chain(executor, body);
        iteration++;

        /// Check for break/continue. `break N` / `continue N` cross N
        /// loop frames: consume_loop_control decrements N and leaves
        /// loop_control armed when N > 1 so the enclosing loop sees
        /// the signal too. See its docstring for full semantics.
        if (executor->loop_control == LOOP_BREAK) {
            (void)consume_loop_control(executor);
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            if (consume_loop_control(executor)) {
                break; /// propagate `continue N>1` outward
            }
            /// Signal consumed; fall through to next iteration.
        }

        /// `return` inside the body: stop iterating, propagate the
        /// function-return signal to the enclosing function.
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

        /// POSIX-required shell abort fired from inside the body.
        if (executor->shell_exit_requested) {
            break;
        }
    }

    /// Decrement loop depth before returning
    executor->loop_depth--;

    /// Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    /// Pop loop context
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

    /// Check for trailing redirections on the until loop
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

    /// Increment loop depth - enables break/continue builtins
    executor->loop_depth++;

    for (;;) {
        /// Execute condition
        int condition_result = execute_node(executor, condition);

        if (executor->debug) {
            printf("DEBUG: UNTIL condition result: %d\n", condition_result);
        }

        /// If condition succeeds (returns 0), exit loop
        /// This is the key difference from while loop
        if (condition_result == 0) {
            break;
        }

        /// Execute body
        last_result = execute_command_chain(executor, body);
        iteration++;

        /// Check for break/continue. See consume_loop_control() for
        /// `break N` / `continue N` propagation semantics.
        if (executor->loop_control == LOOP_BREAK) {
            (void)consume_loop_control(executor);
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            if (consume_loop_control(executor)) {
                break;
            }
        }

        /// `return` inside the body: stop iterating, propagate the
        /// function-return signal to the enclosing function.
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

        /// POSIX-required shell abort fired from inside the body.
        if (executor->shell_exit_requested) {
            break;
        }
    }

    /// Decrement loop depth before returning
    executor->loop_depth--;

    /// Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    /// Pop error context
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

    /// Evaluate count via the standard arg-expansion path (handles
    /// $var, $(cmd), $((expr)), literal numbers) and then parse as
    /// an integer. zsh accepts a negative or zero count as "do
    /// nothing"; match that.
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
            (void)consume_loop_control(executor);
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            if (consume_loop_control(executor)) {
                break;
            }
            continue;
        }
        /// `return` inside the body: stop iterating, propagate the
        /// function-return signal to the enclosing function.
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

    /// Check for trailing redirections on the for loop
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

    /// Push loop scope
    if (symtable_push_scope(executor->symtable, SCOPE_LOOP, "for-loop") != 0) {
        executor_error_add(executor, SHELL_ERR_SCOPE_ERROR, for_node->loc,
                           "failed to create loop scope");
        return 1;
    }

    /// Notify debug system we're entering a loop
    if (g_debug_context && g_debug_context->enabled) {
        debug_enter_loop(g_debug_context, "for", var_name, NULL);
    }

    /// Increment loop depth - enables break/continue builtins
    executor->loop_depth++;

    /// Push error context for structured error reporting
    executor_push_context(executor, for_node->loc, "in for loop over '%s'",
                          var_name);

    int last_result = 0;
    loop_monitor_t monitor;
    loop_monitor_init(&monitor);
    bool runaway_tripped = false;
    bool errexit_tripped = false;
    int iteration = 0;

    /// Build expanded word list for iteration
    char **expanded_words = NULL;
    int word_count = 0;

    /// Process each word in the word list, expanding and splitting
    if (word_list && word_list->first_child) {
        node_t *word = word_list->first_child;
        while (word) {
            /// Index into expanded_words where this word node's
            /// normal-expansion output begins, and whether that output
            /// is eligible for pathname expansion. -1 until the normal
            /// expansion path runs (the `$@` and vector paths produce
            /// already-final values that are never glob-expanded).
            int normal_wc_start = -1;
            bool word_globbable = false;
            if (word->val.str) {
                /// Special handling for "$@" to preserve word boundaries
                if (strcmp(word->val.str, "\"$@\"") == 0 ||
                    strcmp(word->val.str, "$@") == 0) {
                    /// Handle quoted "$@" - preserve word boundaries
                    /// Check if we're in a function scope
                    if (symtable_in_function_scope(executor->symtable)) {
                        /// In function scope - use local positional parameters
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
                        /// Not in function scope - use global shell_argv
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
                    /// Vector-yielding expansions in for-loop word lists:
                    /// `$@`, `"$@"`, `${arr[@]}`, `"${arr[@]}"`,
                    /// `${!arr[@]}`, bare `$arr` (zsh/lush mode), and
                    /// slice variants. Each produces N separate iteration
                    /// values regardless of word-split setting -- the
                    /// for-loop semantics are intrinsically per-element
                    /// for these forms in both bash and zsh. Without this
                    /// the FEATURE_WORD_SPLIT_DEFAULT=false path
                    /// (zsh/lush mode default) treats the joined string
                    /// as one iteration. Issue #99.
                    char **vec = NULL;
                    int vcount = 0;
                    if (try_expand_vector_arg(executor, word, &vec, &vcount,
                                              /*positional_only=*/false)) {
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

                    /// Normal expansion and splitting for other words.
                    /// Record the start index and quotedness so the
                    /// words produced below can be pathname-expanded.
                    normal_wc_start = word_count;
                    word_globbable = (word->type != NODE_STRING_LITERAL &&
                                      word->type != NODE_STRING_EXPANDABLE);
                    char *expanded = expand_if_needed(executor, word->val.str);
                    if (expanded) {
                        /// Check for brace expansion first
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
                                /// Add each brace expansion result
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
                                free(brace_results); /// Free array, not strings
                                                     /// (moved to
                                                     /// expanded_words)
                                free(expanded);
                            } else {
                                /// Brace expansion failed, use original
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
                            /// Field splitting follows the same rule as
                            /// command argv (build_argv_from_ast): an unquoted
                            /// command substitution splits when
                            /// FEATURE_CMDSUB_WORD_SPLIT is on, an unquoted
                            /// parameter expansion when
                            /// FEATURE_WORD_SPLIT_DEFAULT is on; quoted strings
                            /// never split. The two flags differ by mode -- zsh
                            /// splits command subs but not bare $var -- and in
                            /// lush mode BOTH are off, so neither implicitly
                            /// splits (SEMANTICS section 4.1: no implicit
                            /// IFS-driven splitting of command output). This
                            /// keeps a quoted empty "" as one empty iteration
                            /// word and "$x" as a single word. Issue #127.
                            bool should_word_split =
                                (word->type == NODE_COMMAND_SUB &&
                                 shell_mode_allows(
                                     FEATURE_CMDSUB_WORD_SPLIT)) ||
                                (word->type == NODE_VAR &&
                                 shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT));

                            if (should_word_split) {
                                /// symtable_get returns an owned copy; keep it
                                /// to free after the split (NULL => default
                                /// IFS).
                                char *ifs_owned =
                                    symtable_get(executor->symtable, "IFS");
                                const char *ifs =
                                    ifs_owned ? ifs_owned : " \t\n";
                                int field_count = 0;
                                char **fields = ifs_field_split(expanded, ifs,
                                                                &field_count);
                                free(ifs_owned);
                                for (int fi = 0; fi < field_count; fi++) {
                                    char **grown = realloc(expanded_words,
                                                           (word_count + 1) *
                                                               sizeof(char *));
                                    if (!grown) {
                                        for (int k = fi; k < field_count; k++) {
                                            free(fields[k]);
                                        }
                                        free(fields);
                                        free(expanded);
                                        set_executor_error(
                                            executor, "Memory allocation "
                                                      "failed in for loop");
                                        symtable_pop_scope(executor->symtable);
                                        return 1;
                                    }
                                    expanded_words = grown;
                                    expanded_words[word_count++] = fields[fi];
                                }
                                free(
                                    fields); /// strings moved to expanded_words
                                free(expanded);
                            } else if (expanded[0] == '\0' &&
                                       word->type != NODE_STRING_LITERAL &&
                                       word->type != NODE_STRING_EXPANDABLE) {
                                /// Null-word removal: an unquoted expansion
                                /// that produced the empty string contributes
                                /// zero iteration words (`for i in $x` with
                                /// x="" runs zero iterations). A quoted empty
                                /// "" stays one word via the branch below.
                                free(expanded);
                            } else {
                                /// Not split: keep the whole expansion as a
                                /// single field, so a quoted empty "" yields
                                /// one empty iteration word.
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
            /// A fused qualifier on a for-list word (`for i in "$f"(N)`)
            /// filters the literal value like argv/array position -- checked
            /// before the plain glob path because for-list words are
            /// NODE_VAR (so word_globbable is true) yet the qualifier form
            /// must not re-glob metacharacters in the value. The produced
            /// word is scalar, so replace the single element at
            /// normal_wc_start with the 0-or-1 filter result.
            if (word->glob_qualified && normal_wc_start >= 0 &&
                normal_wc_start == word_count - 1 &&
                shell_mode_allows(FEATURE_GLOB_QUALIFIERS)) {
                int qc = 0;
                char **qr = apply_glob_qualifier_to_literal(
                    expanded_words[normal_wc_start], &qc);
                if (qr) {
                    free(expanded_words[normal_wc_start]);
                    if (qc >= 1) {
                        expanded_words[normal_wc_start] = qr[0];
                        for (int k = 1; k < qc; k++) {
                            free(qr[k]);
                        }
                    } else {
                        /// Nothing matched: drop the trailing word.
                        word_count--;
                    }
                    free(qr);
                }
            } else if (normal_wc_start >= 0 && word_globbable) {
                /// Pathname-expand the words this UNQUOTED word node just
                /// produced. normal_wc_start is -1 for the `$@` / vector
                /// paths and for quoted word nodes, leaving those
                /// untouched.
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

    /// Iterate over expanded words
    for (int i = 0; i < word_count; i++) {
        if (expanded_words[i]) {
            /// Update loop variable using POSIX scope-chain semantics: if
            /// a local of this name already exists (e.g. `local i` in the
            /// enclosing function), update that local instead of creating
            /// a parallel global. See issue #47.
            int assign_rc = symtable_assign_var(executor->symtable, var_name,
                                                expanded_words[i]);
            if (assign_rc != 0) {
                if (assign_rc == SYMTABLE_ERR_READONLY) {
                    executor_error_report(executor, SHELL_ERR_READONLY_VAR,
                                          SOURCE_LOC_UNKNOWN,
                                          "%s: readonly variable", var_name);
                } else {
                    set_executor_error(executor, "Failed to set loop variable");
                }
                /// Cleanup expanded words
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

            /// Notify debug system of loop variable update
            if (g_debug_context && g_debug_context->enabled) {
                debug_update_loop_variable(g_debug_context, var_name,
                                           expanded_words[i]);
            }

            /// Execute body
            last_result = execute_command_chain(executor, body);
            iteration++;

            /// Check for break/continue. See consume_loop_control() for
            /// `break N` / `continue N` propagation semantics.
            if (executor->loop_control == LOOP_BREAK) {
                (void)consume_loop_control(executor);
                break;
            } else if (executor->loop_control == LOOP_CONTINUE) {
                if (consume_loop_control(executor)) {
                    break;
                }
            }

            /// `return` inside the body: stop iterating, propagate the
            /// function-return signal to the enclosing function.
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

            /// POSIX-required shell abort fired from inside the body.
            if (executor->shell_exit_requested) {
                break;
            }
        }

        /// Honor abort across the outer iteration as well.
        if (executor->shell_exit_requested) {
            break;
        }
    }

    /// Cleanup expanded words
    for (int i = 0; i < word_count; i++) {
        free(expanded_words[i]);
    }
    free(expanded_words);

    /// Notify debug system we're exiting the loop
    if (g_debug_context && g_debug_context->enabled) {
        debug_exit_loop(g_debug_context);
    }

    /// Decrement loop depth before returning
    executor->loop_depth--;

    /// Pop loop scope
    symtable_pop_scope(executor->symtable);

    /// Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    /// Pop error context
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

    /// Get the four children: init, test, update, body
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

    /// Check for trailing redirections
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

    /// Push loop scope
    if (symtable_push_scope(executor->symtable, SCOPE_LOOP, "for-arith-loop") !=
        0) {
        executor_error_add(executor, SHELL_ERR_SCOPE_ERROR, for_arith_node->loc,
                           "failed to create loop scope");
        return 1;
    }

    /// Notify debug system we're entering a loop
    if (g_debug_context && g_debug_context->enabled) {
        debug_enter_loop(g_debug_context, "for", "(arithmetic)", NULL);
    }

    /// Increment loop depth - enables break/continue builtins
    executor->loop_depth++;

    /// Push error context for structured error reporting
    executor_push_context(executor, for_arith_node->loc, "in C-style for loop");

    int last_result = 0;
    loop_monitor_t monitor;
    loop_monitor_init(&monitor);
    bool runaway_tripped = false;
    bool errexit_tripped = false;
    int iteration = 0;

    /// Execute init expression (once at the start)
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

    /// Loop: test, execute body, update
    while (1) {
        /// Evaluate test expression (if empty, treat as true - infinite loop)
        if (test_node && test_node->val.str && test_node->val.str[0] != '\0') {
            char *test_expanded =
                expand_if_needed(executor, test_node->val.str);
            if (test_expanded) {
                arithm_clear_error();
                char *result_str =
                    arithm_expand_with_executor(executor, test_expanded);
                if (!result_str || arithm_error_is_flagged()) {
                    /// Arithmetic error
                    free(result_str);
                    free(test_expanded);
                    last_result = 1;
                    break;
                }

                /// Convert result to check if non-zero
                long long test_result = strtoll(result_str, NULL, 10);
                free(result_str);
                free(test_expanded);

                /// If test result is 0, exit the loop
                if (test_result == 0) {
                    break;
                }
            }
        }
        /// If test is empty, continue (infinite loop until break)

        /// Execute body
        last_result = execute_command_chain(executor, body);
        iteration++;

        /// Check for break/continue. `continue N>1` propagates via
        /// break so the enclosing loop sees it; otherwise we fall
        /// through to the for-loop's update expression below.
        if (executor->loop_control == LOOP_BREAK) {
            (void)consume_loop_control(executor);
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            if (consume_loop_control(executor)) {
                break;
            }
            /// Fall through to update expression.
        }

        /// `return` inside the body: stop iterating, propagate the
        /// function-return signal to the enclosing function.
        if (loop_body_is_function_return(last_result)) {
            break;
        }

        if (loop_errexit_tripped(last_result)) {
            errexit_tripped = true;
            break;
        }

        /// POSIX-required shell abort fired from inside the body --
        /// skip the update expression and exit the loop.
        if (executor->shell_exit_requested) {
            break;
        }

        /// Execute update expression
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

    /// Notify debug system we're exiting the loop
    if (g_debug_context && g_debug_context->enabled) {
        debug_exit_loop(g_debug_context);
    }

    /// Decrement loop depth before returning
    executor->loop_depth--;

    /// Pop loop scope
    symtable_pop_scope(executor->symtable);

    /// Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    /// Pop error context
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

    /// Check for trailing redirections on the select loop
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

    /// Build expanded word list for menu
    char **menu_items = NULL;
    int item_count = 0;

    if (word_list && word_list->first_child) {
        node_t *word = word_list->first_child;
        while (word) {
            if (word->val.str) {
                char *expanded = expand_if_needed(executor, word->val.str);
                if (expanded) {
                    /// Check if this was a quoted string (no IFS splitting)
                    bool is_quoted = (word->type == NODE_STRING_LITERAL ||
                                      word->type == NODE_STRING_EXPANDABLE);
                    /// Same rule as the for-list / argv sites: an unquoted
                    /// command substitution splits under
                    /// FEATURE_CMDSUB_WORD_SPLIT and an unquoted parameter
                    /// under FEATURE_WORD_SPLIT_DEFAULT (both off in lush
                    /// mode), so `select` stays consistent with `for` --
                    /// previously it split neither in lush mode but also never
                    /// split a command sub in zsh mode.
                    bool should_word_split =
                        (word->type == NODE_COMMAND_SUB &&
                         shell_mode_allows(FEATURE_CMDSUB_WORD_SPLIT)) ||
                        (word->type == NODE_VAR &&
                         shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT));

                    if (!is_quoted && expanded[0] == '\0') {
                        /// Null-word removal: an unquoted expansion that
                        /// produced the empty string contributes zero menu
                        /// items (a quoted empty "" stays one item below).
                        free(expanded);
                    } else if (!should_word_split) {
                        /// Quoted strings or no-word-split mode: keep as single
                        /// item
                        menu_items = realloc(menu_items,
                                             (item_count + 1) * sizeof(char *));
                        if (!menu_items) {
                            free(expanded);
                            return 1;
                        }
                        menu_items[item_count] = expanded;
                        item_count++;
                    } else {
                        /// Unquoted: split via the canonical ifs_field_split
                        /// (respects live IFS and preserves non-whitespace
                        /// empty-field semantics strtok cannot express).
                        char *ifs_owned =
                            symtable_get(executor->symtable, "IFS");
                        const char *ifs = ifs_owned ? ifs_owned : " \t\n";
                        int field_count = 0;
                        char **fields =
                            ifs_field_split(expanded, ifs, &field_count);
                        free(ifs_owned);
                        for (int fi = 0; fi < field_count; fi++) {
                            char **grown = realloc(
                                menu_items, (item_count + 1) * sizeof(char *));
                            if (!grown) {
                                for (int k = fi; k < field_count; k++) {
                                    free(fields[k]);
                                }
                                free(fields);
                                free(expanded);
                                return 1;
                            }
                            menu_items = grown;
                            /// Ownership transfers from fields[fi] into
                            /// menu_items.
                            menu_items[item_count++] = fields[fi];
                        }
                        free(fields);
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
        return 0; /// No items, nothing to do
    }

    /// Push loop scope
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

    /// Get PS3 prompt (default is "#? "). symtable_get returns an owned copy;
    /// hold it for the loop and free it in the cleanup section below.
    char *ps3_owned = symtable_get(executor->symtable, "PS3");
    const char *ps3 = (ps3_owned && *ps3_owned) ? ps3_owned : "#? ";

    while (1) {
        /// Display menu
        for (int i = 0; i < item_count; i++) {
            fprintf(stderr, "%d) %s\n", i + 1, menu_items[i]);
        }

        /// Display prompt and read input
        fprintf(stderr, "%s", ps3);
        fflush(stderr);

        if (!fgets(input_buf, sizeof(input_buf), stdin)) {
            /// EOF - exit loop
            break;
        }

        /// Remove trailing newline
        size_t len = strlen(input_buf);
        if (len > 0 && input_buf[len - 1] == '\n') {
            input_buf[len - 1] = '\0';
        }

        /// Set REPLY to the raw input
        symtable_set(executor->symtable, "REPLY", input_buf);

        /// Parse selection number
        char *endptr;
        long selection = strtol(input_buf, &endptr, 10);

        /// Set loop variable using POSIX scope-chain semantics (issue #47)
        if (*input_buf != '\0' && *endptr == '\0' && selection >= 1 &&
            selection <= item_count) {
            /// Valid selection
            symtable_assign_var(executor->symtable, var_name,
                                menu_items[selection - 1]);
        } else {
            /// Invalid or empty input - set variable to empty
            symtable_assign_var(executor->symtable, var_name, "");
        }

        /// Execute body
        node_t *cmd = body;
        while (cmd) {
            last_result = execute_node(executor, cmd);

            /// Check for break/continue
            if (executor->loop_control != LOOP_NORMAL) {
                break;
            }

            /// POSIX-required shell abort: drop out of the select body.
            if (executor->shell_exit_requested) {
                break;
            }

            cmd = cmd->next_sibling;
        }

        /// Handle break/continue from body. See consume_loop_control()
        /// for `break N` / `continue N` propagation semantics.
        if (executor->loop_control == LOOP_BREAK) {
            (void)consume_loop_control(executor);
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            if (consume_loop_control(executor)) {
                break;
            }
            continue;
        }

        /// POSIX-required shell abort: stop the outer select loop.
        if (executor->shell_exit_requested) {
            break;
        }
    }

    /// Cleanup
    executor->loop_depth--;
    symtable_pop_scope(executor->symtable);
    free(ps3_owned);

    for (int i = 0; i < item_count; i++) {
        free(menu_items[i]);
    }
    free(menu_items);

    /// Restore file descriptors if we set up redirections
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
        return 0; /// Nothing to time
    }

    /// Get start time
    struct timeval start_time, end_time;
    struct rusage start_usage, end_usage;

    gettimeofday(&start_time, NULL);
    getrusage(RUSAGE_CHILDREN, &start_usage);

    /// Execute the pipeline
    int result = execute_node(executor, pipeline);

    /// Get end time
    gettimeofday(&end_time, NULL);
    getrusage(RUSAGE_CHILDREN, &end_usage);

    /// Calculate elapsed times
    double real_time = (end_time.tv_sec - start_time.tv_sec) +
                       (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    double user_time =
        (end_usage.ru_utime.tv_sec - start_usage.ru_utime.tv_sec) +
        (end_usage.ru_utime.tv_usec - start_usage.ru_utime.tv_usec) / 1000000.0;

    double sys_time =
        (end_usage.ru_stime.tv_sec - start_usage.ru_stime.tv_sec) +
        (end_usage.ru_stime.tv_usec - start_usage.ru_stime.tv_usec) / 1000000.0;

    /// Check for TIMEFORMAT variable (Bash extension)
    /// symtable_get returns an owned copy; free it before returning.
    char *timeformat = symtable_get(executor->symtable, "TIMEFORMAT");

    if (posix_format) {
        /// POSIX format: real, user, sys in seconds
        fprintf(stderr, "real %.2f\nuser %.2f\nsys %.2f\n", real_time,
                user_time, sys_time);
    } else if (timeformat && *timeformat) {
        /// Custom format (simplified - just show the times)
        fprintf(stderr, "\nreal\t%.3fs\nuser\t%.3fs\nsys\t%.3fs\n", real_time,
                user_time, sys_time);
    } else {
        /// Default Bash-like format
        fprintf(stderr, "\nreal\t%dm%.3fs\nuser\t%dm%.3fs\nsys\t%dm%.3fs\n",
                (int)(real_time / 60), fmod(real_time, 60.0),
                (int)(user_time / 60), fmod(user_time, 60.0),
                (int)(sys_time / 60), fmod(sys_time, 60.0));
    }

    free(timeformat);
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

    /// Get the coprocess name (default to "COPROC")
    const char *coproc_name = coproc_node->val.str;
    if (!coproc_name || !*coproc_name) {
        coproc_name = "COPROC";
    }

    /// Create pipes for bidirectional communication
    /// pipe_to_coproc: parent writes to [1], coproc reads from [0]
    /// pipe_from_coproc: coproc writes to [1], parent reads from [0]
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
        /// Child process (the coprocess) -- asynchronous: mark the context so
        /// it ignores SIGINT per the POSIX async-list rule, then reset the
        /// inherited hangup and fault handlers (#375).
        executor->async_context = true;
        reset_subshell_signals();

        /// Redirect stdin from pipe_to_coproc[0]
        close(pipe_to_coproc[1]); /// Close write end
        dup2(pipe_to_coproc[0], STDIN_FILENO);
        close(pipe_to_coproc[0]);

        /// Redirect stdout to pipe_from_coproc[1]
        close(pipe_from_coproc[0]); /// Close read end
        dup2(pipe_from_coproc[1], STDOUT_FILENO);
        close(pipe_from_coproc[1]);

        /// Execute the command
        int result = execute_node(executor, command);
        fflush(stdout);
        fflush(stderr);
        subshell_cleanup();
        _exit(result);
    }

    /// Parent process

    /// Close the ends we don't need
    close(pipe_to_coproc[0]);   /// Close read end of input pipe
    close(pipe_from_coproc[1]); /// Close write end of output pipe

    /// Store file descriptors in NAME array
    /// NAME[0] = fd to read from coproc (pipe_from_coproc[0])
    /// NAME[1] = fd to write to coproc (pipe_to_coproc[1])
    char fd_str[32];

    /// Set NAME[0] - read fd
    snprintf(fd_str, sizeof(fd_str), "%d", pipe_from_coproc[0]);
    symtable_set_array_element(coproc_name, "0", fd_str);

    /// Set NAME[1] - write fd
    snprintf(fd_str, sizeof(fd_str), "%d", pipe_to_coproc[1]);
    symtable_set_array_element(coproc_name, "1", fd_str);

    /// Store PID in NAME_PID variable
    char pid_var_name[256];
    snprintf(pid_var_name, sizeof(pid_var_name), "%s_PID", coproc_name);
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", (int)pid);
    symtable_set_global(pid_var_name, pid_str);

    /// Add to job table (background job)
    /// The coprocess runs in background, so we don't wait for it here

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
        return 0; /// Empty anonymous function
    }

    /// Collect and expand trailing arguments BEFORE pushing the
    /// function scope. Arg expressions (e.g. $vars inside double-quoted
    /// args) must resolve in the caller's scope, mirroring how
    /// build_argv_from_ast / execute_function_call do for regular
    /// function calls. The expanded strings are then set as positional
    /// parameters once the new scope is active.
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
            /// Use the shared type-aware expansion helper so anon-function
            /// args follow the same per-node-type semantics as regular
            /// command arguments — single-quoted stays literal, double-
            /// quoted expands variables, arith / command substitution /
            /// process substitution all dispatch correctly.
            argv[i++] = expand_arg_node(executor, arg);
            if (!argv[i - 1]) {
                argv[i - 1] = strdup("");
            }
        }
    }

    /// Create a new scope for the anonymous function
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

    /// Set positional parameters $1..$N from the pre-expanded args.
    for (int i = 0; i < argc; i++) {
        char param_name[16];
        snprintf(param_name, sizeof(param_name), "%d", i + 1);
        symtable_set_local_var(executor->symtable, param_name,
                               argv[i] ? argv[i] : "");
    }
    char argc_str[16];
    snprintf(argc_str, sizeof(argc_str), "%d", argc);
    symtable_set_local_var(executor->symtable, "#", argc_str);

    /// Free the expanded arg strings; symtable owns its own copies.
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);

    /// Execute the body
    int result = execute_node(executor, body);

    /// Check for function return (special code 200-455)
    if (result >= 200 && result <= 455) {
        result = result - 200; /// Extract actual return value
    }

    /// Pop the scope
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

    /// Execute left command
    int left_result = execute_node(executor, left);

    /// A variable-assignment error (readonly) on the left aborts the
    /// whole AND-OR list -- it is not an ordinary failure that the
    /// operator gets to react to. Skip the right operand regardless of
    /// the && / || sense.
    if (executor->command_abort) {
        return left_result;
    }

    /// Only execute right command if left succeeded (exit code 0)
    if (left_result == 0) {
        return execute_node(executor, right);
    }

    /// Left failed, return its exit code without executing right
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

    /// Execute left command
    int left_result = execute_node(executor, left);

    /// A variable-assignment error (readonly) on the left aborts the
    /// whole AND-OR list -- the `||` must not treat it as an ordinary
    /// failure and run the right operand. Skip the right operand.
    if (executor->command_abort) {
        return left_result;
    }

    /// Only execute right command if left failed (non-zero exit code)
    if (left_result != 0) {
        return execute_node(executor, right);
    }

    /// Left succeeded, return its exit code without executing right
    return left_result;
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
 *   ${@:N}, ${@:N:M}         positional-param slice
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

/// True when a `:`-led subscript suffix (`spec` points just past the `:`)
/// is an array element slice offset (${arr[@]:1:2}) rather than one of the
/// `:-` / `:+` / `:=` / `:?` parameter operators. A slice offset is numeric,
/// optionally a space-then-sign (`: -2`); the operator sigils -/+/=/? can
/// never begin a slice, and strtol would otherwise consume the +/- of
/// `:+N` / `:-N` and mis-read the operand as a slice offset (#530). Shared
/// by the scalar-slot slice and the vector-slice paths so the two agree.
static bool slice_spec_is_numeric(const char *spec) {
    if (*spec == '-' || *spec == '+' || *spec == '=' || *spec == '?') {
        return false;
    }
    char *endp = NULL;
    (void)strtol(spec, &endp, 10);
    return endp != spec;
}

/// Parse an optional `:N` / `:N:M` slice suffix shared by the array and
/// positional vector forms. @p p points just past the subscript (the `]`
/// of NAME[@], or the `@`/`*` of a positional ref); @p end is just past
/// the closing `}`. On a `:` slice sets *has_slice and the offset/length;
/// with no suffix leaves them untouched. Returns false on malformed junk
/// (anything other than end-of-content or a `:`-led numeric slice).
static bool parse_vector_slice_suffix(const char *p, const char *end,
                                      bool *has_slice, int *slice_offset,
                                      int *slice_length) {
    if (p == end) {
        return true; /// no slice suffix
    }
    if (*p != ':') {
        return false; /// junk between subscript/ref and `}`
    }
    p++;
    if (p >= end) {
        return false;
    }
    size_t spec_len = (size_t)(end - p);
    char spec[64];
    if (spec_len >= sizeof(spec)) {
        return false;
    }
    memcpy(spec, p, spec_len);
    spec[spec_len] = '\0';
    if (!slice_spec_is_numeric(spec)) {
        return false; /// a :- / :+ / := / :? operator, not a slice
    }
    char *endp = NULL;
    *slice_offset = (int)strtol(spec, &endp, 10);
    if (!endp) {
        return false;
    }
    if (*endp == ':') {
        *slice_length = (int)strtol(endp + 1, NULL, 10);
    } else if (*endp != '\0') {
        return false;
    }
    *has_slice = true;
    return true;
}

/// Writes the word-join separator for star expansions into out[0..1]: the
/// first character of IFS. POSIX joins "$*" / "${a[*]}" on this. IFS unset
/// -> a single space (the first char of the default IFS); IFS set but empty
/// -> the empty string (elements concatenated, no separator); IFS set ->
/// its first byte. `out` must be at least 2 bytes; no allocation, so there
/// is nothing to free and no failure path. Distinguishing unset (default
/// space) from empty ("" = no separator) is load-bearing, and symtable_get
/// preserves that distinction (NULL vs "").
static void ifs_join_separator(executor_t *executor, char out[2]) {
    char *ifs = symtable_get(executor->symtable, "IFS");
    if (!ifs) {
        out[0] = ' ';
        out[1] = '\0';
        return;
    }
    out[0] = ifs[0]; /// '\0' when IFS="" -> empty separator
    out[1] = '\0';
    free(ifs);
}

/// The separator used to FLATTEN a list/map into a scalar in a scalar slot
/// under a relaxed (non-strict-typing) mode, matching the active oracle: bash
/// (and POSIX, curated to the bash family) joins a `[@]`-in-scalar flatten on
/// a literal SPACE, ignoring IFS; zsh joins on the first character of IFS.
/// This is the IMPLICIT `[@]` flatten only -- the explicit `[*]` / `$*` join
/// is IFS[0] in every mode (SEMANTICS section 3.5) and uses
/// ifs_join_separator directly. Writes out[0..1].
static void relaxed_flatten_sep(executor_t *executor, char out[2]) {
    if (shell_mode_get() == SHELL_MODE_ZSH) {
        ifs_join_separator(executor, out);
        return;
    }
    out[0] = ' '; /// bash / posix: space, IFS-independent
    out[1] = '\0';
}

/// Flatten a bare collection reference (${arr}, or a bare name meeting a
/// scalar operator) to its mode-appropriate scalar under the SEMANTICS
/// section 3.9 RELAXED policy (FEATURE_STRICT_VALUE_TYPING off): bash and
/// posix read element 0 (the subscript-0 convention); zsh reads the whole
/// array joined on the first char of IFS. Returns an OWNED string (empty,
/// not NULL, for an empty/missing element; NULL only on allocation failure),
/// matching symtable_get_var's ownership contract so callers free it
/// uniformly. The single definition shared by expand_array_unsubscripted
/// (bare ${arr}) and the bare-collection scalar-operator path keeps the two
/// section 3.9 sites from drifting.
static char *flatten_bare_collection_relaxed(executor_t *executor,
                                             array_value_t *array) {
    if (shell_mode_get() == SHELL_MODE_ZSH) {
        char sep[2];
        relaxed_flatten_sep(executor, sep); /// IFS[0] under zsh mode
        char *joined = symtable_array_expand(array, sep);
        return joined ? joined : strdup("");
    }
    const char *first = symtable_array_get_index(array, 0);
    return strdup(first ? first : "");
}

/// SEMANTICS.md section 3.9 gate for a zsh parameter-FLAG form whose operand
/// is a COLLECTION reaching a scalar slot -- the (v)/(k)/(kv)/(@)/(s)-on-array
/// forms, and any transform flag applied to a collection. Mirrors the
/// ${arr[@]} scalar-slot gate and the ${!arr[@]} keys gate: strict typing
/// (lush default) is a runtime type error; a relaxed compat mode flattens to
/// the oracle scalar.
///
/// `flags`  the flag characters between ( and ); `is_map` distinguishes a map
/// from an indexed list for the diagnostic; `form` is the reconstructed source
/// for the message; out_sep receives the separator the caller uses to build
/// the flattened scalar in the relaxed path.
///
/// An explicit (j) collapse is the sanctioned list->scalar operator, so when
/// 'j' is present the gate is a no-op: it returns true with out_sep=" " (the
/// (j) handler re-joins on its own separator afterward, ignoring IFS). Under
/// strict typing it emits E1134, requests a POSIX exit, and returns false --
/// the caller frees its locals and returns strdup(""). Under a relaxed mode it
/// returns true with out_sep set to the oracle separator (bash/posix space,
/// zsh IFS[0]).
static bool section39_flag_scalar_gate(executor_t *executor, const char *flags,
                                       bool is_map, const char *form,
                                       char out_sep[2]) {
    if (strchr(flags, 'j') != NULL) { /// sanctioned explicit join
        out_sep[0] = ' ';
        out_sep[1] = '\0';
        return true;
    }
    if (shell_mode_allows(FEATURE_STRICT_VALUE_TYPING)) {
        shell_error_t *err = shell_error_create(
            SHELL_ERR_TYPE_MISMATCH, SHELL_SEVERITY_ERROR,
            executor_current_loc(executor),
            "type mismatch: %s value %s in a scalar position",
            is_map ? "map" : "list", form);
        if (err) {
            shell_error_set_suggestion(
                err, "join the elements explicitly -- add a (j:sep:) flag "
                     "(e.g. ${(j: :)name}), or use the form as a vector.");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
            executor->has_error = true;
        } else {
            executor_error_report(
                executor, SHELL_ERR_TYPE_MISMATCH,
                executor_current_loc(executor),
                "type mismatch: %s value %s in a scalar position",
                is_map ? "map" : "list", form);
        }
        executor_request_posix_exit(executor, 1);
        return false;
    }
    relaxed_flatten_sep(executor, out_sep); /// bash/posix space, zsh IFS[0]
    return true;
}

/// Join `count` strings with `sep` between them into one owned string
/// (NULL on allocation failure). An empty `sep` concatenates.
static char *join_strings_with_sep(char **items, int count, const char *sep) {
    size_t sep_len = strlen(sep);
    size_t total = 1; /// trailing NUL
    for (int i = 0; i < count; i++) {
        total += strlen(items[i]) + (i > 0 ? sep_len : 0);
    }
    char *result = malloc(total);
    if (!result) {
        return NULL;
    }
    result[0] = '\0';
    for (int i = 0; i < count; i++) {
        if (i > 0 && sep_len) {
            strcat(result, sep);
        }
        strcat(result, items[i]);
    }
    return result;
}

/// Gather the positional parameters ($1..$N) for the CURRENT scope into a
/// freshly-allocated argv-style array. In function scope reads the local
/// "1".."N" symbols (bounded by "#"); otherwise reads the global
/// shell_argv[1..]. Every element and the array itself are owned by the
/// caller (free each element, then the array). *out_count receives N. Returns
/// NULL with *out_count == 0 for an empty positional set or on allocation
/// failure -- join_strings_with_sep(NULL, 0, sep) is a valid empty join, so
/// callers need not special-case NULL. The single scope-aware accessor keeps
/// the braced (${@}/${*}) and unbraced ($@/$*) positional forms resolving
/// through one path.
static char **collect_positional_params(executor_t *executor, int *out_count) {
    bool in_fn = symtable_in_function_scope(executor->symtable);
    int count;
    if (in_fn) {
        char *argc_str = symtable_get_var(executor->symtable, "#");
        count = argc_str ? atoi(argc_str) : 0;
        free(argc_str);
    } else {
        count = shell_argc > 1 ? shell_argc - 1 : 0;
    }
    if (count <= 0) {
        *out_count = 0;
        return NULL;
    }
    char **params = malloc(sizeof(char *) * (size_t)count);
    if (!params) {
        *out_count = 0;
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        if (in_fn) {
            char name[16];
            snprintf(name, sizeof(name), "%d", i + 1);
            char *v = symtable_get_var(executor->symtable, name);
            params[i] = v ? v : strdup("");
        } else {
            params[i] = strdup(shell_argv[i + 1] ? shell_argv[i + 1] : "");
        }
    }
    *out_count = count;
    return params;
}

static bool try_expand_vector_arg(executor_t *executor, node_t *node,
                                  char ***out_vec, int *out_count,
                                  bool positional_only) {
    if (!node || !node->val.str) {
        return false;
    }
    /// Only quoted-string and string-expandable shapes carry a single
    /// parameter-expansion as their entire payload. Other node types
    /// (command sub, arith, etc.) are not vector candidates.
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

    /// $@ / $* (unbraced).
    bool positional_at =
        (len == 2 && s[0] == '$' && (s[1] == '@' || s[1] == '*'));
    /// ${...} braced forms.
    bool braced = (len >= 3 && s[0] == '$' && s[1] == '{' && s[len - 1] == '}');
    /// Bare `$NAME` where NAME is an array. zsh expands bare array
    /// references to N words in word-list contexts (for-loop iteration,
    /// command argv) regardless of word-split setting. Bash's `$arr`
    /// is the first element only, so only treat this as vector form
    /// in zsh/lush mode. Detected here so execute_for and
    /// build_argv_from_ast both honor it via the same helper.
    /// Issue #99.
    bool bare_array = false;
    if (!positional_at && !braced && len >= 2 && s[0] == '$') {
        size_t walked = lush_ident_match_start(s + 1, len - 1);
        if (walked > 0) {
            while (walked + 1 < len) {
                size_t n =
                    lush_ident_match_continue(s + 1 + walked, len - 1 - walked);
                if (n == 0) {
                    break;
                }
                walked += n;
            }
        }
        if (walked > 0 && walked + 1 == len) {
            char name_buf[256];
            size_t nlen = len - 1;
            if (nlen < sizeof(name_buf)) {
                memcpy(name_buf, s + 1, nlen);
                name_buf[nlen] = '\0';
                array_value_t *probe = symtable_get_array(name_buf);
                if (probe) {
                    shell_mode_t mode = shell_mode_get();
                    /// Curated: zsh + lush explode bare $arr; bash + posix
                    /// keep it scalar (first element via the existing
                    /// expand_array_unsubscripted path).
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

    /// Distinguish what's inside the braces. For positional_at the
    /// "name" is `@` / `*`; subscript is implied. For braced forms:
    /// - `${@}` / `${*}` -> positional, same as $@/$*
    /// - `${NAME[@]}` / `${NAME[*]}` -> array all
    /// - `${!NAME[@]}` -> array keys
    /// - any of the above with `:N` or `:N:M` slicing suffix
    /// - anything else -> not a vector form
    bool keys_form = false;
    const char *name_start = NULL;
    size_t name_len = 0;
    char subscript = '@'; /// @ vs *; only matters for joining policy
    int slice_offset = 0;
    int slice_length = -1; /// -1 = "to end"
    bool has_slice = false;
    bool is_positional = positional_at;

    if (positional_at) {
        subscript = s[1];
    } else if (bare_array) {
        /// Bare $NAME: name is s+1, length is len-1. Treat as if it
        /// were ${NAME[@]} -- produce all elements as separate words.
        name_start = s + 1;
        name_len = len - 1;
        subscript = '@';
    } else {
        /// inner content between { and }
        const char *p = s + 2;
        const char *end = s + len - 1;
        if (p == end) {
            return false;
        }
        /// ${(FLAGS)NAME} or ${(FLAGS)NAME[@]} -- zsh parameter-flag
        /// vector form. Recognized flag chars: k (keys), v (values,
        /// default), o (sort ascending), O (sort descending), a
        /// (array-order, no sort), u (unique), @ (vector presentation),
        /// s:SEP: (split scalar NAME on SEP -- returns a list of
        /// substrings). Builds the vector directly and short-circuits
        /// the rest of try_expand_vector_arg. Issue #104 + #188.
        ///
        /// A parameter-flag form is never a positional-parameter vector, so
        /// a positional_only caller (the command NAME position) skips it:
        /// falling through leaves name_len==0 (the leading `(`), which
        /// returns false, routing a named list/map flag form in command
        /// position to the SEMANTICS section 3.9 scalar-slot type error
        /// instead of silently spreading it as a command (#529 review).
        if (*p == '(' && !positional_only) {
            const char *flag_start = p + 1;
            const char *flag_end = flag_start;
            int paren_depth = 1;
            /// Scan for the closing `)`. Multi-char sub-args like
            /// `s:SEP:` may contain any character including ones that
            /// look special; only `)` at depth 0 terminates the flag
            /// list. (We DO NOT respect arbitrary nested parens here --
            /// no zsh flag uses them.)
            while (flag_end < end && paren_depth > 0) {
                if (*flag_end == ')') {
                    paren_depth--;
                    if (paren_depth == 0) {
                        break;
                    }
                }
                flag_end++;
            }
            if (flag_end < end && *flag_end == ')') {
                bool flag_k = false, flag_v = false, flag_o = false,
                     flag_O = false;
                bool flag_u = false;
                bool flag_s = false;
                char *split_sep = NULL; /// owned; set when flag_s
                bool ok_flags = true;
                for (const char *f = flag_start; f < flag_end; f++) {
                    switch (*f) {
                    case 'k':
                        flag_k = true;
                        break;
                    case 'v':
                        /// Values. `(kv)` (both k and v) interleaves pairs.
                        flag_v = true;
                        break;
                    case 'o':
                        flag_o = true;
                        break;
                    case 'O':
                        flag_O = true;
                        break;
                    case 'a':
                        /// array order, no sort -- default
                        flag_o = false;
                        flag_O = false;
                        break;
                    case 'u':
                        flag_u = true;
                        break;
                    case '@':
                        /// Per SEMANTICS.md section 3.7, (@) is a
                        /// spelling alias for vector presentation --
                        /// redundant with the [@] subscript. Accept
                        /// it as a no-op flag so ${(@)arr} yields
                        /// exactly what ${arr[@]} would.
                        break;
                    case 's': {
                        /// (s:SEP:) -- split scalar parameter on SEP.
                        /// The next char after `s` is the delimiter
                        /// character used to bracket SEP; SEP is the
                        /// span up to the matching delimiter.
                        flag_s = true;
                        if (f + 1 >= flag_end) {
                            ok_flags = false;
                            break;
                        }
                        char d = f[1];
                        const char *sep_start = f + 2;
                        const char *sep_end = sep_start;
                        while (sep_end < flag_end && *sep_end != d) {
                            sep_end++;
                        }
                        if (sep_end >= flag_end) {
                            ok_flags = false;
                            break;
                        }
                        size_t sep_len = (size_t)(sep_end - sep_start);
                        free(split_sep);
                        split_sep = malloc(sep_len + 1);
                        if (!split_sep) {
                            ok_flags = false;
                            break;
                        }
                        memcpy(split_sep, sep_start, sep_len);
                        split_sep[sep_len] = '\0';
                        f = sep_end; /// will be incremented by loop
                        break;
                    }
                    default:
                        ok_flags = false;
                        break;
                    }
                }
                const char *np = flag_end + 1;
                const char *nstart = np;
                while (np < end) {
                    size_t n =
                        lush_ident_match_continue(np, (size_t)(end - np));
                    if (n == 0) {
                        break;
                    }
                    np += n;
                }
                /// Accept an optional [@] / [*] subscript after the
                /// name so `${(u)arr[@]}` parses (zsh idiom for "uniq
                /// the all-elements presentation"). The subscript is
                /// redundant with our vector-yielding semantics: we
                /// already iterate all elements either way.
                const char *name_end = np;
                if (np + 2 < end && np[0] == '[' &&
                    (np[1] == '@' || np[1] == '*') && np[2] == ']') {
                    np += 3;
                }
                if (ok_flags && np == end && name_end > nstart) {
                    char nbuf[256];
                    size_t nlen = (size_t)(name_end - nstart);
                    if (nlen < sizeof(nbuf)) {
                        memcpy(nbuf, nstart, nlen);
                        nbuf[nlen] = '\0';
                        size_t kc = 0;
                        char **items = NULL;
                        if (flag_s && split_sep) {
                            /// (s:SEP:) on a SCALAR: look up the named
                            /// scalar, split on SEP, yield a list of
                            /// substrings. If the name is bound to an
                            /// array we still treat it as a scalar by
                            /// joining (matching zsh's behavior of
                            /// applying (s) to the textual value).
                            char *scalar =
                                symtable_get_var(executor->symtable, nbuf);
                            const char *src = scalar ? scalar : "";
                            size_t seplen = strlen(split_sep);
                            size_t cap = 4;
                            items = malloc(sizeof(char *) * cap);
                            if (items && seplen > 0) {
                                const char *cursor = src;
                                while (1) {
                                    const char *hit = strstr(cursor, split_sep);
                                    size_t flen = hit ? (size_t)(hit - cursor)
                                                      : strlen(cursor);
                                    if (kc + 1 >= cap) {
                                        cap *= 2;
                                        char **grown = realloc(
                                            items, sizeof(char *) * cap);
                                        if (!grown) {
                                            for (size_t i = 0; i < kc; i++) {
                                                free(items[i]);
                                            }
                                            free(items);
                                            items = NULL;
                                            break;
                                        }
                                        items = grown;
                                    }
                                    char *piece = malloc(flen + 1);
                                    if (!piece) {
                                        for (size_t i = 0; i < kc; i++) {
                                            free(items[i]);
                                        }
                                        free(items);
                                        items = NULL;
                                        break;
                                    }
                                    memcpy(piece, cursor, flen);
                                    piece[flen] = '\0';
                                    items[kc++] = piece;
                                    if (!hit) {
                                        break;
                                    }
                                    cursor = hit + seplen;
                                }
                            } else if (items && seplen == 0) {
                                /// Degenerate empty-separator: treat as
                                /// a single-element list to avoid the
                                /// infinite loop a 0-length step would
                                /// trigger.
                                items[0] = strdup(src);
                                kc = items[0] ? 1 : 0;
                            }
                            free(scalar);
                            free(split_sep);
                            split_sep = NULL;
                        } else {
                            array_value_t *arr = symtable_get_array(nbuf);
                            if (arr) {
                                if (flag_k && flag_v) {
                                    /// (kv): interleaved key value ... (2N
                                    /// slots). Map keys/values in insertion
                                    /// order; a list uses index positions as
                                    /// keys.
                                    size_t n = 0, nv = 0;
                                    char **ks = NULL, **vs = NULL;
                                    if (arr->is_associative) {
                                        ks = symtable_array_get_keys(arr, &n);
                                        vs =
                                            symtable_array_get_values(arr, &nv);
                                    } else {
                                        n = symtable_array_length(arr);
                                    }
                                    items =
                                        malloc(sizeof(char *) * (2 * n + 1));
                                    if (items) {
                                        for (size_t i = 0; i < n; i++) {
                                            if (arr->is_associative) {
                                                items[2 * i] =
                                                    ks ? ks[i] : strdup("");
                                                items[2 * i + 1] =
                                                    (vs && i < nv) ? vs[i]
                                                                   : strdup("");
                                            } else {
                                                char idx[32];
                                                snprintf(idx, sizeof(idx),
                                                         "%zu", i);
                                                items[2 * i] = strdup(idx);
                                                const char *e =
                                                    symtable_array_get_index(
                                                        arr, (int)i);
                                                items[2 * i + 1] =
                                                    strdup(e ? e : "");
                                            }
                                        }
                                        kc = 2 * n;
                                        /// assoc element ptrs moved into items;
                                        /// free only the source arrays.
                                        free(ks);
                                        free(vs);
                                    } else {
                                        /// OOM: reclaim the source arrays.
                                        for (size_t i = 0; ks && i < n; i++) {
                                            free(ks[i]);
                                        }
                                        for (size_t i = 0; vs && i < nv; i++) {
                                            free(vs[i]);
                                        }
                                        free(ks);
                                        free(vs);
                                    }
                                } else if (flag_k) {
                                    items = symtable_array_get_keys(arr, &kc);
                                } else if (arr->is_associative) {
                                    /// Values, insertion order (get_index
                                    /// returns NULL for a map).
                                    items = symtable_array_get_values(arr, &kc);
                                } else {
                                    size_t total = symtable_array_length(arr);
                                    items =
                                        malloc(sizeof(char *) * (total + 1));
                                    if (items) {
                                        for (size_t i = 0; i < total; i++) {
                                            const char *e =
                                                symtable_array_get_index(
                                                    arr, (int)i);
                                            items[i] = strdup(e ? e : "");
                                        }
                                        kc = total;
                                    }
                                }
                            }
                        }
                        if (items) {
                            if (flag_u) {
                                size_t w = 0;
                                for (size_t i = 0; i < kc; i++) {
                                    bool dup = false;
                                    for (size_t j = 0; j < w; j++) {
                                        if (strcmp(items[i], items[j]) == 0) {
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
                                        int cmp = strcmp(items[i], items[j]);
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
        if (*p == '!') {
            keys_form = true;
            p++;
        }
        if (p == end) {
            return false;
        }
        /// ${@} / ${*}, optionally sliced: ${@:N} / ${@:N:M}.
        if (!keys_form && (*p == '@' || *p == '*')) {
            is_positional = true;
            subscript = *p;
            p++;
            if (!parse_vector_slice_suffix(p, end, &has_slice, &slice_offset,
                                           &slice_length)) {
                return false;
            }
        } else {
            /// Must be NAME[@] / NAME[*] optionally followed by :N or :N:M
            name_start = p;
            while (p < end) {
                size_t n = lush_ident_match_continue(p, (size_t)(end - p));
                if (n == 0) {
                    break;
                }
                p += n;
            }
            name_len = (size_t)(p - name_start);
            if (name_len == 0) {
                return false;
            }
            /// Braced bare `${NAME}` (no subscript). Per SEMANTICS.md
            /// section 3.9, a bare reference to a list/map value is a
            /// vector-yielding expansion -- it contributes its elements
            /// to the surrounding argv/word-list slot, exactly as
            /// `${NAME[@]}` does. Curated zsh/lush behavior; bash and
            /// posix mode keep the legacy "first-element" form via
            /// expand_array_unsubscripted. The unbraced `$NAME` case
            /// is already handled above; this is the braced peer.
            /// Issue: SEMANTICS section 3.9 conformance.
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
                /// Fall through to the per-array assembly path below.
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
            /// Optional slicing :N or :N:M (shared parser).
            if (!parse_vector_slice_suffix(p, end, &has_slice, &slice_offset,
                                           &slice_length)) {
                return false;
            }
        }
    }

braced_bare_array_ready:;
    /// A positional-only caller (the command NAME position) accepts a
    /// positional-parameter vector ($@ / $* / ${@} / ${*}) but, under strict
    /// value typing (lush default), declines a named list/map vector
    /// (${arr[@]}, bare ${arr}), which must reach the SEMANTICS section 3.9
    /// scalar-slot type error instead of silently vector-expanding as a
    /// command. Positionals are argv, not a lush list-kind value. Under a
    /// relaxed compat mode the boundary policy follows the oracle: bash and
    /// zsh spread a named array in command position, so the vector is allowed
    /// through (bash routes bare ${arr} to element 0 earlier, at the
    /// braced-bare mode check above; zsh spreads it, matching the oracle).
    if (positional_only && !is_positional &&
        shell_mode_allows(FEATURE_STRICT_VALUE_TYPING)) {
        return false;
    }
    /// Now produce the element vector.
    char **vec = NULL;
    int vcount = 0;
    int vcap = 0;

    if (is_positional) {
        /// Iterate $1..$N from positional params. Match the existing
        /// "$@" loop in execute_for: in function scope use symtable
        /// "1".."N"; otherwise use shell_argv.
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

        /// Apply ${@:offset:length} slicing. bash slices the conceptual
        /// sequence [$0, $1, ..., $N], so $0 sits at index 0; vec holds
        /// $1..$N. A negative offset counts from the end; an omitted
        /// length runs to the end.
        if (has_slice) {
            char *zero = symtable_get_var(executor->symtable, "0");
            if (!zero) {
                zero = strdup((shell_argv && shell_argc > 0 && shell_argv[0])
                                  ? shell_argv[0]
                                  : "");
            }
            int nelem = vcount + 1; /// includes $0
            int start = slice_offset;
            if (start < 0) {
                start = nelem + start;
            }
            if (start < 0) {
                start = 0;
            }
            int end_idx =
                (slice_length >= 0) ? start + slice_length - 1 : nelem - 1;
            if (end_idx >= nelem) {
                end_idx = nelem - 1;
            }
            char **sliced = NULL;
            int scount = 0, scap = 0;
            bool oom = false;
            for (int idx = start; idx <= end_idx && idx >= 0; idx++) {
                const char *src = (idx == 0) ? zero : vec[idx - 1];
                char *val = strdup(src ? src : "");
                if (!add_to_argv_list(&sliced, &scount, &scap, val)) {
                    free(val);
                    oom = true;
                    break;
                }
            }
            free(zero);
            for (int k = 0; k < vcount; k++) {
                free(vec[k]);
            }
            free(vec);
            if (oom) {
                for (int k = 0; k < scount; k++) {
                    free(sliced[k]);
                }
                free(sliced);
                return false;
            }
            vec = sliced;
            vcount = scount;
            vcap = scap;
        }
    } else {
        /// Array name lookup. Need a NUL-terminated name.
        char name_buf[256];
        if (name_len >= sizeof(name_buf)) {
            return false;
        }
        memcpy(name_buf, name_start, name_len);
        name_buf[name_len] = '\0';

        array_value_t *array = symtable_get_array(name_buf);
        if (!array) {
            /// Not actually an array -- not our case; fall back.
            return false;
        }

        if (keys_form) {
            /// Produce keys (assoc) or indices (indexed). Honor slicing.
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
                /// Indexed array: keys are the ACTUAL stored indices.
                /// For dense arrays these are 0..N-1; for sparse arrays
                /// they are the explicit indices assigned. The prior
                /// implementation generated 0..N-1 dense, which
                /// silently turned sparse arrays into dense ones and
                /// broke `for k in "${!arr[@]}"; do echo arr[$k]` on
                /// sparse data (issue #101). symtable_array_get_keys
                /// returns the real indices via array->indices[].
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
            /// Produce values, in insertion order for a map. Honor slicing.
            /// Associative arrays are keyed, not index-addressable:
            /// symtable_array_get_index returns NULL for a map (which silently
            /// produced empty slots), so read map values via the values
            /// accessor instead. Matches SEMANTICS 3.9 (a map in a
            /// vector-accepting slot contributes its values, like ${arr[@]}).
            size_t total = symtable_array_length(array);
            char **avals = NULL;
            size_t aval_count = 0;
            if (array->is_associative) {
                avals = symtable_array_get_values(array, &aval_count);
                /// get_values returns NULL for BOTH a real allocation failure
                /// and an EMPTY map (count 0); only a populated map's NULL is a
                /// failure. An empty map contributes zero elements
                /// (aval_count == 0 -> the loop is skipped), not a type error.
                if (!avals && symtable_array_length(array) > 0) {
                    return false;
                }
                total = aval_count;
            }
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
                const char *elem =
                    avals ? avals[k] : symtable_array_get_index(array, k);
                if (!add_to_argv_list(&vec, &vcount, &vcap,
                                      strdup(elem ? elem : ""))) {
                    for (int j = 0; j < vcount; j++) {
                        free(vec[j]);
                    }
                    free(vec);
                    if (avals) {
                        for (size_t j = 0; j < aval_count; j++) {
                            free(avals[j]);
                        }
                        free(avals);
                    }
                    return false;
                }
            }
            if (avals) {
                for (size_t j = 0; j < aval_count; j++) {
                    free(avals[j]);
                }
                free(avals);
            }
        }
    }

    /// Quoted "$*" / "${a[*]}" is ONE word: the elements joined by the
    /// first character of IFS (POSIX). A quoted expansion arrives as
    /// NODE_STRING_EXPANDABLE; unquoted $* / ${a[*]} (NODE_VAR) and every
    /// @ form keep exploding into separate words. An empty set yields one
    /// empty word -- a quoted expansion is never null-word-removed.
    if (subscript == '*' && node->type == NODE_STRING_EXPANDABLE) {
        char sep[2];
        ifs_join_separator(executor, sep);
        char *joined =
            vcount > 0 ? join_strings_with_sep(vec, vcount, sep) : strdup("");
        for (int i = 0; i < vcount; i++) {
            free(vec[i]);
        }
        free(vec);
        if (!joined) {
            return false;
        }
        char **one = malloc(sizeof(char *));
        if (!one) {
            free(joined);
            return false;
        }
        one[0] = joined;
        *out_vec = one;
        *out_count = 1;
        return true;
    }

    *out_vec = vec;
    *out_count = vcount;
    return true;
}

char *expand_arg_node(executor_t *executor, node_t *node) {
    if (!node || !node->val.str) {
        return strdup("");
    }
    switch (node->type) {
    case NODE_STRING_LITERAL:
        if (node->val.str[0] == '$' && node->val.str[1] == '\'' &&
            shell_mode_allows(FEATURE_ANSI_QUOTING)) {
            size_t len = strlen(node->val.str);
            if (len >= 3 && node->val.str[len - 1] == '\'') {
                return lush_expand_escapes(node->val.str + 2, len - 3,
                                           LUSH_ESC_ANSI_C);
            }
        }
        return strdup(node->val.str);
    case NODE_STRING_EXPANDABLE:
        /// Per parser.c collect_word_argument: word-context backslashes
        /// have been pre-stripped during multi-token concat, so any `\X`
        /// still present in node->val.str came from a `"..."` segment and
        /// must be resolved with double-quote rules. When the word fused quote
        /// contexts, node->quote_prov drives per-character decisions (#498);
        /// NULL keeps the whole-string double-quote policy.
        return expand_quoted_string_prov(executor, node->val.str, true,
                                         node->quote_prov, true);
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
    if (!shell_mode_allows(FEATURE_STRICT_VALUE_TYPING)) {
        /// Relaxed compat mode: follow the oracle (bash/posix element 0,
        /// zsh whole-join) via the shared bare-collection flatten.
        return flatten_bare_collection_relaxed(executor, array);
    }
    /// Strict value typing (lush default): per SEMANTICS.md section 3.9, a
    /// list/map value reaching a scalar slot (or glued to text within a
    /// word) is a runtime type error. We get here only AFTER
    /// try_expand_vector_arg declined to handle the reference as
    /// vector-yielding -- which means the surrounding context is scalar
    /// (variable assignment RHS, case word, here-string, arithmetic
    /// operand, conditional-expression operand, or a within-word "glued"
    /// position). Emit the type-mismatch diagnostic and request a POSIX
    /// shell abort so a script halts before the bad value reaches a
    /// downstream command.
    shell_error_t *err = shell_error_create(
        SHELL_ERR_TYPE_MISMATCH, SHELL_SEVERITY_ERROR,
        executor_current_loc(executor),
        "type mismatch: %s value ${%s} in a scalar position",
        array->is_associative ? "map" : "list", arr_name ? arr_name : "?");
    if (err) {
        shell_error_set_suggestion(
            err, "join the list explicitly to place it in a string position -- "
                 "${name[*]} for IFS-joining, or an explicit join.");
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

/// True when a word node carries a quoted segment (so an empty expansion must
/// stay one empty word rather than be null-removed). NODE_VAR /
/// NODE_COMMAND_SUB / NODE_ARITH_EXP are wholly-unquoted expansions.
static inline bool word_node_is_quoted(const node_t *node) {
    return node && (node->type == NODE_STRING_LITERAL ||
                    node->type == NODE_STRING_EXPANDABLE);
}

/// word_eval get-callback for the live executor (Step 2 Word CST dual-routing).
/// Resolves $name against the symtable -- the same source the legacy expansion
/// reads, so a covered word sees identical values. Returns an OWNED copy (the
/// symtable does; word_eval frees it per the get contract).
/// A word's UNCOMMITTED assign-operator writes.
///
/// `${var:=x}` mutates the shell, but a CST evaluation is only provisional:
/// word_eval can defer a LATER part of the same word, after which the whole
/// word is re-expanded by the legacy expander. If the write had already landed,
/// that re-expansion would run against mutated state and the EARLIER parts
/// would expand differently than on a legacy-only run -- `echo
/// A$u-B${u:=x}-C$g` (deferring on $g's brace value) printed `Ax-Bx` where
/// legacy prints `A-Bx`. Idempotency of the operator itself does not save it;
/// what breaks is the rest of the word.
///
/// So the writes are buffered for the duration of one word_eval and committed
/// only once the word is known fully covered; a deferred word discards them and
/// legacy re-expands from unmutated state, which is exactly a legacy-only run.
/// Reads consult the buffer first, so within-word visibility (`${u:=x}${u}` ->
/// `xx`) still matches legacy. Committing AFTER the audit compare likewise
/// keeps the audit's legacy re-evaluation on unmutated state -- which means
/// that under LUSH_WORD_CST_AUDIT the assignment happens TWICE (legacy's own
/// write during the compare, then this commit writing the same value). That is
/// audit-only and unobservable: the store carries no hook and the second write
/// is the same name/value pair.
typedef struct {
    char **names;
    char **values;
    size_t n;
    size_t cap;
} word_assign_txn_t;

/// Evaluation context for the Word CST callbacks: the executor plus the word's
/// pending-write buffer.
typedef struct {
    executor_t *ex;
    word_assign_txn_t *txn;
} word_eval_ctx_t;

/// Most recent pending value for @p name, or NULL when the word has not
/// assigned it. Later entries win (a name can be assigned at most once per
/// word in practice, but scanning backwards keeps that from being a
/// requirement).
static const char *word_txn_lookup(const word_assign_txn_t *txn,
                                   const char *name) {
    if (!txn || !name) {
        return NULL;
    }
    for (size_t i = txn->n; i > 0; i--) {
        if (strcmp(txn->names[i - 1], name) == 0) {
            return txn->values[i - 1];
        }
    }
    return NULL;
}

/// Record a pending write. False on allocation failure, which the caller turns
/// into a defer (the legacy path then performs the assignment itself).
static bool word_txn_push(word_assign_txn_t *txn, const char *name,
                          const char *value) {
    if (!txn || !name || !value) {
        return false;
    }
    if (txn->n == txn->cap) {
        size_t cap = txn->cap ? txn->cap * 2 : 4;
        char **nn = realloc(txn->names, cap * sizeof(*nn));
        if (!nn) {
            return false;
        }
        txn->names = nn;
        char **nv = realloc(txn->values, cap * sizeof(*nv));
        if (!nv) {
            return false;
        }
        txn->values = nv;
        txn->cap = cap;
    }
    char *dn = strdup(name);
    char *dv = strdup(value);
    if (!dn || !dv) {
        free(dn);
        free(dv);
        return false;
    }
    txn->names[txn->n] = dn;
    txn->values[txn->n] = dv;
    txn->n++;
    return true;
}

static void word_txn_clear(word_assign_txn_t *txn) {
    for (size_t i = 0; i < txn->n; i++) {
        free(txn->names[i]);
        free(txn->values[i]);
    }
    free(txn->names);
    free(txn->values);
    txn->names = NULL;
    txn->values = NULL;
    txn->n = 0;
    txn->cap = 0;
}

/// Apply the buffered writes in assignment order, using the same
/// symtable_set_var call the legacy scalar path makes (same scope resolution,
/// same readonly enforcement), then clear the buffer.
static void word_txn_commit(executor_t *ex, word_assign_txn_t *txn) {
    for (size_t i = 0; i < txn->n; i++) {
        symtable_set_var(ex->symtable, txn->names[i], txn->values[i],
                         SYMVAR_NONE);
    }
    word_txn_clear(txn);
}

static char *executor_symtable_word_get(void *ctx, const char *name) {
    word_eval_ctx_t *wc = (word_eval_ctx_t *)ctx;
    executor_t *ex = wc ? wc->ex : NULL;
    if (!ex || !ex->symtable || !name) {
        return NULL;
    }
    /// An assign operator earlier in this word has already produced a value
    /// for `name`, even though the write is not committed yet -- see
    /// word_assign_txn_t. Legacy would see it, so this read must too.
    const char *pending = word_txn_lookup(wc->txn, name);
    if (pending) {
        return strdup(pending);
    }
    /// The scalar specials $?/$$/$#/$!/$- and the single-digit positionals
    /// $0..$9 are resolved by the legacy expander from live state
    /// (last_exit_status, the shell PID, the positional count / vector, the
    /// last background PID, the option flags), not necessarily the plain
    /// symtable read. Resolve them through that same expander so the covered
    /// value matches legacy by construction rather than re-deriving it here.
    if (name[0] != '\0' && name[1] == '\0' &&
        (name[0] == '?' || name[0] == '$' || name[0] == '#' || name[0] == '!' ||
         name[0] == '-' || (name[0] >= '0' && name[0] <= '9'))) {
        char ref[3] = {'$', name[0], '\0'};
        return expand_if_needed(ex, ref);
    }
    return symtable_get_var(ex->symtable, name);
}

/// True iff `$name` would expand in scalar context here: a scalar binding or an
/// unset name (whose bare reference is scalar-empty). A list/map returns false
/// so word_eval defers -- its bare `$name` reference is a vector the legacy
/// path produces via the multi-field expander, which this slice does not model.
///
/// symtable_lookup reads the global manager (and its live scope stack), the
/// SAME store the scalar `get` callback (ex->symtable) and the legacy expander
/// read: every production executor's symtable IS symtable_get_global_manager()
/// (executor_new), and function/subshell scopes push onto it rather than
/// swapping the executor's manager, so the kind seen here cannot diverge from
/// the value `get` sources. (The only non-global executor,
/// executor_new_with_symtable, is test-only and never builds an argv.)
static bool executor_symtable_word_is_scalar(void *ctx, const char *name) {
    (void)ctx;
    if (!name) {
        return true;
    }
    lush_value_view_t view;
    if (!symtable_lookup(name, &view)) {
        return true; /// unset -> scalar-empty
    }
    bool scalar =
        (view.kind == LUSH_VALUE_SCALAR || view.kind == LUSH_VALUE_NONE);
    lush_value_view_clear(&view);
    return scalar;
}

/// Apply a parameter-expansion operator via the SAME primitive the legacy
/// expander uses (apply_param_operator), so the covered `${var:-x}` family
/// matches by construction. The covered set is the alternation, pattern-strip,
/// case-conversion, substring, substitution and assign families.
/// `value` may be NULL (unset var, which the unset-only operators `-`/`+`
/// distinguish from empty); apply_param_operator reads but does not mutate
/// value/deflt -- it is PURE and reports a needed write through assign_back.
///
/// The assign operators `${var:=x}` / `${var=x}` are the only covered family
/// with a side effect, and this is where it is RECORDED: on assign_back the
/// value goes into the word's pending-write buffer, which word_txn_commit
/// later applies with the same symtable_set_var call the legacy scalar path
/// uses (same scope resolution, same readonly enforcement) -- but only once
/// the whole word is known covered. The legacy path's relaxed-mode bare-array
/// branch (assign to element 0) is unreachable here because word_eval defers
/// any non-scalar name.
static char *executor_word_apply_op(void *ctx, const char *name,
                                    const char *value, const char *deflt,
                                    int op) {
    word_eval_ctx_t *wc = (word_eval_ctx_t *)ctx;
    executor_t *ex = wc ? wc->ex : NULL;
    if (!ex) {
        return NULL;
    }
    /// BOUNDARY GUARD for the required-parameter operators. word_eval must not
    /// raise -- it is a value producer whose evaluation is PROVISIONAL, and a
    /// diagnostic emitted here would survive a later defer and be reported a
    /// second time by legacy's re-expansion. word_eval already pre-checks the
    /// trigger and defers, so this is defence in depth: if a firing shape ever
    /// reaches this callback, refuse it (NULL defers the word) instead of
    /// letting apply_param_operator report E1307 and request the POSIX exit
    /// from inside a "covered" evaluation. Both bench apply_op callbacks carry
    /// the same guard, so the production and bench boundaries behave alike.
    if (lush_param_op_required_fires(op, value)) {
        return NULL;
    }
    bool assign_back = false;
    char *result = apply_param_operator(ex, name, (char *)value, (char *)deflt,
                                        op, &assign_back);
    if (assign_back && result) {
        /// The pending-write buffer models a store that SUCCEEDS: a later read
        /// in the same word is served the buffered value. A readonly target
        /// breaks that model -- symtable_set_var refuses the write, so on the
        /// legacy path the same read still sees the OLD value (`readonly r;
        /// ${r:=v}${r}` is `v`, not `vv`, and a second `:=` on the name
        /// re-fires because the variable is still empty). Rather than teach
        /// the overlay to predict the refusal, defer the whole word and let
        /// the legacy expander produce the refusal semantics it owns.
        /// symtable_get_flags walks the scope chain while symtable_set_var
        /// only refuses in the CURRENT scope, so this over-defers for a
        /// readonly binding shadowed by a function frame -- deferring is
        /// always safe. The one asymmetry that could UNDER-defer is that
        /// find_var (behind symtable_get_flags) skips SYMVAR_UNSET tombstones
        /// while the store's refusal check does not, so a current-scope entry
        /// that was both UNSET and READONLY would slip through. No such entry
        /// is reachable today (unset refuses on a readonly, and readonly on an
        /// unset name rewrites the entry as set-empty); keep it that way, or
        /// probe the current scope directly.
        if (symtable_get_flags(ex->symtable, name) & SYMVAR_READONLY) {
            free(result);
            return NULL;
        }
        /// BUFFERED, not written: the word may still defer (see
        /// word_assign_txn_t). Failing to buffer -- only under OOM -- defers
        /// the word, and the legacy path then performs the assignment itself.
        if (!word_txn_push(wc->txn, name, result)) {
            free(result);
            return NULL;
        }
    }
    return result;
}

/// Audit-mode check (Step 2, gated on LUSH_WORD_CST_AUDIT): abort loudly if the
/// Word CST fields for a covered argument disagree with the legacy expansion.
/// Covered words are lush-mode-no-split, so they yield 0 fields (a
/// null-word-removed empty) or 1 field (which must equal the legacy scalar).
/// This pinpoints a live divergence at the exact expansion point, converting a
/// silent mismatch into an immediate, diagnosable abort.
///
/// Two checks together prove full post-glob parity for a covered word:
///  1. The pre-glob scalar compare below: the CST field(s) must equal the value
///     legacy's expand_arg_node yields (catches any dequote / variable / ANSI-C
///     divergence upstream).
///  2. The post-scalar gate: legacy would next brace/glob-expand that scalar
///     into a DIFFERENT field vector, which the scalar compare is blind to (a
///     covered `\*` matches the scalar `*` yet legacy globs it). word_eval must
///     defer any such word; if a covered word's scalar would still trigger
///     legacy expansion, that is a coverage bug. We MIRROR the legacy gate from
///     build_argv verbatim -- quoted-string nodes are exempt, brace/glob only
///     fire otherwise, and glob no-ops under set -f -- so the prediction cannot
///     disagree with what legacy actually does (no false positives).
/// Field splitting needs no check: legacy splits only NODE_COMMAND_SUB /
/// NODE_VAR words in split mode, both of which word_eval already defers.
/// The expansion-error state a word's expansion can leave on the executor.
/// word_cst_audit expands each covered word a SECOND time on the legacy path
/// purely to compare the two routes; that comparison must not be able to
/// change the run, so this is snapshotted and restored around the call.
/// error_message is a BORROWED pointer (set_executor_error stores the caller's
/// string; the structured path sets NULL), so restoring it needs no free.
typedef struct {
    bool expansion_error;
    int expansion_exit_status;
    bool shell_exit_requested;
    int shell_exit_status;
    bool has_error;
    const char *error_message;
} expansion_error_state_t;

static expansion_error_state_t
expansion_error_state_save(const executor_t *executor) {
    expansion_error_state_t s = {
        .expansion_error = executor->expansion_error,
        .expansion_exit_status = executor->expansion_exit_status,
        .shell_exit_requested = executor->shell_exit_requested,
        .shell_exit_status = executor->shell_exit_status,
        .has_error = executor->has_error,
        .error_message = executor->error_message,
    };
    return s;
}

static void expansion_error_state_restore(executor_t *executor,
                                          const expansion_error_state_t *s) {
    executor->expansion_error = s->expansion_error;
    executor->expansion_exit_status = s->expansion_exit_status;
    executor->shell_exit_requested = s->shell_exit_requested;
    executor->shell_exit_status = s->shell_exit_status;
    executor->has_error = s->has_error;
    executor->error_message = s->error_message;
}

/// True when the legacy expansion RAISED an error the word did not arrive
/// with. Transition-based (false -> true) rather than absolute: an expansion
/// earlier in the same command may have left a flag set, and this cannot tell
/// whether legacy raised it again -- under-reporting there is fine, since the
/// first raise already aborted.
static bool expansion_error_state_raised(const expansion_error_state_t *before,
                                         const executor_t *executor) {
    return (executor->expansion_error && !before->expansion_error) ||
           (executor->shell_exit_requested && !before->shell_exit_requested) ||
           (executor->has_error && !before->has_error);
}

static void word_cst_audit(executor_t *executor, node_t *child, char **fields,
                           int n, const expansion_error_state_t *pre_eval) {
    /// Did the CST evaluation itself raise? word_eval is a value producer, but
    /// its callbacks re-enter the legacy expander (the get callback resolves
    /// the specials and positionals through expand_if_needed, and apply_op's
    /// substring arm expands its spec), so an error CAN be raised during a
    /// "covered" evaluation. Snapshotting only at audit entry would miss it and
    /// score the word as clean; @p pre_eval is taken before word_eval so the
    /// two routes are compared symmetrically.
    bool cst_raised = expansion_error_state_raised(pre_eval, executor);

    /// Isolate the comparison expansion: snapshot the error state, mute the
    /// diagnostic channel, and restore both afterwards. Without this the audit
    /// is not observation-only -- legacy's expansion_error suppresses the
    /// command and turns rc 0 into rc 1 (issue #687). The restore target is the
    /// state as of audit ENTRY, not pre_eval: an error the CST route raised is
    /// real and must survive.
    expansion_error_state_t before = expansion_error_state_save(executor);
    free(g_error_capture);
    g_error_capture = NULL;
    g_error_display_muted = true;
    char *legacy = expand_arg_node(executor, child);
    g_error_display_muted = false;
    bool legacy_raised = expansion_error_state_raised(&before, executor);
    expansion_error_state_restore(executor, &before);
    char *captured = g_error_capture;
    g_error_capture = NULL;

    /// A covered word whose two routes disagree about RAISING AN ERROR is a
    /// divergence even when they agree on the string: legacy reports a
    /// diagnostic and fails the command while the CST route silently yields a
    /// value, or the reverse. word_eval has no error channel of its own -- it
    /// defers anything it cannot express -- so in practice this fires as
    /// "legacy raised, the CST did not", meaning the word should have
    /// deferred; the symmetric form catches an error raised through a
    /// re-entrant callback.
    if (legacy_raised != cst_raised) {
        fprintf(stderr,
                "WORD_CST AUDIT ERROR-STATE MISMATCH: word='%s' cst_n=%d "
                "cst[0]='%s' legacy='%s' -- %s raised an expansion error the "
                "other route did not%s.\n%s",
                child->val.str ? child->val.str : "", n, n > 0 ? fields[0] : "",
                legacy ? legacy : "(null)",
                legacy_raised ? "legacy" : "the CST route",
                legacy_raised ? "; the word should have deferred" : "",
                captured ? captured : "");
        abort();
    }
    free(captured);
    bool match;
    if (n == 0) {
        match = (!legacy || legacy[0] == '\0');
    } else if (n == 1) {
        match = (legacy && strcmp(fields[0], legacy) == 0);
    } else {
        match = false; /// >1 field is not a covered lush-mode shape yet
    }
    /// A scalar-matching covered word still diverges if legacy would then
    /// brace/glob-expand that scalar. Gate mirrors build_argv exactly.
    bool post_expand =
        match && n >= 1 && legacy && child->type != NODE_STRING_LITERAL &&
        child->type != NODE_STRING_EXPANDABLE &&
        (needs_brace_expansion(legacy) ||
         (!shell_opts.no_globbing && needs_glob_expansion(legacy)));
    if (!match || post_expand) {
        fprintf(stderr,
                "WORD_CST AUDIT MISMATCH: word='%s' cst_n=%d cst[0]='%s' "
                "legacy='%s'%s\n",
                child->val.str ? child->val.str : "", n, n > 0 ? fields[0] : "",
                legacy ? legacy : "(null)",
                post_expand ? " [post-glob/brace divergence]" : "");
        abort();
    }
    free(legacy);
}

/// Whether the Word CST dual-routing path is active. It is the DEFAULT: a
/// covered command-argument word expands on the CST backbone, with the exact
/// legacy path as fallback for anything not covered. Opt out (fall back to the
/// legacy expander for every word) by setting LUSH_WORD_CST to a falsey value
/// -- 0 / off / false / no (case-insensitive) -- for troubleshooting a
/// suspected CST divergence. Any other value, or unset, keeps the CST on.
static bool word_cst_enabled(void) {
    const char *v = getenv("LUSH_WORD_CST");
    if (!v || !*v) {
        return true;
    }
    return !(strcasecmp(v, "0") == 0 || strcasecmp(v, "off") == 0 ||
             strcasecmp(v, "false") == 0 || strcasecmp(v, "no") == 0);
}

static char **build_argv_from_ast(executor_t *executor, node_t *command,
                                  int *argc) {
    if (!executor || !command || !argc) {
        return NULL;
    }

    /// Dynamic argument list to handle glob expansion
    char **argv_list = NULL;
    int argv_count = 0;
    int argv_capacity = 0;

    /// Find here document delimiters to exclude
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

    /// Add command name (no glob expansion for command names). A positional
    /// vector in command position ($@ / "$@" / $* / "$*" / ${@} / ${*})
    /// expands like any other word vector: the first element becomes the
    /// command and the rest lead the arguments; an empty positional set
    /// contributes zero words (a null command, handled below via the
    /// argc==0 path). A named list/map (${arr[@]}, bare ${arr}) is
    /// mode-dependent: under strict value typing (lush mode)
    /// try_expand_vector_arg declines it under positional_only, so it falls
    /// to the scalar path and raises the SEMANTICS section 3.9 type error
    /// (positionals are argv, but a named list value in a scalar slot is a
    /// type mismatch); under a relaxed compat mode the vector is allowed
    /// through and spreads into command words, matching the oracle. For a
    /// plain scalar name, null-word removal still applies: an unquoted empty
    /// `$x` contributes zero words (a null command), while a quoted empty
    /// `"$x"` / `''` stays one empty word (command not found).
    if (command->val.str) {
        node_t cmd_word = {0};
        cmd_word.type =
            command->name_quoted ? NODE_STRING_EXPANDABLE : NODE_VAR;
        cmd_word.val.str = command->val.str;
        char **cvec = NULL;
        int cvcount = 0;
        if (try_expand_vector_arg(executor, &cmd_word, &cvec, &cvcount,
                                  /*positional_only=*/true)) {
            for (int j = 0; j < cvcount; j++) {
                if (!add_to_argv_list(&argv_list, &argv_count, &argv_capacity,
                                      cvec[j])) {
                    for (int k = j; k < cvcount; k++) {
                        free(cvec[k]);
                    }
                    free(cvec);
                    goto cleanup_and_fail;
                }
            }
            free(cvec);
        } else {
            char *expanded_cmd = expand_if_needed(executor, command->val.str);
            if (!argv_append_word(&argv_list, &argv_count, &argv_capacity,
                                  expanded_cmd, command->name_quoted)) {
                goto cleanup_and_fail;
            }
        }
    }

    /// Process arguments with glob expansion
    child = command->first_child;
    while (child) {
        /// Skip redirection nodes and cmd_prefix assignments (NODE_ASSIGN
        /// children are applied as the command's temporary environment,
        /// not passed as argv words).
        if (!is_redirection_node(child) && child->type != NODE_ASSIGN) {
            if (child->val.str) {
                /// Check if this is a here document delimiter. NFC-
                /// equivalent (see redirection.c / parser.c for the
                /// same swap on the body-read / parse-time delimiter
                /// scans; this argv-time check stays consistent so a
                /// heredoc whose script delimiter and stdin
                /// terminator differ only in Unicode normalization
                /// resolves uniformly across all three sites).
                bool is_delimiter = false;
                for (int i = 0; i < delimiter_count; i++) {
                    if (heredoc_delimiters[i] &&
                        lle_unicode_strings_equal(
                            child->val.str, heredoc_delimiters[i], NULL)) {
                        is_delimiter = true;
                        break;
                    }
                }

                if (!is_delimiter) {
                    /// Word CST dual-routing (default path): if this argument
                    /// carries a covered Word CST, evaluate it on the CST
                    /// backbone. word_eval composes the same field_split /
                    /// null-word primitives build_argv uses and resolves $name
                    /// from the same symtable, so a covered word cannot drift;
                    /// a not-ok result falls through to the exact legacy path
                    /// below. Opt out via LUSH_WORD_CST=0 (see
                    /// word_cst_enabled).
                    if (child->word && word_cst_enabled()) {
                        char *ifs_val =
                            symtable_get_var(executor->symtable, "IFS");
                        /// Assign-operator writes are buffered per word and
                        /// committed only if the word evaluates fully on the
                        /// CST route (word_assign_txn_t).
                        word_assign_txn_t wtxn = {0};
                        word_eval_ctx_t wctx = {.ex = executor, .txn = &wtxn};
                        word_eval_env_t wenv = {
                            .get = executor_symtable_word_get,
                            .is_scalar = executor_symtable_word_is_scalar,
                            .apply_op = executor_word_apply_op,
                            .ctx = &wctx,
                            .ifs = ifs_val,
                            .word_split_default =
                                shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT),
                            .zsh_extended_glob =
                                shell_mode_allows(FEATURE_ZSH_EXTENDED_GLOB),
                            .ansi_c_quoting =
                                shell_mode_allows(FEATURE_ANSI_QUOTING),
                            .nounset = shell_opts.unset_error,
                        };
                        int wn = 0;
                        bool wok = false;
                        /// Snapshot BEFORE the CST evaluation so the audit can
                        /// tell an error raised by the CST route (through a
                        /// re-entrant callback) from one raised by its own
                        /// legacy comparison run.
                        expansion_error_state_t pre_eval =
                            expansion_error_state_save(executor);
                        char **wfields =
                            word_eval(child->word, &wenv, &wn, &wok);
                        free(ifs_val);
                        if (wok) {
                            /// Audit BEFORE committing: the legacy
                            /// re-evaluation must see the same state the CST
                            /// evaluation started from, or a word that reads a
                            /// variable it also assigns (`"$u${u:=x}"`) would
                            /// report a false mismatch.
                            if (getenv("LUSH_WORD_CST_AUDIT")) {
                                word_cst_audit(executor, child, wfields, wn,
                                               &pre_eval);
                            }
                            word_txn_commit(executor, &wtxn);
                            bool append_ok = true;
                            for (int i = 0; i < wn; i++) {
                                if (!add_to_argv_list(&argv_list, &argv_count,
                                                      &argv_capacity,
                                                      wfields[i])) {
                                    for (int k = i; k < wn; k++) {
                                        free(wfields[k]);
                                    }
                                    append_ok = false;
                                    break;
                                }
                            }
                            free(wfields);
                            if (!append_ok) {
                                goto cleanup_and_fail;
                            }
                            child = child->next_sibling;
                            continue;
                        }
                        /// Deferred: drop the pending writes so the legacy
                        /// expander below re-expands the whole word from
                        /// unmutated state and performs the assignment itself.
                        word_txn_clear(&wtxn);
                        free(wfields); /// wok==false: NULL; fall through
                    }

                    /// Vector-yielding expansion: `"$@"`, `"${arr[@]}"`,
                    /// `"${!arr[@]}"` and their slice variants must
                    /// produce N separate argv slots, not one
                    /// concatenated slot. Detect before scalar
                    /// expansion so the original element boundaries
                    /// survive (issue #97).
                    char **vec = NULL;
                    int vcount = 0;
                    if (try_expand_vector_arg(executor, child, &vec, &vcount,
                                              /*positional_only=*/false)) {
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

                    char *expanded_arg = NULL;

                    /// An unquoted assignment-style argument word `name=~/path`
                    /// gets its RHS tilde-expanded like a real assignment
                    /// value. This happens for the assignment-aware builtins
                    /// (export/local/declare/typeset/readonly) always -- POSIX
                    /// assignment semantics -- and for every command under
                    /// zsh's magic_equal_subst. Restricted to bareword
                    /// (unquoted) words, so `export E="~/a"` and non-assignment
                    /// args are untouched. The tilde runs on the RAW value
                    /// BEFORE variable and command expansion, exactly like
                    /// execute_assignment, so a tilde that later emerges from
                    /// $var/$(...) is not itself expanded (`export E=~/a:$u`
                    /// with u=~/b keeps ~/b literal).
                    /// A quoted assignment-shaped word carries a provenance
                    /// value (double-quoted tildes escaped as \~) so only its
                    /// unquoted tilde segments expand; a fully-unquoted word
                    /// tilde-expands its own value. A plain quoted word (no
                    /// provenance, no assignment shape) is left to the normal
                    /// quoted-string path. #488.
                    const char *tilde_word = child->magic_equal_value
                                                 ? child->magic_equal_value
                                                 : child->val.str;
                    bool tilde_word_eligible =
                        child->magic_equal_value != NULL ||
                        (child->type != NODE_STRING_LITERAL &&
                         child->type != NODE_STRING_EXPANDABLE);
                    if (tilde_word && tilde_word_eligible &&
                        (shell_mode_allows(FEATURE_MAGIC_EQUAL_SUBST) ||
                         is_assignment_builtin(command->val.str))) {
                        char *pre_tilde = magic_equal_tilde_expand(tilde_word);
                        if (pre_tilde) {
                            expanded_arg =
                                expand_if_needed(executor, pre_tilde);
                            free(pre_tilde);
                        }
                    }

                    /// Type-aware expansion via the shared helper for every
                    /// other word (and for assignment words that were not a
                    /// name=value shape). Process substitution is the only path
                    /// that propagates failure as NULL -- everything else
                    /// either succeeds or returns "".
                    if (!expanded_arg) {
                        expanded_arg = expand_arg_node(executor, child);
                        if (!expanded_arg &&
                            (child->type == NODE_PROC_SUB_IN ||
                             child->type == NODE_PROC_SUB_OUT)) {
                            goto cleanup_and_fail;
                        }
                    }

                    if (getenv("NEW_PARSER_DEBUG")) {
                        fprintf(stderr,
                                "DEBUG: Processing argument: '%s' -> '%s'\n",
                                child->val.str, expanded_arg);
                    }

                    /// A fused glob qualifier (`"$f"(N)`, `"$f"(Nm-1)`)
                    /// applies to the expanded value even though it came from
                    /// a quoted string: the qualifier filters the literal
                    /// value, it does not re-glob it (SEMANTICS 3.6). Gated to
                    /// the modes that enable glob qualifiers; elsewhere the
                    /// word falls through and stays a literal `value(N)`,
                    /// matching the unquoted `*(.)`-stays-literal behavior.
                    if (child->glob_qualified && expanded_arg &&
                        shell_mode_allows(FEATURE_GLOB_QUALIFIERS)) {
                        int glob_count = 0;
                        char **glob_results = apply_glob_qualifier_to_literal(
                            expanded_arg, &glob_count);
                        if (glob_results) {
                            for (int j = 0; j < glob_count; j++) {
                                if (!add_to_argv_list(&argv_list, &argv_count,
                                                      &argv_capacity,
                                                      glob_results[j])) {
                                    for (int k = j; k < glob_count; k++) {
                                        free(glob_results[k]);
                                    }
                                    free(glob_results);
                                    free(expanded_arg);
                                    goto cleanup_and_fail;
                                }
                            }
                            free(glob_results);
                            free(expanded_arg);
                            child = child->next_sibling;
                            continue;
                        }
                        /// glob failed: fall through to add the value literally
                    }

                    /// Check if argument needs brace expansion first
                    /// Skip brace/glob expansion for quoted strings
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
                            /// Process each brace expansion result for
                            /// potential glob expansion
                            for (int j = 0; j < brace_count; j++) {
                                if (needs_glob_expansion(brace_results[j])) {
                                    int glob_count;
                                    char **glob_results = expand_glob_pattern(
                                        brace_results[j], &glob_count);

                                    if (glob_results) {
                                        /// Add all glob results, free brace
                                        /// result since we won't use it
                                        free(brace_results[j]);
                                        for (int k = 0; k < glob_count; k++) {
                                            if (!add_to_argv_list(
                                                    &argv_list, &argv_count,
                                                    &argv_capacity,
                                                    glob_results[k])) {
                                                /// Cleanup remaining strings on
                                                /// failure
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
                                        /// Glob expansion failed, use brace
                                        /// result
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
                                    /// No glob expansion needed, use brace
                                    /// result directly
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
                            /// Brace expansion failed, fall back to normal
                            /// expansion
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
                                        NULL; /// Ownership transferred
                                }
                            } else {
                                if (!add_to_argv_list(&argv_list, &argv_count,
                                                      &argv_capacity,
                                                      expanded_arg)) {
                                    free(expanded_arg);
                                    goto cleanup_and_fail;
                                }
                                expanded_arg = NULL; /// Ownership transferred
                            }
                        }
                        if (expanded_arg) {
                            free(expanded_arg);
                        }
                    } else if (child->type != NODE_STRING_LITERAL &&
                               child->type != NODE_STRING_EXPANDABLE &&
                               needs_glob_expansion(expanded_arg)) {
                        /// No brace expansion, check for glob expansion
                        /// Skip glob expansion for quoted strings
                        int glob_count;
                        char **glob_results =
                            expand_glob_pattern(expanded_arg, &glob_count);

                        if (glob_results) {
                            /// Add all glob results
                            for (int j = 0; j < glob_count; j++) {
                                if (!add_to_argv_list(&argv_list, &argv_count,
                                                      &argv_capacity,
                                                      glob_results[j])) {
                                    /// Cleanup on failure
                                    for (int k = j; k < glob_count; k++) {
                                        free(glob_results[k]);
                                    }
                                    free(glob_results);
                                    free(expanded_arg);
                                    goto cleanup_and_fail;
                                }
                            }
                            free(glob_results); /// Free the array but not the
                                                /// strings
                        } else {
                            /// Glob expansion failed, use original
                            if (!add_to_argv_list(&argv_list, &argv_count,
                                                  &argv_capacity,
                                                  expanded_arg)) {
                                free(expanded_arg);
                                goto cleanup_and_fail;
                            }
                        }
                        free(expanded_arg); /// We copied the strings or used
                                            /// them
                    } else {
                        /// Check if this needs field splitting. An unquoted
                        /// command substitution $(cmd)/`cmd` splits when
                        /// FEATURE_CMDSUB_WORD_SPLIT is on; an unquoted
                        /// parameter expansion $var splits when
                        /// FEATURE_WORD_SPLIT_DEFAULT is on. The two flags
                        /// diverge by mode (zsh splits command subs but not
                        /// bare $var), and in lush mode both are off, so
                        /// neither implicitly splits: SEMANTICS section 4.1
                        /// promises no implicit IFS-driven splitting of command
                        /// output, and leaving `$(cmd)` unsplit gives the same
                        /// mental model as `$var` (`set -- $(echo a b)` and
                        /// `set -- $x` both yield one word). The compat modes
                        /// restore the traditional split. Explicit splitting
                        /// stays available via an array literal `arr=( $(cmd)
                        /// )`. Quoted strings never split.
                        bool should_word_split =
                            (child->type == NODE_COMMAND_SUB &&
                             shell_mode_allows(FEATURE_CMDSUB_WORD_SPLIT)) ||
                            (child->type == NODE_VAR &&
                             shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT));

                        if (should_word_split) {

                            /// Get IFS for field splitting. symtable_get
                            /// returns an owned copy; free it once the scan and
                            /// split below have consumed it.
                            char *ifs_owned =
                                symtable_get(executor->symtable, "IFS");
                            const char *ifs = ifs_owned ? ifs_owned : " \t\n";

                            /// Check if expanded_arg contains any IFS
                            /// characters
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
                                free(ifs_owned);

                                if (fields && field_count > 0) {
                                    /// Add each field as separate argument
                                    for (int i = 0; i < field_count; i++) {
                                        if (!add_to_argv_list(
                                                &argv_list, &argv_count,
                                                &argv_capacity, fields[i])) {
                                            /// Cleanup remaining fields on
                                            /// failure
                                            for (int j = i; j < field_count;
                                                 j++) {
                                                free(fields[j]);
                                            }
                                            free(fields);
                                            free(expanded_arg);
                                            goto cleanup_and_fail;
                                        }
                                        /// Ownership transferred, don't free
                                        /// fields[i]
                                    }
                                    free(fields);
                                    free(expanded_arg);
                                } else {
                                    /// Field splitting failed, use original
                                    if (!argv_append_word(
                                            &argv_list, &argv_count,
                                            &argv_capacity, expanded_arg,
                                            word_node_is_quoted(child))) {
                                        goto cleanup_and_fail;
                                    }
                                }
                            } else {
                                /// No field splitting needed
                                free(ifs_owned);
                                if (!argv_append_word(
                                        &argv_list, &argv_count, &argv_capacity,
                                        expanded_arg,
                                        word_node_is_quoted(child))) {
                                    goto cleanup_and_fail;
                                }
                            }
                        } else {
                            /// No field splitting for non-variables
                            if (!argv_append_word(&argv_list, &argv_count,
                                                  &argv_capacity, expanded_arg,
                                                  word_node_is_quoted(child))) {
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
        /// Legitimate null command: every word was removed by null-word
        /// removal (an unquoted empty expansion contributes zero words).
        /// Return a valid empty argv (argv[0] == NULL, *argc == 0) so the
        /// caller can distinguish this from the failure path, which returns
        /// NULL.
        free(argv_list);
        char **empty_argv = malloc(sizeof(char *));
        if (!empty_argv) {
            goto cleanup_delimiters;
        }
        empty_argv[0] = NULL;
        *argc = 0;
        for (int k = 0; k < delimiter_count; k++) {
            free(heredoc_delimiters[k]);
        }
        return empty_argv;
    }

    /// Convert to final argv array
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

    /// Clean up here document delimiters
    for (int k = 0; k < delimiter_count; k++) {
        free(heredoc_delimiters[k]);
    }

    return argv;

cleanup_and_fail:
    /// Free all allocated arguments
    for (int i = 0; i < argv_count; i++) {
        free(argv_list[i]);
    }
    free(argv_list);

cleanup_delimiters:
    /// Clean up here document delimiters
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
        /// Undeclared name: lush treats an absent name as empty in every
        /// expansion context -- `$q`, `"${q[@]}"`, and a bare `${q[@]}` in an
        /// array literal all contribute nothing -- so a kind sigil on an
        /// undeclared name likewise contributes nothing. The sigil presents a
        /// collection, and an absent name is an empty collection, exactly as a
        /// declared-but-empty list gives `%l` / `@l` -> nothing. It is a
        /// value-model decision, not a spelling one: this is `@q`/`%q`, the
        /// vector/pair operators, applied to no collection.
        ///
        /// A *scalar* under `%` is the different, real case handled below: a
        /// scalar is a value that exists but has no pair component, which is a
        /// type error (E1134), not an absence. Absence is empty; a wrong-shaped
        /// value is the error.
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
    } else { /// sigil == '%'
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
            /// worst case: "INT v INT v ..." -- 32 bytes per entry is plenty
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

/// Return true when `text` is a single command substitution -- $(...) or
/// `...` -- that spans the entire string, i.e. nothing follows the closing
/// delimiter. When text continues after it (an assignment value like
/// x=$(cmd):b or x=`cmd`.txt), the word is a concatenation and must go
/// through the general expander: expand_command_substitution treats the whole
/// word as the command when it does not end in the closer, so the substitution
/// output plus the trailing literal collapse to empty. The scan is quote- and
/// nesting-aware so a ) or ` inside the substitution's own quotes does not end
/// it prematurely.
static bool cmdsub_spans_whole_word(const char *text) {
    size_t tlen = strlen(text);
    if (strncmp(text, "$(", 2) == 0) {
        /// Match the '(' at text[1] to its ')'. lush_find_matching_brace is
        /// nesting-aware and understands the four shell quote dialects (so a
        /// ) inside '...'/"..."/`...`/$'...' does not end it early), and it
        /// also spans the outer '))' of an arithmetic $((...)). The
        /// substitution covers the whole word iff its ')' is the last byte.
        size_t off;
        if (!lush_find_matching_brace(text + 1, tlen - 1, &off)) {
            return false;
        }
        return (1 + off + 1) == tlen;
    }
    if (text[0] == '`') {
        size_t i = 1;
        while (i < tlen && text[i] != '`') {
            if (text[i] == '\\' && i + 1 < tlen) {
                i++;
            }
            i++;
        }
        /// i is at the closing backtick; the sub spans the word iff it is the
        /// last character.
        return i < tlen && i + 1 == tlen;
    }
    return true;
}

char *expand_if_needed(executor_t *executor, const char *text) {
    if (!executor || !text) {
        return NULL;
    }

    /// zsh `$+NAME` / `$+NAME[SUBSCRIPT]` unbraced is-set test. Rewrite
    /// to the braced ${+NAME...} form so the existing parameter-expansion
    /// handler in parse_parameter_expansion picks it up. The whole
    /// span is one token coming out of the tokenizer (see the $+IDENT
    /// handler in src/tokenizer.c), so the rewrite is just text-shape.
    if (text[0] == '$' && text[1] == '+' &&
        lush_ident_match_start(text + 2, strlen(text + 2)) > 0) {
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

    /// Kind sigil top-level dispatch: `@NAME` / `%NAME` with NAME a valid
    /// identifier.  Tokenizer has already done the regex check; we just need
    /// to verify the shape before routing to expand_kind_sigil.
    if ((text[0] == '@' || text[0] == '%') &&
        shell_mode_allows(FEATURE_KIND_SIGILS) &&
        lush_ident_match_start(text + 1, strlen(text + 1)) > 0) {
        const char *p = text + 1;
        size_t rem = strlen(p);
        while (rem > 0) {
            size_t n = lush_ident_match_continue(p, rem);
            if (n == 0) {
                break;
            }
            p += n;
            rem -= n;
        }
        if (*p == '\0') {
            return expand_kind_sigil(executor, text);
        }
    }

    /// Handle strings that contain single quotes - process them specially
    /// Single-quoted content should not be expanded (POSIX requirement)
    /// BUT: Don't enter this path for command substitution $(...) or `...`
    /// which may contain quotes internally
    /// Enter the single-quote-handling block only when the text has
    /// at least one MATCHED pair of UNESCAPED single quotes. The
    /// parser pre-escapes `'` chars inside TOK_EXPANDABLE_STRING
    /// content (issue #102) so they appear here as `\'` and must
    /// not count toward the pair check -- those are literal
    /// characters that the POSIX-unquoted backslash rule resolves
    /// downstream.
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
                /// ANSI-C quoting $'...' - expand escape sequences
                i += 2; /// Skip $'
                size_t content_start = i;
                /// Find closing quote (handling escaped quotes)
                while (i < len) {
                    if (text[i] == '\\' && i + 1 < len) {
                        i += 2; /// Skip escaped character
                    } else if (text[i] == '\'') {
                        break;
                    } else {
                        i++;
                    }
                }
                /// Extract and expand the ANSI-C string content
                size_t content_len = i - content_start;
                if (shell_mode_allows(FEATURE_ANSI_QUOTING)) {
                    char *expanded = lush_expand_escapes(
                        &text[content_start], content_len, LUSH_ESC_ANSI_C);
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
                    /// Feature disabled - copy literally (including $')
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
                /// i now points to closing quote (or end of string)
            } else if (text[i] == '\'') {
                /// Regular single quote - copy content literally until closing
                /// quote
                i++; /// Skip opening quote
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
                /// i now points to closing quote (or end of string)
            } else if (text[i] == '"') {
                /// Double quote - expand content until closing quote
                i++; /// Skip opening quote
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
                /// Extract double-quoted content and expand it
                size_t dq_len = i - dq_start;
                char *dq_content = malloc(dq_len + 1);
                if (dq_content) {
                    strncpy(dq_content, &text[dq_start], dq_len);
                    dq_content[dq_len] = '\0';
                    /// Content inside `"..."` -- DQ rules apply.
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
                /// i now points to closing quote
            } else if (text[i] == '$') {
                /// Outside quotes - expand variable
                size_t var_start = i;
                /// Find end of variable reference
                if (i + 1 < len && text[i + 1] == '{') {
                    /// ${var} format - find closing brace
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
                    /// $(cmd) or $((arith)) - find the matching ')' with the
                    /// canonical structure-aware matcher (quote-aware, and a
                    /// case-pattern ')' is not a group close -- #486/#494). The
                    /// opener is the '(' at var_start + 1; for $((arith)) it
                    /// matches the outer paren pair.
                    size_t cs_off = 0;
                    bool cs_matched = lush_find_matching_brace(
                        &text[var_start + 1], len - (var_start + 1), &cs_off);
                    i = cs_matched ? var_start + 1 + cs_off : len - 1;
                } else {
                    /// $var format
                    i++;
                    while (i < len) {
                        size_t n = lush_ident_match_continue(text + i, len - i);
                        if (n == 0) {
                            break;
                        }
                        i += n;
                    }
                    i--; /// Back up to last char of variable
                }
                /// Extract and expand the variable reference
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
                /// Regular character outside quotes
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

    /// Check for tilde expansion first
    if (text[0] == '~') {
        char *tilde_expanded = expand_tilde(text);
        if (tilde_expanded && strcmp(tilde_expanded, text) != 0) {
            /// Tilde was expanded, now check if result needs variable expansion
            const char *first_dollar = strchr(tilde_expanded, '$');
            if (first_dollar) {
                /// Unquoted text post-tilde-expansion: POSIX-unquoted
                /// escape rules apply to any surviving backslashes.
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

    /// Check if this looks like it contains variables (has $)
    /// This is a heuristic for expandable strings
    const char *first_dollar = strchr(text, '$');
    if (first_dollar) {
        /// Count dollar signs to determine if we have multiple variables
        int dollar_count = 0;
        for (const char *p = text; *p; p++) {
            if (*p == '$') {
                dollar_count++;
            }
        }

        /// If we have multiple dollar signs or the first dollar is not at
        /// position 0, treat as quoted string with multiple expansions.
        /// This branch is reached from unquoted contexts (NODE_VAR with
        /// embedded $ etc.); pass in_double_quotes=false so any surviving
        /// `\X` follows POSIX-unquoted rules.
        if (dollar_count > 1 || first_dollar != text) {
            return expand_quoted_string(executor, text, false);
        }

        /// Single expansion starting at position 0
        if (strncmp(text, "$'", 2) == 0) {
            /// ANSI-C quoting $'...'
            /// Find closing quote (handling escaped quotes)
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
                /// Check if feature is allowed
                if (!shell_mode_allows(FEATURE_ANSI_QUOTING)) {
                    /// Feature disabled, return literal
                    return strdup(text);
                }
                char *expanded = lush_expand_escapes(text + 2, quote_end - 2,
                                                     LUSH_ESC_ANSI_C);
                /// If there's text after the closing quote, append it
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
            /// $((expr))<text> (e.g. x=$((1+2)):b) is a concatenation, not a
            /// bare arithmetic/substitution -- route it through the general
            /// expander so the trailing literal survives, the same as the
            /// $(...) and `...` branches below.
            if (!cmdsub_spans_whole_word(text)) {
                return expand_quoted_string(executor, text, false);
            }
            /// Disambiguate `$((` between arithmetic and command-sub of an
            /// anonymous function `$(() {...})` (issue #99). Shared helper
            /// with the tokenizer; `${...}` parameter expansions inside the
            /// arithmetic are handled there (issue #118).
            size_t tlen = strlen(text);
            if (lush_dollar_paren_is_arithmetic(text + 3, tlen - 3)) {
                return expand_arithmetic(executor, text);
            }
            return expand_command_substitution(executor, text);
        } else if (strncmp(text, "$(", 2) == 0) {
            /// A bare $(...) is a pure substitution; $(...)<text> (e.g.
            /// x=$(cmd):b) is a concatenation whose trailing literal
            /// expand_command_substitution would swallow. Route the latter
            /// through the general expander, mirroring the ${...} branch.
            if (!cmdsub_spans_whole_word(text)) {
                return expand_quoted_string(executor, text, false);
            }
            return expand_command_substitution(executor, text);
        } else if (strncmp(text, "${", 2) == 0) {
            /// ${var} format - check if there's more text after }
            const char *close_brace = strchr(text, '}');
            if (close_brace && close_brace[1] != '\0') {
                /// Text continues after ${var}; unquoted context.
                return expand_quoted_string(executor, text, false);
            }
            return expand_variable(executor, text);
        } else {
            /// $var format - check if there's more text after variable name
            const char *p = text + 1; /// Skip $
            /// Find end of variable name. Uses the lush identifier
            /// predicate so a non-ASCII name ($café, $Σ) extends to
            /// its full Unicode extent when FEATURE_UNICODE_IDENTIFIERS
            /// is on; ASCII names hit the fast path.
            if (*p == '?' || *p == '$' || *p == '#' || *p == '*' || *p == '@' ||
                *p == '!' || *p == '-' || (*p >= '0' && *p <= '9')) {
                p++; /// Single character special variable
            } else {
                size_t remaining = strlen(p);
                while (*p) {
                    size_t n = lush_ident_match_continue(p, remaining);
                    if (n == 0) {
                        break;
                    }
                    p += n;
                    remaining -= n;
                }
            }
            /// Trailing text after the variable; unquoted context.
            if (*p != '\0') {
                return expand_quoted_string(executor, text, false);
            }
            return expand_variable(executor, text);
        }
    }

    /// Check for backtick command substitution
    if (text[0] == '`') {
        /// `cmd`<text> (e.g. x=`cmd`:b) is a concatenation, not a bare
        /// substitution; route it through the general expander so the
        /// trailing literal survives.
        if (!cmdsub_spans_whole_word(text)) {
            return expand_quoted_string(executor, text, false);
        }
        return expand_command_substitution(executor, text);
    }

    /// Regular text reaching this path has no quote machinery and no
    /// expansion markers ($, ~, `, ') -- only possibly backslash
    /// escapes. Per POSIX, `\X` outside any quote produces a literal X
    /// (with `\<newline>` removed entirely as line continuation). The
    /// older code was returning strdup(text) here, which left the
    /// backslashes in the argument and produced bug #90 (a typed
    /// `rm a\ test\ file.txt` shipped the literal backslash through
    /// to rm). The fix walks the text removing the escape backslashes;
    /// the post-walk string is the dequoted form the executor will
    /// eventually pass through field splitting and into argv.
    {
        size_t len = strlen(text);
        char *result = malloc(len + 1);
        if (!result)
            return strdup(text); /// OOM fallback
        size_t out = 0;
        for (size_t i = 0; i < len; i++) {
            if (text[i] == '\\' && i + 1 < len) {
                char next = text[i + 1];
                if (next == '\n') {
                    /// Line continuation: drop both bytes.
                    i++;
                    continue;
                }
                /// Strip the backslash; emit the escaped character.
                /// Multi-byte UTF-8 escapees survive because their
                /// continuation bytes are >= 0x80 and are not '\\'
                /// themselves; the next loop iteration sees them as
                /// regular bytes and copies them through.
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

    /// Execute the child (pipeline)
    node_t *child = negate_node->first_child;
    if (!child) {
        return 1;
    }

    int result = execute_node(executor, child);

    /// Invert the exit status: 0 -> 1, non-zero -> 0
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

    /// Push error context for structured error reporting
    executor_push_context(executor, group->loc, "in brace group");

    /// Check for trailing redirections on the brace group
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
        /// Skip redirection nodes - they've already been processed
        if (is_redirection_node(command)) {
            command = command->next_sibling;
            continue;
        }

        /// Bash-style DEBUG pseudo-signal: fires BEFORE each command in a
        /// brace group. fire_debug_trap gates on functrace + scope.
        fire_debug_trap();

        last_result = execute_node(executor, command);

        if (executor->debug) {
            printf("DEBUG: Brace group command result: %d\n", last_result);
        }

        /// Bash-style ERR pseudo-signal: fires on a non-zero exit
        /// inside a brace group. fire_err_trap itself gates on
        /// errtrace + function scope so the trap is suppressed inside
        /// functions by default and surfaces only when the user opts
        /// in.
        if (last_result != 0 && last_result < 200) {
            fire_err_trap();
        }

        /// Check for function return (special code 200-455) - propagate it
        if (last_result >= 200 && last_result <= 455) {
            if (has_redirections) {
                restore_file_descriptors(&redir_state);
            }
            executor_pop_context(executor);
            return last_result;
        }

        /// Check for loop control (break/continue)
        if (executor->loop_control != LOOP_NORMAL) {
            break;
        }

        /// POSIX-required shell abort: drop out of the brace group so
        /// the request propagates to the surrounding command list.
        if (executor->shell_exit_requested) {
            break;
        }

        command = command->next_sibling;
    }

    /// Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    /// Pop error context
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

    /// Push error context for structured error reporting
    executor_push_context(executor, subshell->loc, "in subshell");

    /// Fork a new process for the subshell
    pid_t pid = lush_fork();
    if (pid == -1) {
        executor_error_add(executor, SHELL_ERR_FORK_FAILED, subshell->loc,
                           "failed to fork for subshell: %s", strerror(errno));
        executor_pop_context(executor);
        return 1;
    }

    if (pid == 0) {
        /// Child process - execute commands in subshell environment.
        /// Reset the inherited interactive SIGHUP/SIGSEGV handlers so a
        /// hangup or fault terminates the subshell normally.
        reset_subshell_signals();

        /// Set up any redirections attached to the subshell
        if (count_redirections(subshell) > 0) {
            int redir_result = setup_redirections(executor, subshell);
            if (redir_result != 0) {
                lush_process_terminate(redir_result);
            }
        }

        int last_result = 0;
        node_t *command = subshell->first_child;

        while (command) {
            /// Skip redirection nodes - they've already been applied
            if (is_redirection_node(command)) {
                command = command->next_sibling;
                continue;
            }
            last_result = execute_node(executor, command);

            /// Update $? between subshell commands so subsequent
            /// argv expansions see the correct exit status. NODE_PIPE
            /// and NODE_BUILTIN paths set last_exit_status via
            /// set_exit_status() inside execute_command, but the
            /// pipeline executor itself returns directly without
            /// updating it. The outer execute_command_list does this
            /// after each command; the subshell loop was missing the
            /// same update. Issue #100.
            set_exit_status(last_result);

            /// Honor set -e inside the subshell: if a command fails
            /// (non-zero exit) and the option is on, abort the rest
            /// of the subshell body. The existing exit_on_error check
            /// lives in execute_command_list / execute_command_chain
            /// which the subshell loop bypasses. The standard
            /// exceptions for if-conditions and ||/&& chains are
            /// already handled by their respective execute_* paths
            /// not propagating last_result to here. Issue #100.
            if (shell_opts.exit_on_error && last_result != 0) {
                break;
            }

            /// POSIX-required shell abort -- same flag the outer
            /// walker honors; if the subshell hit a ${var:?} or
            /// similar, terminate.
            if (executor->shell_exit_requested) {
                last_result = executor->shell_exit_status;
                break;
            }

            command = command->next_sibling;
        }

        /// Fire any EXIT trap registered in this subshell before
        /// terminating the child process. bash, zsh, and dash all
        /// fire the trap (inherited from the parent and / or
        /// installed locally inside the `( ... )`) when the subshell
        /// exits; lush was the outlier, calling bare exit() and
        /// silently dropping the trap. POSIX 2.11 also requires this.
        execute_exit_traps();

        /// Terminate the child through the shared path: _exit (never exit) so
        /// stdio cleanup cannot fclose and lseek the shared script input,
        /// leaving the parent to re-execute the script tail (Issue #441 /
        /// #444).
        lush_process_terminate(last_result);
    } else {
        /// Parent process - wait for subshell to complete
        int status = 0;
        /// Wait for the subshell, retrying past incidental EINTR; a hangup
        /// terminates the shell.
        executor_wait_foreground(pid, &status);

        int result;
        if (WIFEXITED(status)) {
            result = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            /// Child was killed by signal - return 128 + signal number (bash
            /// convention)
            result = 128 + WTERMSIG(status);
        } else {
            result = 1; /// Abnormal termination
        }

        /// Pop error context
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
    GLOB_QUAL_NONE = 0,      /// No qualifier
    GLOB_QUAL_FILE = 1,      /// (.) - regular files only
    GLOB_QUAL_DIR = 2,       /// (/) - directories only
    GLOB_QUAL_LINK = 4,      /// (@) - symbolic links only
    GLOB_QUAL_EXEC = 8,      /// (*) - executable files
    GLOB_QUAL_READABLE = 16, /// (r) - readable files
    GLOB_QUAL_WRITABLE = 32, /// (w) - writable files
    /// Behavior modifiers (not type/permission filters):
    GLOB_QUAL_NULLGLOB = 64, /// (N) - no match -> empty, never literal
    GLOB_QUAL_DOTGLOB = 128, /// (D) - include dot (hidden) files
} glob_qualifier_t;

/* Bits that filter by file type or permission (vs. behavior modifiers
 * like N/D). matches_glob_qualifier only needs to act when one of
 * these is set. */
#define GLOB_QUAL_FILTER_MASK                                                  \
    (GLOB_QUAL_FILE | GLOB_QUAL_DIR | GLOB_QUAL_LINK | GLOB_QUAL_EXEC |        \
     GLOB_QUAL_READABLE | GLOB_QUAL_WRITABLE)

/**
 * @brief A parsed glob qualifier group `(...)`.
 *
 * The type/permission/behavior letters combine as a bitmask in `flags`.
 * The modification-time qualifier `m[Mwhms][+-]n` is parametric (unit,
 * comparison, count) rather than a flag, so it rides alongside the mask.
 */
typedef struct {
    glob_qualifier_t flags; ///< OR of the GLOB_QUAL_* letter bits
    bool has_mtime;         ///< an `m` modification-time qualifier is present
    char mtime_unit;        ///< 's','m','h','d','w','M'; default 'd' (days)
    char mtime_cmp;         ///< '-' younger-than, '+' older-than, 0 == exact
    long mtime_count;       ///< n in the `m...n` spec
} glob_qual_spec_t;

/// True when the group carries any qualifier at all (letters or an mtime
/// spec). A bare `(N)`/`(D)` counts: those steer nullglob/dotglob behavior.
static inline bool spec_has_qualifier(const glob_qual_spec_t *s) {
    return s->flags != GLOB_QUAL_NONE || s->has_mtime;
}

/// True when the group requires per-file inspection: a type/permission
/// letter or an mtime spec. N and D alone need no lstat/stat filtering.
static inline bool spec_needs_filter(const glob_qual_spec_t *s) {
    return (s->flags & GLOB_QUAL_FILTER_MASK) || s->has_mtime;
}

/// Seconds in one unit of the zsh time-qualifier alphabet.
static long glob_mtime_unit_seconds(char unit) {
    switch (unit) {
    case 's':
        return 1L;
    case 'm':
        return 60L;
    case 'h':
        return 3600L;
    case 'w':
        return 7L * 86400L;
    case 'M':
        return 30L * 86400L; /// zsh defines a month as 30 days here
    case 'd':
    default:
        return 86400L;
    }
}

/**
 * @brief Parse and strip a trailing glob qualifier group from a pattern.
 *
 * @param pattern      Input pattern (not modified)
 * @param base_pattern Output: pattern without the qualifier (caller frees)
 * @return Parsed qualifier spec; flags == GLOB_QUAL_NONE and has_mtime ==
 *         false when the pattern carries no valid qualifier group.
 */
static glob_qual_spec_t parse_glob_qualifier(const char *pattern,
                                             char **base_pattern) {
    glob_qual_spec_t spec = {0};
    spec.flags = GLOB_QUAL_NONE;

    if (!pattern || !base_pattern) {
        if (base_pattern) {
            *base_pattern = pattern ? strdup(pattern) : NULL;
        }
        return spec;
    }

    size_t len = strlen(pattern);

    /// Check for qualifier pattern: ends with (X) or (X,Y,...) where X,Y are
    /// qualifier chars. A time qualifier (`m[Mwhms][+-]n`) plus combined
    /// letters can run a couple dozen characters, so the backward scan for
    /// the opening paren covers the tail generously while still refusing to
    /// reach a `(` from far earlier in the path.
    if (len >= 3 && pattern[len - 1] == ')') {
        const char *open_paren = NULL;
        size_t min_idx = (len > 24) ? (len - 24) : 1;
        for (size_t i = len - 2; i >= min_idx; i--) {
            if (pattern[i] == '(') {
                open_paren = &pattern[i];
                break;
            }
            if (i == min_idx)
                break; /// Prevent underflow on decrement
        }

        if (open_paren) {
            /// Parse all qualifier characters between ( and )
            glob_qual_spec_t parsed = {0};
            parsed.flags = GLOB_QUAL_NONE;
            bool valid_qualifier = true;

            for (const char *p = open_paren + 1; p < pattern + len - 1; p++) {
                switch (*p) {
                case '.':
                    parsed.flags |= GLOB_QUAL_FILE;
                    break;
                case '/':
                    parsed.flags |= GLOB_QUAL_DIR;
                    break;
                case '@':
                    parsed.flags |= GLOB_QUAL_LINK;
                    break;
                case '*':
                    parsed.flags |= GLOB_QUAL_EXEC;
                    break;
                case 'r':
                    parsed.flags |= GLOB_QUAL_READABLE;
                    break;
                case 'w':
                    parsed.flags |= GLOB_QUAL_WRITABLE;
                    break;
                case 'N':
                    parsed.flags |= GLOB_QUAL_NULLGLOB;
                    break;
                case 'D':
                    parsed.flags |= GLOB_QUAL_DOTGLOB;
                    break;
                case 'm': {
                    /// zsh modification-time qualifier m[Mwhms][+-]n:
                    /// optional unit (default days), optional comparison
                    /// ('-' younger than n units, '+' older, none == exactly
                    /// n units old at unit resolution), then the count.
                    const char *q = p + 1;
                    char unit = 'd';
                    if (*q && strchr("Mwhms", *q)) {
                        unit = *q;
                        q++;
                    }
                    char cmp = 0;
                    if (*q == '+' || *q == '-') {
                        cmp = *q;
                        q++;
                    }
                    if (!isdigit((unsigned char)*q)) {
                        valid_qualifier = false;
                        break;
                    }
                    long n = 0;
                    while (isdigit((unsigned char)*q)) {
                        int digit = *q - '0';
                        /// Saturate rather than overflow: a 20-digit count
                        /// (reachable within the 24-char scan window) would
                        /// wrap a signed long (UB). Any real mtime count is
                        /// tiny; clamp at the last value that stays in range
                        /// while still consuming every digit so the group
                        /// parses as well-formed.
                        if (n <= (LONG_MAX - digit) / 10) {
                            n = n * 10 + digit;
                        }
                        q++;
                    }
                    parsed.has_mtime = true;
                    parsed.mtime_unit = unit;
                    parsed.mtime_cmp = cmp;
                    parsed.mtime_count = n;
                    p = q - 1; /// the loop's p++ steps past the digits
                    break;
                }
                case ',':
                    break; /// Separator, ignore
                default:
                    /// Unknown character - not a valid glob qualifier
                    valid_qualifier = false;
                    break;
                }
                if (!valid_qualifier)
                    break;
            }

            if (valid_qualifier && spec_has_qualifier(&parsed)) {
                /// Strip the qualifier
                *base_pattern = strndup(pattern, open_paren - pattern);
                return parsed;
            }
        }
    }

    *base_pattern = strdup(pattern);
    return spec;
}

/**
 * @brief Check if file matches glob qualifier
 *
 * @param path File path to check
 * @param spec Parsed qualifier spec (type/permission/time filters)
 * @return true if file matches qualifier
 */
static bool matches_glob_qualifier(const char *path,
                                   const glob_qual_spec_t *spec) {
    if (!spec_needs_filter(spec)) {
        return true;
    }

    struct stat st;
    if (lstat(path, &st) != 0) {
        return false;
    }

    /// With combined qualifiers (bitmask), file must match ANY of the type
    /// qualifiers For example, *(.,@) matches files OR symlinks
    bool type_match = false;
    bool has_type_qualifier = false;

    /// Check type qualifiers (file, dir, link, exec) - OR logic
    if (spec->flags & GLOB_QUAL_FILE) {
        has_type_qualifier = true;
        if (S_ISREG(st.st_mode))
            type_match = true;
    }
    if (spec->flags & GLOB_QUAL_DIR) {
        has_type_qualifier = true;
        if (S_ISDIR(st.st_mode))
            type_match = true;
    }
    if (spec->flags & GLOB_QUAL_LINK) {
        has_type_qualifier = true;
        if (S_ISLNK(st.st_mode))
            type_match = true;
    }
    if (spec->flags & GLOB_QUAL_EXEC) {
        has_type_qualifier = true;
        if (S_ISREG(st.st_mode) &&
            (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
            type_match = true;
        }
    }

    /// If no type qualifiers specified, default to matching any type
    if (!has_type_qualifier) {
        type_match = true;
    }

    if (!type_match) {
        return false;
    }

    /// Check permission qualifiers - AND logic (must satisfy all)
    if (spec->flags & GLOB_QUAL_READABLE) {
        if (access(path, R_OK) != 0)
            return false;
    }
    if (spec->flags & GLOB_QUAL_WRITABLE) {
        if (access(path, W_OK) != 0)
            return false;
    }

    /// Modification-time qualifier m[Mwhms][+-]n. Age is measured from the
    /// lstat mtime (consistent with the type checks above, which also do not
    /// dereference symlinks).
    if (spec->has_mtime) {
        long unit_secs = glob_mtime_unit_seconds(spec->mtime_unit);
        double age = difftime(time(NULL), st.st_mtime);
        double threshold = (double)spec->mtime_count * (double)unit_secs;
        if (spec->mtime_cmp == '-') {
            /// younger than n units (modified within the window)
            if (!(age < threshold)) {
                return false;
            }
        } else if (spec->mtime_cmp == '+') {
            /// older than n units
            if (!(age > threshold)) {
                return false;
            }
        } else {
            /// no comparison: exactly n units old, truncated to the unit
            long units_old = (age < 0.0) ? -1 : (long)(age / (double)unit_secs);
            if (units_old != spec->mtime_count) {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Apply a glob qualifier fused onto a quoted word to its expanded
 *        literal value.
 *
 * A double quote already fixed the value as a scalar (SEMANTICS 3.6), so a
 * trailing qualifier is an existence/attribute filter on that literal value
 * -- NOT a re-glob of any pattern metacharacters it may contain. This
 * matches zsh: `f="*.txt"; "$f"(N)` tests the literal `*.txt` for existence
 * (empty) rather than expanding it to the matching files. The result is
 * therefore always zero or one word.
 *
 * When the trailing group is not a well-formed qualifier (a malformed time
 * spec, a digit-only group, etc.) the value is returned verbatim as a single
 * word, so a quoted word is never silently dropped.
 *
 * @param value Already-expanded value, possibly ending in a `(...)` group.
 * @param count OUT: number of words produced (0 or 1).
 * @return Newly-allocated NULL-terminated array (caller frees each element
 *         and the array), or NULL on allocation failure.
 */
static char **apply_glob_qualifier_to_literal(const char *value, int *count) {
    *count = 0;

    char *base = NULL;
    glob_qual_spec_t spec = parse_glob_qualifier(value, &base);

    if (!spec_has_qualifier(&spec)) {
        /// No qualifier (or a malformed group): the literal value stands.
        free(base);
        char **result = malloc(2 * sizeof(char *));
        if (!result) {
            return NULL;
        }
        result[0] = strdup(value);
        if (!result[0]) {
            free(result);
            return NULL;
        }
        result[1] = NULL;
        *count = 1;
        return result;
    }

    /// A qualifier makes this a one-element glob of the literal base: the
    /// path contributes iff it exists and satisfies the type/permission/time
    /// filter; otherwise the word expands to nothing (the `(N)`-style empty
    /// result, never the literal).
    struct stat st;
    bool keep = (lstat(base, &st) == 0) && matches_glob_qualifier(base, &spec);

    char **result;
    if (keep) {
        result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(base);
            if (!result[0]) {
                /// Honor the NULL-on-allocation-failure contract rather than
                /// returning a count==1 array with a NULL element.
                free(result);
                result = NULL;
            } else {
                result[1] = NULL;
                *count = 1;
            }
        }
    } else {
        result = malloc(sizeof(char *));
        if (result) {
            result[0] = NULL;
        }
    }
    free(base);
    return result;
}

/// =============================================================================
/// ZSH EXTENDED GLOB PATTERNS
/// =============================================================================
/// Zsh extended glob uses different syntax than bash:
///   X#   - zero or more of X (like * in regex)
///   X##  - one or more of X (like + in regex)
///   (a|b) - alternation (without preceding operator)
///   ^pat - negation (match everything except pattern)
/// =============================================================================

/**
 * @brief Check if pattern contains zsh-style extglob syntax
 *
 * Detects X#, X##, (a|b) alternation, and ^pattern negation.
 */
static bool has_zsh_extglob_pattern(const char *pattern) {
    if (!pattern) {
        return false;
    }
    /// (a|b) alternation is structured (it requires a `(`), so it stays under
    /// the on-by-default FEATURE_EXTENDED_GLOB. The bare operators -- a leading
    /// ^ negation and X#/X## quantifiers -- turn ordinary punctuation into glob
    /// operators, so they are opt-in via FEATURE_ZSH_EXTENDED_GLOB (off by
    /// default). Keeping them off leaves a mid-word # a literal word, the
    /// bash+zsh default (#448).
    bool alternation = shell_mode_allows(FEATURE_EXTENDED_GLOB);
    bool bare_ops = shell_mode_allows(FEATURE_ZSH_EXTENDED_GLOB);
    if (!alternation && !bare_ops) {
        return false;
    }

    /// Check for ^pattern negation at start (bare operator, opt-in)
    if (bare_ops && pattern[0] == '^') {
        return true;
    }

    /// Check for (a|b) alternation - parentheses with | inside, NOT preceded by
    /// extglob op
    if (alternation) {
        const char *p = pattern;
        while (*p) {
            if (*p == '(' && (p == pattern || !strchr("?*+@!", *(p - 1)))) {
                /// Found ( not preceded by extglob operator - check for |
                /// inside
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
    }

    /// Check for # or ## quantifiers (after a char or ]) (bare operator,
    /// opt-in)
    if (bare_ops) {
        const char *p = pattern;
        while (*p) {
            if (*p == '#') {
                /// # must be preceded by something (char, ], or ))
                if (p > pattern) {
                    char prev = *(p - 1);
                    if (prev != '/' && prev != ' ' && prev != '\t') {
                        return true;
                    }
                }
            }
            p++;
        }
    }

    return false;
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
        return true; /// explicitly unbounded
    }
    return strlen(pattern) <= (size_t)config.regex_pattern_max;
}

/// @brief Expand zsh extglob pattern by reading directory and matching
static char **expand_zsh_extglob_pattern(const char *pattern,
                                         int *expanded_count) {
    *expanded_count = 0;

    if (!pattern) {
        return NULL;
    }

    /// The bare zsh operators (leading ^ negation, X#/X## quantifiers) are
    /// opt-in. A pattern reaches here whenever it carries the always-on
    /// (a|b) alternation, so when the bare operators are off a co-occurring ^
    /// or # must stay literal rather than ride in on the alternation route
    /// (#448). Passing the LUSH_PATTERN_ZSH_EXTENDED flag conditionally keeps
    /// # literal in the matcher; gating is_negated keeps a leading ^ literal.
    unsigned match_flags = lush_shell_pattern_flags();

    /// Check for ^pattern negation
    bool is_negated =
        (match_flags & LUSH_PATTERN_ZSH_EXTENDED) && (pattern[0] == '^');
    const char *match_pattern = is_negated ? pattern + 1 : pattern;

    /// Split pattern into directory and filename parts
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

    /// Collect matching entries
    char **results = NULL;
    size_t result_count = 0;
    size_t result_capacity = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /// Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /// Skip hidden files unless pattern starts with .
        if (entry->d_name[0] == '.' && file_pattern[0] != '.') {
            continue;
        }

        bool zsh_match =
            lush_pattern_match_ex(entry->d_name, file_pattern, match_flags);
        if (is_negated) {
            zsh_match = !zsh_match;
        }
        if (zsh_match) {
            /// Grow array if needed
            if (result_count >= result_capacity) {
                size_t new_capacity =
                    result_capacity == 0 ? 16 : result_capacity * 2;
                char **new_results =
                    realloc(results, (new_capacity + 1) * sizeof(char *));
                if (!new_results) {
                    /// Cleanup on failure
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

            /// Build full path if in subdirectory
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

    /// Sort results. The element type is char *, so the comparator must
    /// dereference to the string; casting strcmp directly would compare the
    /// pointer bytes instead of the names.
    qsort(results, result_count, sizeof(char *), strptr_cmp);

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

    /// Split pattern into directory and filename parts
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

    /// Open directory
    DIR *dir = opendir(dir_path);
    if (!dir) {
        free(pattern_copy);
        return NULL;
    }

    /// Collect matching entries
    char **results = NULL;
    int count = 0;
    int capacity = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /// Skip . and .. unless pattern explicitly starts with .
        if (entry->d_name[0] == '.' && file_pattern[0] != '.') {
            if (!shell_mode_allows(FEATURE_DOT_GLOB)) {
                continue;
            }
        }

        if (lush_shell_pattern_match(entry->d_name, file_pattern)) {
            /// Resize array if needed
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

            /// Build full path
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

    /// Sort like POSIX pathname expansion (readdir order is not sorted).
    qsort(results, (size_t)count, sizeof(char *), strptr_cmp);

    /// Add NULL terminator
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
        return 0; /// Not an error - directory might not be readable
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /// Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /// Skip hidden files unless dotglob is enabled
        if (entry->d_name[0] == '.' && !shell_mode_allows(FEATURE_DOT_GLOB)) {
            continue;
        }

        /// Build full path
        char full_path[PATH_MAX];
        if (base_dir[0]) {
            snprintf(full_path, sizeof(full_path), "%s/%s", base_dir,
                     entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
        }

        /// Check if this path matches the remaining pattern
        if (remaining_pattern && remaining_pattern[0]) {
            /// Build candidate path with remaining pattern
            char candidate[PATH_MAX];
            int written = snprintf(candidate, sizeof(candidate), "%s/%s",
                                   full_path, remaining_pattern);
            if (written < 0 || (size_t)written >= sizeof(candidate)) {
                continue; /// Path too long, skip this entry
            }

            /// Use glob to match the remaining pattern
            glob_t globbuf;
            if (glob(candidate, GLOB_NOSORT, NULL, &globbuf) == 0) {
                for (size_t i = 0; i < globbuf.gl_pathc; i++) {
                    /// Resize array if needed
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
            /// No remaining pattern - match the path itself
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

        /// Recurse into directories
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

    /// Find the ** in the pattern
    const char *starstar = strstr(pattern, "**");
    if (!starstar) {
        return NULL;
    }

    /// Split into prefix (before **) and suffix (after **)
    size_t prefix_len = starstar - pattern;
    char *prefix = malloc(prefix_len + 1);
    if (!prefix)
        return NULL;

    strncpy(prefix, pattern, prefix_len);
    prefix[prefix_len] = '\0';

    /// Remove trailing slash from prefix if present
    if (prefix_len > 0 && prefix[prefix_len - 1] == '/') {
        prefix[prefix_len - 1] = '\0';
    }

    /// Get suffix (after **)
    const char *suffix = starstar + 2;
    if (*suffix == '/')
        suffix++; /* Skip leading slash after ** */

    /// Initialize results
    char **results = NULL;
    int count = 0;
    int capacity = 0;

    /// Start directory
    const char *start_dir = prefix[0] ? prefix : ".";

    /// First, match the start directory itself with the suffix pattern
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

    /// Recursively expand through directories
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

    /// Sort like POSIX pathname expansion: the recursive walk collects in
    /// directory order, which is not sorted.
    qsort(results, (size_t)count, sizeof(char *), strptr_cmp);

    /// Add NULL terminator
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
 * directly with readdir + lush_pattern_match. `.` and `..` are always
 * excluded.
 *
 * Only single-component patterns and `dir/filepat` forms are handled
 * (the qualifier-glob syntax in practice never nests deeper). Results
 * are filtered through matches_glob_qualifier for any type/permission
 * bits combined with D.
 *
 * @param base_pattern Pattern with the qualifier already stripped
 * @param spec         Parsed qualifier spec (includes GLOB_QUAL_DOTGLOB)
 * @param count        OUT: number of matches
 * @return Match array (caller frees), empty array on no match, or NULL
 *         on error
 */
static char **expand_glob_dotglob(const char *base_pattern,
                                  const glob_qual_spec_t *spec, int *count) {
    *count = 0;

    /// Split into directory and filename pattern at the last '/'.
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
        /// lush_pattern_match has no FNM_PERIOD analogue: `*` matches
        /// leading-dot names, which is exactly the D-qualifier semantics.
        if (!lush_shell_pattern_match(entry->d_name, filepat)) {
            continue;
        }
        /// Build the path as the caller would see it.
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
        if (spec_needs_filter(spec) && !matches_glob_qualifier(path, spec)) {
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
        /// No matches: hand back an empty (non-NULL) array.
        result = malloc(sizeof(char *));
        if (result) {
            result[0] = NULL;
        }
        *count = 0;
        return result;
    }
    /// Sort like POSIX pathname expansion (readdir order is not sorted).
    qsort(result, result_count, sizeof(char *), strptr_cmp);
    result[result_count] = NULL;
    *count = (int)result_count;
    return result;
}

static char **expand_glob_pattern(const char *pattern, int *expanded_count) {
    if (!pattern || !expanded_count) {
        *expanded_count = 0;
        return NULL;
    }

    /// Check if globbing is disabled (set -f)
    if (shell_opts.no_globbing) {
        /// Return the original pattern without expansion
        char **result = malloc(sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            *expanded_count = 1;
            return result;
        }
        *expanded_count = 0;
        return NULL;
    }

    /// Try globstar expansion if ** pattern and FEATURE_GLOBSTAR is enabled
    if (shell_mode_allows(FEATURE_GLOBSTAR) && has_globstar_pattern(pattern)) {
        char **globstar_results =
            expand_globstar_pattern(pattern, expanded_count);
        if (globstar_results && *expanded_count > 0) {
            return globstar_results;
        }
        /// Globstar expansion failed or no matches - fall through to handle
        /// according to nullglob setting
        if (shell_mode_allows(FEATURE_NULL_GLOB)) {
            /// Return empty array
            char **result = malloc(sizeof(char *));
            if (result) {
                result[0] = NULL;
                *expanded_count = 0;
                return result;
            }
            *expanded_count = 0;
            return NULL;
        }
        /// Return original pattern
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

    /// Parse glob qualifier FIRST (Zsh-style)
    /// This must happen before extglob detection because *(.) could be either:
    ///   - Zsh glob qualifier: * with file qualifier (.)
    ///   - Bash extglob: zero or more of pattern "."
    /// Glob qualifiers are always a single char at the END, so check that first
    char *base_pattern = NULL;
    glob_qual_spec_t qualifier = {0};
    qualifier.flags = GLOB_QUAL_NONE;

    if (shell_mode_allows(FEATURE_GLOB_QUALIFIERS)) {
        qualifier = parse_glob_qualifier(pattern, &base_pattern);
    }

    /// If we found a glob qualifier, use the base pattern for further expansion
    /// Otherwise, use the original pattern
    const char *pattern_to_expand =
        spec_has_qualifier(&qualifier) ? base_pattern : pattern;

    /// Try zsh-style extglob expansion first (X#, X##, (a|b), ^pattern)
    if (!spec_has_qualifier(&qualifier) &&
        has_zsh_extglob_pattern(pattern_to_expand)) {
        /// Free base_pattern if it was allocated by parse_glob_qualifier
        free(base_pattern);
        char **zsh_results =
            expand_zsh_extglob_pattern(pattern_to_expand, expanded_count);
        if (zsh_results && *expanded_count > 0) {
            return zsh_results;
        }
        /// Zsh extglob expansion failed or no matches - handle according to
        /// nullglob
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
        /// Return original pattern
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

    /// Try bash-style extglob expansion if pattern contains extglob syntax
    /// (only if we didn't already strip a glob qualifier). has_extglob_pattern
    /// already returns false when FEATURE_EXTENDED_GLOB is off, so `@(a|b)` in
    /// a filename is an ordinary literal in the modes where extglob is off.
    if (!spec_has_qualifier(&qualifier) &&
        has_extglob_pattern(pattern_to_expand)) {
        /// Free base_pattern if it was allocated by parse_glob_qualifier
        free(base_pattern);
        char **extglob_results =
            expand_extglob_pattern(pattern_to_expand, expanded_count);
        if (extglob_results && *expanded_count > 0) {
            return extglob_results;
        }
        /// Extglob expansion failed or no matches - fall through to handle
        /// according to nullglob setting
        if (shell_mode_allows(FEATURE_NULL_GLOB)) {
            /// Return empty array
            char **result = malloc(sizeof(char *));
            if (result) {
                result[0] = NULL;
                *expanded_count = 0;
                return result;
            }
            *expanded_count = 0;
            return NULL;
        }
        /// Return original pattern
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

    /// Ensure base_pattern is set to a strdup of the original pattern
    /// when no qualifier was parsed. parse_glob_qualifier already does
    /// this on its GLOB_QUAL_NONE return paths (lines 5887, 5959), so
    /// we only need to strdup here when the qualifier feature is
    /// disabled and parse_glob_qualifier was therefore never called --
    /// doing it unconditionally would leak the parse_glob_qualifier
    /// allocation (issue #112).
    if (!spec_has_qualifier(&qualifier) && !base_pattern) {
        base_pattern = strdup(pattern);
    }

    if (!base_pattern) {
        *expanded_count = 0;
        return NULL;
    }

    /// The zsh `D` qualifier requests dotfile matching, which libc
    /// glob() cannot do portably -- route through a readdir scan.
    if (qualifier.flags & GLOB_QUAL_DOTGLOB) {
        char **dot_results =
            expand_glob_dotglob(base_pattern, &qualifier, expanded_count);
        free(base_pattern);
        if (dot_results) {
            return dot_results;
        }
        /// Scan failed: fall back to the literal pattern.
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
    /// Sort matches: POSIX pathname expansion sorts results per the current
    /// LC_COLLATE, and bash/zsh do the same. GLOB_NOSORT would return raw
    /// readdir order, which is filesystem-dependent and non-deterministic.
    int glob_result = glob(base_pattern, 0, NULL, &globbuf);
    free(base_pattern);

    if (glob_result == GLOB_NOMATCH) {
        /// No matches - nullglob mode OR an explicit (N) qualifier
        /// both mean "expand to nothing" rather than the literal.
        if (shell_mode_allows(FEATURE_NULL_GLOB) ||
            (qualifier.flags & GLOB_QUAL_NULLGLOB)) {
            /// Nullglob: unmatched patterns expand to nothing
            /// Return empty array (not NULL, to distinguish from error)
            char **result = malloc(sizeof(char *));
            if (result) {
                result[0] = NULL;
                *expanded_count = 0;
                return result;
            }
            *expanded_count = 0;
            return NULL;
        }
        /// Default POSIX behavior: return original pattern
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
        /// Error in globbing
        *expanded_count = 0;
        return NULL;
    }

    /// Success - copy results, filtering by qualifier if present
    if (!spec_needs_filter(&qualifier)) {
        /// No filtering needed
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
                /// Cleanup on allocation failure
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
        /// Filter results by glob qualifier
        char **result = malloc((globbuf.gl_pathc + 1) * sizeof(char *));
        if (!result) {
            globfree(&globbuf);
            *expanded_count = 0;
            return NULL;
        }

        size_t match_count = 0;
        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
            if (matches_glob_qualifier(globbuf.gl_pathv[i], &qualifier)) {
                result[match_count] = strdup(globbuf.gl_pathv[i]);
                if (!result[match_count]) {
                    /// Cleanup on allocation failure
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
            /// No matches after filtering - nullglob mode OR an
            /// explicit (N) qualifier both expand to nothing.
            free(result);
            if (shell_mode_allows(FEATURE_NULL_GLOB) ||
                (qualifier.flags & GLOB_QUAL_NULLGLOB)) {
                /// Nullglob: expand to nothing
                /// Return empty array (not NULL, to distinguish from error)
                result = malloc(sizeof(char *));
                if (result) {
                    result[0] = NULL;
                    *expanded_count = 0;
                    return result;
                }
                *expanded_count = 0;
                return NULL;
            }
            /// Default: return original pattern
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

    /// Check for glob metacharacters: *, ?, and character classes [...]
    const char *p = str;
    while (*p) {
        if (*p == '*' || *p == '?' || *p == '[') {
            return true;
        }
        /// Bash-style extglob (?(pat), *(pat), +(pat), @(pat), !(pat)) and zsh
        /// (a|b) alternation stay under the on-by-default
        /// FEATURE_EXTENDED_GLOB: both require a `(`, so they never collide
        /// with ordinary punctuation.
        if (shell_mode_allows(FEATURE_EXTENDED_GLOB)) {
            if ((*p == '?' || *p == '*' || *p == '+' || *p == '@' ||
                 *p == '!') &&
                *(p + 1) == '(') {
                return true;
            }
            /// ( not preceded by extglob op may be zsh alternation
            if (*p == '(' && (p == str || !strchr("?*+@!", *(p - 1)))) {
                /// Check for | inside
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
        /// The zsh bare operators -- a leading ^ negation and X#/X##
        /// quantifiers
        /// -- turn ordinary punctuation into glob operators, so they are opt-in
        /// via FEATURE_ZSH_EXTENDED_GLOB (off by default). Keeping them off
        /// leaves a mid-word # a literal word, the bash+zsh default (#448).
        if (shell_mode_allows(FEATURE_ZSH_EXTENDED_GLOB)) {
            /// ^ at start = negation
            if (p == str && *p == '^') {
                return true;
            }
            /// # after a char = zero or more quantifier
            if (*p == '#' && p > str) {
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

    /// Check for brace expansion patterns: {a,b,c} or {1..10}
    const char *p = str;
    while (*p) {
        if (*p == '{') {
            const char *close = strchr(p + 1, '}');
            if (close) {
                /// Look for comma pattern: {a,b,c}
                const char *comma = strchr(p + 1, ',');
                if (comma && comma < close) {
                    return true;
                }
                /// Look for range pattern: {1..10} or {a..z}
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

    /// Parse: start..end or start..end..step
    const char *dotdot1 = strstr(content, "..");
    if (!dotdot1) {
        return NULL;
    }

    /// Extract start
    size_t start_len = dotdot1 - content;
    char *start_str = strndup(content, start_len);
    if (!start_str)
        return NULL;

    /// Find second .. for step (optional)
    const char *after_first = dotdot1 + 2;
    const char *dotdot2 = strstr(after_first, "..");

    char *end_str = NULL;
    char *step_str = NULL;

    if (dotdot2) {
        /// Has step: start..end..step
        size_t end_len = dotdot2 - after_first;
        end_str = strndup(after_first, end_len);
        step_str = strdup(dotdot2 + 2);
    } else {
        /// No step: start..end
        end_str = strdup(after_first);
    }

    if (!end_str) {
        free(start_str);
        free(step_str);
        return NULL;
    }

    /// Determine if numeric or character range
    bool is_numeric = false;
    bool is_char = false;
    int pad_width = 0;

    /// Check for zero-padding in numeric (e.g., "01")
    if (start_str[0] == '0' && strlen(start_str) > 1) {
        pad_width = strlen(start_str);
    }
    if (end_str[0] == '0' && strlen(end_str) > 1) {
        int end_pad = strlen(end_str);
        if (end_pad > pad_width)
            pad_width = end_pad;
    }

    /// Check if start/end are single chars
    if (strlen(start_str) == 1 && strlen(end_str) == 1 &&
        isalpha(start_str[0]) && isalpha(end_str[0])) {
        is_char = true;
    } else {
        /// Try to parse as numbers
        char *endptr;
        long start_val = strtol(start_str, &endptr, 10);
        if (*endptr == '\0') {
            long end_val = strtol(end_str, &endptr, 10);
            if (*endptr == '\0') {
                is_numeric = true;
                (void)start_val; /// Used below
                (void)end_val;
            }
        }
    }

    if (!is_numeric && !is_char) {
        /// Invalid range - return NULL, caller will use original
        free(start_str);
        free(end_str);
        free(step_str);
        return NULL;
    }

    /// Parse step value
    long step = 1;
    if (step_str && strlen(step_str) > 0) {
        char *endptr;
        step = strtol(step_str, &endptr, 10);
        if (*endptr != '\0' || step == 0) {
            step = 1; /// Invalid step, use default
        }
        if (step < 0)
            step = -step; /// Step is always positive, direction from start/end
    }

    /// Calculate range
    long start_val, end_val;
    if (is_char) {
        start_val = start_str[0];
        end_val = end_str[0];
    } else {
        start_val = strtol(start_str, NULL, 10);
        end_val = strtol(end_str, NULL, 10);
    }

    /// Determine direction
    bool reverse = (start_val > end_val);

    /// Count items
    long range = reverse ? (start_val - end_val) : (end_val - start_val);
    int count = (int)(range / step) + 1;

    int cap = brace_expansion_cap();
    if (count <= 0 || (cap > 0 && count > cap)) {
        /// Range exceeds configured cap (or is degenerate). Signal
        /// limit-exceeded distinctly from malloc/parse failure so the
        /// top-level caller can produce a real diagnostic. The cap path
        /// relies on the sentinel; the count<=0 path keeps prior
        /// "fall back to original pattern" behavior by returning NULL
        /// with *expanded_count = 0 unchanged.
        if (cap > 0 && count > cap) {
            *expanded_count = BRACE_EXPANSION_LIMIT_SENTINEL;
        }
        free(start_str);
        free(end_str);
        free(step_str);
        return NULL;
    }

    /// Allocate result array
    char **result = malloc((count + 1) * sizeof(char *));
    if (!result) {
        free(start_str);
        free(end_str);
        free(step_str);
        return NULL;
    }

    /// Generate expansions
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
            /// Cleanup on failure
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

    /// Recursively expand any remaining brace patterns in suffix
    /// This handles Cartesian products like {1..2}{a..b}
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
                    /// Add all sub-results to final
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

    /// Find the first brace pattern
    const char *open = strchr(pattern, '{');
    if (!open) {
        /// No braces - return original pattern
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

    /// Find the matching close brace, tracking nesting depth so that
    /// patterns like `{{1..3},{a..c}}` resolve the OUTER brace first
    /// rather than the first inner `}`. Without this, the function
    /// splits content as `{1..3` and bails, leaving the pattern
    /// un-expanded (real_world/bash/206).
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
        /// Malformed brace - return original pattern
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

    /// Extract prefix, brace content, and suffix
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

    /// Check if this is a SIMPLE range pattern (e.g. `1..10`, `a..z`,
    /// `1..10..2`). A range pattern has `..` and no top-level commas
    /// and no nested braces — otherwise it's a comma-list whose items
    /// happen to contain ranges (e.g. `{{1..3},{a..c}}`) and must be
    /// split on the outer commas first.
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
        /// Range expansion failed - fall through to return original pattern
        char **result = malloc(2 * sizeof(char *));
        if (result) {
            result[0] = strdup(pattern);
            result[1] = NULL;
            *expanded_count = 1;
        }
        return result;
    }

    /// Count comma-separated items at the TOP LEVEL only. Commas
    /// inside nested braces belong to the inner pattern and must not
    /// split the outer one (e.g. `{{1..3},{a..c}}` has two outer
    /// items, not five).
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

    /// Allocate result array
    char **result = malloc((item_count + 1) * sizeof(char *));
    if (!result) {
        free(prefix);
        free(content);
        *expanded_count = 0;
        return NULL;
    }

    /// Split content by commas and build result strings
    int result_index = 0;
    char *item_start = content;
    char *comma_pos = content;

    while (result_index < item_count) {
        /// Find next TOP-LEVEL comma (depth 0) or end of string.
        /// Nested braces hide their commas from the outer split.
        int depth = 0;
        while (*comma_pos && !(depth == 0 && *comma_pos == ',')) {
            if (*comma_pos == '{') {
                depth++;
            } else if (*comma_pos == '}') {
                depth--;
            }
            comma_pos++;
        }

        /// Extract current item
        size_t item_len = comma_pos - item_start;
        char *item = malloc(item_len + 1);
        if (!item) {
            /// Cleanup on failure
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

        /// Build full result string: prefix + item + suffix
        size_t full_len = strlen(prefix) + strlen(item) + strlen(suffix);
        result[result_index] = malloc(full_len + 1);
        if (!result[result_index]) {
            /// Cleanup on failure
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

        /// Move to next item
        if (*comma_pos == ',') {
            comma_pos++;
            item_start = comma_pos;
        }
    }

    result[item_count] = NULL;
    *expanded_count = item_count;

    free(prefix);
    free(content);

    /// Recursively expand any remaining brace patterns in results.
    /// This handles Cartesian products like `{1..2}{a..b}` (where the
    /// suffix carries another brace) AND nested patterns like
    /// `{{1..3},{a..c}}` (where each comma-separated item is itself a
    /// brace pattern). Scan all results so neither case is missed.
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
                /// Already over cap — drain remaining originals
                /// cleanly to avoid leaks.
                free(result[i]);
                continue;
            }
            if (needs_brace_expansion(result[i])) {
                int sub_count;
                char **sub_results =
                    expand_brace_pattern(result[i], &sub_count);
                if (sub_count == BRACE_EXPANSION_LIMIT_SENTINEL) {
                    /// Recursive cap propagation.
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
                    /// Add all sub-results to final
                    char **new_final =
                        realloc(final_results,
                                (final_count + sub_count) * sizeof(char *));
                    if (new_final) {
                        final_results = new_final;
                        for (int j = 0; j < sub_count; j++) {
                            final_results[final_count++] = sub_results[j];
                        }
                        free(sub_results); /// Free array, not strings
                    } else {
                        /// Memory error - cleanup and return what we have
                        for (int j = 0; j < sub_count; j++) {
                            free(sub_results[j]);
                        }
                        free(sub_results);
                    }
                    free(result[i]); /// Free original since we expanded it
                } else {
                    /// Sub-expansion failed, keep original
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
                        /// realloc failed — original was unmodified, but
                        /// result[i] is now orphaned; free it to avoid
                        /// a leak under malloc pressure.
                        free(result[i]);
                    }
                }
            } else {
                /// No more braces, keep as-is
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

        free(result); /// Free original array

        if (limit_hit) {
            /// Cap exceeded — release the partial accumulation cleanly
            /// and return the limit sentinel for the top-level caller.
            for (int j = 0; j < final_count; j++) {
                free(final_results[j]);
            }
            free(final_results);
            *expanded_count = BRACE_EXPANSION_LIMIT_SENTINEL;
            return NULL;
        }

        /// Add NULL terminator
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

    /// Restricted-shell mode forbids command names containing `/`.
    /// Matches bash rbash and zsh RESTRICTED -- a restricted user
    /// must use PATH lookup, not arbitrary filesystem traversal.
    if (restricted_mode_is_engaged() && strchr(argv[0], '/')) {
        source_location_t loc = command ? command->loc : SOURCE_LOC_UNKNOWN;
        executor_error_report(
            executor, SHELL_ERR_PERMISSION_DENIED, loc,
            "%s: restricted: command names cannot contain '/'", argv[0]);
        return 1;
    }

    /// Check if command exists before forking (for better error messages)
    /// Skip this check for path-based commands (containing '/')
    char *full_path = NULL;
    if (!strchr(argv[0], '/')) {
        full_path = find_command_in_path(argv[0]);
        if (!full_path) {
            /// Command not found - report with suggestions from parent process
            source_location_t loc = command ? command->loc : SOURCE_LOC_UNKNOWN;
            report_command_not_found(executor, argv[0], loc);
            return 127;
        }

        /// If hashall is enabled, remember this command's location
        if (shell_opts.hash_commands) {
            init_command_hash();
            if (command_hash) {
                ht_strstr_insert(command_hash, argv[0], full_path);
            }
        }
        free(full_path);
    }

    /// Reset terminal state before forking for external commands
    /// This ensures git and other commands get proper TTY behavior
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
        /// Restore default dispositions and clear the mask before anything
        /// else: until the execvp below replaces this image, the child still
        /// carries the shell's caught SIGHUP handler. A hangup forwarded by the
        /// parent's foreground wait in this pre-exec window would otherwise be
        /// caught (and lost) by that handler, leaving the exec'd command
        /// running past the hangup. SIG_DFL here makes the window terminate on
        /// a forwarded hangup, and hands the exec'd program a clean slate.
        reset_signals_for_exec();

        /// Child process - setup redirections here
        int redir_result = setup_redirections(executor, command);
        if (redir_result != 0) {
            /// _exit, not exit: this child shares the parent's seekable script
            /// input; exit()'s fclose would lseek it and make the parent
            /// re-execute the script tail (Issue #444).
            lush_process_terminate(1);
        }

        if (redirect_stderr) {
            /// Redirect stderr to /dev/null
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd != -1) {
                dup2(null_fd, STDERR_FILENO);
                close(null_fd);
            }
        }

        execvp(argv[0], argv);
        /// Check errno to determine appropriate exit code
        int exit_code = 127; /// Default: command not found
        if (errno == EACCES) {
            exit_code = 126; /// Permission denied
        } else if (errno == ENOENT) {
            exit_code = 127; /// Command not found
        }
        if (!redirect_stderr) {
            int saved_errno = errno;
            executor_error_report(executor, SHELL_ERR_EXEC_FAILED,
                                  command ? command->loc : SOURCE_LOC_UNKNOWN,
                                  "%s: %s", argv[0], strerror(saved_errno));
        }
        /// _exit via the shared path: this child shares the parent's seekable
        /// script input, so exit()'s fclose would lseek it and make the parent
        /// re-execute the script tail (Issue #444).
        lush_process_terminate(exit_code);
    } else {
        /// Parent process
        set_current_child_pid(pid);

        /// Print trace for external command if -x is enabled
        if (should_trace_execution()) {
            /// Build command string from argv for tracing
            size_t cmd_len = 1; /// for null terminator
            for (int j = 0; argv[j]; j++) {
                cmd_len += strlen(argv[j]) + (j > 0 ? 1 : 0); /// +1 for space
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

        /// Enhanced debug tracing for external commands with setup
        DEBUG_TRACE_COMMAND(argv[0], argv, 0);
        DEBUG_PROFILE_ENTER(argv[0]);

        int status;
        /// Wait for the command, retrying past incidental EINTR; a hangup
        /// terminates the shell.
        if (executor_wait_foreground(pid, &status) == -1) {
            /// Real error - child may have already been reaped
            clear_current_child_pid();
            return 1;
        }
        clear_current_child_pid();

        DEBUG_PROFILE_EXIT(argv[0]);

        /// Handle exit status properly - child may have exited or been signaled
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            /// Child was killed by signal - return 128 + signal number (bash
            /// convention)
            return 128 + WTERMSIG(status);
        }
        return 1;
    }
}

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

    /// Set global executor for job control builtins
    current_executor = executor;

    /// Stash the call-site source location for the duration of this
    /// builtin invocation so builtin error helpers can produce a real
    /// `--> file:line:col` line and source-snippet caret. The swap
    /// returns the previously-stashed loc, which we restore on every
    /// exit path so a re-entrant builtin (e.g. `eval` invoking another
    /// builtin) doesn't clobber the outer caller's location when the
    /// inner call returns.
    source_location_t saved_loc = builtin_swap_source_location(loc);

    /// Find the builtin function in the builtin table
    for (size_t i = 0; i < builtins_count; i++) {
        if (strcmp(argv[0], builtins[i].name) == 0) {
            /// Print trace for builtin command if -x is enabled
            if (should_trace_execution()) {
                /// Build command string from argv for tracing
                size_t cmd_len = 1; /// for null terminator
                for (int j = 0; argv[j]; j++) {
                    cmd_len +=
                        strlen(argv[j]) + (j > 0 ? 1 : 0); /// +1 for space
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

            /// Count arguments
            int argc = 0;
            while (argv[argc]) {
                argc++;
            }

            /// Push "in builtin '<name>'" onto the executor's context
            /// stack for the duration of this builtin invocation.
            /// executor_error_report() (the canonical wrapper) walks
            /// the context stack at display time, so any error a
            /// builtin emits — directly or via the wrapper — picks up
            /// this context frame automatically. Per-builtin sites no
            /// longer need to push it themselves.
            executor_push_context(executor, loc, "in builtin '%s'", argv[0]);

            int result = builtins[i].func(argc, argv);

            /// Restore previous loc + clear builtin context + global executor
            executor_pop_context(executor);
            (void)builtin_swap_source_location(saved_loc);
            current_executor = NULL;

            return result;
        }
    }

    /// Restore previous loc + clear global executor
    (void)builtin_swap_source_location(saved_loc);
    current_executor = NULL;

    return 1; /// Command not found
}

/**
 * @brief Check if command name is a builtin
 *
 * @param cmd Command name to check
 * @return true if command is a shell builtin
 */
static bool is_builtin_command(const char *cmd) { return is_builtin(cmd); }

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

    /// Don't treat parameter expansion ${...} as assignment
    if (text[0] == '$' && text[1] == '{') {
        return false;
    }

    /// Look for '=' not at the beginning
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

    /// Check for += append operation
    bool is_append = (eq > assignment && *(eq - 1) == '+');

    /// Split into variable and value
    size_t var_len = eq - assignment;
    if (is_append) {
        var_len--; /// Exclude the '+' from variable name
    }

    char *var_name = malloc(var_len + 1);
    if (!var_name) {
        return 1;
    }

    strncpy(var_name, assignment, var_len);
    var_name[var_len] = '\0';

    /// Privileged mode security check for environment variable modifications
    if (!is_privileged_path_modification_allowed(var_name)) {
        executor_error_report(
            executor, SHELL_ERR_PERMISSION_DENIED, loc,
            "%s: cannot modify restricted variable in privileged mode",
            var_name);
        free(var_name);
        return 1;
    }

    /// Validate variable name via the project-wide predicate so
    /// FEATURE_UNICODE_IDENTIFIERS opt-ins reach this assignment path.
    if (!lush_is_valid_identifier(var_name)) {
        free(var_name);
        return 1;
    }
    /// Optional homograph guard (off unless
    /// FEATURE_REJECT_MIXED_SCRIPT_IDENTS).
    if (executor_reject_mixed_script_ident(executor, var_name,
                                           executor_current_loc(executor))) {
        free(var_name);
        return 1;
    }

    /// Assignment-context tilde expansion (POSIX 2.6.1): a tilde-prefix at the
    /// value start and after every unquoted colon is expanded, so `x=~/a:~/b`
    /// expands both segments (bash and zsh agree). This runs on the literal
    /// value before variable/command expansion -- a tilde later produced by
    /// $var is not itself expanded -- and honors quote provenance the parser
    /// preserved (single-quoted spans as '...', double-quoted tildes as \~).
    char *pre_tilde = colon_segmented_tilde_expand(eq + 1);
    if (getenv("DBG_TILDE")) {
        fprintf(stderr, "DBG raw=[%s] pre_tilde=[%s]\n", eq + 1,
                pre_tilde ? pre_tilde : "(null)");
    }
    /// Expand the value using modern expansion
    /// Save exit status set by command substitution (POSIX: assignment-only
    /// commands should return the exit status of the last command substitution)
    char *value = expand_if_needed(executor, pre_tilde ? pre_tilde : eq + 1);
    free(pre_tilde);
    int cmd_sub_exit_status = executor->exit_status;

    /// Propagate expansion failure. ${var:?word} and friends set
    /// expansion_error during expand_if_needed; without this check
    /// execute_assignment silently stores the empty fallback and
    /// returns 0, masking the failure from the caller (execute_command
    /// does check this flag in the command-not-assignment path, but
    /// assignment-only commands bypassed that check). Free what was
    /// allocated and surface expansion_exit_status. shell_exit_requested,
    /// if set, has already been raised by executor_request_posix_exit
    /// and will short-circuit the surrounding command list / loop /
    /// function body.
    if (executor->expansion_error) {
        free(value);
        free(var_name);
        return executor->expansion_exit_status;
    }

    /// Integer attribute (declare -i): subsequent assignments to the
    /// variable arith-evaluate the RHS rather than storing the literal
    /// string. `declare -i n; n=5+3` stores "8", not "5+3". Same for
    /// `n=other_var+1` -- the RHS is evaluated as an arithmetic
    /// expression which resolves identifiers as variables. Issue #102.
    symvar_flags_t target_flags =
        symtable_get_flags(executor->symtable, var_name);
    if (target_flags & SYMVAR_INTEGER_ATTR) {
        char *evaluated =
            arithm_expand_with_executor(executor, value ? value : "");
        if (evaluated) {
            free(value);
            value = evaluated;
        }
    }

    /// Case attribute (declare -l / declare -u): subsequent assignments
    /// to the variable fold the RHS to lower- or upper-case before
    /// storing. The fold goes through lle_utf8_tolower / lle_utf8_toupper
    /// so non-ASCII codepoints fold per the project's Unicode case
    /// table. Empty values and values with no case-mappable characters
    /// pass through unchanged. Bash matches this behavior: `declare -l
    /// X; X=HELLO; echo $X` prints "hello".
    if (value && (target_flags & (SYMVAR_LOWERCASE | SYMVAR_UPPERCASE))) {
        char *cased = symtable_apply_case_attr_alloc(value, target_flags);
        if (cased) {
            free(value);
            value = cased;
        }
    }

    /// Resolve nameref if the variable is a nameref (max depth 10)
    const char *target_name = var_name;
    char *resolved_to_free = NULL; /// Track if we need to free resolved name
    if (symtable_is_nameref(executor->symtable, var_name)) {
        const char *resolved =
            symtable_resolve_nameref(executor->symtable, var_name, 10);
        if (resolved && resolved != var_name) {
            target_name = resolved;
            resolved_to_free = (char *)resolved; /// May need to free this
        }
    }

    /// POSIX compliance: variable assignments are GLOBAL by default
    /// Local variables are only created via explicit 'local' builtin
    int result;

    if (is_append) {
        /// += dispatches on the target's kind: list/map appends element,
        /// scalar concatenates string. One symtable_lookup tags the kind
        /// explicitly instead of the legacy "try array, fall back to
        /// scalar" dance.
        lush_value_view_t view = {0};
        symtable_lookup(target_name, &view);
        if (view.kind == LUSH_VALUE_LIST || view.kind == LUSH_VALUE_MAP) {
            symtable_array_append(view.array, value ? value : "");
            lush_value_view_clear(&view);
            result = 0;
        } else {
            /// String append: take ownership of the scalar out of the view
            /// and proceed with the existing concatenate-and-reassign path.
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
                /// No existing value, just set the new value
                result = symtable_assign_var(executor->symtable, target_name,
                                             value ? value : "");
            }
            /// symtable_get_var returns a strdup; free it. Pre-existing
            /// leak fixed as part of the symtable_lookup migration.
            free(existing);
        }
    } else {
        result = symtable_assign_var(executor->symtable, target_name,
                                     value ? value : "");
    }

    /// Free resolved nameref if it was allocated
    if (resolved_to_free) {
        free(resolved_to_free);
    }

    /// Readonly enforcement: symtable_assign_var / symtable_set_var
    /// return SYMTABLE_ERR_READONLY when the target carries the
    /// readonly attribute anywhere in the scope chain.
    ///
    /// This is a variable-assignment error, not an ordinary command
    /// failure: bash, zsh, and dash all decline to feed it into a
    /// following `||`/`&&`. Signal command_abort so the enclosing
    /// AND-OR handler skips its right operand, and stash the diagnostic
    /// rather than emitting it here -- the assignment may be running
    /// under a transient redirection (`ro=x 2>/dev/null`), and the
    /// shell-level diagnostic must reach the real stderr after that
    /// redirection is torn down. The execute_command chokepoint emits it
    /// and applies the mode-exit policy.
    if (result == SYMTABLE_ERR_READONLY) {
        executor->command_abort = true;
        free(executor->pending_readonly_var);
        executor->pending_readonly_var = strdup(var_name);
        executor->pending_readonly_loc = loc;
        free(var_name);
        free(value);
        if (resolved_to_free) {
            free(resolved_to_free);
        }
        return 1;
    }

    /// POSIX -a (allexport): automatically export assigned variables
    if (result == 0 && should_auto_export()) {
        symtable_export_global(var_name);
    }

    /// Notify LLE prompt system when prompt variables are set by user code
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

    /// POSIX: For assignment-only commands, return the exit status of the
    /// last command substitution performed during value expansion, or 0
    /// if no command substitution was performed or if the assignment failed
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

/// Find the next case-alternative `|` separator at the top level: a `|`
/// that is not inside an extglob group `(...)` or a `[...]` bracket, and
/// not backslash-escaped. Without this an extglob pattern like
/// `@(cat|dog)` would be wrongly split at its inner `|`. Returns NULL
/// when the remaining patterns hold no further top-level separator.
static const char *find_case_alt_separator(const char *p) {
    int paren = 0;
    int bracket = 0;
    for (; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++; /// skip the escaped char
            continue;
        }
        if (*p == '[') {
            bracket++;
        } else if (*p == ']') {
            if (bracket > 0) {
                bracket--;
            }
        } else if (*p == '(') {
            paren++;
        } else if (*p == ')') {
            if (paren > 0) {
                paren--;
            }
        } else if (*p == '|' && paren == 0 && bracket == 0) {
            return p;
        }
    }
    return NULL;
}

/// Match a zsh numeric-range case pattern: <lo-hi>, <->, <lo->, <-hi> (#205).
/// Sets *is_range true when @p pattern has the <...-...> shape -- a `<...>`
/// whose interior holds a single '-' separating two all-digit-or-empty bounds
/// -- so the caller skips the glob matcher for it. Returns true when @p word
/// is a non-empty digit run whose numeric value lies within [lo, hi], either
/// bound omitted meaning open. A non-range pattern (no <...>, no interior '-',
/// or a non-digit bound) leaves *is_range false and falls through to the glob
/// matcher, so a literal `<abc>` still matches literally.
static bool case_numeric_range_match(const char *word, const char *pattern,
                                     bool *is_range) {
    *is_range = false;
    if (!word || !pattern) {
        return false;
    }
    size_t plen = strlen(pattern);
    if (plen < 3 || pattern[0] != '<' || pattern[plen - 1] != '>') {
        return false;
    }
    const char *interior = pattern + 1;
    size_t ilen = plen - 2;
    const char *dash = memchr(interior, '-', ilen);
    if (!dash) {
        return false;
    }
    size_t lo_len = (size_t)(dash - interior);
    const char *hi = dash + 1;
    size_t hi_len = ilen - lo_len - 1;
    for (size_t i = 0; i < lo_len; i++) {
        if (!isdigit((unsigned char)interior[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < hi_len; i++) {
        if (!isdigit((unsigned char)hi[i])) {
            return false;
        }
    }
    /// Confirmed a numeric range; from here it never falls to the glob matcher.
    *is_range = true;
    if (word[0] == '\0') {
        return false;
    }
    for (const char *w = word; *w; w++) {
        if (!isdigit((unsigned char)*w)) {
            return false;
        }
    }
    unsigned long long wv = strtoull(word, NULL, 10);
    if (lo_len > 0 && wv < strtoull(interior, NULL, 10)) {
        return false;
    }
    if (hi_len > 0 && wv > strtoull(hi, NULL, 10)) {
        return false;
    }
    return true;
}

static int execute_case(executor_t *executor, node_t *node) {
    if (!executor || !node || node->type != NODE_CASE) {
        return 1;
    }

    /// Get the test word and expand variables in it
    char *test_word = expand_if_needed(executor, node->val.str);
    if (!test_word) {
        return 1;
    }

    /// Push error context for structured error reporting
    executor_push_context(executor, node->loc, "in case statement");

    /// Check for trailing redirections on the case statement
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
    bool execute_next = false; /// For ;& fall-through

    /// Iterate through case items (children)
    node_t *case_item = node->first_child;
    while (case_item && !done) {
        /// The pattern is stored in case_item->val.str with terminator prefix
        /// Format: "<terminator_char><pattern>" where terminator_char is '0',
        /// '1', or '2'
        char *patterns = case_item->val.str;
        if (!patterns || !*patterns) {
            case_item = case_item->next_sibling;
            continue;
        }

        /// Extract terminator type from pattern prefix (for NODE_CASE_ITEM)
        case_terminator_t terminator = CASE_TERM_BREAK;
        if (case_item->type == NODE_CASE_ITEM && patterns[0] >= '0' &&
            patterns[0] <= '2') {
            terminator = (case_terminator_t)(patterns[0] - '0');
            patterns++; /// Skip the prefix byte
        }

        bool matched =
            execute_next; /// If fall-through, execute without testing

        if (!matched) {
            /// Split patterns by `|` and test each. strtok cannot be
            /// used here because it skips empty tokens -- POSIX `case`
            /// accepts an empty pattern (`''` matches the empty word),
            /// and a multi-pattern arm may legitimately contain an
            /// empty alternative (`case x in ''|a) ...`). strtok would
            /// silently drop those, falling through to the default
            /// `*)` and producing a POSIX-violating result (issue #95).
            ///
            /// Use strchr-based splitting that emits empty tokens.
            const char *p = patterns;
            while (p && !matched) {
                const char *bar = find_case_alt_separator(p);
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
                    /// A zsh numeric-range pattern (<lo-hi>/<->) is
                    /// range-tested; anything else falls to the glob matcher
                    /// (#205).
                    bool is_range = false;
                    if (case_numeric_range_match(test_word, expanded_pattern,
                                                 &is_range)) {
                        matched = true;
                    } else if (!is_range && lush_shell_pattern_match(
                                                test_word, expanded_pattern)) {
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
            /// Execute commands for this case item. A case arm is a
            /// command list - run every statement sequentially. Do NOT
            /// short-circuit on a non-zero status (that was wrong: it
            /// dropped `exit $?` after a non-zero `do_status` in
            /// real_world/posix/101 init scripts, and silently dropped
            /// later statements in `x) echo a; false; echo b ;;`).
            /// Errexit (set -e) is enforced at execute_command_list,
            /// not here. Honor loop control / shell exit between
            /// statements so `break` / `continue` / `exit` propagate
            /// out of the arm without running trailing commands.
            node_t *commands = case_item->first_child;
            while (commands) {
                result = execute_node(executor, commands);
                if (executor->loop_control != LOOP_NORMAL ||
                    executor->shell_exit_requested || exit_flag) {
                    break;
                }
                commands = commands->next_sibling;
            }

            /// Handle terminator behavior
            switch (terminator) {
            case CASE_TERM_BREAK:
                /// ;; - stop processing case items
                done = true;
                execute_next = false;
                break;
            case CASE_TERM_FALLTHROUGH:
                /// ;& - execute next case item without testing pattern
                execute_next = true;
                break;
            case CASE_TERM_CONTINUE:
                /// ;;& - continue testing next patterns
                execute_next = false;
                break;
            }
        } else {
            execute_next = false;
        }

        case_item = case_item->next_sibling;
    }

    free(test_word);

    /// Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    /// Pop error context
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

    /// Get function body (can be NULL for empty function bodies)
    node_t *body = node->first_child;

    /// Extract parameter information from function name if encoded
    function_param_t *params = NULL;
    int param_count = 0;
    char *actual_function_name = function_name;

    /// Check if function name contains parameter encoding
    /// POSIX compliance: disable advanced parameter syntax in strict POSIX mode
    char *param_separator = strchr(function_name, '|');
    if (param_separator && !is_posix_mode_enabled()) {
        /// Extract actual function name
        size_t name_len = param_separator - function_name;
        actual_function_name = malloc(name_len + 1);
        strncpy(actual_function_name, function_name, name_len);
        actual_function_name[name_len] = '\0';

        /// Parse parameter information
        char *param_info = param_separator + 1;
        if (strncmp(param_info, "PARAMS{", 7) == 0) {
            char *param_list = param_info + 7;
            char *end_brace = strchr(param_list, '}');
            if (end_brace) {
                *end_brace = '\0'; /// Temporarily null-terminate

                /// Parse parameter list
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
                *end_brace = '}'; /// Restore original string
            }
        }
    }

    /// Optional homograph guard (off unless
    /// FEATURE_REJECT_MIXED_SCRIPT_IDENTS).
    if (executor_reject_mixed_script_ident(executor, actual_function_name,
                                           node->loc)) {
        if (actual_function_name != function_name) {
            free(actual_function_name);
        }
        return 1;
    }

    /// Store function in function table
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

    /// Clean up allocated function name if we created one
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

    /// Validate function parameters (errors already displayed structurally)
    if (validate_function_parameters(executor, func, argv, argc, loc) != 0) {
        return 1;
    }

    if (executor->debug) {
        printf("DEBUG: Calling function '%s' with %d args\n", function_name,
               argc - 1);
    }

    /// Create new scope for function
    if (symtable_push_scope(executor->symtable, SCOPE_FUNCTION,
                            function_name) != 0) {
        set_executor_error(executor, "Failed to create function scope");
        return 1;
    }

    /// Expose the currently-executing function's name as FUNCNAME for
    /// the body of the call (bash + zsh consensus; #185). The scope
    /// push above means this local automatically goes out of scope
    /// when the function returns, matching bash's "FUNCNAME is unset
    /// outside any function" behavior.
    (void)symtable_set_local_var(executor->symtable, "FUNCNAME", function_name);

    /// Set parameters (both positional and named)
    if (func->params) {
        /// Set named parameters with defaults
        int arg_index = 1; /// Skip function name at argv[0]
        function_param_t *param = func->params;

        while (param) {
            const char *value;
            if (arg_index < argc) {
                /// Use provided argument
                value = argv[arg_index++];
            } else {
                /// Use default value (already validated that required params
                /// are present)
                value = param->default_value ? param->default_value : "";
            }

            /// Set named parameter
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

    /// Set positional parameters ($1, $2, etc.) for backward compatibility
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

    /// Set $# (argument count)
    char argc_str[16];
    snprintf(argc_str, sizeof(argc_str), "%d", argc - 1);
    symtable_set_local_var(executor->symtable, "#", argc_str);

    /// No need to clear environment variables with new approach

    /// Push function context for error reporting
    source_location_t func_loc =
        func->body ? func->body->loc : SOURCE_LOC_UNKNOWN;
    executor_push_context(executor, func_loc, "in function '%s'",
                          function_name);

    /// Apply trailing redirections attached to the function definition (issue
    /// #48). The redirection nodes were appended as siblings of the body by
    /// parse_function_definition + parse_trailing_redirections (issue #43);
    /// copy_ast_chain pulled them along into func->body. Use a synthetic
    /// parent so setup_redirections can walk them via first_child like every
    /// other compound command. Save/restore fds so the redirection only
    /// applies for the duration of this call.
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

    /// Execute function body (handle multiple commands; skip redirection
    /// siblings, which were already applied above)
    int result = 0;
    node_t *command = func->body;
    while (command) {
        if (is_redirection_node(command)) {
            command = command->next_sibling;
            continue;
        }

        /// Bash-style DEBUG pseudo-signal: fires BEFORE each command in
        /// the function body. fire_debug_trap gates on functrace +
        /// function scope, so by default it stays silent inside
        /// functions and surfaces only when the user opts in.
        fire_debug_trap();

        result = execute_node(executor, command);

        /// Bash-style ERR pseudo-signal: fires on a non-zero exit
        /// inside the function body. fire_err_trap itself gates on
        /// errtrace + function scope so the trap is suppressed inside
        /// functions by default and surfaces only when the user has
        /// `set -o errtrace`.
        if (result != 0 && result < 200) {
            fire_err_trap();
        }

        /// Check if this is a function return (special code 200-455).
        /// bin_return encodes `return N` as 200 + (N & 0xFF), so the range is
        /// 200-455; a <= 255 bound here dropped every return value above 55,
        /// leaking the raw encoded code (return -1 -> 455 instead of 255).
        if (result >= 200 && result <= 455) {
            /// Extract the actual return value from the special code
            int actual_return = result - 200;

            /// Bash-style RETURN pseudo-signal: fires when a function
            /// returns via the `return` builtin. fire_return_trap gates
            /// on functrace; fires BEFORE we pop the scope so the trap
            /// runs in the function's frame.
            fire_return_trap();

            if (has_redirections) {
                restore_file_descriptors(&redir_state);
            }

            /// Pop function context before returning
            executor_pop_context(executor);

            /// Restore previous scope before returning
            symtable_pop_scope(executor->symtable);

            return actual_return;
        }

        /// set -e (errexit) aborts the function body on a failing command,
        /// exactly as it does at the top level. Without set -e the body
        /// continues past a non-zero command -- the POSIX/bash/zsh consensus
        /// (a non-zero command is not fatal); an unconditional break here made
        /// every function body abort on the first failure (#512).
        if (result != 0 && shell_opts.exit_on_error) {
            break;
        }
        command = command->next_sibling;
    }

    /// Bash-style RETURN pseudo-signal: fires when a function returns
    /// by falling off the end of its body (no explicit `return`).
    /// Fires BEFORE we pop the scope so the trap runs in the
    /// function's frame.
    fire_return_trap();

    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    /// Pop function context
    executor_pop_context(executor);

    /// Restore previous scope
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
    (void)argv; /// Reserved for argument type validation
    (void)argc; /// Reserved for arity checking
    if (!func) {
        return 1;
    }

    /// POSIX compliance: disable parameter validation in strict POSIX mode
    if (is_posix_mode_enabled()) {
        return 0;
    }

    /// If no parameters defined, allow any arguments (backward compatibility)
    if (!func->params) {
        return 0;
    }

    int arg_index = 1; /// Skip function name at argv[0]
    function_param_t *param = func->params;

    while (param) {
        if (arg_index < argc) {
            /// Argument provided for this parameter
            arg_index++;
        } else if (param->is_required) {
            /// Required parameter missing
            executor_error_report(executor, SHELL_ERR_FUNCTION_ERROR, loc,
                                  "function '%s' requires parameter '%s'",
                                  func->name, param->name);
            return 1;
        }
        /// Optional parameter without argument - will use default
        param = param->next;
    }

    /// Check for too many arguments
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

    /// NFC-normalize the lookup name so it matches whatever store_function
    /// canonicalized on the write side.
    char *canon = lush_ident_canonicalize_alloc(function_name);
    if (!canon) {
        return NULL;
    }

    function_def_t *func = executor->functions;
    while (func) {
        if (strcmp(func->name, canon) == 0) {
            free(canon);
            return func;
        }
        func = func->next;
    }
    free(canon);
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

    /// NFC-normalize on the write side so the function table is keyed
    /// by canonical form, matching find_function's lookup behavior.
    char *canon = lush_ident_canonicalize_alloc(function_name);
    if (!canon) {
        return 1;
    }
    function_name = canon;

    /// Check if function already exists and remove it
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

    /// Create new function definition
    function_def_t *new_func = malloc(sizeof(function_def_t));
    if (!new_func) {
        free(canon);
        return 1;
    }

    new_func->name = strdup(function_name);
    if (!new_func->name) {
        free(new_func);
        free(canon);
        return 1;
    }

    /// Create a deep copy of the body AST (including sibling chain)
    /// Allow NULL bodies for empty functions
    new_func->body = copy_ast_chain(body);
    if (!new_func->body && body != NULL) {
        /// Only fail if body was non-NULL but copy failed
        free(new_func->name);
        free(new_func);
        free(canon);
        return 1;
    }

    /// Store parameter information
    new_func->params = params;
    new_func->param_count = param_count;

    /// Add to front of function list
    new_func->next = executor->functions;
    executor->functions = new_func;

    free(canon);
    return 0;
}

/**
 * @brief Deep-copy a single AST node and its children (the canonical node copy)
 *
 * Creates a deep copy of an AST node including all children. Does not copy
 * siblings - use copy_ast_chain for a sibling chain.
 *
 * This is the single AST deep-copy path. It replaced two near-identical
 * walkers (the former copy_ast_node and copy_node_simple) that had drifted:
 * one guarded the val union on val_type and the other did not, and neither
 * carried node->loc. Consolidating removes that duplicate-logic hazard -- a
 * field added to node_t is now handled in exactly one place.
 *
 * @param node Node to copy
 * @return Deep copy of node, or NULL on failure
 */
node_t *node_copy(node_t *node) {
    if (!node) {
        return NULL;
    }

    node_t *copy = new_node(node->type);
    if (!copy) {
        return NULL;
    }

    /// Copy the type-tagged payload. Copy the whole union by value first (so a
    /// non-string val -- number, char -- is carried), then deep-copy the string
    /// when VAL_STR owns one. Guarding on val_type is mandatory: the union
    /// overlaps a long double, so testing val.str without it can strdup the
    /// bytes of a non-pointer member.
    copy->val_type = node->val_type;
    copy->val = node->val;
    if (node->val_type == VAL_STR && node->val.str) {
        copy->val.str = strdup(node->val.str);
        if (!copy->val.str) {
            free_node_tree(copy);
            return NULL;
        }
    }

    /// Source location must survive the copy: a function body is deep-copied at
    /// definition time, and dropping loc leaves errors raised inside the body
    /// with no location.
    copy->loc = node->loc;

    copy->glob_qualified = node->glob_qualified;
    copy->name_quoted = node->name_quoted;

    /// The assignment-tilde provenance value must survive a deep copy (a
    /// function body is copied at definition time); otherwise a mixed-quote
    /// declaration inside a function loses it and stops expanding (#488).
    if (node->magic_equal_value) {
        copy->magic_equal_value = strdup(node->magic_equal_value);
        if (!copy->magic_equal_value) {
            free_node_tree(copy);
            return NULL;
        }
    }

    /// Same node-copy-completeness rule for the per-character quote map (#498).
    if (node->quote_prov) {
        copy->quote_prov = strdup(node->quote_prov);
        if (!copy->quote_prov) {
            free_node_tree(copy);
            return NULL;
        }
    }

    /// Deep-copy the dual-carried Word CST representation, handled here in the
    /// one canonical walker -- copy-safe by construction (a single owned field,
    /// a single copy site). NULL for every node until a later step populates
    /// it.
    if (node->word) {
        copy->word = word_copy(node->word);
        if (!copy->word) {
            free_node_tree(copy);
            return NULL;
        }
    }

    /// Copy children (recursively). Siblings are not copied here.
    node_t *child = node->first_child;
    while (child) {
        node_t *child_copy = node_copy(child);
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

    node_t *first_copy = node_copy(node);
    if (!first_copy) {
        return NULL;
    }

    node_t *current_copy = first_copy;
    node_t *current_orig = node->next_sibling;

    while (current_orig) {
        node_t *sibling_copy = node_copy(current_orig);
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

    /// ${var@Q} produces a quoted representation safe to re-eval.
    /// bash uses two output forms:
    ///   - For printable strings without control chars: 'content'
    ///     with embedded single quotes escaped as '\'' (close quote,
    ///     literal '\'', reopen quote).
    ///   - For strings containing control chars (\n, \t, etc): $'...'
    ///     ANSI-C quoting with \n / \t / \xNN escapes.
    /// Match bash's choice so diff_oracle can byte-compare against
    /// the corpus. Issue #102.
    bool has_control = false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 32) {
            has_control = true;
            break;
        }
    }

    if (has_control) {
        /// $'...' ANSI-C form for strings with control chars.
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
        /// Single-quoted form with bash's close-escape-reopen idiom
        /// for embedded single quotes. Each `'` in str becomes
        /// `'\''`: close the open quote (`'`), emit a literal-quoted
        /// single quote (`\'`), then reopen (`'`). For a string
        /// with no embedded quotes this collapses to the simple
        /// `'content'` form. Each `'` worst-case expands to 4 chars,
        /// so worst-case output size is len*4 + 3.
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
                /// Emit `'\''`: close, escape, reopen.
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

    /// Quote the value
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
    size_t result_size = len * 4 + 256; /// Allow for expansion
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
                /// Username
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
                /// Hostname (short - up to first dot)
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
                /// Hostname (full)
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
                /// Current working directory
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd))) {
                    /// Replace home dir with ~
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
                /// Basename of current working directory
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
                /// $ or # based on UID
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
                /// Unknown escape, keep as-is
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

    /// Get variable flags
    symvar_flags_t flags = symtable_get_flags(mgr, name);

    /// Bash ${var@a} attribute-string ordering: bash emits `irx` form
    /// (integer, readonly, exported) and 'a' / 'A' for indexed /
    /// associative arrays. Order matters only for stylistic match
    /// with bash output; bash's actual order is by attribute introduction
    /// date. Issue #102.
    if (flags & SYMVAR_INTEGER_ATTR) {
        attrs[idx++] = 'i';
    }
    if (flags & SYMVAR_READONLY) {
        attrs[idx++] = 'r';
    }
    if (flags & SYMVAR_EXPORTED) {
        attrs[idx++] = 'x';
    }

    /// Check if it's an array
    if (symtable_is_array(name)) {
        array_value_t *arr = symtable_get_array(name);
        if (arr && arr->is_associative) {
            attrs[idx++] = 'A';
        } else {
            attrs[idx++] = 'a';
        }
    }

    /// Check for nameref
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
char *expand_variables_in_string(executor_t *executor, const char *str) {
    if (!str || !executor) {
        return strdup("");
    }

    size_t len = strlen(str);
    char *result = malloc(len * 2 + 1); /// Start with double size
    if (!result) {
        return strdup("");
    }

    size_t result_pos = 0;
    size_t result_size = len * 2 + 1;

    for (size_t i = 0; i < len; i++) {
        if (str[i] == '$') {
            /// Check for arithmetic expansion $((...)
            if (i + 2 < len && str[i + 1] == '(' && str[i + 2] == '(') {
                /// $(( is ambiguous: arithmetic expansion or command
                /// substitution of an anonymous function `$(() {...})`.
                /// Same disambiguation rule as the tokenizer (issue #99):
                /// if the lookahead from after $(( finds `{`, `}`, `;`,
                /// or `\n` before matched `))`, the input is command
                /// substitution and must be routed through the next-
                /// branch's $(...) handler instead. Walk the lookahead;
                /// if it doesn't pass the arithmetic shape check, fall
                /// through to the $(...) handler below.
                bool looks_arith =
                    lush_dollar_paren_is_arithmetic(str + i + 3, len - (i + 3));
                if (!looks_arith) {
                    /// Re-route into the $(...) command-sub handler at
                    /// the next branch (else-if on str[i + 1] == '('),
                    /// which fires when str[i+2] != '(' OR when we
                    /// intentionally skip the arithmetic path. To
                    /// trigger it cleanly, just fall through to the
                    /// next condition test by NOT entering the arith
                    /// block. The post-block `i = arith_end - 1`
                    /// advancement is skipped because we don't `continue`
                    /// here.
                    goto try_cmd_sub_path;
                }
                /// This is arithmetic expansion $((expr))
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
                    /// Extract arithmetic expression including $(( and ))
                    size_t full_arith_len = arith_end - arith_start;
                    char *full_arith_expr = malloc(full_arith_len + 1);
                    if (full_arith_expr) {
                        strncpy(full_arith_expr, &str[arith_start],
                                full_arith_len);
                        full_arith_expr[full_arith_len] = '\0';

                        /// Expand arithmetic expression
                        char *arith_result =
                            expand_arithmetic(executor, full_arith_expr);
                        if (arith_result) {
                            size_t result_len = strlen(arith_result);

                            /// Ensure buffer is large enough
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

                            /// Copy arithmetic result
                            strcpy(&result[result_pos], arith_result);
                            result_pos += result_len;
                            free(arith_result);
                        }
                        free(full_arith_expr);
                    }

                    i = arith_end - 1; /// Skip past the entire $((...)
                    continue;
                }
            }
            /// Check for command substitution $(...)
            else if (i + 1 < len && str[i + 1] == '(') {
            try_cmd_sub_path:;
                /// Find matching closing parenthesis via the canonical
                /// brace matcher (quote-/escape-aware, codepoint-aware).
                const char *temp_str =
                    &str[i + 1]; /// Start from the opening parenthesis
                size_t brace_offset = 0;
                if (lush_find_matching_brace(temp_str, 0, &brace_offset)) {
                    /// Extract command from $(...)
                    size_t cmd_len =
                        brace_offset - 1; /// Exclude the closing paren
                    char *command = malloc(cmd_len + 1);
                    if (command) {
                        strncpy(command, &str[i + 2], cmd_len); /// Skip $(
                        command[cmd_len] = '\0';

                        /// Execute command substitution - need to wrap in $()
                        /// format
                        char *wrapped_cmd =
                            malloc(cmd_len + 4); /// +3 for $() +1 for null
                        if (wrapped_cmd) {
                            snprintf(wrapped_cmd, cmd_len + 4, "$(%s)",
                                     command);
                            char *cmd_result = expand_command_substitution(
                                executor, wrapped_cmd);
                            free(wrapped_cmd);
                            if (cmd_result) {
                                size_t value_len = strlen(cmd_result);

                                /// Ensure buffer is large enough
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
                        i = i + 1 +
                            brace_offset; /// Skip past the entire $(...)
                        continue;
                    }
                }
            }

            /// Find variable name
            size_t var_start = i + 1;
            size_t var_end = var_start;

            /// Handle ${var} format
            if (var_start < len && str[var_start] == '{') {
                /// Use proper brace matching for nested expressions via
                /// the canonical brace matcher.
                const char *brace_str = &str[var_start];
                size_t brace_len = 0;
                if (lush_find_matching_brace(brace_str, 0, &brace_len)) {
                    /// brace_len is the index of the closing brace
                    var_end = var_start + brace_len +
                              1; /// Point to after closing brace
                } else {
                    /// Fallback: find closing brace manually with nesting
                    /// support
                    int brace_count = 1;
                    var_end = var_start + 1; /// Start after opening {

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
                /// Handle $var format
                /// Check for special single-character variables first
                if (var_end < len &&
                    (str[var_end] == '?' || str[var_end] == '$' ||
                     str[var_end] == '#' || str[var_end] == '*' ||
                     str[var_end] == '@' || str[var_end] == '!' ||
                     str[var_end] == '-' ||
                     (str[var_end] >= '0' && str[var_end] <= '9'))) {
                    var_end++; /// Single character special variable
                } else {
                    /// Regular variable names. lush_ident_match_continue
                    /// honors FEATURE_UNICODE_IDENTIFIERS, so multibyte
                    /// codepoints stay attached to the name in lush mode.
                    while (var_end < len) {
                        size_t n = lush_ident_match_continue(str + var_end,
                                                             len - var_end);
                        if (n == 0) {
                            break;
                        }
                        var_end += n;
                    }
                    /// Zsh bare-subscript form: $var[N] / $var[N,M].
                    /// Consume the bracket span so var_expr becomes
                    /// "$var[N]" rather than "$var" + literal "[N]".
                    /// Gated on FEATURE_ZSH_BARE_SUBSCRIPT — bash mode
                    /// keeps the literal-[N]-after-$var semantic.
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
                /// Extract and expand variable
                size_t var_len = var_end - i;
                char *var_expr = malloc(var_len + 1);
                if (var_expr) {
                    strncpy(var_expr, &str[i], var_len);
                    var_expr[var_len] = '\0';

                    char *var_value = expand_variable(executor, var_expr);
                    if (var_value) {
                        size_t value_len = strlen(var_value);

                        /// Ensure buffer is large enough
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
                    i = var_end - 1; /// Skip past variable
                    continue;
                }
            }
        }

        /// Regular character - ensure buffer space
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
        if (p[0] == '$' && p[1] == '{') {
            /// Skip a nested ${...} so an operator inside it is not
            /// mistaken for this expansion's operator -- the inner ':'
            /// of ${${p:t}:r} must not hide the outer ':r'.
            int depth = 1;
            p += 2;
            while (*p && depth > 0) {
                if (*p == '{') {
                    depth++;
                } else if (*p == '}') {
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

/// zsh `:h` (head / dirname): everything up to the last '/'. No slash
/// yields "." (a relative name has no directory part).
static char *zsh_mod_head(const char *v) {
    const char *slash = strrchr(v, '/');
    if (!slash) {
        return strdup(".");
    }
    if (slash == v) {
        return strdup("/"); /// "/foo" -> "/"
    }
    size_t n = (size_t)(slash - v);
    char *r = malloc(n + 1);
    if (r) {
        memcpy(r, v, n);
        r[n] = '\0';
    }
    return r;
}

/// zsh `:t` (tail / basename): everything after the last '/'.
static char *zsh_mod_tail(const char *v) {
    const char *slash = strrchr(v, '/');
    return strdup(slash ? slash + 1 : v);
}

/// Locate the extension dot in the tail component: the last '.' that is
/// after the last '/' and not the leading character of the tail (so a
/// dotfile like ".bashrc" has no extension). Returns NULL when none.
static const char *zsh_ext_dot(const char *v) {
    const char *slash = strrchr(v, '/');
    const char *base = slash ? slash + 1 : v;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) {
        return NULL;
    }
    return dot;
}

/// zsh `:r` (root): strip the last extension (".gz" off "x.tar.gz").
static char *zsh_mod_root(const char *v) {
    const char *dot = zsh_ext_dot(v);
    if (!dot) {
        return strdup(v);
    }
    size_t n = (size_t)(dot - v);
    char *r = malloc(n + 1);
    if (r) {
        memcpy(r, v, n);
        r[n] = '\0';
    }
    return r;
}

/// zsh `:e` (extension): the text after the last extension dot, or "".
static char *zsh_mod_ext(const char *v) {
    const char *dot = zsh_ext_dot(v);
    return strdup(dot ? dot + 1 : "");
}

/// zsh `:q` (quote): backslash-escape characters that are not safe bare,
/// so the result re-evaluates to the original (zsh emits "Hello\ World",
/// not the single-quoted form bash's @Q uses). UTF-8 continuation bytes
/// (>= 0x80) are left intact so multibyte characters are not mangled.
static char *zsh_mod_quote(const char *v) {
    size_t len = strlen(v);
    char *r = malloc(len * 2 + 1);
    if (!r) {
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)v[i];
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '/' || c == '.' ||
                    c == '_' || c == '-' || c == '+' || c == ',' || c >= 0x80;
        if (!safe) {
            r[j++] = '\\';
        }
        r[j++] = (char)c;
    }
    r[j] = '\0';
    return r;
}

/// Apply a zsh modifier chain (the argument after the first ':' in
/// ${var:...}) to @p value. Supports h/t/r/e (path), l/u (case),
/// q (quote), and s/old/new/ plus gs/old/new/ (substitute, optional g
/// for global), separated or run together (e.g. "t:r" or "h:h").
/// Returns a newly malloc'd string; the caller frees. Unknown modifier
/// letters are skipped (zsh would error; we degrade gracefully).
static char *apply_zsh_modifiers(executor_t *executor, const char *value,
                                 const char *chain) {
    (void)executor;
    char *cur = strdup(value ? value : "");
    if (!cur) {
        return NULL;
    }
    const char *p = chain;
    while (p && *p) {
        if (*p == ':') {
            p++;
            continue;
        }
        char m = *p;
        char *next = NULL;
        if (m == 'h') {
            next = zsh_mod_head(cur);
            p++;
        } else if (m == 't') {
            next = zsh_mod_tail(cur);
            p++;
        } else if (m == 'r') {
            next = zsh_mod_root(cur);
            p++;
        } else if (m == 'e') {
            next = zsh_mod_ext(cur);
            p++;
        } else if (m == 'l') {
            next = lush_case_all_lower(cur);
            p++;
        } else if (m == 'u') {
            next = lush_case_all_upper(cur);
            p++;
        } else if (m == 'q') {
            next = zsh_mod_quote(cur);
            p++;
        } else if (m == 's' || (m == 'g' && p[1] == 's')) {
            bool global = (m == 'g');
            if (global) {
                p++; /// past 'g'
            }
            p++; /// past 's'
            if (!*p) {
                break; /// malformed: no delimiter
            }
            char delim = *p++;
            const char *old_start = p;
            while (*p && *p != delim) {
                p++;
            }
            if (*p != delim) {
                break; /// malformed: unterminated old
            }
            size_t old_len = (size_t)(p - old_start);
            p++; /// past middle delim
            const char *new_start = p;
            while (*p && *p != delim) {
                p++;
            }
            size_t new_len = (size_t)(p - new_start);
            if (*p == delim) {
                p++; /// optional trailing delim
            }
            char *oldp = malloc(old_len + 1);
            char *newp = malloc(new_len + 1);
            if (oldp && newp) {
                memcpy(oldp, old_start, old_len);
                oldp[old_len] = '\0';
                memcpy(newp, new_start, new_len);
                newp[new_len] = '\0';
                next = lush_pattern_substitute(cur, oldp, newp, global);
            }
            free(oldp);
            free(newp);
        } else {
            /// Unknown modifier letter: skip it.
            p++;
            continue;
        }
        if (next) {
            free(cur);
            cur = next;
        }
    }
    return cur;
}

/// True if the argument after ':' in ${var:...} is a zsh modifier chain
/// (leads with a modifier letter) rather than a substring offset (which
/// begins with a digit, sign, space, or arithmetic paren).
static bool looks_like_zsh_modifier(const char *arg) {
    if (!arg) {
        return false;
    }
    switch (arg[0]) {
    case 'h':
    case 't':
    case 'r':
    case 'e':
    case 'l':
    case 'u':
    case 'q':
    case 's':
    case 'g':
        return true;
    default:
        return false;
    }
}

/* ============================================================================
 * Param-expansion helpers: space-separated word lists
 * --------------------------------------------------------------------------
 * The zsh `(o)` / `(O)` / `(u)` parameter-expansion flags all reduce to the
 * same shape -- count words, allocate an array, strdup-split on space,
 * transform the array (sort / dedup), rejoin with single spaces. These
 * helpers centralize that pattern so the three call sites don't drift apart.
 * ========================================================================== */

/**
 * @brief Split `text` on single-space runs into a heap array of strdup'd
 *        word strings. *out_count receives the number of words. Caller
 *        frees each entry plus the array (use words_free).
 *
 * Returns NULL on NULL input, empty input, or allocation failure.
 */
static char **words_split_on_space(const char *text, size_t *out_count) {
    *out_count = 0;
    if (!text || !*text) {
        return NULL;
    }
    size_t word_count = 0;
    bool in_word = false;
    for (const char *c = text; *c; c++) {
        if (*c == ' ') {
            in_word = false;
        } else if (!in_word) {
            word_count++;
            in_word = true;
        }
    }
    if (word_count == 0) {
        return NULL;
    }
    char **words = malloc(word_count * sizeof(char *));
    if (!words) {
        return NULL;
    }
    char *copy = strdup(text);
    if (!copy) {
        free(words);
        return NULL;
    }
    size_t idx = 0;
    char *tok = strtok(copy, " ");
    while (tok && idx < word_count) {
        words[idx] = strdup(tok);
        if (!words[idx]) {
            for (size_t i = 0; i < idx; i++) {
                free(words[i]);
            }
            free(words);
            free(copy);
            return NULL;
        }
        idx++;
        tok = strtok(NULL, " ");
    }
    free(copy);
    *out_count = idx;
    return words;
}

/**
 * @brief Rejoin `words[0..count)` with single-space separators into a
 *        fresh malloc'd NUL-terminated string. NULL on OOM or count==0.
 */
static char *words_join_on_space(char **words, size_t count) {
    if (!words || count == 0) {
        return NULL;
    }
    size_t total_len = 0;
    for (size_t i = 0; i < count; i++) {
        total_len += strlen(words[i]) + 1;
    }
    char *out = malloc(total_len + 1);
    if (!out) {
        return NULL;
    }
    out[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            strcat(out, " ");
        }
        strcat(out, words[i]);
    }
    return out;
}

/**
 * @brief Free the array returned by words_split_on_space (NULL-safe).
 */
static void words_free(char **words, size_t count) {
    if (!words) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(words[i]);
    }
    free(words);
}

/// qsort comparator: strcmp ascending.
static int cmp_str_asc(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/// qsort comparator: strcmp descending.
static int cmp_str_desc(const void *a, const void *b) {
    return strcmp(*(const char *const *)b, *(const char *const *)a);
}

static char *parse_parameter_expansion(executor_t *executor,
                                       const char *expansion,
                                       bool in_double_quotes) {
    if (!expansion) {
        return strdup("");
    }

    /// zsh `${+NAME}` is-set test: returns "1" if NAME is bound, "0"
    /// otherwise. Used heavily in arithmetic contexts -- `(( ${+v} ))`
    /// is the idiomatic zsh test for variable presence. Real-world
    /// corpus example: `(( ${+commands[dircolors]} ))` checks whether
    /// `dircolors` is on $path via zsh's special `$commands` hash.
    ///
    /// Lush handles the plain `${+NAME}` form by walking the symbol
    /// table, and the `${+commands[NAME]}` shape as a PATH lookup
    /// (since lush has no `$commands` hash but the user-observable
    /// semantic is "is this command available?").
    if (expansion[0] == '+' && expansion[1] != '\0') {
        const char *name = expansion + 1;
        /// Special-case zsh's $commands[X] -- treat as a PATH lookup.
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
        /// Plain ${+NAME}: ignore any trailing [subscript] for now and
        /// probe the symbol table for the base name.
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

    /// Handle zsh-style parameter flags: ${(X)var}
    /// Flags: U=uppercase, L=lowercase, C=capitalize, f=split on newlines,
    ///        o=sort ascending, O=sort descending, k=keys, v=values
    ///        j:X:=join with X, s:X:=split on X
    if (expansion[0] == '(') {
        const char *close_paren = strchr(expansion, ')');
        if (close_paren && close_paren > expansion + 1) {
            /// Extract flags between ( and )
            size_t flags_len = close_paren - expansion - 1;
            char *flags = malloc(flags_len + 1);
            if (!flags) {
                return strdup("");
            }
            strncpy(flags, expansion + 1, flags_len);
            flags[flags_len] = '\0';

            /// Rest of expansion after )
            const char *rest = close_paren + 1;

            /// Check for 'k' / 'v' flags (return keys / values for arrays).
            /// The combination 'kv' (or 'vk') asks for interleaved key/value
            /// pairs and must be detected before the keys-only branch — see
            /// the (want_keys && want_values) handler below.
            bool want_keys = (strchr(flags, 'k') != NULL);
            bool want_values = (strchr(flags, 'v') != NULL);

            char *inner_result = NULL;

            if (want_keys && want_values) {
                /// Handle (kv)/(vk): emit interleaved "k1 v1 k2 v2 ..."
                /// Both symtable_array_get_keys() and
                /// symtable_array_get_values() iterate the same source data
                /// (hashtable for assoc, indices array for indexed) in the
                /// same order, so keys[i] pairs with values[i].
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
                        /// (kv) on a non-collection is a type mismatch.
                        /// Distinguish unset (silent empty) from scalar
                        /// (error) so unset-by-omission stays a non-event
                        /// while genuine misapplication on a scalar
                        /// surfaces as a runtime error.
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
                    if (array) {
                        /// A collection reaching this scalar backend is the
                        /// SEMANTICS section 3.9 list-in-scalar crossing:
                        /// strict E1134 in lush mode, oracle flatten (into
                        /// `sep`) in the compat modes. (j) short-circuits.
                        char form[288];
                        snprintf(form, sizeof(form), "${(%s)%s}", flags,
                                 arr_name);
                        char sep[2] = {' ', '\0'};
                        if (!section39_flag_scalar_gate(executor, flags,
                                                        array->is_associative,
                                                        form, sep)) {
                            free(arr_name);
                            free(flags);
                            return strdup("");
                        }
                        if (shell_mode_get() == SHELL_MODE_ZSH &&
                            !array->is_associative) {
                            /// zsh-mode special case: ${(kv)indexed_array}
                            /// emits values only — zsh treats indexed arrays
                            /// as having no meaningful "keys" so (kv) collapses
                            /// to (v). lush mode keeps the interleaved
                            /// indices+values form (curated pick: internally
                            /// consistent with lush's (k)/(v) semantics).
                            inner_result = symtable_array_expand(array, sep);
                        } else {
                            size_t kc = 0, vc = 0;
                            char **keys = symtable_array_get_keys(array, &kc);
                            char **values =
                                symtable_array_get_values(array, &vc);
                            size_t pairs = (kc < vc) ? kc : vc;
                            if (keys && values && pairs > 0) {
                                size_t total_len = 0;
                                for (size_t i = 0; i < pairs; i++) {
                                    total_len += strlen(keys[i]) + 1 +
                                                 strlen(values[i]) + 1;
                                }
                                inner_result = malloc(total_len + 1);
                                if (inner_result) {
                                    inner_result[0] = '\0';
                                    for (size_t i = 0; i < pairs; i++) {
                                        if (i > 0)
                                            strcat(inner_result, sep);
                                        strcat(inner_result, keys[i]);
                                        strcat(inner_result, sep);
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
                    }
                    free(arr_name);
                }
                if (!inner_result) {
                    inner_result = strdup("");
                }
            } else if (want_keys) {
                /// Handle (k) flag: return array keys instead of values
                /// Parse array name from rest (e.g., "arr[@]" -> "arr")
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
                        /// (k) on a non-collection is a type mismatch.
                        /// See the (kv) branch above for the unset vs
                        /// scalar distinction.
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
                    if (array) {
                        /// A collection reaching this scalar backend is the
                        /// SEMANTICS section 3.9 list-in-scalar crossing:
                        /// strict E1134 in lush mode, oracle flatten (into
                        /// `sep`) in the compat modes.
                        char form[288];
                        snprintf(form, sizeof(form), "${(%s)%s}", flags,
                                 arr_name);
                        char sep[2] = {' ', '\0'};
                        if (!section39_flag_scalar_gate(executor, flags,
                                                        array->is_associative,
                                                        form, sep)) {
                            free(arr_name);
                            free(flags);
                            return strdup("");
                        }
                        if (shell_mode_get() == SHELL_MODE_ZSH &&
                            !array->is_associative) {
                            /// zsh-mode special case: ${(k)indexed_array} emits
                            /// values only (zsh treats indexed-array indices as
                            /// not meaningfully "keys" — `(k)` collapses to
                            /// `(v)`). lush mode keeps the existing 0-based
                            /// indices behavior (curated pick: more useful for
                            /// iteration / debugging than redundantly emitting
                            /// values which `(v)` and `${arr[@]}` already
                            /// give).
                            inner_result = symtable_array_expand(array, sep);
                        } else {
                            /// Get all keys (works for indexed and assoc).
                            size_t count;
                            char **keys =
                                symtable_array_get_keys(array, &count);
                            if (keys && count > 0) {
                                size_t total_len = 0;
                                for (size_t i = 0; i < count; i++) {
                                    total_len += strlen(keys[i]) + 1;
                                }
                                inner_result = malloc(total_len + 1);
                                if (inner_result) {
                                    inner_result[0] = '\0';
                                    for (size_t i = 0; i < count; i++) {
                                        if (i > 0)
                                            strcat(inner_result, sep);
                                        strcat(inner_result, keys[i]);
                                        free(keys[i]);
                                    }
                                }
                                free(keys);
                            }
                        }
                    }
                    free(arr_name);
                }
                if (!inner_result) {
                    inner_result = strdup("");
                }
            } else if (strchr(flags, 'w') != NULL && rest[0] == '#') {
                /// Handle (w)# - word count instead of character count
                /// Get the variable value first
                const char *var_name = rest + 1; /// Skip the #
                char *var_value =
                    parse_parameter_expansion(executor, var_name, false);
                if (var_value) {
                    /// Count words (space-separated)
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
                    /// Return word count as string
                    char count_buf[32];
                    snprintf(count_buf, sizeof(count_buf), "%zu", word_count);
                    inner_result = strdup(count_buf);
                    free(var_value);
                } else {
                    inner_result = strdup("0");
                }
            } else {
                /// Collection-operand resolution for the flag pipeline.
                ///
                /// Several flags ((U)(L)(C)(s)(j)(o)(O)(u)(f)) operate on
                /// the space-separated string form of a collection's
                /// elements. The default recursive call below (the older
                /// "Normal expansion" path) routes `arr[@]` through the
                /// general path that rejects list-in-scalar-slot at the
                /// subscript handler, so the flag never gets to do its
                /// work. Detect when `rest` is a collection reference
                /// (bare name, name[@], or name[*]) and extract elements
                /// directly via symtable_get_array, joining with " " --
                /// the format every flag handler in this loop already
                /// expects.
                ///
                /// Also catches collection-only flags ((j)(k)(v)) applied
                /// to a scalar and raises SHELL_ERR_TYPE_MISMATCH at
                /// that site rather than silently returning empty.
                char *arr_name = NULL;
                bool rest_is_simple_ref = false;
                const char *bracket = strchr(rest, '[');
                if (bracket) {
                    /// Allow name[@] and name[*] forms; reject name[N].
                    if ((bracket[1] == '@' || bracket[1] == '*') &&
                        bracket[2] == ']' && bracket[3] == '\0') {
                        arr_name = strndup(rest, (size_t)(bracket - rest));
                        rest_is_simple_ref = (arr_name != NULL);
                    }
                } else if (rest[0] && lush_is_valid_identifier(rest)) {
                    arr_name = strdup(rest);
                    rest_is_simple_ref = (arr_name != NULL);
                }

                bool has_collection_only_flag = strchr(flags, 'j') != NULL ||
                                                strchr(flags, 'k') != NULL ||
                                                strchr(flags, 'v') != NULL;

                array_value_t *array =
                    arr_name ? symtable_get_array(arr_name) : NULL;

                if (array) {
                    /// Collection operand: extract elements as a separator-
                    /// joined string. The flag handlers below iterate the
                    /// words and apply per-element semantics for
                    /// (U)(L)(C)(s)(o)(O)(u)(f), and (j:X:) joins them
                    /// with the user-specified delimiter for the explicit
                    /// list-to-scalar form.
                    ///
                    /// A VECTOR-PRODUCING flag ((v)/(@)/(s)) applied to a
                    /// collection here is the SEMANTICS section 3.9
                    /// list-in-scalar crossing (the vector-accepting slots are
                    /// handled earlier by try_expand_vector_arg): strict E1134
                    /// in lush mode, oracle flatten (into `sep`) in the compat
                    /// modes. The flag chars are read from the prefix before
                    /// any ':' separator so a (j:X:) / (s:X:) delimiter cannot
                    /// false-trigger; kind-preserving transforms ((U)/(L)/(C)/
                    /// (f)) and the sanctioned explicit (j) join keep the space
                    /// form and their existing per-word handling below.
                    size_t flag_prefix = strcspn(flags, ":");
                    bool vector_flag = false;
                    for (size_t fi = 0; fi < flag_prefix; fi++) {
                        char fc = flags[fi];
                        if (fc == 'v' || fc == '@' || fc == 's') {
                            vector_flag = true;
                            break;
                        }
                    }
                    char sep[2] = {' ', '\0'};
                    if (vector_flag) {
                        char form[288];
                        snprintf(form, sizeof(form), "${(%s)%s}", flags,
                                 arr_name);
                        if (!section39_flag_scalar_gate(executor, flags,
                                                        array->is_associative,
                                                        form, sep)) {
                            free(arr_name);
                            free(flags);
                            return strdup("");
                        }
                    }
                    inner_result = symtable_array_expand(array, sep);
                    if (!inner_result) {
                        inner_result = strdup("");
                    }
                    free(arr_name);
                } else if (rest_is_simple_ref && has_collection_only_flag) {
                    /// The name resolves (or doesn't) to something that
                    /// is not a collection, but the user wrote a flag
                    /// that only makes sense for one. Surface the
                    /// mismatch as a structured runtime error rather
                    /// than silently returning empty.
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
                    /// Existing path. `rest` is what comes after the
                    /// flag-paren group: a scalar variable, or a nested
                    /// full parameter expansion like ${(s/,/)${INNER}}
                    /// (the zsh idiom ${(flag)${INNER}} applies the
                    /// outer flag to the inner expansion's result).
                    free(arr_name);
                    if (rest[0] == '$' && rest[1] == '{') {
                        inner_result = expand_variable(executor, rest);
                    } else {
                        inner_result =
                            parse_parameter_expansion(executor, rest, false);
                    }
                }
            }

            if (!inner_result) {
                free(flags);
                return strdup("");
            }

            /// Process flags in order
            char *result = inner_result;
            const char *p = flags;

            while (*p) {
                char *new_result = NULL;

                switch (*p) {
                case 'P':
                    /// Indirect: treat the current value as the NAME of a
                    /// parameter and expand that parameter. ${(P)ref} yields
                    /// the value of the variable named by $ref, comparable to
                    /// bash ${!ref}.
                    new_result = symtable_get_var(executor->symtable, result);
                    if (result != inner_result) {
                        free(result);
                    }
                    result = new_result ? new_result : strdup("");
                    p++;
                    break;

                case 'U':
                    /// Uppercase all
                    new_result = lush_case_all_upper(result);
                    if (result != inner_result)
                        free(result);
                    result = new_result ? new_result : strdup("");
                    p++;
                    break;

                case 'L':
                    /// Lowercase all
                    new_result = lush_case_all_lower(result);
                    if (result != inner_result)
                        free(result);
                    result = new_result ? new_result : strdup("");
                    p++;
                    break;

                case 'C':
                    /// Capitalize each word
                    new_result = lush_case_capitalize_words(result);
                    if (result != inner_result)
                        free(result);
                    result = new_result ? new_result : strdup("");
                    p++;
                    break;

                case 'f':
                    /// Split on newlines - for now, replace newlines with
                    /// spaces (full array support would require different
                    /// return type)
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
                    /// Join with separator: j<DELIM>X<DELIM>
                    /// zsh accepts any non-')' character after 'j' as the
                    /// delimiter; the same character closes the argument.
                    char delim = p[1];
                    if (delim && delim != ')') {
                        /// Find closing delim
                        const char *sep_start = p + 2;
                        const char *sep_end = strchr(sep_start, delim);
                        if (sep_end) {
                            size_t sep_len = sep_end - sep_start;
                            char *sep = malloc(sep_len + 1);
                            if (sep) {
                                strncpy(sep, sep_start, sep_len);
                                sep[sep_len] = '\0';

                                /// Replace spaces with separator
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
                            p++; /// Malformed, skip
                        }
                    } else {
                        p++; /// No separator specified
                    }
                    break;
                }

                case 's': {
                    /// Split on separator: s<DELIM>X<DELIM> - replace X with
                    /// space. zsh accepts any non-')' character after 's' as
                    /// the delimiter; the same character closes the argument.
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

                                /// Replace separator with space
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

                case 'o': {
                    /// Sort ascending via the shared word-list helpers.
                    size_t count = 0;
                    char **words = words_split_on_space(result, &count);
                    if (words && count > 1) {
                        qsort(words, count, sizeof(char *), cmp_str_asc);
                        new_result = words_join_on_space(words, count);
                        if (new_result) {
                            if (result != inner_result) {
                                free(result);
                            }
                            result = new_result;
                        }
                    }
                    words_free(words, count);
                    p++;
                    break;
                }

                case 'O': {
                    /// Sort descending via the shared word-list helpers.
                    size_t count = 0;
                    char **words = words_split_on_space(result, &count);
                    if (words && count > 1) {
                        qsort(words, count, sizeof(char *), cmp_str_desc);
                        new_result = words_join_on_space(words, count);
                        if (new_result) {
                            if (result != inner_result) {
                                free(result);
                            }
                            result = new_result;
                        }
                    }
                    words_free(words, count);
                    p++;
                    break;
                }

                case 'k':
                    /// Keys flag - already handled before inner expansion
                    p++;
                    break;

                case 'v':
                    /// Values flag - no-op (values are the default)
                    p++;
                    break;

                case 'u': {
                    /// Unique: drop ALL duplicates (not just adjacent).
                    /// zsh (u) preserves first-seen order. Combine with
                    /// (o) or (O) for sort+unique. Issue #103. The
                    /// O(N^2) dedup is fine for typical zsh array sizes;
                    /// a hash set would be premature.
                    size_t count = 0;
                    char **words = words_split_on_space(result, &count);
                    if (words && count > 1) {
                        size_t unique_count = 0;
                        for (size_t i = 0; i < count; i++) {
                            bool seen = false;
                            for (size_t k = 0; k < unique_count; k++) {
                                if (strcmp(words[k], words[i]) == 0) {
                                    seen = true;
                                    break;
                                }
                            }
                            if (seen) {
                                free(words[i]);
                                words[i] = NULL;
                            } else if (unique_count != i) {
                                words[unique_count++] = words[i];
                                words[i] = NULL;
                            } else {
                                unique_count++;
                            }
                        }
                        new_result = words_join_on_space(words, unique_count);
                        if (new_result) {
                            if (result != inner_result) {
                                free(result);
                            }
                            result = new_result;
                        }
                        /// Free any remaining entries (in case unique_count
                        /// < count and we left holes -- but the loop above
                        /// already freed dropped entries).
                        for (size_t i = unique_count; i < count; i++) {
                            free(words[i]);
                            words[i] = NULL;
                        }
                        free(words);
                    } else {
                        words_free(words, count);
                    }
                    p++;
                    break;
                }

                case 'l':
                case 'r': {
                    /// Padding flags. zsh syntax:
                    ///   (l:N:)              -- left-pad to width N w/ spaces
                    ///   (l:N::FILL:)        -- left-pad with FILL string
                    ///   (r:N:) / (r:N::FILL:) -- right-pad analogously
                    /// If the value is wider than N, zsh truncates the
                    /// value to N chars (left-pad keeps the rightmost
                    /// N; right-pad keeps the leftmost N). The N
                    /// argument is bracketed by `:` chars (zsh accepts
                    /// any non-`)` delimiter; we accept `:` to match
                    /// the common form the corpus uses). Issue #103.
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

                    /// Optional fill: another :FILL: after the width.
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

                    /// Advance p past the entire (l:N::FILL:) span,
                    /// up to but not including the closing `)` of the
                    /// flag group -- the outer while loop is iterating
                    /// `flags` which is the content between `(` and `)`
                    /// already, so `closing` is the position right
                    /// after the trailing `:`.
                    p = closing;

                    const char *fill_str = (fill && fill[0]) ? fill : " ";
                    size_t fill_len = strlen(fill_str);
                    size_t result_len = strlen(result);

                    if (width <= 0) {
                        free(fill);
                        break;
                    }
                    if ((int)result_len >= width) {
                        /// Truncate. For left-pad, keep last N chars;
                        /// for right-pad, keep first N chars.
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
                    /// (Q) flag: strip one level of quoting from the
                    /// value. zsh accepts `'a b c'` -> `a b c` and
                    /// `"a b c"` -> `a b c`. If the value isn't wrapped
                    /// in matching quotes, return unchanged.
                    /// Issue #103.
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
                    /// Quote-family flags (issue #103):
                    ///   (q)   -- backslash-escape shell metacharacters
                    ///   (qq)  -- single-quote the entire value
                    ///   (qqq) -- double-quote the entire value
                    /// Count consecutive 'q' chars to pick the variant.
                    int q_count = 0;
                    while (p[q_count] == 'q') {
                        q_count++;
                    }

                    size_t result_len = strlen(result);
                    if (q_count == 2) {
                        /// Single-quote: wrap with ' and escape any
                        /// embedded ' using bash's '\\'' idiom (zsh
                        /// accepts the same form).
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
                        /// Double-quote: wrap with " and escape `"` `$`
                        /// `` ` `` `\` chars.
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
                        /// (q): backslash-escape shell-meta chars. zsh's
                        /// (q) escapes characters that would be
                        /// special in any shell context -- space, tab,
                        /// newline, and shell metacharacters
                        /// (; & | < > ( ) { } [ ] $ ` " ' \ * ? ~ # !
                        /// = % ^).
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
                    /// Unknown flag, skip
                    p++;
                    break;
                }
            }

            free(flags);
            if (result == inner_result) {
                return result; /// No transformation applied
            }
            free(inner_result);
            return result;
        }
    }

    /// Handle indirect expansion: ${!name} or ${!prefix*} or ${!prefix@}
    if (expansion[0] == '!') {
        const char *var_name = expansion + 1;
        size_t name_len = strlen(var_name);

        /// Check for ${!prefix*} or ${!prefix@} - list variable names
        if (name_len > 0 &&
            (var_name[name_len - 1] == '*' || var_name[name_len - 1] == '@')) {
            /// Extract prefix (without * or @)
            char *prefix = malloc(name_len);
            if (!prefix) {
                return strdup("");
            }
            strncpy(prefix, var_name, name_len - 1);
            prefix[name_len - 1] = '\0';

            /// Enumerate the symbol table for matching names. The prior
            /// implementation only scanned `environ` (exported vars
            /// only); most shell-local variables never reach environ.
            /// Collect into a dynamic array via the symtable enumerator,
            /// sort alphabetically for determinism (bash documents the
            /// order as unspecified but the corpus depends on a stable
            /// order for byte-for-byte diff_oracle comparison).
            /// Issue #102. The callback and qsort comparator are
            /// file-scope helpers because C lacks nested functions.
            prefix_collect_ctx_t ctx = {NULL, 0, 0, prefix, strlen(prefix)};
            symtable_enumerate_global_vars(prefix_collect_cb, &ctx);

            if (ctx.count > 1) {
                qsort(ctx.names, ctx.count, sizeof(char *), strptr_cmp);
            }

            /// Build space-separated result.
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

        /// Check for ${!arr[@]} or ${!arr[*]} - array keys
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

            if (array && bracket[1] == '@') {
                /// ${!arr[@]} reaching this general parameter-expansion
                /// fallthrough sits in a SCALAR-REQUIRING context (the
                /// vector-accepting positions are handled earlier by
                /// try_expand_vector_arg). The keys of a list or map are
                /// themselves a list. Under strict value typing this @ keys
                /// form in a scalar slot is a type mismatch, exactly as
                /// ${arr[@]} is; under a relaxed compat mode it flattens the
                /// keys to the oracle scalar (bash/posix space, zsh IFS[0]).
                /// (${!arr[*]} joins on IFS[0] in every mode -- below.)
                if (!shell_mode_allows(FEATURE_STRICT_VALUE_TYPING)) {
                    size_t count;
                    char **keys = symtable_array_get_keys(array, &count);
                    char *result = NULL;
                    if (keys && count > 0) {
                        char sep[2];
                        relaxed_flatten_sep(executor, sep);
                        result = join_strings_with_sep(keys, (int)count, sep);
                        for (size_t i = 0; i < count; i++) {
                            free(keys[i]);
                        }
                    }
                    free(keys);
                    free(arr_name);
                    return result ? result : strdup("");
                }
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_TYPE_MISMATCH, SHELL_SEVERITY_ERROR,
                    executor_current_loc(executor),
                    "type mismatch: list value ${!%s[@]} in a scalar "
                    "position",
                    arr_name);
                if (err) {
                    shell_error_set_suggestion(
                        err, "join the keys explicitly -- use ${!name[*]} for "
                             "IFS-joining, or iterate ${!name[@]} as a "
                             "vector.");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                    executor->has_error = true;
                } else {
                    executor_error_report(executor, SHELL_ERR_TYPE_MISMATCH,
                                          executor_current_loc(executor),
                                          "type mismatch: list value "
                                          "${!%s[@]} in a scalar position",
                                          arr_name);
                }
                executor_request_posix_exit(executor, 1);
                free(arr_name);
                return strdup("");
            }
            free(arr_name);

            if (array) {
                /// ${!arr[*]} - array keys joined on the first char of IFS.
                size_t count;
                char **keys = symtable_array_get_keys(array, &count);
                if (keys && count > 0) {
                    char sep[2];
                    ifs_join_separator(executor, sep);
                    char *result = join_strings_with_sep(keys, (int)count, sep);
                    for (size_t i = 0; i < count; i++) {
                        free(keys[i]);
                    }
                    free(keys);
                    return result ? result : strdup("");
                }
                free(keys);
            }
            return strdup("");
        }

        /// Simple indirect expansion: ${!name} - value of variable named by
        /// name. symtable_get_var returns owned copies; free both the pointer
        /// name and the resolved value.
        char *indirect_name = symtable_get_var(executor->symtable, var_name);
        char *result = NULL;
        if (indirect_name && indirect_name[0]) {
            /// Get the value of the variable whose name is in indirect_name
            char *value = symtable_get_var(executor->symtable, indirect_name);
            result = strdup(value ? value : "");
            free(value);
        }
        free(indirect_name);
        return result ? result : strdup("");
    }

    /// Handle array length: ${#arr[@]} or ${#arr[*]}
    if (expansion[0] == '#') {
        const char *var_name = expansion + 1;

        /// Nested form ${#${INNER}}: count the length of the inner
        /// expansion's result. expand_variable handles the full ${...}
        /// form. Issue #98. Bash/zsh return codepoint count, not byte
        /// count, for multi-byte strings; use the canonical UTF-8
        /// counter to match the consensus.
        if (var_name[0] == '$' && var_name[1] == '{') {
            char *inner = expand_variable(executor, var_name);
            if (inner) {
                size_t inner_len =
                    lle_utf8_count_codepoints(inner, strlen(inner));
                free(inner);
                char buf[32];
                snprintf(buf, sizeof(buf), "%zu", inner_len);
                return strdup(buf);
            }
            return strdup("0");
        }

        /// Check for array subscript
        const char *bracket = strchr(var_name, '[');
        if (bracket) {
            size_t name_len = bracket - var_name;
            char *arr_name = malloc(name_len + 1);
            if (!arr_name) {
                return strdup("0");
            }
            strncpy(arr_name, var_name, name_len);
            arr_name[name_len] = '\0';

            /// Get subscript
            const char *close = strchr(bracket, ']');
            if (close) {
                size_t sub_len = close - bracket - 1;
                char *subscript = malloc(sub_len + 1);
                if (subscript) {
                    strncpy(subscript, bracket + 1, sub_len);
                    subscript[sub_len] = '\0';

                    /// Check if array exists
                    array_value_t *array = symtable_get_array(arr_name);
                    if (array) {
                        char result_buf[32];

                        if (strcmp(subscript, "@") == 0 ||
                            strcmp(subscript, "*") == 0) {
                            /// ${#arr[@]} - number of elements
                            snprintf(result_buf, sizeof(result_buf), "%zu",
                                     symtable_array_length(array));
                        } else {
                            /// ${#arr[n]} - length of element at index n
                            arithm_clear_error();
                            char *idx_result = arithm_expand_with_executor(
                                executor, subscript);
                            if (idx_result && !arithm_error_is_flagged()) {
                                long long idx = strtoll(idx_result, NULL, 10);
                                free(idx_result);

                                /// Same indexing convention as ${arr[n]}:
                                /// zsh-mode rejects 0, decrements positives,
                                /// passes negatives through to the symtable
                                /// helper's "from-end" handler. Lush/bash
                                /// pass through directly. (Issue #68.)
                                if (!shell_mode_allows(
                                        FEATURE_ARRAY_ZERO_INDEXED)) {
                                    if (idx == 0) {
                                        snprintf(result_buf, sizeof(result_buf),
                                                 "0");
                                    } else {
                                        if (idx > 0) {
                                            idx--; /// 1-based -> 0-based
                                        }
                                        const char *elem =
                                            symtable_array_get_index(array,
                                                                     idx);
                                        size_t elem_len =
                                            elem ? lle_utf8_count_codepoints(
                                                       elem, strlen(elem))
                                                 : 0;
                                        snprintf(result_buf, sizeof(result_buf),
                                                 "%zu", elem_len);
                                    }
                                } else {
                                    const char *elem =
                                        symtable_array_get_index(array, idx);
                                    size_t elem_len =
                                        elem ? lle_utf8_count_codepoints(
                                                   elem, strlen(elem))
                                             : 0;
                                    snprintf(result_buf, sizeof(result_buf),
                                             "%zu", elem_len);
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

        /// Regular variable length: ${#var}. Mode-aware for the array
        /// case on a bare array name (issue #99):
        ///   zsh:  number of elements
        ///   bash: length of arr[0] (treats $arr as ${arr[0]})
        ///   lush: number of elements (curated zsh idiom)
        ///   posix: arrays don't exist, but if one was carried over
        ///          from a prior mode, match bash's first-element rule.
        /// Unified lookup branches on kind in a single call. Length is
        /// counted in codepoints (bash/zsh consensus) via the canonical
        /// UTF-8 counter, not in bytes.
        lush_value_view_t view = {0};
        symtable_lookup(var_name, &view);
        if (view.kind == LUSH_VALUE_SCALAR) {
            const char *s = view.scalar_value;
            size_t len = lle_utf8_count_codepoints(s, strlen(s));
            lush_value_view_clear(&view);
            char *result = malloc(24);
            if (result) {
                snprintf(result, 24, "%zu", len);
            }
            return result ? result : strdup("0");
        }
        if (view.kind == LUSH_VALUE_LIST || view.kind == LUSH_VALUE_MAP) {
            array_value_t *array = view.array;
            shell_mode_t mode = shell_mode_get();
            /// size_t can render up to 20 digits on 64-bit + null.
            char *result = malloc(24);
            if (!result) {
                lush_value_view_clear(&view);
                return strdup("0");
            }
            if (mode == SHELL_MODE_BASH || mode == SHELL_MODE_POSIX) {
                const char *first = symtable_array_get_index(array, 0);
                size_t first_len =
                    first ? lle_utf8_count_codepoints(first, strlen(first)) : 0;
                snprintf(result, 24, "%zu", first_len);
            } else {
                snprintf(result, 24, "%zu", symtable_array_length(array));
            }
            lush_value_view_clear(&view);
            return result;
        }
        return strdup("0");
    }

    /// Handle array element access: ${arr[n]}, ${arr[@]}, ${arr[*]}.
    /// Only routes through this branch when the prefix before `[` is a
    /// valid shell identifier; otherwise the `[` belongs to something
    /// else (e.g. the character-class pattern inside a substitution
    /// `${var/[abc]/X}`) and must not be consumed here. The prior
    /// unconditional `strchr(expansion, '[')` matched any `[` and
    /// silently emptied substitutions whose patterns happened to
    /// contain a bracket. Issue #96.
    const char *bracket = strchr(expansion, '[');
    if (bracket && bracket > expansion) {
        size_t name_len = bracket - expansion;
        bool valid_name = false;
        size_t walked = lush_ident_match_start(expansion, name_len);
        if (walked > 0) {
            while (walked < name_len) {
                size_t n = lush_ident_match_continue(expansion + walked,
                                                     name_len - walked);
                if (n == 0) {
                    break;
                }
                walked += n;
            }
            valid_name = (walked == name_len);
        }
        if (!valid_name) {
            bracket = NULL;
        }
    } else if (bracket && bracket == expansion) {
        /// `[` at the very start means there is no name -- not an
        /// array access.
        bracket = NULL;
    }
    /// Per-element passthrough: ${arr[@]op...} and ${arr[*]op...} should
    /// route through the operator dispatch below so the trailing op
    /// applies to each element. The bracket block would otherwise
    /// intercept the [@]/[*] subscript and raise a list-in-scalar-slot
    /// mismatch before the operator gets to dispatch.
    ///
    /// Conditions to skip the bracket block:
    ///   - subscript is exactly @ or *
    ///   - the next char after ] starts a per-element-amenable scalar
    ///     operator (^, ',', #, %, /, @). Notably NOT `:` -- slicing
    ///     ${arr[@]:0:2} and the conditional family ${arr[@]:-default}
    ///     keep their existing handling in the bracket block.
    ///   - the name resolves to a known array (otherwise the bracket
    ///     belongs to something else and the existing block handles
    ///     non-array names correctly).
    if (bracket) {
        /// Same quote/escape/nested-aware span the write path uses (#631), so
        /// the per-element `[@]`/`[*]` detection sees the true closing `]`.
        subscript_span_t espan =
            scan_subscript_bounds(bracket, strlen(bracket));
        const char *close = espan.is_valid ? bracket + espan.close : NULL;
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

        /// Delimit the subscript with the same span finder the write store and
        /// the array-element-assignment parser use (#631). strchr(']') stopped
        /// at the first `]`, so a key with an escaped/quoted/nested `]`
        /// (m[x\]y], m["a]b"], m[$(f)]) truncated on read and missed the value
        /// the write -- now quote/escape/nested-aware -- stored.
        subscript_span_t rspan =
            scan_subscript_bounds(bracket, strlen(bracket));
        const char *close = rspan.is_valid ? bracket + rspan.close : NULL;
        if (close) {
            size_t sub_len = close - bracket - 1;
            char *subscript = malloc(sub_len + 1);
            if (subscript) {
                strncpy(subscript, bracket + 1, sub_len);
                subscript[sub_len] = '\0';

                /// Resolve nameref if applicable. The resolved name is owned,
                /// so free it once symtable_get_array has consumed it.
                const char *resolved_arr_name = arr_name;
                char *resolved_arr_owned = NULL;
                symtable_manager_t *mgr = symtable_get_global_manager();
                if (mgr && symtable_is_nameref(mgr, arr_name)) {
                    resolved_arr_owned =
                        (char *)symtable_resolve_nameref(mgr, arr_name, 10);
                    if (resolved_arr_owned) {
                        resolved_arr_name = resolved_arr_owned;
                    }
                }

                array_value_t *array = symtable_get_array(resolved_arr_name);
                free(resolved_arr_owned);
                if (array) {
                    char *result = NULL;

                    /// Detect a bash slicing suffix `:N` / `:N:M` after
                    /// the closing `]`. ${arr[@]:N}, ${arr[@]:N:M},
                    /// ${arr[*]:N:M} are element-wise slices on the
                    /// array, not byte-wise on the joined string -- the
                    /// generic substring case (parse_parameter_expansion
                    /// case 14) would silently drop these because it
                    /// couldn't resolve `arr[@]` as a scalar var, and
                    /// even with a resolution it would byte-slice the
                    /// joined string instead of picking elements. Issue
                    /// #97.
                    int slice_offset = 0;
                    int slice_length = -1; /// -1 = "to end"
                    bool has_slice = false;
                    if (close[1] == ':' &&
                        (strcmp(subscript, "@") == 0 ||
                         strcmp(subscript, "*") == 0) &&
                        !array->is_associative) {
                        /// A numeric offset is an array-element slice
                        /// (${arr[@]:1:2}); the :- / :+ / := / :? operators
                        /// also start with `:` but are applied after the
                        /// subscript below, not treated as slices (#530).
                        const char *spec = close + 2;
                        if (slice_spec_is_numeric(spec)) {
                            char *endp = NULL;
                            slice_offset = (int)strtol(spec, &endp, 10);
                            has_slice = true;
                            if (*endp == ':') {
                                slice_length = (int)strtol(endp + 1, NULL, 10);
                            }
                        }
                    }

                    if (has_slice) {
                        size_t total = symtable_array_length(array);
                        /// Negative offset counts from end (bash).
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
                            /// Slice join uses the first character of IFS
                            /// (POSIX star-join), space when IFS is unset,
                            /// no separator when IFS is "".
                            char sep[2];
                            ifs_join_separator(executor, sep);
                            size_t sep_len = strlen(sep);
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
                                    size_t need = pos + elen +
                                                  (pos > 0 ? sep_len : 0) + 1;
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
                                    if (pos > 0 && sep_len) {
                                        memcpy(result + pos, sep, sep_len);
                                        pos += sep_len;
                                    }
                                    memcpy(result + pos, elem, elen);
                                    pos += elen;
                                    result[pos] = '\0';
                                }
                            }
                        }
                    } else if (strcmp(subscript, "*") == 0) {
                        /// ${arr[*]} - explicit scalar join (SEMANTICS.md
                        /// section 3.5): all elements joined with the first
                        /// character of IFS (space when IFS is unset, empty
                        /// separator when IFS is "").
                        char sep[2];
                        ifs_join_separator(executor, sep);
                        result = symtable_array_expand(array, sep);
                    } else if (strcmp(subscript, "@") == 0) {
                        /// ${arr[@]} reaching this general parameter-
                        /// expansion fallthrough sits in a SCALAR-REQUIRING
                        /// context (vector-accepting positions are handled
                        /// earlier by try_expand_vector_arg). Under strict
                        /// value typing (lush default, SEMANTICS.md section
                        /// 3.9) a list/map value in a scalar slot is a type
                        /// mismatch; under a relaxed compat mode it flattens
                        /// to the oracle scalar (bash/posix: space-join;
                        /// zsh: IFS[0]-join).
                        if (!shell_mode_allows(FEATURE_STRICT_VALUE_TYPING)) {
                            char sep[2];
                            relaxed_flatten_sep(executor, sep);
                            result = symtable_array_expand(array, sep);
                        } else {
                            const char *kind = (array && array->is_associative)
                                                   ? "map"
                                                   : "list";
                            shell_error_t *err = shell_error_create(
                                SHELL_ERR_TYPE_MISMATCH, SHELL_SEVERITY_ERROR,
                                executor_current_loc(executor),
                                "type mismatch: %s value ${%s[@]} in a "
                                "scalar position",
                                kind, arr_name ? arr_name : "?");
                            if (err) {
                                shell_error_set_suggestion(
                                    err, "join the values explicitly -- use "
                                         "${name[*]} for IFS-joining, or build "
                                         "a scalar from the elements with an "
                                         "explicit join.");
                                shell_error_display(err, stderr,
                                                    isatty(STDERR_FILENO));
                                shell_error_free(err);
                                executor->has_error = true;
                            } else {
                                executor_error_report(
                                    executor, SHELL_ERR_TYPE_MISMATCH,
                                    executor_current_loc(executor),
                                    "type mismatch: %s value ${%s[@]} in a "
                                    "scalar position",
                                    kind, arr_name ? arr_name : "?");
                            }
                            /// In a script, a type mismatch aborts before the
                            /// bad value can reach a downstream command;
                            /// interactively, the prompt continues.
                            executor_request_posix_exit(executor, 1);
                            result = strdup("");
                        }
                    } else if ((strncmp(subscript, "(r)", 3) == 0 ||
                                strncmp(subscript, "(R)", 3) == 0) &&
                               !array->is_associative) {
                        /// zsh subscript flags ${arr[(r)pat]} / ${arr[(R)pat]}:
                        /// return the VALUE of the first / last element
                        /// matching pat, or empty string on no match.
                        /// pat is a glob pattern (lush_pattern_match). Issue
                        /// #104.
                        bool last_match = (subscript[1] == 'R');
                        const char *pat = subscript + 3;
                        size_t total = symtable_array_length(array);
                        const char *found = NULL;
                        bool is_glob = (strchr(pat, '*') || strchr(pat, '?') ||
                                        strchr(pat, '[') ||
                                        lush_pattern_opens_extglob_group(pat));
                        for (size_t k = 0; k < total; k++) {
                            const char *elem =
                                symtable_array_get_index(array, (int)k);
                            if (!elem) {
                                continue;
                            }
                            bool match;
                            if (is_glob) {
                                match = lush_shell_pattern_match(elem, pat);
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
                        /// zsh subscript flags ${arr[(i)pat]} / ${arr[(I)pat]}:
                        /// return the 1-based index of the first / last
                        /// element matching pat, or N+1 / 0 if no match.
                        /// pat is a glob pattern (lush_pattern_match). Issue
                        /// #99.
                        bool last_index = (subscript[1] == 'I');
                        const char *pat = subscript + 3;
                        size_t total = symtable_array_length(array);
                        int found = last_index ? 0 : (int)(total + 1);
                        bool any_match = false;
                        bool is_glob = (strchr(pat, '*') || strchr(pat, '?') ||
                                        strchr(pat, '[') ||
                                        lush_pattern_opens_extglob_group(pat));
                        for (size_t k = 0; k < total; k++) {
                            const char *elem =
                                symtable_array_get_index(array, (int)k);
                            if (!elem) {
                                continue;
                            }
                            bool match;
                            if (is_glob) {
                                match = lush_shell_pattern_match(elem, pat);
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
                        /// zsh-style range subscript ${arr[N,M]} / $arr[N,M]
                        /// on an indexed array: join elements N..M with a
                        /// single space, matching zsh's default output of
                        /// `$arr[N,M]`. The arithmetic expander interprets
                        /// `N,M` as the C comma operator and returns M --
                        /// which without this branch silently selected the
                        /// M-th element instead of slicing. Supports
                        /// negative indices (zsh: -1 = last); honors
                        /// FEATURE_ARRAY_ZERO_INDEXED for the 1-based vs
                        /// 0-based decision (same shape as the string-
                        /// slicing fallback further down). Comma in
                        /// associative-array subscripts has no range
                        /// meaning -- those keep the C-comma key path.
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
                            /// Concatenate elements [start_idx..end_idx]
                            /// with single-space separator.
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
                        /// Associative array - use subscript as string key.
                        /// Same canonicalization as the store path (#631) so
                        /// the read keys on the identical bytes the write
                        /// produced.
                        char *expanded_subscript = subscript_normalize_key(
                            executor, subscript, strlen(subscript));
                        const char *key =
                            expanded_subscript ? expanded_subscript : subscript;
                        const char *elem = symtable_array_get_assoc(array, key);
                        result = strdup(elem ? elem : "");
                        if (expanded_subscript)
                            free(expanded_subscript);
                    } else {
                        /// Indexed array - ${arr[n]} - specific element
                        arithm_clear_error();
                        char *idx_raw = pe_dequote_subscript(subscript);
                        char *idx_result = arithm_expand_with_executor(
                            executor, idx_raw ? idx_raw : subscript);
                        free(idx_raw);
                        if (idx_result && !arithm_error_is_flagged()) {
                            long long idx = strtoll(idx_result, NULL, 10);
                            free(idx_result);

                            /// zsh-mode (1-based): 0 is invalid (returns
                            /// empty); positive indices need
                            /// decrement-to-0-based; negative indices pass
                            /// through unchanged so the symtable helper's
                            /// built-in "from-end" handling fires.
                            /// lush/bash-mode (0-based): pass through
                            /// directly. (Issue #68 — array half.)
                            if (!shell_mode_allows(
                                    FEATURE_ARRAY_ZERO_INDEXED)) {
                                if (idx == 0) {
                                    result = strdup("");
                                } else {
                                    if (idx > 0) {
                                        idx--; /// 1-based -> 0-based
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

                    /// Apply a trailing parameter-expansion operator on an
                    /// indexed/associative element (`${arr[key]op...}`)
                    /// through the SAME operator engine the scalar
                    /// `${var op...}` path uses, so the full operator set --
                    /// `##`, `%%`, `^^`, `,,`, `//`, `/`, substring,
                    /// `@`-transforms -- applies uniformly to one element,
                    /// not only `:-`/`:+` (issue #514). A single element
                    /// can't distinguish unset from empty (a missing key
                    /// reads as ""); an empty element is passed as NULL so
                    /// the unset-keyed operators (`:-`/`-`/`:=`/`=`/`:?`/`?`)
                    /// fire, matching the established array semantics.
                    /// Apply a trailing parameter operator on the element
                    /// value -- unless a numeric slice already consumed the
                    /// `:offset:length` suffix (has_slice), which must not be
                    /// re-applied as a substring operator (issue #530: the
                    /// element slice and the substring operator both matched
                    /// `:0:2`, double-slicing ${arr[*]:0:2}).
                    const char *after_bracket = close + 1;
                    if (*after_bracket != '\0' && !has_slice) {
                        const char *rhs = NULL;
                        int el_op =
                            detect_param_operator_suffix(after_bracket, &rhs);
                        if (el_op >= 0) {
                            char *el_default = expand_variables_in_string(
                                executor, rhs ? rhs : "");
                            char *elem_val =
                                (result && *result) ? result : NULL;
                            bool assign_back = false;
                            char *applied = apply_param_operator(
                                executor, arr_name, elem_val,
                                el_default ? el_default : (char *)"", el_op,
                                &assign_back);
                            if (assign_back) {
                                /// `:=`/`=` persist to the ELEMENT, not a
                                /// scalar named arr_name. Resolve the
                                /// key/index the same way the element read
                                /// did: string key for a map, arithmetic
                                /// subscript with the 1-based adjustment for
                                /// a list. The low-level element setters
                                /// bypass readonly enforcement, so refuse a
                                /// write to a readonly array here (mirroring
                                /// the direct-assignment guard) instead of
                                /// silently mutating it.
                                if (symtable_array_get_flags(arr_name) &
                                    SYMVAR_READONLY) {
                                    executor_error_report(
                                        executor, SHELL_ERR_READONLY_VAR,
                                        executor_current_loc(executor),
                                        "%s: readonly variable", arr_name);
                                    executor->expansion_error = true;
                                    executor->expansion_exit_status = 1;
                                } else if (array->is_associative) {
                                    char *ek =
                                        expand_variable(executor, subscript);
                                    symtable_array_set_assoc(
                                        array, ek ? ek : subscript, applied);
                                    free(ek);
                                } else {
                                    arithm_clear_error();
                                    char *ir = arithm_expand_with_executor(
                                        executor, subscript);
                                    if (ir && !arithm_error_is_flagged()) {
                                        long ix = strtoll(ir, NULL, 10);
                                        bool one_based = !shell_mode_allows(
                                            FEATURE_ARRAY_ZERO_INDEXED);
                                        /// 1-based mode: index 0 is invalid
                                        /// (the read returns empty and stores
                                        /// nothing), so skip the write rather
                                        /// than clobber physical index 0.
                                        if (!(one_based && ix == 0)) {
                                            if (one_based && ix > 0) {
                                                ix--; /// 1-based -> 0-based
                                            }
                                            symtable_array_set_index(
                                                array, (int)ix, applied);
                                        }
                                    }
                                    free(ir);
                                }
                            }
                            free(el_default);
                            free(result);
                            result = applied;
                        }
                    }

                    free(subscript);
                    free(arr_name);
                    return result ? result : strdup("");
                }

                /// Array doesn't exist. Compute the element value, then feed
                /// it through the shared operator engine so a trailing
                /// ${name[N]op...} applies (issue #527) -- previously a
                /// trailing operator was dropped by a no-op stub here. Two
                /// element-value sources flow into the SAME operator apply:
                ///   (a) arr_name names a scalar string ->
                ///   ${str[N]}/${str[N,M]}
                ///       is a grapheme-cluster slice (TR#29 boundaries), and
                ///   (b) arr_name is fully unset -> the element is NULL.
                const char *after_bracket = close + 1;
                const char *el_rhs = NULL;
                int el_op = -1;
                if (*after_bracket != '\0') {
                    el_op =
                        detect_param_operator_suffix(after_bracket, &el_rhs);
                }

                /// Grapheme-slice fallback for a scalar name. Honors
                /// FEATURE_ARRAY_ZERO_INDEXED (1-based zsh, 0-based bash/lush).
                /// Subscript "@" / "*" are array-only and handled above.
                /// Sets elem_result (owned) on success; name_is_scalar tracks
                /// whether arr_name resolved to a scalar so a `:=`/`=` does not
                /// synthesize an array over a scalar binding.
                char *elem_result = NULL;
                bool name_is_scalar = false;
                if (strcmp(subscript, "@") != 0 &&
                    strcmp(subscript, "*") != 0) {
                    char *str_value =
                        symtable_get_var(executor->symtable, arr_name);
                    if (str_value) {
                        name_is_scalar = true;
                        int start_idx = 0, end_idx = -1;
                        char *comma = strchr(subscript, ',');
                        if (comma) {
                            *comma = '\0';
                            start_idx = atoi(subscript);
                            end_idx = atoi(comma + 1);
                            *comma = ',';
                        } else {
                            start_idx = atoi(subscript);
                            end_idx = start_idx; /// single grapheme
                        }
                        size_t value_len = strlen(str_value);
                        bool slice_empty = false;

                        /// Negative-index handling: ${str[-N]} counts from
                        /// the end (issue #68), via lle_utf8_count_graphemes.
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

                        /// Convert from 1-based (zsh) to 0-based if needed.
                        if (!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED)) {
                            if (start_idx <= 0 || end_idx <= 0) {
                                slice_empty = true;
                            } else {
                                start_idx--;
                                end_idx--;
                            }
                        }

                        /// Inverted range yields empty.
                        if (!slice_empty && end_idx < start_idx) {
                            slice_empty = true;
                        }

                        if (slice_empty) {
                            elem_result = strdup("");
                        } else {
                            int count = end_idx - start_idx + 1;
                            elem_result = lush_slice_graphemes(
                                str_value, value_len, start_idx, count);
                            if (!elem_result) {
                                elem_result = strdup("");
                            }
                        }
                        free(str_value);
                    }
                }

                if (el_op >= 0) {
                    /// Apply the operator to the element value. A single
                    /// element cannot distinguish unset from empty, so an
                    /// empty value is passed as NULL: the unset-keyed operators
                    /// (`:-`/`-`/`:=`/`=`/`:?`/`?`) fire, matching the declared
                    /// array-element path.
                    char *el_default = expand_variables_in_string(
                        executor, el_rhs ? el_rhs : "");
                    char *elem_val =
                        (elem_result && *elem_result) ? elem_result : NULL;
                    bool assign_back = false;
                    char *applied = apply_param_operator(
                        executor, arr_name, elem_val,
                        el_default ? el_default : (char *)"", el_op,
                        &assign_back);
                    if (assign_back && !name_is_scalar) {
                        /// `:=`/`=` on an element of an unset name creates the
                        /// array and persists the element, matching bash
                        /// (`${undef[0]:=x}` -> undef=(x)).
                        array_value_t *new_arr = symtable_array_create(false);
                        if (new_arr) {
                            arithm_clear_error();
                            char *ir = arithm_expand_with_executor(executor,
                                                                   subscript);
                            if (ir && !arithm_error_is_flagged()) {
                                long ix = strtoll(ir, NULL, 10);
                                bool one_based = !shell_mode_allows(
                                    FEATURE_ARRAY_ZERO_INDEXED);
                                if (!(one_based && ix == 0)) {
                                    if (one_based && ix > 0) {
                                        ix--; /// 1-based -> 0-based
                                    }
                                    symtable_array_set_index(new_arr, ix,
                                                             applied);
                                }
                            }
                            free(ir);
                            /// symtable_assign_array takes ownership on success
                            /// and resolves scope like a bare assignment (#614:
                            /// `${undef[i]:=x}` in a function persists to the
                            /// enclosing/global scope); free on failure so it
                            /// is not leaked.
                            if (symtable_assign_array(arr_name, new_arr) != 0) {
                                symtable_array_free(new_arr);
                            }
                        }
                    }
                    free(el_default);
                    free(elem_result);
                    free(subscript);
                    free(arr_name);
                    return applied ? applied : strdup("");
                }

                if (elem_result) {
                    free(subscript);
                    free(arr_name);
                    return elem_result;
                }

                free(subscript);
            }
        }
        free(arr_name);
        return strdup("");
    }

    /// Look for parameter expansion operators. The table is the shared
    /// file-scope param_operators[]; order matters (longer operators
    /// before the shorter ones they contain) so this detection loop
    /// resolves e.g. `##` before `#` and `:-` before `:`.
    const char *op_pos = NULL;
    const char *const *operators = param_operators;
    int op_type = -1;

    /// Special-parameter names at position 0 (@, *, #, ?, !, $, -, 0..9)
    /// are variable names, not operators. Without this guard ${@^}
    /// gets parsed as the `@` transformation operator (op_type 17)
    /// applied to an empty var_name, instead of `@` as the variable
    /// with the `^` case-mod operator. Same for ${*^}, ${#:-default}
    /// variants on the special params, etc. Issue #96.
    bool first_is_special_param = false;
    if (expansion[0]) {
        char c0 = expansion[0];
        if (c0 == '@' || c0 == '*' || c0 == '#' || c0 == '?' || c0 == '!' ||
            c0 == '$' || c0 == '-' || (c0 >= '0' && c0 <= '9')) {
            first_is_special_param = true;
        }
    }

    /// Find the first valid operator - prioritize longer operators first.
    /// Use the bracket-aware variant so subscript chars inside `[...]`
    /// (notably `@` in `arr[@]`) don't get picked as operators.
    for (int i = 0; operators[i]; i++) {
        const char *found = find_op_outside_brackets(expansion, operators[i]);
        /// If the operator matches at position 0 and the first char is
        /// a special-param name, search again starting after it -- the
        /// apparent operator at position 0 is really the variable.
        if (found == expansion && first_is_special_param) {
            found = find_op_outside_brackets(expansion + 1, operators[i]);
        }
        if (found) {
            /// Skip single-character operators that are part of longer ones
            if (strlen(operators[i]) == 1) {
                /// Check if this single char is part of a longer operator
                bool part_of_longer = false;

                /// Check for :- :+ := :? before processing single :
                if (strcmp(operators[i], ":") == 0) {
                    if ((found > expansion &&
                         (found[-1] == '-' || found[-1] == '+')) ||
                        (found[1] == '-' || found[1] == '+' ||
                         found[1] == '=' || found[1] == '?')) {
                        part_of_longer = true;
                    }
                }

                /// Check for ## and %% before processing single # or %
                if (strcmp(operators[i], "#") == 0 && found[1] == '#') {
                    part_of_longer = true;
                }
                if (strcmp(operators[i], "%") == 0 && found[1] == '%') {
                    part_of_longer = true;
                }
                /// Check for /// before processing single /
                if (strcmp(operators[i], "/") == 0 && found[1] == '/') {
                    part_of_longer = true;
                }
                /// Check for :? before processing single ?
                if (strcmp(operators[i], "?") == 0 && found > expansion &&
                    found[-1] == ':') {
                    part_of_longer = true;
                }

                if (part_of_longer) {
                    continue;
                }
            }

            /// If we haven't found an operator yet, or this one comes first,
            /// use it
            if (!op_pos || found < op_pos) {
                op_pos = found;
                op_type = i;
            }
        }
    }

    if (op_pos) {
        /// Extract variable name
        size_t var_len = op_pos - expansion;

        /// If operator is at position 0, it might actually be a special
        /// variable like $- (which contains the '-' character itself)
        if (var_len == 0 && strlen(expansion) == 1 && expansion[0] == '-') {
            /// This is $- (shell options), not a parameter expansion operator
            /// Fall through to regular variable lookup below
            op_pos = NULL;
            op_type = -1;
        }
    }

    if (op_pos) {
        /// Extract variable name
        size_t var_len = op_pos - expansion;
        char *var_name = malloc(var_len + 1);
        if (!var_name) {
            return strdup("");
        }

        strncpy(var_name, expansion, var_len);
        var_name[var_len] = '\0';

        /// Scalar-operator on bare collection: type mismatch.
        ///
        /// A bare name like ${arr:-default} or ${arr##pattern} or
        /// ${arr^^} reaches this dispatch when arr is a list/map.
        /// The operator is scalar-shaped; applying it would either
        /// silently degrade the collection or treat the unset-scalar
        /// path as if the collection were empty. Both are exactly
        /// the implicit list-to-scalar coercion the engine is
        /// designed to reject. Surface a type mismatch with a hint
        /// pointing at the explicit forms (slice + scalar op, or
        /// [@]-vectorized op).
        ///
        /// Subscripted forms ${arr[@]op...} and ${arr[N]op...} go
        /// through a different path; they're already vectorized or
        /// single-element. The bare-name check fires only when the
        /// operator's left operand is a complete collection
        /// identifier with no subscript.
        ///
        /// Exception: the @a attribute query is metadata-only and
        /// kind-agnostic. It reports the declared-attribute letters
        /// (a / A for indexed / associative arrays, plus i/r/x/...)
        /// and must work on a collection, unlike the value-shaped @
        /// transforms (@Q/@E/@U/...) which do need an explicit [@] to
        /// vectorize. Let @a fall through to the case 17 dispatch,
        /// where get_variable_attributes handles every kind even when
        /// the scalar value lookup misses (arrays have a NULL scalar).
        bool attr_query = (op_type == 17 && op_pos[1] == 'a');
        /// Under a relaxed compat mode a bare collection meeting a scalar
        /// operator is flattened to its mode scalar (bash/posix element 0,
        /// zsh whole-join) and the operator applies to that scalar.
        /// relaxed_bare_value carries the pre-computed scalar past the
        /// scalar re-lookup below; relaxed_bare_array retains the binding so
        /// := / = can assign back to element 0 (preserving the array).
        char *relaxed_bare_value = NULL;
        array_value_t *relaxed_bare_array = NULL;
        if (var_len > 0 && !attr_query) {
            array_value_t *bare_array = symtable_get_array(var_name);
            if (bare_array && !shell_mode_allows(FEATURE_STRICT_VALUE_TYPING)) {
                /// Relaxed (bash/zsh/posix): the SEMANTICS section 3.9
                /// boundary policy follows the oracle instead of the strict
                /// type error. Flatten the collection to a scalar and let the
                /// existing apply_param_operator run on it -- exact bash
                /// element-0 parity for every operator, and zsh scalar-slot
                /// parity via whole-join. zsh's unquoted element-wise pattern
                /// ops and its element-slicing ${arr:o:l} are curated to this
                /// single flatten-then-apply reading (documented divergence);
                /// the explicit ${arr[@]op...} vector form stays available.
                relaxed_bare_value =
                    flatten_bare_collection_relaxed(executor, bare_array);
                relaxed_bare_array = bare_array;
            } else if (bare_array) {
                const char *op_str = operators[op_type];
                const char *kind_label =
                    bare_array->is_associative ? "map" : "list";
                const char *hint = NULL;
                switch (op_type) {
                case 0:  /// :-
                case 1:  /// :+
                case 10: /// -
                case 11: /// +
                case 12: /// :=
                case 13: /// =
                case 18: /// :?
                case 19: /// ?
                    hint = "use ${name[0]:-default} for a scalar-"
                           "element default, or assign a list literal "
                           "directly for a list-shaped default";
                    break;
                case 14: /// :
                    hint = "substring is scalar-only -- use "
                           "${name[@]:offset:length} for list slicing, "
                           "or ${name[N]:offset:length} for substring "
                           "of one element";
                    break;
                case 2:  /// ##
                case 3:  /// %%
                case 6:  /// #
                case 7:  /// %
                case 15: /// ///
                case 16: /// /
                case 4:  /// ^^
                case 5:  /// ,,
                case 8:  /// ^
                case 9:  /// ,
                case 17: /// @ transformations
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
                    /// The error_report path emits the message
                    /// immediately; the help line is set as part of
                    /// the structured error if we have one. The
                    /// downstream display already includes the help
                    /// field from the most recent error in the
                    /// collector, so we don't need to re-emit. The
                    /// hint stays paired with the operator class
                    /// above so future operators get a tailored
                    /// suggestion when they land.
                    (void)hint;
                }
                executor_request_posix_exit(executor, 1);
                free(var_name);
                return strdup("");
            }
        }

        /// Get variable value. A nested expansion in the name position
        /// (${${inner}:op}) is expanded first so the outer operator or
        /// modifier applies to the inner result, e.g. ${${p:t}:r}.
        char *var_value;
        if (relaxed_bare_array) {
            /// The relaxed bare-collection path was taken (keyed on the array,
            /// not the value, so an allocation failure that left
            /// relaxed_bare_value NULL degrades to an empty operand rather
            /// than silently re-looking up). Use the pre-computed mode scalar
            /// rather than symtable_get_var, which returns NULL for an array
            /// binding and would lose the flatten.
            var_value = relaxed_bare_value;
        } else if (var_name[0] == '$' && var_name[1] == '{') {
            var_value = expand_variable(executor, var_name);
        } else {
            var_value = symtable_get_var(executor->symtable, var_name);
        }
        const char *default_value = op_pos + strlen(operators[op_type]);

        /// Quote-remove the operand, then expand it. The tokenizer now hands
        /// the whole `${...}` over verbatim (its double-quote scanner has a
        /// `${` branch), so an operand's quote bytes reach here intact --
        /// previously the scanner split the word at the inner quote and the
        /// parser's re-fusion stripped them by accident, which is what made
        /// `${v#"a"}` appear to work. Quote removal is the operand's own step,
        /// and for a PATTERN operand the quoted metacharacters are then made
        /// literal so `${x#"a*"}` matches the text `a*` rather than globbing
        /// (SEMANTICS section 3.6). Operators whose operand is not a single
        /// value-or-pattern -- substring, replace, transform -- keep the plain
        /// `$`-only pass and handle their own structure.
        pe_operand_class_t op_class;
        char *expanded_default;
        /// A VALUE operand inside a `"..."` string is not a fresh word, so its
        /// quote characters are LITERAL -- `"${u:-'lit'}"` yields `'lit'` in
        /// both bash and zsh, while the unquoted `${u:-'lit'}` yields `lit`.
        /// A PATTERN operand dequotes in BOTH contexts (`"${p#'ab'}"` strips
        /// `ab`), because there the quotes mark glob-literality rather than
        /// word syntax. Verified across bash and zsh; only this one cell of the
        /// class-by-context table needs the enclosing quote state.
        bool value_operand_in_dq = in_double_quotes &&
                                   pe_operand_op(op_type, &op_class) &&
                                   op_class == PE_OPERAND_VALUE;
        if (value_operand_in_dq) {
            expanded_default =
                expand_variables_in_string(executor, default_value);
        } else if (op_type == 15 || op_type == 16) {
            /// `/` and `//`: two halves, split before quote removal.
            expanded_default = pe_process_replace_operand(
                executor, default_value, strlen(default_value));
        } else if (pe_operand_op(op_type, &op_class)) {
            expanded_default =
                pe_process_operand(executor, default_value,
                                   strlen(default_value), op_class, false);
        } else {
            expanded_default =
                expand_variables_in_string(executor, default_value);
        }

        char *result = NULL;

        /// Per-element dispatch for vector-yielding var names with case-
        /// modification operators. ${@^}, ${@^^[pat]}, ${@,}, ${@,,[pat]}
        /// and the analogous ${arr[@]^^[pat]} family apply the operator
        /// to each positional parameter or array element independently,
        /// then join with space. Bash semantics; scope intentionally
        /// narrowed to case-mod ops for issue #96 (other operators on
        /// vector names -- substitution, trim, substring -- are
        /// separate work).
        bool case_mod_op =
            (op_type == 4 || op_type == 5 || op_type == 8 || op_type == 9);
        /// Per-element-amenable scalar operators when the variable
        /// is a vector ($@, $* or arr[@] / arr[*]): case-mod, pattern
        /// strip, replace, and the @-transform family. Each applies
        /// to each element independently and the results join with
        /// space.
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
                /// arr[@] / arr[*]
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

            /// Apply the per-element scalar op to each element and join
            /// results with a single space. Each branch produces a
            /// freshly allocated transformed string per element; the
            /// join loop then concatenates with growth.
            size_t out_cap = 64;
            size_t out_pos = 0;
            char *joined = malloc(out_cap);
            if (joined) {
                joined[0] = '\0';
                for (int i = 0; i < n_elems; i++) {
                    char *converted = NULL;
                    switch (op_type) {
                    case 4: /// ^^ uppercase all
                    case 5: /// ,, lowercase all
                    case 8: /// ^  uppercase first / match
                    case 9: /// ,  lowercase first / match
                    {
                        bool to_upper = (op_type == 4 || op_type == 8);
                        bool first_only = (op_type == 8 || op_type == 9);
                        if (expanded_default && expanded_default[0]) {
                            converted =
                                lush_case_pattern(elems[i], expanded_default,
                                                  to_upper, first_only);
                        } else if (first_only) {
                            converted = to_upper
                                            ? lush_case_first_upper(elems[i])
                                            : lush_case_first_lower(elems[i]);
                        } else {
                            converted = to_upper
                                            ? lush_case_all_upper(elems[i])
                                            : lush_case_all_lower(elems[i]);
                        }
                        break;
                    }
                    case 2: /// ## longest prefix strip
                    case 6: /// #  shortest prefix strip
                    {
                        int match = lush_prefix_match_len(
                            elems[i], expanded_default, op_type == 2);
                        converted = strdup(elems[i] + match);
                        break;
                    }
                    case 3: /// %% longest suffix strip
                    case 7: /// %  shortest suffix strip
                    {
                        int match = lush_suffix_match_len(
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
                    case 15: /// /// replace all
                    case 16: /// /  replace first
                    {
                        /// NOTE: this per-element copy of the spec split has
                        /// NOT been folded onto split_substitution_spec in
                        /// param_op.c, and it already diverges -- its
                        /// no-separator branch skips the `\/` -> `/`
                        /// canonicalization, so `${arr[@]//\/}` leaves the
                        /// slashes the scalar `${v//\/}` removes. Tracked as
                        /// #684; folding it is a behavior change and belongs
                        /// in its own commit.
                        /// Split expanded_default at first unescaped '/'.
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
                            converted = lush_pattern_substitute(
                                elems[i], pattern, replacement, global);
                            free(pattern);
                        }
                        if (!converted) {
                            converted = strdup(elems[i]);
                        }
                        break;
                    }
                    case 17: /// @transform (Q E P A a)
                    {
                        char tcode = (expanded_default && expanded_default[0])
                                         ? expanded_default[0]
                                         : '\0';
                        switch (tcode) {
                        case 'Q': /// shell-quoted form
                        {
                            /// Conservative: wrap in single quotes and
                            /// escape embedded single quotes. Matches
                            /// bash's @Q for typical strings.
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
                        case 'U': /// uppercase all
                            converted = lush_case_all_upper(elems[i]);
                            break;
                        case 'u': /// uppercase first character
                            converted = lush_case_first_upper(elems[i]);
                            break;
                        case 'L': /// lowercase all
                            converted = lush_case_all_lower(elems[i]);
                            break;
                        case 'E': /// backslash-escape processing
                            /// Passthrough for now; per-element parity
                            /// with the scalar @E path.
                            converted = strdup(elems[i]);
                            break;
                        case 'P': /// prompt expansion
                            converted = strdup(elems[i]);
                            break;
                        case 'A': /// assignment form
                            converted = strdup(elems[i]);
                            break;
                        case 'a': /// attributes
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

        bool op_assign_back = false;
        result =
            apply_param_operator(executor, var_name, var_value,
                                 expanded_default, op_type, &op_assign_back);
        if (op_assign_back) {
            if (relaxed_bare_array) {
                /// := / = on a bare array in a relaxed mode assigns element 0
                /// and preserves the array shape (curated: the bash rule; a
                /// scalar symtable_set_var would clobber the array binding,
                /// and zsh's collapse-to-one-element is not adopted).
                symtable_array_set_index(relaxed_bare_array, 0, result);
            } else {
                symtable_set_var(executor->symtable, var_name, result,
                                 SYMVAR_NONE);
            }
        }
        free(var_name);
        free(var_value);
        free(expanded_default);
        return result;
    }

    /// No operator found, just get the variable value
    /// First check for special variables that aren't in the symbol table
    if (strlen(expansion) == 1) {
        char buffer[1024];

        switch (expansion[0]) {
        case '?': /// Exit status of last command
            snprintf(buffer, sizeof(buffer), "%d", last_exit_status);
            return strdup(buffer);

        case '$': /// Shell process ID
            snprintf(buffer, sizeof(buffer), "%d", (int)shell_pid);
            return strdup(buffer);

        case '#': /// Number of positional parameters
            snprintf(buffer, sizeof(buffer), "%d",
                     shell_argc > 1 ? shell_argc - 1 : 0);
            return strdup(buffer);

        case '!': /// Process ID of last background command
            if (last_background_pid > 0) {
                snprintf(buffer, sizeof(buffer), "%d",
                         (int)last_background_pid);
                return strdup(buffer);
            } else {
                return strdup("");
            }

        case '-': { /// Current option flags
            /// Build string of current shell option flags
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

        case '*': { /// $* / ${*} -- positionals joined on the first char of
                    /// IFS. Scope-aware (local positionals in function scope)
                    /// so the braced ${*} matches the unbraced $* and the
                    /// numeric $N form.
            char sep[2];
            ifs_join_separator(executor, sep);
            int n = 0;
            char **params = collect_positional_params(executor, &n);
            char *result = join_strings_with_sep(params, n, sep);
            for (int i = 0; i < n; i++) {
                free(params[i]);
            }
            free(params);
            return result ? result : strdup("");
        }

        case '@': { /// $@ / ${@} in a scalar context -- joined on the first
                    /// char of IFS (zsh/dash majority; the whole-word "$@"
                    /// vector form is handled earlier). Scope-aware, so the
                    /// braced ${@} reaches a function's local positionals
                    /// rather than the global argv.
            char sep[2];
            ifs_join_separator(executor, sep);
            int n = 0;
            char **params = collect_positional_params(executor, &n);
            char *result = join_strings_with_sep(params, n, sep);
            for (int i = 0; i < n; i++) {
                free(params[i]);
            }
            free(params);
            return result ? result : strdup("");
        }

        default:
            if (expansion[0] >= '0' && expansion[0] <= '9') {
                /// Handle positional parameters $0, $1, $2, etc.
                int pos = expansion[0] - '0';

                if (pos == 0) {
                    /// $0 is the script/shell name
                    return strdup((shell_argc > 0 && shell_argv[0])
                                      ? shell_argv[0]
                                      : "lush");
                } else if (pos > 0) {
                    /// $1, $2, etc. - check function scope first
                    if (symtable_in_function_scope(executor->symtable)) {
                        /// In function scope - get from local positional params
                        char param_name[16];
                        snprintf(param_name, sizeof(param_name), "%d", pos);
                        char *value =
                            symtable_get_var(executor->symtable, param_name);
                        if (value && value[0] != '\0') {
                            return value; /// Already allocated by
                                          /// symtable_get_var
                        }
                        free(value);
                        return strdup("");
                    } else if (pos < shell_argc && shell_argv[pos]) {
                        /// Global scope - use shell_argv
                        return strdup(shell_argv[pos]);
                    } else {
                        return strdup("");
                    }
                }
            }
            break;
        }
    }

    /// Fall back to symbol table lookup for regular variables
    /// Bare ${var} / ${arr} via the unified value view. The view
    /// condenses the historical "try array, fall back to scalar" dance
    /// into one call. For lists/maps, hand to expand_array_unsubscripted
    /// (which enforces SEMANTICS section 3.9 in zsh/lush mode); for
    /// scalars, take ownership of the strdup and continue to the
    /// set -u check.
    lush_value_view_t view = {0};
    symtable_lookup(expansion, &view);
    if (view.kind == LUSH_VALUE_LIST || view.kind == LUSH_VALUE_MAP) {
        char *result =
            expand_array_unsubscripted(executor, view.array, expansion);
        lush_value_view_clear(&view);
        return result;
    }
    /// Transfer ownership of the scalar strdup out of the view so the
    /// subsequent free path stays the same as the legacy code. clear()
    /// is now a no-op on the (zeroed) scalar field.
    char *value = view.scalar_value;
    view.scalar_value = NULL;
    lush_value_view_clear(&view);

    /// Check for unset variable error (set -u) for ${var} syntax
    if (!value && shell_opts.unset_error) {
        /// Don't error on special variables that have default behavior
        if (strlen(expansion) != 1 ||
            (expansion[0] != '?' && expansion[0] != '$' &&
             expansion[0] != '#' && expansion[0] != '0' &&
             expansion[0] != '@' && expansion[0] != '*' &&
             expansion[0] != '-' && expansion[0] != '!')) {
            /// Report structured error for unbound variable
            executor_error_report(executor, SHELL_ERR_UNBOUND_VARIABLE,
                                  executor_current_loc(executor),
                                  "%s: unbound variable", expansion);
            /// Set expansion error instead of exiting to allow || constructs
            executor->expansion_error = true;
            executor->expansion_exit_status = 1;
            return strdup(""); /// Return empty string for unbound variable
        }
    }

    /// value is already strdup'd by symtable_get_var, don't strdup again
    return value ? value : strdup("");
}

static char *apply_param_operator(executor_t *executor, const char *var_name,
                                  char *var_value, char *expanded_default,
                                  int op_type, bool *assign_back) {
    if (assign_back) {
        *assign_back = false;
    }
    /// Every operator that is a pure function of (value, operand) lives in
    /// param_op.c, so the Word CST bench evaluates it through the SAME code
    /// rather than a hand-written copy that can drift (issue #681).
    /// Operator 14 is pure only once the zsh modifier chains and the
    /// `$`-expanding offset spec -- both executor-dependent -- are resolved
    /// below; 17/18/19 need variable metadata or the error path.
    if (op_type != 14 && lush_param_op_is_pure(op_type)) {
        return lush_param_op_apply(op_type, var_value, expanded_default,
                                   assign_back);
    }
    char *result = NULL;
    switch (op_type) {
    case 14: /// ${var:offset:length} substring, or ${var:h...} modifiers
        if (var_value && shell_mode_allows(FEATURE_ZSH_PARAM_MODIFIERS) &&
            looks_like_zsh_modifier(expanded_default)) {
            /// zsh modifier chain (:h, :t, :r, :e, :l, :u, :q, :s///,
            /// :gs///). The leading char is a modifier letter, which is
            /// never a valid substring offset (those are numeric).
            result = apply_zsh_modifiers(executor, var_value, expanded_default);
        } else if (var_value) {
            /// Substring: `$`-expand the offset[:length] spec here (that is
            /// the executor-dependent half), then hand the resolved numeric
            /// spec to the shared pure core.
            char *expanded_offset_str =
                expand_variables_in_string(executor, expanded_default);
            result =
                lush_param_op_apply(14, var_value, expanded_offset_str, NULL);
            free(expanded_offset_str);
        } else {
            result = strdup("");
        }
        break;

    case 17: /// ${var@op} - transformations
        if (expanded_default[0]) {
            char op = expanded_default[0];
            /// The @a (attribute query) variant only inspects the
            /// variable's metadata and doesn't need var_value to
            /// be set. Arrays specifically have NULL var_value
            /// (scalar lookup misses them), so the prior
            /// `if (var_value && ...)` guard hid the attribute
            /// for `declare -A arr; echo "${arr@a}"`. Issue #102.
            /// Other @op flavors do still need a value; for those
            /// fall through to the empty-result path.
            if (op == 'a') {
                result = get_variable_attributes(var_name);
                break;
            }
            if (!var_value) {
                result = strdup("");
                break;
            }
            switch (op) {
            case 'Q': /// Quote value for reuse as input
                result = transform_quote(var_value);
                break;
            case 'E': /// Expand escape sequences (ANSI-C, like $'...')
                result = lush_expand_escapes(var_value, strlen(var_value),
                                             LUSH_ESC_ANSI_C);
                break;
            case 'P': /// Expand as prompt string
                result = transform_prompt(var_value);
                break;
            case 'A': /// Assignment statement form
                result = transform_assignment(var_name, var_value);
                break;
            case 'U': /// Uppercase all
                result = lush_case_all_upper(var_value);
                break;
            case 'u': /// Uppercase first
                result = lush_case_first_upper(var_value);
                break;
            case 'L': /// Lowercase all
                result = lush_case_all_lower(var_value);
                break;
            default:
                result = strdup(var_value);
                break;
            }
        } else {
            result = strdup("");
        }
        break;

    case 18: /// ${var:?word} - error if var unset or null (POSIX)
    case 19: /// ${var?word} - error if var unset (null permitted) (POSIX)
        /// The TRIGGER is a pure predicate over the value and lives in
        /// param_op.c so the Word CST evaluator tests the same rule when it
        /// decides whether it may cover this expansion (it defers a firing
        /// one -- it has no error channel). Only the diagnostic and the POSIX
        /// exit request stay here.
        if (lush_param_op_required_fires(op_type, var_value)) {
            result = handle_required_param_error(
                executor, var_name, expanded_default,
                op_type == 18 ? "parameter null or not set"
                              : "parameter not set");
        } else {
            result = lush_param_op_required_value(var_value);
        }
        break;
    }
    return result ? result : strdup("");
}

static int detect_param_operator_suffix(const char *suffix,
                                        const char **rhs_out) {
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; param_operators[i]; i++) {
        size_t ol = strlen(param_operators[i]);
        if (ol > best_len && strncmp(suffix, param_operators[i], ol) == 0) {
            best = i;
            best_len = ol;
        }
    }
    if (best >= 0 && rhs_out) {
        *rhs_out = suffix + best_len;
    }
    return best;
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

    /// Special case: if var_text is exactly "$$", treat it as shell PID
    if (strcmp(var_text, "$$") == 0) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", (int)shell_pid);
        return strdup(buffer);
    }

    /// Special case: if var_text is exactly "$", treat it as shell PID
    if (strcmp(var_text, "$") == 0) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", (int)shell_pid);
        return strdup(buffer);
    }

    /// Special case: if var_text is exactly "$?", treat it as exit status
    if (strcmp(var_text, "$?") == 0) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", last_exit_status);
        return strdup(buffer);
    }

    const char *var_name = var_text + 1;

    /// Handle ${var} format with advanced parameter expansion
    if (var_name[0] == '{') {

        /// Find the matching closing brace, not the first one. Nested
        /// parameter expansion ${(flag)${INNER}} has an inner `}` that
        /// the outer brace match must skip past. strchr would stop at
        /// the inner brace and leave the outer expansion truncated.
        /// lush_find_matching_brace counts depth and returns the matched
        /// close. Issue #98.
        size_t close_offset = 0;
        char *close = lush_find_matching_brace(var_name, 0, &close_offset)
                          ? (char *)(var_name + close_offset)
                          : strchr(var_name, '}');
        if (close) {
            size_t len = close - var_name - 1;
            char *expansion = malloc(len + 1);
            if (expansion) {
                strncpy(expansion, var_name + 1, len);
                expansion[len] = '\0';

                char *result =
                    parse_parameter_expansion(executor, expansion, false);

                free(expansion);
                return result;
            }
        }
    } else {
        /// Simple $var format - handle special variables and regular variables
        size_t name_len = 0;

        /// Check for special single-character variables first
        if (var_name[0] == '?' || var_name[0] == '$' || var_name[0] == '#' ||
            var_name[0] == '*' || var_name[0] == '@' || var_name[0] == '!' ||
            var_name[0] == '-' || (var_name[0] >= '0' && var_name[0] <= '9')) {
            name_len = 1;
        } else {
            /// Regular variable names. lush_ident_match_continue
            /// returns the byte length consumed (1 for ASCII, 2-4 for
            /// multi-byte UTF-8 when FEATURE_UNICODE_IDENTIFIERS is on);
            /// loop until it stops matching.
            size_t total = strlen(var_name);
            while (name_len < total) {
                size_t n = lush_ident_match_continue(var_name + name_len,
                                                     total - name_len);
                if (n == 0) {
                    break;
                }
                name_len += n;
            }
        }

        /// Zsh bare-subscript form: $var[N] / $var[N,M]. The caller in
        /// expand_variables_in_string already consumed the bracket span
        /// into var_text when FEATURE_ZSH_BARE_SUBSCRIPT is enabled, so
        /// if we see '[' after the name we route through
        /// parse_parameter_expansion("var[N]") — same backend as the
        /// brace form ${var[N]}. Gating here is a defensive double-check;
        /// the primary gate is at the caller.
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
                        parse_parameter_expansion(executor, expansion, false);
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

                /// Resolve nameref if applicable (max depth 10 to prevent
                /// loops)
                const char *resolved_name = name;
                char *resolved_to_free = NULL; /// Track if we need to free
                if (symtable_is_nameref(executor->symtable, name)) {
                    const char *target =
                        symtable_resolve_nameref(executor->symtable, name, 10);
                    if (target && target != name) {
                        resolved_name = target;
                        resolved_to_free = (char *)target;
                    }
                }

                /// Bare $arr / $var via the unified value view (mirrors
                /// the parse_parameter_expansion braced ${arr} site).
                /// bash mode gives first element, zsh/lush mode raises a
                /// type error in scalar slots; both come out of
                /// expand_array_unsubscripted. (Issue #65.)
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
                    /// Caller (expand_variables_in_string) appends any
                    /// trailing literal text after the variable name.
                    return result ? result : strdup("");
                }
                /// Transfer ownership of the scalar out of the view so
                /// the legacy unset / set -u path below stays identical.
                char *value = view.scalar_value;
                view.scalar_value = NULL;
                lush_value_view_clear(&view);

                /// Free resolved nameref if it was allocated
                if (resolved_to_free) {
                    free(resolved_to_free);
                }

                /// Check for unset variable error (set -u)
                if (!value && shell_opts.unset_error && name_len > 0) {
                    /// Don't error on special variables that have default
                    /// behavior
                    if (name_len != 1 ||
                        (name[0] != '?' && name[0] != '$' && name[0] != '#' &&
                         name[0] != '0' && name[0] != '@' && name[0] != '*' &&
                         name[0] != '-' && name[0] != '!')) {
                        /// Report structured error for unbound variable
                        executor_error_report(executor,
                                              SHELL_ERR_UNBOUND_VARIABLE,
                                              executor_current_loc(executor),
                                              "%s: unbound variable", name);
                        free(name);
                        /// Set expansion error instead of exiting to allow ||
                        /// constructs
                        executor->expansion_error = true;
                        executor->expansion_exit_status = 1;
                        return strdup(
                            ""); /// Return empty string for unbound variable
                    }
                }

                /// If not found in symbol table and it's a special variable,
                /// handle it directly
                if (!value && name_len == 1) {
                    char buffer[1024];

                    switch (name[0]) {
                    case '?': /// Exit status of last command
                        snprintf(buffer, sizeof(buffer), "%d",
                                 last_exit_status);
                        free(name);
                        return strdup(buffer);

                    case '$': /// Shell process ID
                        snprintf(buffer, sizeof(buffer), "%d", (int)shell_pid);
                        free(name);
                        return strdup(buffer);

                    case '#': /// Number of positional parameters
                        snprintf(buffer, sizeof(buffer), "%d",
                                 shell_argc > 1 ? shell_argc - 1 : 0);
                        free(name);
                        return strdup(buffer);

                    case '!': /// Process ID of last background command
                        if (last_background_pid > 0) {
                            snprintf(buffer, sizeof(buffer), "%d",
                                     (int)last_background_pid);
                            free(name);
                            return strdup(buffer);
                        } else {
                            free(name);
                            return strdup("");
                        }

                    case '*': { /// ${*} -- positionals joined on IFS[0]
                        char sep[2];
                        ifs_join_separator(executor, sep);
                        char *result = NULL;
                        char *func_argc_str =
                            symtable_get_var(executor->symtable, "#");
                        if (func_argc_str && executor->symtable) {
                            /// Function scope: local positional parameters.
                            int func_argc = atoi(func_argc_str);
                            char **items = NULL;
                            int n = 0;
                            for (int i = 1; i <= func_argc; i++) {
                                char param_name[16];
                                snprintf(param_name, sizeof(param_name), "%d",
                                         i);
                                char *v = symtable_get_var(executor->symtable,
                                                           param_name);
                                if (v) {
                                    char **grown = realloc(
                                        items, (n + 1) * sizeof(char *));
                                    if (!grown) {
                                        free(v);
                                        break;
                                    }
                                    items = grown;
                                    items[n++] = v;
                                }
                            }
                            result = join_strings_with_sep(items, n, sep);
                            for (int i = 0; i < n; i++) {
                                free(items[i]);
                            }
                            free(items);
                        } else {
                            /// Global positional parameters.
                            result = join_strings_with_sep(
                                shell_argv + 1,
                                shell_argc > 1 ? shell_argc - 1 : 0, sep);
                        }
                        free(func_argc_str);
                        free(name);
                        return result ? result : strdup("");
                    }

                    case '@': { /// ${@} in a scalar context -- joined on the
                                /// first char of IFS (the whole-word "${@}"
                                /// vector form is handled earlier).
                        char sep[2];
                        ifs_join_separator(executor, sep);
                        char *result = NULL;
                        char *func_argc_str =
                            symtable_get_var(executor->symtable, "#");
                        if (func_argc_str && executor->symtable) {
                            /// Function scope: local positional parameters.
                            int func_argc = atoi(func_argc_str);
                            char **items = NULL;
                            int n = 0;
                            for (int i = 1; i <= func_argc; i++) {
                                char param_name[16];
                                snprintf(param_name, sizeof(param_name), "%d",
                                         i);
                                char *v = symtable_get_var(executor->symtable,
                                                           param_name);
                                if (v) {
                                    char **grown = realloc(
                                        items, (n + 1) * sizeof(char *));
                                    if (!grown) {
                                        free(v);
                                        break;
                                    }
                                    items = grown;
                                    items[n++] = v;
                                }
                            }
                            result = join_strings_with_sep(items, n, sep);
                            for (int i = 0; i < n; i++) {
                                free(items[i]);
                            }
                            free(items);
                        } else {
                            /// Global positional parameters.
                            result = join_strings_with_sep(
                                shell_argv + 1,
                                shell_argc > 1 ? shell_argc - 1 : 0, sep);
                        }
                        free(func_argc_str);
                        free(name);
                        return result ? result : strdup("");
                    }

                    case '-': { /// Current option flags
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
                            /// Handle positional parameters $0, $1, $2, etc.
                            int pos = name[0] - '0';

                            if (pos == 0) {
                                /// $0 is the script/shell name
                                free(name);
                                return strdup((shell_argc > 0 && shell_argv[0])
                                                  ? shell_argv[0]
                                                  : "lush");
                            } else if (pos > 0 && pos < shell_argc &&
                                       shell_argv[pos]) {
                                /// $1, $2, etc. are script arguments
                                free(name);
                                return strdup(shell_argv[pos]);
                            } else {
                                /// Parameter doesn't exist, return empty string
                                free(name);
                                return strdup("");
                            }
                        }
                        break;
                    }
                }

                free(name);
                /// value is already strdup'd by symtable_get_var
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

    /// Find the end of the tilde expression (until '/' or end of string)
    const char *slash = strchr(text, '/');
    const char *rest = slash ? slash : "";
    size_t tilde_len = slash ? (size_t)(slash - text) : strlen(text);

    if (tilde_len == 1) {
        /// Simple ~ expansion to $HOME
        const char *home = getenv("HOME");
        if (!home) {
            /// Fallback if HOME is not set
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
        /// ~user expansion to user's home directory
        char *username = malloc(tilde_len);
        if (!username) {
            return strdup(text);
        }

        strncpy(username, text + 1, tilde_len - 1);
        username[tilde_len - 1] = '\0';

        struct passwd *pw = getpwnam(username);
        free(username);

        if (!pw) {
            /// User not found, return original text
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

/// True when `word` is an assignment-style word `NAME=...` with NAME a valid
/// shell identifier -- the form zsh's magic_equal_subst acts on. A bare `=`,
/// a non-identifier left side (`a/b=x`), or a leading digit does not qualify.
/// NAME is validated through lush_is_valid_identifier -- the same predicate the
/// real assignment path uses -- so it honors FEATURE_UNICODE_IDENTIFIERS and
/// does not do byte comparisons on possibly-non-ASCII text.
static bool is_magic_equal_word(const char *word) {
    if (!word) {
        return false;
    }
    const char *eq = strchr(word, '=');
    if (!eq || eq == word) {
        return false;
    }
    char *name = strndup(word, (size_t)(eq - word));
    if (!name) {
        return false;
    }
    bool ok = lush_is_valid_identifier(name);
    free(name);
    return ok;
}

/// Byte length of an expansion construct starting at `s` -- a backtick command
/// substitution, `$(...)`, `$((...))`, or `${...}` -- or 0 if `s` does not
/// begin one. Its interior (colons and tildes included) is the source of a
/// later expansion, not a path, so assignment tilde expansion must treat the
/// whole span opaquely. Quotes and nested constructs inside are respected so a
/// `)` / `}` / backtick inside a quoted or nested span does not close early.
static size_t expansion_construct_len(const char *s, size_t n) {
    if (n == 0) {
        return 0;
    }
    if (s[0] == '`') {
        for (size_t i = 1; i < n; i++) {
            if (s[i] == '\\' && i + 1 < n) {
                i++;
                continue;
            }
            if (s[i] == '`') {
                return i + 1;
            }
        }
        return n; /// unterminated: consume the rest
    }
    if (s[0] == '$' && n >= 2 && (s[1] == '(' || s[1] == '{')) {
        char close = (s[1] == '(') ? ')' : '}';
        bool in_sq = false;
        bool in_dq = false;
        for (size_t i = 2; i < n; i++) {
            char c = s[i];
            if (c == '\\' && i + 1 < n) {
                i++;
                continue;
            }
            if (in_sq) {
                if (c == '\'') {
                    in_sq = false;
                }
                continue;
            }
            if (in_dq) {
                if (c == '"') {
                    in_dq = false;
                }
                continue;
            }
            if (c == '\'') {
                in_sq = true;
                continue;
            }
            if (c == '"') {
                in_dq = true;
                continue;
            }
            if (c == close) {
                return i + 1;
            }
            /// Recurse into a nested construct so its own delimiters do not
            /// prematurely close this one.
            size_t sub = expansion_construct_len(s + i, n - i);
            if (sub > 0) {
                i += sub - 1;
            }
        }
        return n; /// unterminated
    }
    return 0;
}

/// Assignment-context tilde expansion (POSIX 2.6.1, matched by bash and zsh): a
/// tilde-prefix at the value start and immediately after every unquoted colon
/// is tilde-expanded, both the leading and colon segments (`~/a:~bob/b`). Quote
/// provenance is carried into `value` by the parser: single-quoted spans are
/// re-wrapped `'...'` (tracked here so an interior colon does not split and an
/// interior `~` is not expanded); a `~` that was inside double quotes arrives
/// escaped as `\~`, which never begins with a bare `~` and so is left for the
/// downstream backslash-removal pass. Expansion constructs ($(...), ${...},
/// `...`) are skipped opaquely so their interior colons/tildes are untouched.
/// Returns a newly-allocated string, or NULL on allocation failure. Shared by
/// execute_assignment and magic_equal_subst.
static char *colon_segmented_tilde_expand(const char *value) {
    if (!value) {
        return NULL;
    }
    size_t vlen = strlen(value);
    size_t cap = vlen + 1;
    char *out = malloc(cap);
    if (!out) {
        return NULL;
    }
    size_t olen = 0;
    size_t seg_start = 0;
    bool in_squote = false;

    for (size_t i = 0;; i++) {
        bool at_end = (i == vlen);
        char c = at_end ? '\0' : value[i];
        /// An expansion construct ($(...), $((...)), ${...}, `...`) is opaque:
        /// its interior colons and tildes belong to a later expansion, not to
        /// this value's path segments, so skip the whole span. Not inside
        /// '...', where such text is literal.
        if (!at_end && !in_squote) {
            size_t clen = expansion_construct_len(value + i, vlen - i);
            if (clen > 0) {
                i += clen - 1;
                continue;
            }
        }
        /// A backslash escapes the next byte: neither can toggle quoting or act
        /// as a separator. Skip both (the loop's i++ skips the backslash).
        if (!at_end && c == '\\' && value[i + 1]) {
            i++;
            continue;
        }
        if (!at_end && c == '\'') {
            in_squote = !in_squote;
            continue;
        }
        bool is_sep = !at_end && c == ':' && !in_squote;
        if (!is_sep && !at_end) {
            continue;
        }

        /// Emit value[seg_start, i), tilde-expanding it iff it begins with a
        /// bare ~ (a single-quoted segment begins with ' and an escaped tilde
        /// with \, so neither is touched).
        size_t seg_len = i - seg_start;
        char *seg = strndup(value + seg_start, seg_len);
        if (!seg) {
            free(out);
            return NULL;
        }
        char *piece = seg;
        if (seg_len > 0 && seg[0] == '~') {
            char *exp = expand_tilde(seg);
            if (exp) {
                free(seg);
                piece = exp;
            }
            /// expand_tilde OOM (NULL) -> keep the literal segment.
        }

        size_t plen = strlen(piece);
        if (olen + plen + 2 > cap) {
            cap = (olen + plen + 2) * 2;
            char *grown = realloc(out, cap);
            if (!grown) {
                free(piece);
                free(out);
                return NULL;
            }
            out = grown;
        }
        memcpy(out + olen, piece, plen);
        olen += plen;
        free(piece);

        if (is_sep) {
            out[olen++] = ':';
            seg_start = i + 1;
        }
        if (at_end) {
            break;
        }
    }

    out[olen] = '\0';
    return out;
}

/// The assignment-aware builtins. Their name=value arguments follow
/// assignment semantics -- notably tilde expansion of the value after `=` and
/// each unquoted `:`, exactly like a real assignment RHS (POSIX 2.6.1; bash and
/// zsh agree). This is independent of the zsh magic_equal_subst option, which
/// extends the same treatment to every command's arguments.
/// is_assignment_builtin lives in src/shell_mode.c so the parser (which uses
/// it at parse time) can link it without pulling in the executor.

/// For magic_equal_subst: tilde-expand the value of an assignment-style word
/// via the shared colon_segmented_tilde_expand primitive -- the same rule a
/// real assignment RHS follows (`foo=~/a:~bob/b`). Returns a newly-allocated
/// word, or NULL when `word` is not an assignment-style word (caller keeps the
/// original) or on allocation failure.
static char *magic_equal_tilde_expand(const char *word) {
    if (!is_magic_equal_word(word)) {
        return NULL;
    }

    const char *eq = strchr(word, '=');
    size_t head = (size_t)(eq - word) + 1; /// through the '='

    char *value = colon_segmented_tilde_expand(eq + 1);
    if (!value) {
        return NULL;
    }
    size_t vlen = strlen(value);
    char *out = malloc(head + vlen + 1);
    if (!out) {
        free(value);
        return NULL;
    }
    memcpy(out, word, head);             /// copy "NAME="
    memcpy(out + head, value, vlen + 1); /// value plus its NUL
    free(value);
    return out;
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

    /// Use the modern arithmetic evaluator with executor context for scoped
    /// variables
    char *result = arithm_expand_with_executor(executor, arith_text);
    if (result) {
        return result;
    }

    /// A nested $((...)) expanded during an enclosing arithmetic expression's
    /// Layer-0 pre-pass: leave the error flagged for the enclosing expression
    /// to detect and report exactly once. Rendering it here would both
    /// double-report the same diagnostic and let the enclosing expansion clear
    /// it, so return an empty string without touching the executor state.
    if (arithm_expansion_in_progress()) {
        return strdup("");
    }

    /// Drain the typed error state from arithmetic.c and emit a fully
    /// structured shell error: specific code (one per failure mode rather
    /// than the old blanket SHELL_ERR_ARITHMETIC_SYNTAX), site-specific
    /// `while:` context, and site-specific `help:` suggestion. The
    /// arithmetic module owns the error semantics; the executor owns
    /// displaying them.
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
            /// Fallback if shell_error_create failed (e.g. OOM)
            executor_error_report(
                executor, arithm_error_code(), executor_current_loc(executor),
                "arithmetic: %s", msg ? msg : "evaluation error");
        }
    } else {
        executor_error_report(executor, SHELL_ERR_ARITHMETIC_SYNTAX,
                              executor_current_loc(executor),
                              "arithmetic: evaluation error");
    }

    /// Set expansion error flag instead of immediate exit status
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
/**
 * @brief The $(<file) / `<file` fast read.
 *
 * When a command-substitution body is a lone input redirection with no
 * command (`< file`), expand to the file's contents directly, without
 * forking a capturing subshell -- the "no subshell" idiom
 * ADVANCED_SCRIPTING_GUIDE.md advertises, and the behavior bash and zsh
 * share (a lone `< file` command otherwise produces no output, so the
 * substitution was silently empty).
 *
 * The body must be exactly a plain `<` input redirection: a leading `<`
 * (not `<<`, `<<<`, `<&`, `<>`, `<(...)`) followed by a single filename word.
 * lush's parser does not accept a redirection with no command, so the redirect
 * is attached to a synthetic no-op `:` and the resulting AST is required to be
 * that `:` carrying exactly one `<` redirection child and nothing else; the
 * filename then expands through the same expand_arg_node the redirection path
 * uses. Anything else (a real command, additional redirections, a here-doc,
 * an fd dup, a process substitution) returns false to fall through to the
 * normal captured subshell.
 *
 * @return true if handled (result set to an owned string, possibly ""),
 *         false to fall through to the subshell path (result untouched).
 */
static bool cmdsub_try_fast_read(executor_t *executor, const char *command,
                                 char **result) {
    /// Fast reject: the body must begin with a plain `<` input redirect.
    /// Leading whitespace -- including the newlines of a multi-line spelling
    /// such as `$(\n  <file\n)` -- is skipped so that form is still read (bash
    /// and zsh read it) rather than silently falling through to empty.
    const char *p = command;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' ||
           *p == '\f') {
        p++;
    }
    if (p[0] != '<' || p[1] == '<' || p[1] == '&' || p[1] == '>' ||
        p[1] == '(') {
        return false;
    }

    /// Attach the redirect to a no-op `:` so it parses as a well-formed
    /// command (a bare redirection is otherwise a parse error), letting the
    /// filename reuse the redirection-target expansion. Build from the trimmed
    /// `p`, not the raw body, so leading newlines do not push the redirect
    /// onto a second line of the synthetic (which would parse as its own
    /// command and defeat the detection).
    size_t syn_len = strlen(p) + 3;
    char *synthetic = malloc(syn_len);
    if (!synthetic) {
        return false;
    }
    snprintf(synthetic, syn_len, ": %s", p);

    parser_t *parser =
        parser_new_with_source(synthetic, "<command substitution>", 1);
    if (!parser) {
        free(synthetic);
        return false;
    }
    node_t *ast = parser_parse(parser);
    node_t *target_node = NULL;
    if (ast && !parser_has_error(parser) && !ast->next_sibling &&
        ast->type == NODE_COMMAND) {
        node_t *child = ast->first_child;
        if (child && child->type == NODE_REDIR_IN && !child->next_sibling &&
            child->first_child &&
            child->first_child->type != NODE_PROC_SUB_IN) {
            target_node = child->first_child;
        }
    }
    if (!target_node) {
        if (ast) {
            free_node_tree(ast);
        }
        parser_free(parser);
        free(synthetic);
        return false;
    }

    /// Expand the filename exactly as a redirection target does (#505).
    char *path = expand_arg_node(executor, target_node);
    source_location_t loc = target_node->loc;
    free_node_tree(ast);
    parser_free(parser);
    free(synthetic);
    if (!path) {
        *result = strdup("");
        return true;
    }

    /// Honor the same privileged-mode restriction the real redirection path
    /// applies to a redirect target (redirection.c), so the fast read is not a
    /// way around it.
    if (!is_privileged_redirection_allowed(path)) {
        fprintf(stderr,
                "lush: %s: restricted redirection target in privileged mode\n",
                path);
        executor->exit_status = 1;
        free(path);
        *result = strdup("");
        return true;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        shell_error_t *error =
            shell_error_create(SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR,
                               loc, "%s: %s", path, strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        executor->exit_status = 1;
        free(path);
        *result = strdup("");
        return true;
    }

    char *buf = NULL;
    size_t cap = 0, len = 0;
    char chunk[4096];
    ssize_t n;
    bool read_error = false;
    while ((n = read(fd, chunk, sizeof(chunk))) != 0) {
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            read_error = true;
            break; /// I/O error (e.g. EISDIR on a directory target)
        }
        if (len + (size_t)n + 1 > cap) {
            size_t newcap = cap ? cap : 4096;
            while (len + (size_t)n + 1 > newcap) {
                newcap *= 2;
            }
            char *nb = realloc(buf, newcap);
            if (!nb) {
                free(buf);
                close(fd);
                free(path);
                *result = strdup("");
                return true;
            }
            buf = nb;
            cap = newcap;
        }
        memcpy(buf + len, chunk, (size_t)n);
        len += (size_t)n;
    }
    close(fd);
    free(path);
    /// A read failure (a directory target, or a mid-file I/O error) is
    /// signalled through $? rather than silently reported as success with
    /// truncated data -- the silent-empty-on-failure footgun this fast read
    /// exists to remove. This curates toward zsh's honest failure over bash's
    /// silent success.
    executor->exit_status = read_error ? 1 : 0;

    if (!buf) {
        *result = strdup(""); /// empty file (or an unreadable directory)
        return true;
    }
    buf[len] = '\0';
    /// Strip trailing newlines, like a normal command substitution.
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    *result = buf;
    return true;
}

static char *expand_command_substitution(executor_t *executor,
                                         const char *cmd_text) {
    if (!executor || !cmd_text) {
        return strdup("");
    }

    /// Record that a command substitution ran during the current word
    /// expansion; a resulting null command adopts its status (see the argc==0
    /// path in execute_command_dispatch).
    executor->word_cmdsub_ran = true;

    /// Extract command from $(command) or `command` format
    char *command = NULL;
    if (strncmp(cmd_text, "$(", 2) == 0 &&
        cmd_text[strlen(cmd_text) - 1] == ')') {
        /// Extract from $(command)
        size_t len = strlen(cmd_text) - 3; /// Remove $( and )
        command = malloc(len + 1);
        if (!command) {
            return strdup("");
        }
        strncpy(command, cmd_text + 2, len);
        command[len] = '\0';
    } else if (cmd_text[0] == '`' && cmd_text[strlen(cmd_text) - 1] == '`') {
        /// Extract from `command`
        size_t len = strlen(cmd_text) - 2; /// Remove backticks
        command = malloc(len + 1);
        if (!command) {
            return strdup("");
        }
        strncpy(command, cmd_text + 1, len);
        command[len] = '\0';
    } else {
        /// Already extracted command
        command = strdup(cmd_text);
        if (!command) {
            return strdup("");
        }
    }

    /// The $(<file) fast read: a lone input redirection expands to the file's
    /// contents without forking a capturing subshell (see
    /// cmdsub_try_fast_read).
    char *fast_result = NULL;
    if (cmdsub_try_fast_read(executor, command, &fast_result)) {
        free(command);
        return fast_result;
    }

    /// Pre-fork variable expansion of the command text was removed in
    /// the #97 fix: it collapsed array values ("${!arr[@]}", "${arr[@]}")
    /// into space-joined scalars before the child parser ever saw them,
    /// which destroyed the per-element word boundaries the child would
    /// otherwise have honored via the vector-expansion path in
    /// build_argv_from_ast. The child inherits parent state through
    /// fork() (full memory copy, including non-exported locals), so it
    /// can parse and expand the raw command text natively -- which is
    /// also what bash/dash/zsh do for $(...) -- and produce correctly
    /// separated arguments. Pre-expansion was an architectural layering
    /// violation that masked array semantics.

    /// Create a pipe to capture command output
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
        /// Child process - execute command using lush's own parser/executor.
        /// Reset the inherited interactive SIGHUP/SIGSEGV handlers so a
        /// hangup or fault terminates the substitution child normally.
        reset_subshell_signals();
        close(pipefd[0]);               /// Close read end
        dup2(pipefd[1], STDOUT_FILENO); /// Redirect stdout to pipe
        close(pipefd[1]);

        /// Parse and execute command using lush's own parser/executor
        /// This preserves all function definitions and variables in the child
        const char *src_name = executor->current_script_file
                                   ? executor->current_script_file
                                   : "<command substitution>";
        /// Command substitution context: input is its own logical
        /// source slice, line 1 of that slice.
        parser_t *parser = parser_new_with_source(command, src_name, 1);
        int result = 127;

        if (parser) {
            node_t *ast = parser_parse(parser);
            if (!parser_has_error(parser) && ast) {
                /// Execute in current context (functions are inherited via
                /// fork) Use executor_execute to handle command sequences
                /// (next_sibling)
                result = executor_execute(executor, ast);
                free_node_tree(ast);
            }
            parser_free(parser);
        }

        /// Ensure all output is flushed before exit
        fflush(stdout);
        free(command);
        subshell_cleanup();
        _exit(result);
    } else {
        /// Parent process - read output
        close(pipefd[1]); /// Close write end
        free(command);

        char *output = malloc(1024);
        size_t output_size = 1024;
        size_t output_len = 0;

        if (!output) {
            close(pipefd[0]);
            executor_wait_foreground(pid, NULL);
            return strdup("");
        }

        ssize_t bytes_read;
        char buffer[4096];

        /// Drain the capture pipe to EOF BEFORE reaping the child. Reaping
        /// first deadlocks on large output: a child whose stdout exceeds the
        /// kernel pipe buffer (~64KB) blocks in write() on the full pipe while
        /// the parent blocks in wait() on that same child. poll() keeps the
        /// drain responsive to a hangup -- exit_flag is raised by the SIGHUP
        /// cascade -- so a mid-capture hangup abandons the read promptly rather
        /// than blocking on a grandchild that still holds the write end.
        bool abandoned = false;
        for (;;) {
            /// A hangup during capture: forward it to the substitution child so
            /// it stops writing, record the hangup status, and abandon the
            /// read. Draining before reaping means executor_wait_foreground --
            /// which formerly raised exit_flag off this same flag -- has not
            /// run yet, so poll the raw sighup flag here directly.
            if (sighup_was_received()) {
                kill(pid, SIGHUP);
                set_exit_status(128 + SIGHUP);
                exit_flag = true;
            }
            if (exit_flag) {
                abandoned = true;
                break;
            }
            /// 100ms timeout bounds how long a hangup can go unobserved.
            struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN};
            int pr = poll(&pfd, 1, 100);
            if (pr < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break; /// unexpected poll failure; stop draining
            }
            if (pr == 0) {
                continue; /// timeout; loop back to re-check exit_flag
            }
            bytes_read = read(pipefd[0], buffer, sizeof(buffer));
            if (bytes_read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break; /// read error; stop draining
            }
            if (bytes_read == 0) {
                break; /// EOF: every write end of the pipe is closed
            }
            /// Keep at least one spare byte for the terminating NUL below.
            if (output_len + (size_t)bytes_read + 1 > output_size) {
                while (output_len + (size_t)bytes_read + 1 > output_size) {
                    output_size *= 2;
                }
                char *new_output = realloc(output, output_size);
                if (!new_output) {
                    free(output);
                    close(pipefd[0]);
                    executor_wait_foreground(pid, NULL);
                    return strdup("");
                }
                output = new_output;
            }
            memcpy(output + output_len, buffer, bytes_read);
            output_len += bytes_read;
        }

        close(pipefd[0]);

        /// A hangup abandoned the capture: the shell is terminating and the
        /// captured value is unused. Leave the orphaned child for the
        /// terminating shell to reap rather than risk blocking on it here.
        if (abandoned) {
            free(output);
            return strdup("");
        }

        /// The child has closed its write end (EOF above), so reaping it now
        /// cannot block. Propagate its exit status for $?.
        int status = 0;
        executor_wait_foreground(pid, &status);
        if (WIFEXITED(status)) {
            executor->exit_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            executor->exit_status = 128 + WTERMSIG(status);
        }

        /// The drain loop always keeps a spare byte for the terminator, so
        /// null-terminate directly, then strip trailing newlines.
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
        /// Create a copy of the function definition
        function_def_t *new_func = malloc(sizeof(function_def_t));
        if (!new_func) {
            break;
        }

        new_func->name = strdup(src_func->name);
        if (!new_func->name) {
            free(new_func);
            break;
        }

        /// Deep-copy the function body AST
        new_func->body = node_copy(src_func->body);
        if (!new_func->body) {
            free(new_func->name);
            free(new_func);
            break;
        }

        /// Add to destination's function list
        new_func->next = dest->functions;
        dest->functions = new_func;

        src_func = src_func->next;
    }
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
/// Thin wrapper: the whole-string double-quote policy, no per-character map.
static char *expand_quoted_string(executor_t *executor, const char *str,
                                  bool in_double_quotes) {
    return expand_quoted_string_prov(executor, str, in_double_quotes, NULL,
                                     true);
}

char *expand_dequoted_key(executor_t *executor, const char *text,
                          const char *prov) {
    /// Array-subscript key normalization: the text/prov already came through
    /// lush_dequote_span, so apply the double-quote/`$`-expansion rules the
    /// provenance map encodes -- but a subscript is a single-word key context,
    /// not a word, so a leading `~` keys on the literal `~`
    /// (allow_tilde=false).
    return expand_quoted_string_prov(executor, text, false, prov, false);
}

static char *expand_quoted_string_prov(executor_t *executor, const char *str,
                                       bool in_double_quotes, const char *prov,
                                       bool allow_tilde) {
    if (!executor || !str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    /// The per-character quote map must line up with the string one-for-one; a
    /// mismatch (should never happen) disables it rather than risking an
    /// out-of-bounds read.
    if (prov && strlen(prov) != len) {
        prov = NULL;
    }

    /// Allocate a buffer for expansion (estimate double the original size)
    size_t buffer_size = len * 2 + 256;
    char *result = malloc(buffer_size);
    if (!result) {
        return strdup("");
    }

    size_t result_pos = 0;
    size_t i = 0;

    /// Edit 4 -- an unquoted leading `~` is tilde-expanded even though the word
    /// as a whole came through the double-quote expander (a fused word like
    /// `~/a"b"`). Only fires when the first character is genuinely unquoted; a
    /// quoted or non-`~` first character keeps the legacy behavior. A `~` after
    /// a `:` is NOT expanded here -- that is the assignment/magic_equal path.
    /// Callers that are not a word context (an array subscript key, which keys
    /// on a literal `~`) pass allow_tilde=false to suppress it.
    if (allow_tilde && prov && str[0] == '~' &&
        prov[0] == QUOTE_PROV_UNQUOTED) {
        size_t tprefix = 1; /// consume `~` and any `~user` up to `/` or end
        while (tprefix < len && str[tprefix] != '/' &&
               prov[tprefix] == QUOTE_PROV_UNQUOTED) {
            tprefix++;
        }
        char *tprefix_str = malloc(tprefix + 1);
        char *home = NULL;
        if (tprefix_str) {
            memcpy(tprefix_str, str, tprefix);
            tprefix_str[tprefix] = '\0';
            home = expand_tilde(tprefix_str);
            free(tprefix_str);
        }
        if (home) {
            size_t hlen = strlen(home);
            while (result_pos + hlen + 1 >= buffer_size) {
                buffer_size *= 2;
                char *nr = realloc(result, buffer_size);
                if (!nr) {
                    free(home);
                    free(result);
                    return strdup("");
                }
                result = nr;
            }
            memcpy(result + result_pos, home, hlen);
            result_pos += hlen;
            free(home);
            i = tprefix; /// continue after the consumed `~`-prefix
        }
    }

    while (i < len) {
        /// Edit 1 -- a single-quoted or backslash-escaped character is literal:
        /// no `$`/backtick/backslash processing. This is what keeps a
        /// single-quoted `$x` (`'$x'y`) from expanding.
        if (prov &&
            (prov[i] == QUOTE_PROV_SINGLE || prov[i] == QUOTE_PROV_ESCAPED)) {
            if (result_pos + 2 >= buffer_size) {
                buffer_size *= 2;
                char *nr = realloc(result, buffer_size);
                if (!nr) {
                    free(result);
                    return strdup("");
                }
                result = nr;
            }
            result[result_pos++] = str[i++];
            continue;
        }
        /// `@` and `%` kind sigils are bare-word-only: like `~` tilde
        /// expansion, double quotes suppress them, so a quoted string stays a
        /// literal -- `printf "%s\n"`, `"user@host"`, and `"100% off"` all
        /// survive.  The tokenizer recognizes a bare sigil only at word start
        /// (never mid-word), so expanding `@`/`%` anywhere inside quotes here
        /// would itself violate the SEMANTICS §3.6 invariant that quoting does
        /// not change presentation: `echo user@host` and `echo "user@host"`
        /// must agree.  Inside quotes, list interpolation uses the `$` form,
        /// `"${arr[@]}"`, handled below.
        if (str[i] == '$' && i + 1 < len) {
            /// NOTE: ANSI-C quoting $'...' is NOT expanded inside double quotes
            /// per POSIX/bash behavior. It's only recognized at the outer
            /// level. So we skip the $' check here and treat it as a literal $.

            /// Check for arithmetic expansion $((...))
            if (str[i + 1] == '(' && i + 2 < len && str[i + 2] == '(') {
                /// $(( disambiguation: arithmetic vs command-sub of
                /// anonymous function. Same shape as tokenizer +
                /// expand_variables_in_string + expand_if_needed
                /// (issue #99).
                bool qs_looks_arith =
                    lush_dollar_paren_is_arithmetic(str + i + 3, len - (i + 3));
                if (!qs_looks_arith) {
                    /// Fall through to the $(...) command-sub handler
                    /// later in this function -- which is exactly the
                    /// else-if test on str[i+1] == '(' that doesn't
                    /// require str[i+2] == '('. To avoid restructuring
                    /// the giant conditional chain, mark this branch
                    /// as "not arithmetic" by setting paren_depth so
                    /// the post-check fails through. Simplest path:
                    /// just skip and let the next branch handle it.
                    goto qs_try_cmd_sub;
                }
                /// This is arithmetic expansion $((expr))
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
                    /// Extract arithmetic expression including $(( and ))
                    size_t full_arith_len = arith_end - arith_start;
                    char *full_arith_expr = malloc(full_arith_len + 1);
                    if (full_arith_expr) {
                        strncpy(full_arith_expr, &str[arith_start],
                                full_arith_len);
                        full_arith_expr[full_arith_len] = '\0';

                        /// Expand arithmetic expression
                        char *arith_result =
                            expand_arithmetic(executor, full_arith_expr);
                        if (arith_result) {
                            size_t result_len = strlen(arith_result);
                            /// Ensure buffer is large enough
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

                            /// Copy arithmetic result
                            strcpy(&result[result_pos], arith_result);
                            result_pos += result_len;
                            free(arith_result);
                        }

                        free(full_arith_expr);
                        i = arith_end; /// Skip past the closing ))
                        continue;
                    }
                }
            }
            /// Check for command substitution $(...)
            else if (str[i + 1] == '(') {
            qs_try_cmd_sub:;
                /// Use the canonical brace matcher to handle nested quotes
                /// and escapes.
                size_t cmd_start = i;

                /// Start from the opening parenthesis.
                const char *temp_str = &str[i + 1];
                size_t brace_offset = 0;
                if (lush_find_matching_brace(temp_str, 0, &brace_offset)) {
                    /// Found matching closing parenthesis
                    size_t cmd_end =
                        i + 1 + brace_offset; /// Points to the closing paren

                    /// Extract command substitution including $( and )
                    size_t full_cmd_len = cmd_end - cmd_start + 1;
                    char *full_cmd_expr = malloc(full_cmd_len + 1);
                    if (full_cmd_expr) {
                        strncpy(full_cmd_expr, &str[cmd_start], full_cmd_len);
                        full_cmd_expr[full_cmd_len] = '\0';

                        /// Expand command substitution
                        char *cmd_result = expand_command_substitution(
                            executor, full_cmd_expr);
                        if (cmd_result) {
                            size_t result_len = strlen(cmd_result);
                            /// Ensure buffer is large enough
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

                            /// Copy command result
                            strcpy(&result[result_pos], cmd_result);
                            result_pos += result_len;
                            free(cmd_result);
                        }

                        free(full_cmd_expr);
                        i = cmd_end + 1; /// Skip past the closing )
                        continue;
                    }
                }
            }

            /// Variable expansion needed
            size_t var_start = i + 1;
            size_t var_end = var_start;

            /// Handle ${var} format
            if (str[var_start] == '{') {

                /// Use proper brace matching for nested expressions
                int brace_count = 1;
                var_end = var_start + 1; /// Start after opening {

                while (var_end < len && brace_count > 0) {
                    if (str[var_end] == '{') {
                        brace_count++;
                    } else if (str[var_end] == '}') {
                        brace_count--;
                    }
                    var_end++;
                }

                if (brace_count == 0) {
                    var_start++; /// Skip opening brace for variable name
                                 /// extraction
                    var_end--;   /// Point to closing brace
                    /// Extract variable name
                    size_t var_name_len = var_end - var_start;
                    char *var_name = malloc(var_name_len + 1);
                    if (var_name) {
                        strncpy(var_name, &str[var_start], var_name_len);
                        var_name[var_name_len] = '\0';
                        /// Use parameter expansion to handle operators like =,
                        /// :-, etc.
                        char *var_value = parse_parameter_expansion(
                            executor, var_name, in_double_quotes);

                        if (var_value) {
                            size_t value_len = strlen(var_value);
                            /// Ensure buffer is large enough
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
                        i = var_end + 1; /// Skip past closing brace
                    } else {
                        result[result_pos++] = str[i++];
                    }
                } else {
                    result[result_pos++] = str[i++];
                }
            } else {
                /// Simple $var format - handle special variables and regular
                /// variables
                size_t var_name_len = 0;

                /// Check for special single-character variables first
                if (str[var_start] == '?' || str[var_start] == '$' ||
                    str[var_start] == '#' || str[var_start] == '*' ||
                    str[var_start] == '@' || str[var_start] == '!' ||
                    str[var_start] == '-' ||
                    (str[var_start] >= '0' && str[var_start] <= '9')) {
                    var_name_len = 1;
                } else {
                    /// Regular variable names; honor
                    /// FEATURE_UNICODE_IDENTIFIERS via
                    /// lush_ident_match_continue.
                    while (var_start + var_name_len < len) {
                        /// Edit 2 -- bound the name at a quote-context change
                        /// so
                        /// `"$y"z` reads `$y` (the name shares the `$`'s class,
                        /// at var_start - 1) and leaves `z` literal, rather
                        /// than greedily reading `$yz`.
                        if (prov && prov[var_start + var_name_len] !=
                                        prov[var_start - 1]) {
                            break;
                        }
                        size_t n = lush_ident_match_continue(
                            str + var_start + var_name_len,
                            len - var_start - var_name_len);
                        if (n == 0) {
                            break;
                        }
                        var_name_len += n;
                    }
                    /// Zsh bare-subscript form: $var[N] / $var[N,M] inside
                    /// a double-quoted string. Extend var_name_len through
                    /// the bracket span so we pass "$var[N]" to
                    /// expand_variable, which routes it through
                    /// parse_parameter_expansion. Gated on
                    /// FEATURE_ZSH_BARE_SUBSCRIPT — bash mode keeps the
                    /// literal-[N]-after-$var semantic. Mirrors the
                    /// unquoted path in expand_variables_in_string.
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
                    /// Create variable expression for expansion
                    char *var_expr = malloc(
                        var_name_len + 2); /// +2 for '$' and null terminator
                    if (var_expr) {
                        var_expr[0] = '$';
                        strncpy(&var_expr[1], &str[var_start], var_name_len);
                        var_expr[var_name_len + 1] = '\0';

                        /// Use the main variable expansion function
                        char *var_value = expand_variable(executor, var_expr);
                        if (var_value) {
                            size_t value_len = strlen(var_value);
                            /// Ensure buffer is large enough
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
            /// Handle backtick command substitution
            size_t cmd_start = i;
            size_t cmd_end = i + 1;

            /// Find closing backtick
            while (cmd_end < len && str[cmd_end] != '`') {
                if (str[cmd_end] == '\\' && cmd_end + 1 < len) {
                    cmd_end += 2; /// Skip escaped character
                } else {
                    cmd_end++;
                }
            }

            if (cmd_end < len && str[cmd_end] == '`') {
                /// Found matching closing backtick
                size_t full_cmd_len = cmd_end - cmd_start + 1;
                char *full_cmd_expr = malloc(full_cmd_len + 1);
                if (full_cmd_expr) {
                    strncpy(full_cmd_expr, &str[cmd_start], full_cmd_len);
                    full_cmd_expr[full_cmd_len] = '\0';

                    /// Expand command substitution
                    char *cmd_result =
                        expand_command_substitution(executor, full_cmd_expr);
                    if (cmd_result) {
                        size_t result_len = strlen(cmd_result);
                        /// Ensure buffer is large enough
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

                        /// Copy command result
                        strcpy(&result[result_pos], cmd_result);
                        result_pos += result_len;
                        free(cmd_result);
                    }

                    free(full_cmd_expr);
                    i = cmd_end + 1; /// Skip past the closing backtick
                    continue;
                }
            }

            /// If we get here, no matching backtick found, treat as literal
            result[result_pos++] = str[i++];
        } else if (str[i] == '\\' && i + 1 < len) {
            /// Two escape regimes share this loop:
            ///  in_double_quotes=true  -- POSIX double-quote: only \\, \",
            ///      \$, \` are meaningful; all other `\X` is kept literally
            ///      as `\X` so the consumer (e.g. echo with XPG escape
            ///      interp) can still process it.
            ///  in_double_quotes=false -- POSIX unquoted: any `\X` (other
            ///      than `\<newline>` already eaten by the tokenizer)
            ///      collapses to literal X, including suppressing the
            ///      special meaning of `$` and `` ` `` so that `\$VAR`
            ///      yields literal `$VAR` with no parameter expansion.
            ///      Single-pass interleaving with variable expansion
            ///      relies on emitting the literal byte and skipping past
            ///      it, so we never re-enter the var-scan on the escaped
            ///      character.
            char next_char = str[i + 1];
            bool is_dq_meta = (next_char == '\\' || next_char == '"' ||
                               next_char == '$' || next_char == '`');
            /// Edit 3 -- the backslash regime is per character when a map is
            /// present: a DOUBLE-quoted backslash keeps double-quote rules, an
            /// UNQUOTED one collapses `\X` to X. (SINGLE/ESCAPED never reach
            /// here -- edit 1 already emitted them literally.)
            bool eff_dq =
                prov ? (prov[i] == QUOTE_PROV_DOUBLE) : in_double_quotes;

            if (!eff_dq || is_dq_meta) {
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
                /// DQ + non-meta: keep the backslash and char literally.
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
            /// Regular character
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
    executor->current_job = 0;
    executor->previous_job = 0;

#ifdef LUSH_FUZZ_SANDBOX
    /// Fuzz harness must not perform tty job-control operations.
    /// executor_new() runs per fuzz iteration; tcgetpgrp / tcsetpgrp /
    /// kill(-pgid, SIGTTIN) inside this function send signals to the
    /// fuzzer's process group every iteration when stdin is a TTY,
    /// causing eventual SIGABRT after many iterations as accumulated
    /// signal state corrupts the process. The fuzz binary never needs
    /// to take terminal control — it does not run external commands
    /// (LUSH_FUZZ_SANDBOX makes lush_fork() return -1) and has no
    /// interactive prompt. (Issue #75.)
    executor->shell_pgid = getpgrp();
    return;
#endif

    /// For interactive login shells, take control of the terminal
    if (isatty(STDIN_FILENO)) {
        /// Wait until we're in the foreground
        while (tcgetpgrp(STDIN_FILENO) != (executor->shell_pgid = getpgrp())) {
            kill(-executor->shell_pgid, SIGTTIN);
        }

        /// Put ourselves in our own process group
        executor->shell_pgid = getpid();
        if (setpgid(0, executor->shell_pgid) < 0) {
            /// Not fatal - we may already be a process group leader
            executor->shell_pgid = getpgrp();
        }

        /// Grab control of the terminal
        tcsetpgrp(STDIN_FILENO, executor->shell_pgid);

        /// Ignore interactive and job-control signals in the shell process
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
static process_t *create_process(pid_t pid, const char *command) {
    process_t *proc = malloc(sizeof(process_t));
    if (!proc) {
        return NULL;
    }

    proc->pid = pid;
    proc->command = command ? strdup(command) : NULL;
    proc->status = 0;
    proc->done = false;
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

/// Mark job_id as the current job (%+ / %%), demoting the prior current to
/// previous (%-). Current/previous is the POSIX job-control concept both bash
/// and zsh track: the current job is the most recently backgrounded, stopped,
/// or fg/bg-selected job; the previous is the one that held that role before.
static void note_current_job(executor_t *executor, int job_id) {
    if (job_id <= 0 || job_id == executor->current_job) {
        return;
    }
    executor->previous_job = executor->current_job;
    executor->current_job = job_id;
}

/// Restore the current/previous invariant after a job is removed: both must
/// name existing jobs. A gone current is replaced by the previous (else the
/// newest remaining job); previous is then re-derived as the newest remaining
/// job that is not current whenever it is unset, dangling, or equal to current
/// -- so a '-' job always exists while two or more jobs remain.
static void reconcile_job_markers(executor_t *executor) {
    if (executor->current_job &&
        !executor_find_job(executor, executor->current_job)) {
        executor->current_job =
            executor_find_job(executor, executor->previous_job)
                ? executor->previous_job
                : (executor->jobs ? executor->jobs->job_id : 0);
        executor->previous_job = 0;
    }
    if (executor->previous_job == executor->current_job ||
        !executor_find_job(executor, executor->previous_job)) {
        executor->previous_job = 0;
        for (job_t *j = executor->jobs; j; j = j->next) {
            if (j->job_id != executor->current_job) {
                executor->previous_job = j->job_id;
                break;
            }
        }
    }
}

/// The job-listing marker: '+' for the current job (%+ / %%), '-' for the
/// previous (%-), space otherwise. Reflects the tracked current/previous state,
/// not the transient foreground flag.
static char job_marker(const executor_t *executor, const job_t *job) {
    if (job->job_id == executor->current_job) {
        return '+';
    }
    if (job->job_id == executor->previous_job) {
        return '-';
    }
    return ' ';
}

/// The title-cased state word lush shows for a job's state.
static const char *job_state_string(const job_t *job) {
    switch (job->state) {
    case JOB_RUNNING:
        return "Running";
    case JOB_STOPPED:
        return "Stopped";
    case JOB_DONE:
        return "Done";
    default:
        return "Unknown";
    }
}

/// Write one job line in lush's single curated format, shared by the `jobs`
/// listing and every launch/stop/continue notice so all job surfaces align on
/// the same columns: "[id]<marker> <state> <command>", with the marker glued to
/// the id and the state left-justified in a fixed field.
///
/// The shape is a deliberate curation, not a copy: bash and zsh differ here --
/// bash glues the marker and title-cases the state ("[2]- Stopped"), zsh spaces
/// the marker and lower-cases the state ("[2]  - suspended"). lush curates the
/// compact, single-column-aligned form (glued marker, title-cased state word)
/// as the one legible layout every surface reuses.
static void job_write_line(FILE *out, const executor_t *executor,
                           const job_t *job, const char *state_str,
                           bool long_form) {
    /// A tracked background pipeline is a multi-process job. In long form it
    /// lists every stage pid on its own line -- the [id] and state head the
    /// first line, the remaining stages align beneath it -- matching the
    /// per-process detail bash and zsh both show for `jobs -l`. Short form (and
    /// any single-process job) keeps the compact one-line format.
    if (long_form && job->processes && job->processes->next) {
        char head[32];
        int head_len = snprintf(head, sizeof(head), "[%d]%c ", job->job_id,
                                job_marker(executor, job));
        if (head_len < 0) {
            head_len = 0;
        }
        bool first = true;
        for (process_t *p = job->processes; p; p = p->next) {
            const char *cmd = p->command ? p->command : "unknown";
            if (first) {
                fprintf(out, "%s%d %-20s %s\n", head, (int)p->pid, state_str,
                        cmd);
                first = false;
            } else {
                /// Align the pid under the first line's; leave the state blank.
                fprintf(out, "%*s%d %-20s %s\n", head_len, "", (int)p->pid, "",
                        cmd);
            }
        }
        return;
    }

    /// The long format (zsh long_list_jobs / bash `jobs -l`) adds the job
    /// leader's PID column between the marker and the state.
    if (long_form) {
        fprintf(out, "[%d]%c %d %-20s %s\n", job->job_id,
                job_marker(executor, job), (int)job->pid, state_str,
                job->command_line ? job->command_line : "unknown");
    } else {
        fprintf(out, "[%d]%c %-20s %s\n", job->job_id,
                job_marker(executor, job), state_str,
                job->command_line ? job->command_line : "unknown");
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
job_t *executor_add_job(executor_t *executor, pid_t pid, pid_t pgid,
                        bool own_pgroup, const char *command_line) {
    if (!executor) {
        return NULL;
    }

    job_t *job = malloc(sizeof(job_t));
    if (!job) {
        return NULL;
    }

    job->job_id = executor->next_job_id++;
    job->pid = pid;
    job->pgid = pgid;
    job->own_pgroup = own_pgroup;
    job->state = JOB_RUNNING;
    job->status = 0;
    job->reported = false;
    job->foreground = false;
    job->no_sighup = false;
    job->processes = NULL;
    job->command_line = command_line ? strdup(command_line) : NULL;
    job->next = executor->jobs;

    executor->jobs = job;
    note_current_job(executor, job->job_id);
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

job_t *executor_find_job_by_pid(executor_t *executor, pid_t pid) {
    if (!executor) {
        return NULL;
    }

    job_t *job = executor->jobs;
    while (job) {
        if (job->pid == pid) {
            return job;
        }
        /// A multi-process job (a tracked background pipeline) matches on any
        /// of its stage pids, so `wait <pid>` for the last stage ($!) or any
        /// stage resolves to the job rather than falling through to an
        /// untracked wait.
        for (process_t *p = job->processes; p; p = p->next) {
            if (p->pid == pid) {
                return job;
            }
        }
        job = job->next;
    }
    return NULL;
}

job_t *executor_resolve_job_spec(executor_t *executor, const char *spec,
                                 const char **reason) {
    const char *ignored;
    if (!reason) {
        reason = &ignored;
    }
    *reason = "no such job";
    if (!executor || !spec || spec[0] != '%') {
        *reason = "invalid job spec";
        return NULL;
    }

    const char *body = spec + 1;

    /// %%, %+ -> the current job; %- -> the previous job.
    if (body[0] == '\0' || strcmp(body, "%") == 0 || strcmp(body, "+") == 0) {
        return executor_find_job(executor, executor->current_job);
    }
    if (strcmp(body, "-") == 0) {
        return executor_find_job(executor, executor->previous_job);
    }

    /// %n -> job number n. Reject an out-of-range or non-numeric-tail literal
    /// rather than truncating it onto an unrelated job.
    /// A spec that begins with a digit is a job number in full: a trailing
    /// non-digit is rejected rather than reinterpreted as a name match. bash
    /// would prefix-match "%2x" as a command name and zsh would read it as job
    /// 2; lush rejects the mixed form so a mistyped number never silently
    /// resolves onto an unrelated job. (The cost is that a command beginning
    /// with a digit cannot be prefix-matched; use its job number instead.)
    if (isdigit((unsigned char)body[0])) {
        char *end;
        errno = 0;
        long n = strtol(body, &end, 10);
        if (*end != '\0' || n <= 0 || errno == ERANGE || n > INT_MAX) {
            *reason = "invalid job spec";
            return NULL;
        }
        return executor_find_job(executor, (int)n);
    }

    /// %?str -> the job whose command contains str; %str -> the job whose
    /// command begins with str. Byte-level prefix/substring matching is exact
    /// for UTF-8 command text (the encoding is prefix-preserving and
    /// self-synchronizing) and matches the shells' literal, non-case-folding
    /// job-spec semantics.
    ///
    /// More than one match is rejected as ambiguous rather than resolved to an
    /// arbitrary job: acting on the wrong job for a fg/bg/kill is worse than an
    /// error. bash rejects likewise; zsh silently takes the most recent match,
    /// which lush does not follow.
    bool substring = body[0] == '?';
    const char *pat = substring ? body + 1 : body;
    if (pat[0] == '\0') {
        *reason = "invalid job spec";
        return NULL;
    }
    size_t pat_len = strlen(pat);
    job_t *match = NULL;
    for (job_t *j = executor->jobs; j; j = j->next) {
        if (!j->command_line) {
            continue;
        }
        bool hit = substring ? (strstr(j->command_line, pat) != NULL)
                             : (strncmp(j->command_line, pat, pat_len) == 0);
        if (hit) {
            if (match && match != j) {
                *reason = "ambiguous job spec";
                return NULL;
            }
            match = j;
        }
    }
    return match;
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
            reconcile_job_markers(executor);
            return;
        }
        prev = job;
        job = job->next;
    }
}

void executor_signal_job(const job_t *job, int sig) {
    if (!job) {
        return;
    }
    /// A multi-process job sharing the shell's process group must be signaled
    /// per-process: kill(-pgid) would target the shell's own group. A job that
    /// owns its group (or any single-process job) is signaled through
    /// job_target(), reaching the whole group in one call.
    if (job->processes && !job->own_pgroup) {
        for (process_t *p = job->processes; p; p = p->next) {
            if (!p->done) {
                kill(p->pid, sig);
            }
        }
        return;
    }
    kill(job_target(job), sig);
}

/// The exit status a multi-process job reports: its last stage's, matching the
/// shell rule that a pipeline's status is that of its final command. Returns 0
/// for an empty list (defensive).
static int job_last_process_status(const job_t *job) {
    int status = 0;
    for (process_t *p = job->processes; p; p = p->next) {
        status = p->status;
    }
    return status;
}

/// Reap the processes of a multi-process job (a tracked background pipeline).
/// Blocking waits each still-live process to termination (the `wait` contract
/// -- a stop does not satisfy it); non-blocking polls with WNOHANG|WUNTRACED,
/// noting a stop. The job becomes JOB_DONE once every process has terminated,
/// its status set to the last stage's; a poll that sees a stop marks it
/// JOB_STOPPED. Returns a signal number if a blocking wait was broken by a
/// signal the shell must act on, else 0.
static int reap_job_processes(job_t *job, bool blocking) {
    if (blocking) {
        /// Wait every still-live stage to termination (a stop does not satisfy
        /// the `wait` contract). Handlers run without SA_RESTART, so waitpid
        /// can return EINTR; a signal the shell must act on breaks the wait,
        /// the rest are retried.
        for (process_t *p = job->processes; p; p = p->next) {
            if (p->done) {
                continue;
            }
            for (;;) {
                int status;
                pid_t r = waitpid(p->pid, &status, 0);
                if (r == p->pid) {
                    p->status = status;
                    p->done = true;
                    break;
                }
                if (r == -1 && errno == EINTR) {
                    int brk = signal_wait_break_check();
                    if (brk > 0) {
                        return brk;
                    }
                    continue;
                }
                /// ECHILD or another non-EINTR error: the child is gone; treat
                /// it as terminated rather than blocking forever.
                p->done = true;
                break;
            }
        }
        job->state = JOB_DONE;
        job->status = job_last_process_status(job);
        return 0;
    }

    /// Non-blocking poll: reap any stage that has exited, note stops, and leave
    /// still-running (or signal-interrupted) stages for the next poll --
    /// exactly the discipline of the single-process path, which treats a
    /// non-positive waitpid result as "still running, leave as-is."
    bool any_stopped = false;
    bool any_running = false;
    for (process_t *p = job->processes; p; p = p->next) {
        if (p->done) {
            continue;
        }
        int status;
        pid_t r = waitpid(p->pid, &status, WNOHANG | WUNTRACED);
        if (r == p->pid) {
            if (WIFSTOPPED(status)) {
                any_stopped = true;
            } else {
                p->status = status;
                p->done = true;
            }
        } else if (r == -1 && errno == ECHILD) {
            /// No such child -- already reaped elsewhere. Treat as terminated
            /// so the job can complete rather than polling a pid that never
            /// returns.
            p->done = true;
        } else {
            /// r == 0 (still running) or r == -1 with EINTR (a caught signal
            /// interrupted this poll): the stage is still live; the next poll
            /// retries it. Marking it done here would leak a zombie and report
            /// a stale status.
            any_running = true;
        }
    }

    bool all_done = true;
    for (process_t *p = job->processes; p; p = p->next) {
        if (!p->done) {
            all_done = false;
            break;
        }
    }
    if (all_done) {
        job->state = JOB_DONE;
        job->status = job_last_process_status(job);
    } else if (any_stopped && !any_running) {
        /// Every still-live stage is stopped: the job is stopped. bash marks a
        /// job stopped only when all its processes are, not on the first stop.
        job->state = JOB_STOPPED;
    }
    return 0;
}

int executor_reap_job(job_t *job, bool blocking) {
    if (!job) {
        return 0;
    }

    /// A tracked background pipeline reaps each of its stages; a blocking wait
    /// blocks past a stop until every stage terminates.
    if (job->processes) {
        if (blocking && job->state == JOB_DONE) {
            return 0;
        }
        if (!blocking && job->state != JOB_RUNNING) {
            return 0;
        }
        return reap_job_processes(job, blocking);
    }

    if (blocking) {
        /// A blocking wait (the `wait` builtin) must return only when the job
        /// truly ends, so it waits without WUNTRACED: a stop does not satisfy
        /// it, and a job that is already stopped is waited on until it is
        /// continued and exits. This is the POSIX `wait` contract -- a stopped
        /// job has not terminated, so the wait blocks until it does.
        if (job->state == JOB_DONE) {
            return 0;
        }
        for (;;) {
            int status;
            pid_t result = waitpid(job_target(job), &status, 0);
            if (result > 0) {
                job->status = status;
                job->state = JOB_DONE;
                return 0;
            }
            if (result == -1 && errno == EINTR) {
                /// bash breaks a `wait` for a signal it must act on (a trap, a
                /// hangup, an interrupt) and resumes it across an incidental
                /// one. A break returns the signal number so the caller can
                /// report 128 + signo.
                int brk = signal_wait_break_check();
                if (brk > 0) {
                    return brk;
                }
                continue;
            }
            /// A genuine error (e.g. ECHILD): leave the job as-is.
            return 0;
        }
    }

    /// A non-blocking poll (the status sweep) also detects stops, so a `jobs`
    /// listing can show a Stopped job.
    if (job->state != JOB_RUNNING) {
        return 0;
    }
    int status;
    pid_t result = waitpid(job_target(job), &status, WNOHANG | WUNTRACED);
    if (result <= 0) {
        return 0;
    }
    if (WIFSTOPPED(status)) {
        job->state = JOB_STOPPED;
    } else {
        job->status = status;
        job->state = JOB_DONE;
    }
    return 0;
}

int executor_job_status_code(const job_t *job) {
    if (!job) {
        return 0;
    }
    if (WIFEXITED(job->status)) {
        return WEXITSTATUS(job->status);
    }
    if (WIFSIGNALED(job->status)) {
        return 128 + WTERMSIG(job->status);
    }
    return 0;
}

/// Backstop bound on completed jobs that were never consumed by a `wait`.
///
/// lush's completed-job lifecycle is single-consumption: an explicit `wait`
/// delivers a finished job's status and drops the job (see wait_for_tracked_job
/// in bin_wait.c). This bound covers only the jobs that are NOT waited for -- a
/// fire-and-forget background loop, or completions the shell reaped before any
/// `wait` -- whose statuses lush still remembers in case a later `wait` asks.
/// It is the POSIX "remember a terminated job's status until it is waited for,"
/// made finite: at least this many recent completions are retained, older ones
/// dropped, so a long-running shell cannot grow the list without limit. It is a
/// deliberate lush guardrail, not a copy of any shell's completed-job cache.
#define COMPLETED_JOB_CAP 32

/**
 * @brief Bound the retained never-consumed completed jobs.
 *
 * A completed job consumed by an explicit `wait` is already gone (dropped at
 * consumption). What remains here are completions no `wait` claimed -- kept so
 * a later `wait` can still report them. Their count is bounded to
 * COMPLETED_JOB_CAP, dropping the oldest first.
 *
 * @param executor Executor context
 */
static void prune_completed_jobs(executor_t *executor) {
    int done = 0;
    for (job_t *j = executor->jobs; j; j = j->next) {
        if (j->state == JOB_DONE) {
            done++;
        }
    }
    /// The list is newest-first, so the last completed job in it is the oldest.
    while (done > COMPLETED_JOB_CAP) {
        job_t *oldest = NULL;
        for (job_t *j = executor->jobs; j; j = j->next) {
            if (j->state == JOB_DONE) {
                oldest = j;
            }
        }
        if (!oldest) {
            break;
        }
        executor_remove_job(executor, oldest->job_id);
        done--;
    }
}

void executor_reap_finished_jobs(executor_t *executor) {
    if (!executor) {
        return;
    }
    for (job_t *job = executor->jobs; job; job = job->next) {
        executor_reap_job(job, false);
    }
    prune_completed_jobs(executor);
}

/**
 * @brief Update status of all jobs
 *
 * Polls each running job with WNOHANG, caching the status of any that finished
 * or stopped. Interactive shells print a completion notice once; the list is
 * then pruned of reported completions and bounded. A completed job whose status
 * nothing has read yet is kept so a later `wait` can still report it.
 *
 * @param executor Executor context
 */
void executor_update_job_status(executor_t *executor) {
    if (!executor) {
        return;
    }

    for (job_t *job = executor->jobs; job; job = job->next) {
        job_state_t prior = job->state;
        executor_reap_job(job, false);

        /// A job that has just stopped becomes the current job (%+): a stop is
        /// a job-control event that promotes the job, matching the current/
        /// previous discipline POSIX, bash, and zsh share.
        if (prior != JOB_STOPPED && job->state == JOB_STOPPED) {
            note_current_job(executor, job->job_id);
        }

        /// Completion notices ([id]+ Done / Stopped) are an interactive
        /// convenience; a non-interactive script stays silent (the shell
        /// consensus for asynchronous job notices under `-c`/script).
        /// The notice reports the job's state change exactly once: `reported`
        /// marks the current Done or Stopped state as already announced, so a
        /// persistently stopped job does not reprint on every prompt render.
        /// (fg/bg clear it when they continue a job, so the eventual Done still
        /// prints.) The marker reflects the tracked current/previous state.
        if (is_interactive_shell() && !job->reported) {
            if (job->state == JOB_DONE) {
                job_write_line(stdout, executor, job, "Done", false);
                job->reported = true;
            } else if (job->state == JOB_STOPPED) {
                job_write_line(stdout, executor, job, "Stopped", false);
                job->reported = true;
            }
        }
    }

    prune_completed_jobs(executor);
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

/// Execute a backgrounded pipeline (`cmd1 | cmd2 &`) as a tracked multi-process
/// job. Each stage is a direct child of the shell -- so every stage pid is
/// reapable and listed by `jobs -l` -- sharing one process group (its own under
/// job control, else the shell's). `$!` becomes the last stage's pid, matching
/// bash and zsh, and each stage is recorded in job->processes in pipeline
/// order. Returns 0 once the job is launched (the pipeline runs
/// asynchronously), or 1 on a setup failure.
static int executor_execute_background_pipeline(executor_t *executor,
                                                node_t *pipeline,
                                                const char *command_line) {
    enum { MAX_PIPELINE_STAGES = 256 };
    node_t *stages[MAX_PIPELINE_STAGES];
    bool stderr_to_next[MAX_PIPELINE_STAGES];
    size_t nstages = 0;

    if (!flatten_pipeline_chain(pipeline, stages, stderr_to_next, &nstages,
                                MAX_PIPELINE_STAGES) ||
        nstages < 2) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           pipeline->loc, "malformed pipeline");
        return 1;
    }

    size_t npipes = nstages - 1;
    int (*pipes)[2] = calloc(npipes, sizeof(*pipes));
    if (!pipes) {
        executor_error_add(executor, SHELL_ERR_PIPE_FAILED, pipeline->loc,
                           "pipeline allocation failed");
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
            return 1;
        }
    }

    bool own_pgroup = shell_opts.job_control;
    pid_t *pids = calloc(nstages, sizeof(pid_t));
    if (!pids) {
        for (size_t i = 0; i < npipes; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        free(pipes);
        return 1;
    }

    pid_t pgid = 0; /// The group every stage joins (the first stage's pid).

    for (size_t i = 0; i < nstages; i++) {
        pid_t pid = lush_fork();
        if (pid == -1) {
            executor_error_report(
                executor, SHELL_ERR_FORK_FAILED, pipeline->loc,
                "failed to fork for background pipeline: %s", strerror(errno));
            for (size_t j = 0; j < npipes; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            /// Reap the stages already forked so they do not leak as zombies,
            /// retrying past an EINTR (handlers run without SA_RESTART).
            for (size_t j = 0; j < i; j++) {
                pid_t r;
                do {
                    r = waitpid(pids[j], NULL, 0);
                } while (r == -1 && errno == EINTR);
            }
            free(pids);
            free(pipes);
            return 1;
        }

        if (pid == 0) {
            /// Child stage. Join the job's process group -- the first stage
            /// creates it, the rest join -- so one kill(-pgid) reaches the
            /// whole pipeline under job control.
            if (own_pgroup) {
                setpgid(0, (i == 0) ? 0 : pgid);
            }
            /// A backgrounded pipeline is an async list: ignore SIGINT/SIGQUIT
            /// (a terminal Ctrl-C must not kill it) and restore the default
            /// hangup/fault handlers for the exit-time SIGHUP cascade (#375).
            executor->async_context = true;
            reset_subshell_signals();

            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < npipes) {
                dup2(pipes[i][1], STDOUT_FILENO);
                if (stderr_to_next[i]) {
                    dup2(pipes[i][1], STDERR_FILENO);
                }
            }
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

        /// Parent. Set group membership from this side too (both sides run to
        /// close the fork-order race); the first stage's pid is the group id.
        if (own_pgroup) {
            if (i == 0) {
                pgid = pid;
            }
            setpgid(pid, pgid);
        }
        pids[i] = pid;
    }

    /// Parent: close every pipe fd so each stage sees EOF as its neighbor
    /// exits.
    for (size_t i = 0; i < npipes; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    free(pipes);

    pid_t leader = pids[0];
    pid_t group = own_pgroup ? leader : getpgrp();

    job_t *job =
        executor_add_job(executor, leader, group, own_pgroup, command_line);
    if (job) {
        /// Record one process per stage, in pipeline order, so `jobs -l` lists
        /// every pid and reaping/signaling can address each stage.
        process_t *tail = NULL;
        for (size_t i = 0; i < nstages; i++) {
            char *stage_src = node_to_source(stages[i]);
            process_t *proc = create_process(pids[i], stage_src);
            free(stage_src);
            if (!proc) {
                continue;
            }
            if (tail) {
                tail->next = proc;
            } else {
                job->processes = proc;
            }
            tail = proc;
        }
    }

    /// $! is the last stage's pid (bash/zsh), not the leader's.
    last_background_pid = pids[nstages - 1];
    free(pids);

    /// The launch notice ([id] pid) is an interactive convenience and reports
    /// $! (the last stage), matching a single-command background job.
    if (job && is_interactive_shell()) {
        printf("[%d] %d\n", job->job_id, (int)last_background_pid);
    }

    return 0;
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

    /// Reap and prune already-finished jobs before adding another, so a
    /// non-interactive loop that backgrounds work keeps the job list (and its
    /// zombies) bounded rather than growing it for the life of the shell.
    executor_reap_finished_jobs(executor);

    /// Render the full backgrounded command from its AST so the job list shows
    /// a pipeline, subshell, or brace group in full rather than as its first
    /// word alone (or "unknown" when the child is not a plain command).
    /// executor_add_job copies the string; free the rendered source after.
    char *command_line = node_to_source(command->first_child);

    /// A backgrounded pipeline is tracked as a multi-process job: every stage
    /// is a direct child of the shell, so each stage pid is reapable, listed by
    /// `jobs -l`, and $! is the last stage (bash/zsh). A single command (or an
    /// and-or list, subshell, brace group, ...) stays a one-child job on the
    /// path below.
    if (command->first_child && command->first_child->type == NODE_PIPE) {
        int rc = executor_execute_background_pipeline(
            executor, command->first_child, command_line);
        free(command_line);
        return rc;
    }

    /// Job control (set -m) governs process-group topology and terminal
    /// management, not whether a job is tracked. lush keeps the job list a
    /// property of the engine, not of a preset: a background job is recorded
    /// either way, so jobs / wait / %job work whether or not `set -m` is on.
    /// (bash and zsh both maintain the job list independent of `set -m`; the
    /// shared behavior is the consensus lush follows.) With job control on the
    /// job leads its own process group; with it off it shares the shell's group
    /// and is reaped and signaled by its leader pid instead of a group id.
    bool own_pgroup = shell_opts.job_control;

    pid_t pid = lush_fork();
    if (pid == -1) {
        int saved_errno = errno;
        executor_error_report(executor, SHELL_ERR_FORK_FAILED, command->loc,
                              "failed to fork for background job: %s",
                              strerror(saved_errno));
        free(command_line);
        return 1;
    }

    if (pid == 0) {
        /// Child process. Under job control it leads a new process group.
        if (own_pgroup) {
            setpgid(0, 0);
        }

        /// This child heads an asynchronous list: mark the context so it and
        /// everything nested inside it (subshells, execs) ignore SIGINT per the
        /// POSIX async-list rule -- a terminal Ctrl-C must not kill a
        /// background job. reset_subshell_signals reads the flag (#375).
        executor->async_context = true;

        /// A login shell's exit-time SIGHUP cascade (send_sighup_to_jobs) must
        /// terminate this job; reset the inherited hangup and fault handlers so
        /// the cascade is not swallowed.
        reset_subshell_signals();

        int result = execute_node(executor, command->first_child);
        fflush(stdout);
        fflush(stderr);
        subshell_cleanup();
        _exit(result);
    }

    /// Parent process.
    pid_t pgid;
    if (own_pgroup) {
        setpgid(pid, pid); /// Redundant with the child's own setpgid; both run
                           /// to close the fork-order race.
        pgid = pid;
    } else {
        pgid = getpgrp(); /// Shares the shell's process group.
    }

    /// Store the background PID for $!.
    last_background_pid = pid;

    job_t *job =
        executor_add_job(executor, pid, pgid, own_pgroup, command_line);
    free(command_line);

    /// The launch notification ([id] pid) is an interactive convenience; a
    /// non-interactive script stays silent (the shell consensus for job
    /// notices under `-c`/script).
    if (job && is_interactive_shell()) {
        printf("[%d] %d\n", job->job_id, pid);
    }

    return 0; /// Background job started successfully
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
    if (!executor) {
        return 1;
    }

    /// The long format adds the PID column. It is requested per-call with
    /// `jobs -l` (bash/zsh) or made the default by the long_list_jobs option
    /// (zsh). Any other `-X` is an invalid option (bash/zsh/POSIX); `--` ends
    /// option parsing; remaining words are jobspecs that filter the listing.
    bool long_form = shell_mode_allows(FEATURE_LONG_LIST_JOBS);
    int argc = 0;
    while (argv && argv[argc]) {
        argc++;
    }
    const char **specs =
        argc > 1 ? malloc((size_t)(argc - 1) * sizeof(*specs)) : NULL;
    int nspecs = 0;
    bool opts_ended = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!opts_ended && a[0] == '-' && a[1] != '\0') {
            if (strcmp(a, "--") == 0) {
                opts_ended = true;
                continue;
            }
            for (const char *p = a + 1; *p; p++) {
                if (*p == 'l') {
                    long_form = true;
                } else {
                    executor_error_report(executor, SHELL_ERR_INVALID_OPTION,
                                          builtin_get_source_location(),
                                          "jobs: -%c: invalid option", *p);
                    free(specs);
                    return 2;
                }
            }
        } else {
            /// The first non-option word ends option parsing (POSIX operand
            /// rule); it and the rest are jobspecs.
            opts_ended = true;
            if (specs) {
                specs[nspecs++] = a;
            }
        }
    }

    /// Background jobs are tracked regardless of job control (set -m), so the
    /// listing does not depend on it: `jobs` reports the job list in scripts as
    /// well as interactive shells (job tracking is an engine property, not a
    /// preset -- the shell consensus).

    /// Update job statuses first
    executor_update_job_status(executor);

    /// Buffer the listing through open_memstream and hand it to
    /// lle_pager_present so long job tables paginate in interactive
    /// shells. On memstream allocation failure the per-iteration
    /// writes target stdout directly, preserving prior behavior.
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *out = open_memstream(&buf, &buf_len);
    FILE *sink = out ? out : stdout;

    /// The job list is stored newest-first (add prepends); present it
    /// oldest-first so ids read in ascending order, the ordering POSIX, bash,
    /// and zsh share. Collect the jobs and walk them in reverse; on allocation
    /// failure fall back to the stored newest-first order.
    int job_count = 0;
    for (job_t *j = executor->jobs; j; j = j->next) {
        job_count++;
    }
    job_t **ordered =
        job_count ? malloc((size_t)job_count * sizeof(*ordered)) : NULL;
    if (ordered) {
        int fill = job_count;
        for (job_t *j = executor->jobs; j; j = j->next) {
            ordered[--fill] = j;
        }
    }

    int rc = 0;

    if (nspecs > 0) {
        /// Filtered listing: resolve each jobspec in argument order, emitting
        /// its line or a per-spec error (bash lists the valid specs and errors
        /// on the rest, returning nonzero). A bare number is accepted as a job
        /// id, matching the fg/bg convenience; otherwise a leading % is
        /// required.
        for (int i = 0; i < nspecs; i++) {
            const char *spec = specs[i];
            const char *reason = "no such job";
            job_t *job = NULL;
            if (spec[0] == '%') {
                job = executor_resolve_job_spec(executor, spec, &reason);
            } else {
                char *end;
                errno = 0;
                long n = strtol(spec, &end, 10);
                if (spec[0] != '\0' && *end == '\0' && n > 0 &&
                    errno != ERANGE && n <= INT_MAX) {
                    job = executor_find_job(executor, (int)n);
                } else {
                    reason = "invalid job spec";
                }
            }
            if (!job) {
                executor_error_report(executor, SHELL_ERR_JOB_NOT_FOUND,
                                      builtin_get_source_location(),
                                      "jobs: %s: %s", spec, reason);
                rc = 1;
                continue;
            }
            job_write_line(sink, executor, job, job_state_string(job),
                           long_form);
            if (job->state == JOB_DONE) {
                job->reported = true;
            }
        }
    } else {
        /// Emit one job per line. The ordered walk (oldest-first) is the common
        /// path; it runs only when the ordering buffer was allocated, so on the
        /// rare allocation failure the fallback newest-first walk below takes
        /// over. A completion already reported is shown once then omitted (a
        /// `wait`-consumed completion is already gone from the list); running
        /// and stopped jobs are always listed.
        for (int idx = 0; ordered && idx < job_count; idx++) {
            job_t *job = ordered[idx];

            if (job->state == JOB_DONE && job->reported) {
                continue;
            }
            job_write_line(sink, executor, job, job_state_string(job),
                           long_form);

            /// Reporting a completion here means the next listing omits it.
            if (job->state == JOB_DONE) {
                job->reported = true;
            }
        }

        if (!ordered) {
            /// Allocation-failure fallback: list in the stored newest-first
            /// order.
            for (job_t *job = executor->jobs; job; job = job->next) {
                if (job->state == JOB_DONE && job->reported) {
                    continue;
                }
                job_write_line(sink, executor, job, job_state_string(job),
                               long_form);
                if (job->state == JOB_DONE) {
                    job->reported = true;
                }
            }
        }
    }

    free(ordered);
    free(specs);

    if (out) {
        fclose(out);
        lle_pager_present(NULL, buf);
        free(buf);
    }

    return rc;
}

/// Resolve the job argument shared by fg and bg: no argument selects the
/// current job (%+, the bash/zsh default), a `%...` argument is a job spec, and
/// a bare number is a job number. Returns NULL and sets *reason on failure.
///
/// The bare-number form is a curated lush convenience: bash accepts it but
/// prints a deprecation warning ("job specification requires leading `%'") and
/// zsh rejects it outright, so the two shells disagree. lush accepts a bare
/// number as a job number without a warning, because a fg/bg argument names
/// only jobs (never a pid), so `fg 2` is unambiguous and needs no `%`.
static job_t *resolve_fgbg_job(executor_t *executor, const char *arg,
                               const char **reason) {
    *reason = "no such job";
    if (!arg) {
        job_t *job = executor_find_job(executor, executor->current_job);
        if (!job) {
            *reason = "no current job";
        }
        return job;
    }
    if (arg[0] == '%') {
        return executor_resolve_job_spec(executor, arg, reason);
    }
    char *end;
    errno = 0;
    long n = strtol(arg, &end, 10);
    if (*end != '\0' || n <= 0 || errno == ERANGE || n > INT_MAX) {
        *reason = "invalid job spec";
        return NULL;
    }
    return executor_find_job(executor, (int)n);
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

    const char *reason;
    job_t *job = resolve_fgbg_job(executor, argv[1], &reason);
    if (!job) {
        executor_error_report(executor, SHELL_ERR_JOB_NOT_FOUND,
                              builtin_get_source_location(), "%s: %s",
                              argv[1] ? argv[1] : "fg", reason);
        return 1;
    }
    int job_id = job->job_id;

    /// Refresh the job's status before acting on it: it may have stopped or
    /// finished since the last prompt-time status poll, and fg must see the
    /// current state (a non-blocking WUNTRACED reap updates job->state).
    executor_reap_job(job, false);

    if (job->state == JOB_DONE) {
        executor_error_report(
            executor, SHELL_ERR_JOB_NOT_FOUND, builtin_get_source_location(),
            "%s: job has terminated", argv[1] ? argv[1] : "fg");
        return 1;
    }

    /// Selecting a job for the foreground makes it the current job (%+); if it
    /// stops again it is already current, and if it ends the marker state is
    /// reconciled on removal.
    note_current_job(executor, job_id);

    /// Give the job's own process group control of the terminal. A job that
    /// shares the shell's group already runs in the foreground group, so no
    /// handoff is needed.
    if (job->own_pgroup && isatty(STDIN_FILENO) && job->pgid > 0) {
        tcsetpgrp(STDIN_FILENO, job->pgid);
    }

    /// Continue the job if it was stopped. executor_signal_job reaches every
    /// stage of a multi-process pipeline even when it shares the shell's group.
    if (job->state == JOB_STOPPED) {
        executor_signal_job(job, SIGCONT);
    }

    job->foreground = true;
    job->state = JOB_RUNNING;
    /// Continuing the job clears the reported flag so its eventual completion
    /// is announced, rather than being suppressed by an earlier Stopped notice.
    job->reported = false;

    /// Wait for the job to complete or stop. A multi-process pipeline waits
    /// every stage to termination, returning early if the group stops; its
    /// status is the last stage's. A single-process job waits its one target.
    bool stopped = false;
    int status = 0;
    if (job->processes) {
        for (process_t *p = job->processes; p && !stopped; p = p->next) {
            if (p->done) {
                continue;
            }
            for (;;) {
                int st;
                pid_t r = waitpid(p->pid, &st, WUNTRACED);
                if (r == p->pid) {
                    if (WIFSTOPPED(st)) {
                        stopped = true;
                    } else {
                        p->status = st;
                        p->done = true;
                    }
                    break;
                }
                if (r == -1 && errno == EINTR) {
                    continue;
                }
                /// ECHILD or another error: treat the stage as terminated.
                p->done = true;
                break;
            }
        }
        if (!stopped) {
            status = job_last_process_status(job);
        }
    } else {
        waitpid(job_target(job), &status, WUNTRACED);
        stopped = WIFSTOPPED(status);
    }

    /// Reclaim terminal control for the shell
    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, executor->shell_pgid);
    }

    if (!stopped) {
        executor_remove_job(executor, job_id);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    job->state = JOB_STOPPED;
    job->foreground = false;
    /// A stop makes the job current (%+); it was already marked current on
    /// entry, so the marker below reflects that.
    note_current_job(executor, job_id);
    job_write_line(stdout, executor, job, "Stopped", false);
    /// This notice reports the stop; mark it so the next prompt's status
    /// sweep does not print a second Stopped line for the same job.
    job->reported = true;

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

    const char *reason;
    job_t *job = resolve_fgbg_job(executor, argv[1], &reason);
    if (!job) {
        executor_error_report(executor, SHELL_ERR_JOB_NOT_FOUND,
                              builtin_get_source_location(), "%s: %s",
                              argv[1] ? argv[1] : "bg", reason);
        return 1;
    }
    int job_id = job->job_id;

    /// Refresh the job's status before acting: it may have stopped since the
    /// last prompt-time poll -- e.g. `kill -STOP %n; bg %n` in a script -- and
    /// bg must see the current Stopped state, not a stale Running (a
    /// non-blocking WUNTRACED reap updates job->state).
    executor_reap_job(job, false);

    if (job->state != JOB_STOPPED) {
        executor_error_report(
            executor, SHELL_ERR_JOB_NOT_FOUND, builtin_get_source_location(),
            "%s: job already in background", argv[1] ? argv[1] : "bg");
        return 1;
    }

    /// Continue the job in background
    job->state = JOB_RUNNING;
    job->foreground = false;
    /// Continuing the job clears the reported flag so its eventual completion
    /// is announced, rather than being suppressed by an earlier Stopped notice.
    job->reported = false;
    /// Resuming a job in the background makes it the current job (%+).
    note_current_job(executor, job_id);
    /// executor_signal_job reaches every stage of a multi-process pipeline even
    /// when it shares the shell's process group.
    executor_signal_job(job, SIGCONT);

    /// The bg notice reuses the shared one-line job format (id, tracked marker,
    /// state, command). lush does not append the trailing `&` cosmetic bash
    /// prints here; the shared format keeps every job surface identical.
    job_write_line(stdout, executor, job, "Running", false);

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

    /// If stdout is not a terminal (tty), it's likely being captured
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
        /// Check for stdout-affecting redirections
        if (child->type == NODE_REDIR_OUT ||         /// >
            child->type == NODE_REDIR_APPEND ||      /// >>
            child->type == NODE_REDIR_BOTH ||        /// &>
            child->type == NODE_REDIR_BOTH_APPEND || /// &>>
            child->type == NODE_REDIR_CLOBBER) {     /// >|
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

    /// Only these builtins are "pure" -- they produce output but read no shell
    /// state that a subprocess could not see and mutate none that the parent
    /// must keep. The job-control builtins (jobs, wait, fg, bg) are
    /// deliberately excluded: a forked copy cannot waitpid the shell's
    /// background children or persist job-list changes back to the parent, so
    /// `wait $! >file` would return 0 instead of the job's status and `fg`/`bg`
    /// would resume a job while leaving the parent's job list stale. They run
    /// in the parent with the redirection applied there, the way every
    /// state-dependent builtin does. (kill has no builtin in lush; it is not
    /// listed.)
    static const char *pure_builtins[] = {
        "echo",  "printf", "true", "false", "test",  "[", "type",
        "which", "help",   "pwd",  "dirs",  "times", NULL};

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
        /// Child process - setup redirections and execute builtin.
        /// Reset the inherited interactive SIGHUP/SIGSEGV handlers so a
        /// hangup or fault terminates this child normally.
        reset_subshell_signals();
        int redir_result = setup_redirections(executor, command);
        if (redir_result != 0) {
            subshell_cleanup();
            _exit(1);
        }

        /// Execute the builtin command
        int result = execute_builtin_command(executor, argv, command->loc);

        /// Flush stdio buffers before _exit() - critical for file redirections
        /// Without this, output redirected to files would be lost because
        /// _exit() doesn't flush stdio buffers (unlike exit())
        fflush(stdout);
        fflush(stderr);
        subshell_cleanup();
        _exit(result);
    } else {
        /// Parent process - wait for child, retrying past incidental EINTR; a
        /// hangup terminates the shell.
        int status;
        if (executor_wait_foreground(pid, &status) == -1) {
            set_executor_error(executor,
                               "Failed to wait for builtin child process");
            return 1;
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

    /// zsh `$+NAME` is the unbraced shorthand for `${+NAME}` (is-set
    /// test).  Rewrite to the braced form so the existing ${+NAME}
    /// expansion handler in parse_parameter_expansion picks it up.
    ///
    /// Real-world example: `(( $+commands[dircolors] ))` -- common idiom
    /// for "is command available on $path".  Without this rewrite, the
    /// arithmetic parser splits `$+commands[...]` and chokes on the
    /// bare `$`.
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
                        lush_ident_match_start(p + 2, strlen(p + 2)) > 0) {
                        /// Emit `${+`
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
                        p += 2; /// consume $+
                        /// Copy the name and optional [subscript].
                        int bracket_depth = 0;
                        while (*p) {
                            bool advance = false;
                            if (bracket_depth > 0 || *p == '[') {
                                advance = true;
                            } else {
                                size_t n =
                                    lush_ident_match_continue(p, strlen(p));
                                if (n > 0) {
                                    advance = true;
                                    /// Copy n bytes for multi-byte codepoints
                                    if (out_len + n + 1 >= out_cap) {
                                        out_cap = (out_len + n + 1) * 2;
                                        char *grown =
                                            realloc(rewritten_expr, out_cap);
                                        if (!grown) {
                                            free(rewritten_expr);
                                            rewritten_expr = NULL;
                                            break;
                                        }
                                        rewritten_expr = grown;
                                    }
                                    memcpy(rewritten_expr + out_len, p, n);
                                    out_len += n;
                                    p += n;
                                    continue;
                                }
                            }
                            if (!advance) {
                                break;
                            }
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

    /// Pre-expand ${...} parameter expansions before arithmetic evaluation
    /// The arithmetic module handles simple $var but not complex ${...} syntax
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

    /// Empty arithmetic command `(( ))` is treated as false by bash and zsh
    /// -- the expression evaluates to 0, the command exits 1 (the inverse
    /// convention). No error is raised. Scripts use this idiom as a
    /// placeholder false. Match the convention before invoking the
    /// evaluator, which would otherwise flag empty input as a syntax error.
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

    /// Use the existing arithmetic evaluator with executor context
    /// This handles simple variable expansion internally
    arithm_clear_error();
    char *result_str = arithm_expand_with_executor(executor, expr);

    bool arith_flagged = arithm_error_is_flagged();
    if (!result_str || arith_flagged) {
        /// When the evaluator flagged a specific diagnostic (division by
        /// zero, readonly assignment, operand underflow, a tokenizing syntax
        /// error, ...), surface THAT targeted error so the (( )) command form
        /// reports the same precise cause as the $(( )) expansion form.
        /// Masking every flagged failure behind the generic "expects
        /// arithmetic expressions, not shell commands" syntax error -- as this
        /// handler previously did -- is both inaccurate and unhelpful. The
        /// evaluator flags an error on every NULL return, so the else branch
        /// is a defensive fallback for a NULL result with no flagged cause; it
        /// must not read arithm_error_message() (NULL when unflagged).
        shell_error_t *error;
        if (arith_flagged) {
            error = shell_error_create(
                arithm_error_code(), SHELL_SEVERITY_ERROR, arith_node->loc,
                "arithmetic: %s", arithm_error_message());
        } else {
            error = shell_error_create(
                SHELL_ERR_ARITHMETIC_SYNTAX, SHELL_SEVERITY_ERROR,
                arith_node->loc, "arithmetic syntax error in expression: %s",
                expr);
        }
        if (error) {
            /// Build the source line: (( expr ))
            char *source_line = NULL;
            size_t expr_len = strlen(expr);
            if (asprintf(&source_line, "(( %s ))", expr) > 0) {
                shell_error_set_source_line(error, source_line, 3,
                                            3 + expr_len);
                free(source_line);
            }
            /// Update location to highlight the expression (column is
            /// 1-indexed)
            error->location.column = 4; /// After "(( "
            error->location.length = expr_len;
            /// Add context stack from executor
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            if (arith_flagged) {
                /// Preserve the evaluator's own while-context and help so the
                /// command form reads identically to the expansion form.
                const char *wc = arithm_error_while();
                if (wc) {
                    shell_error_push_context(error, "%s", wc);
                }
                shell_error_push_context(
                    error, "evaluating arithmetic command (( %s ))", expr);
                const char *help = arithm_error_help();
                if (help) {
                    shell_error_set_suggestion(error, help);
                }
            } else {
                /// Add specific context for arithmetic command
                shell_error_push_context(
                    error, "evaluating arithmetic command (( %s ))", expr);
                /// Add help suggestion
                shell_error_set_suggestion(error,
                                           "(( )) expects arithmetic "
                                           "expressions, not shell commands");
            }
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

    /// Convert result to long long to check if non-zero
    long long result = strtoll(result_str, NULL, 10);
    free(result_str);
    free(expanded_expr); /// Safe to free NULL
    free(rewritten_expr);

    /// Update exit status
    executor->exit_status = (result != 0) ? 0 : 1;

    if (executor->debug) {
        printf("DEBUG: Arithmetic result: %lld, exit status: %d\n", result,
               executor->exit_status);
    }

    /// Return 0 if non-zero (true), 1 if zero (false)
    return (result != 0) ? 0 : 1;
}

/**
 * @brief Match a string against a glob pattern
 *
 * Delegates to lush_pattern_match, which handles POSIX glob plus bash
 * extglob (`?(...)`, `*(...)`, `+(...)`, `@(...)`, `!(...)`) plus zsh's
 * bare-alternation form (`(...)` == `@(...)`). All three families share
 * one engine so syntax differences alone never produce different match
 * results (PHILOSOPHY: syntax is polyglot).
 *
 * @param str String to match
 * @param pattern Glob pattern
 * @return true if matches, false otherwise
 */
static bool extended_test_pattern_match(const char *str, const char *pattern) {
    return lush_shell_pattern_match(str, pattern);
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
        /// Regex compilation failed
        if (executor->debug) {
            char errbuf[256];
            regerror(ret, &regex, errbuf, sizeof(errbuf));
            printf("DEBUG: Regex compilation failed: %s\n", errbuf);
        }
        return false;
    }

    /// Match with capture groups (up to 10 groups)
    regmatch_t matches[10];
    ret = regexec(&regex, str, 10, matches, 0);

    if (ret == 0) {
        /// Match successful - populate BASH_REMATCH array
        /// Use symtable_set_array_element which handles array creation
        for (int i = 0; i < 10 && matches[i].rm_so != -1; i++) {
            size_t match_len = matches[i].rm_eo - matches[i].rm_so;
            char *match_str = malloc(match_len + 1);
            if (match_str) {
                strncpy(match_str, str + matches[i].rm_so, match_len);
                match_str[match_len] = '\0';

                /// Convert index to string for subscript
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
 * Tree-walks the conditional-expression AST built by parse_extended_test
 * (NODE_COND_OR / _AND / _NOT / _BINARY / _UNARY with word-node operands).
 * Operands are expanded per node with expand_arg_node (scalar, provenance
 * aware, no word-splitting, no globbing); operators dispatch on the canonical
 * spelling stored in val.str.
 *
 * @param executor Executor context
 * @param test_node NODE_EXTENDED_TEST wrapper (0 children = empty = false)
 * @return 0 if the test passes (true), 1 if it fails (false)
 */

/// Forward declaration for the recursive conditional evaluator.
static bool cond_eval(executor_t *executor, node_t *node);

/// Expand a conditional operand word-node to a scalar string (no split, no
/// glob), honoring quote provenance. Never returns NULL.
static char *cond_expand_operand(executor_t *executor, node_t *node) {
    char *v = expand_arg_node(executor, node);
    if (!v) {
        v = strdup("");
    }
    return v;
}

/// The glob metacharacters lush_pattern_match treats as active on the [[ ]]
/// path -- basic glob (`* ? [ ]`), the extglob introducers (`+ @ !` before a
/// `(`), and grouping/alternation (`( )`), plus the escape byte itself.
/// Emitting a backslash before one makes the shared matcher match it literally.
static bool cond_pattern_meta(char c) {
    return c != '\0' && strchr("*?[]()+@!\\", c) != NULL;
}

/// Copy `expanded` into a new owned string, backslash-escaping every
/// metacharacter (used when the whole RHS operand is literal -- a single- or
/// fully double-quoted pattern). Frees `expanded`; returns it unchanged only on
/// allocation failure.
static char *cond_escape_all_meta(char *expanded) {
    size_t n = strlen(expanded);
    char *out = malloc(n * 2 + 1);
    if (!out) {
        return expanded;
    }
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (cond_pattern_meta(expanded[i])) {
            out[j++] = '\\';
        }
        out[j++] = expanded[i];
    }
    out[j] = '\0';
    free(expanded);
    return out;
}

/// Make the quoted glob metacharacters of a dequoted+expanded PATTERN operand
/// literal, leaving unquoted (U-provenance) metacharacters glob-active (#654).
/// `text`/`prov` are the pre-expansion (dequoted text, U/S/D/E map) from
/// lush_dequote_span; `expanded` is `text` after `$`-expansion. Frees
/// `expanded`, returns an owned string. Same discriminator and length-equality
/// strategy as cond_expand_rhs_pattern (the `[[ ]]` `==`/`!=` path).
static char *pe_glob_suppress(char *expanded, const char *text,
                              const char *prov) {
    if (!expanded || !prov || !text) {
        return expanded;
    }
    /// A fully unquoted operand keeps every metacharacter active -- an exact
    /// passthrough, which is what makes the fix a no-op for `${v%.gz}` etc.
    bool all_unquoted = true;
    for (size_t i = 0; prov[i] != '\0'; i++) {
        if (prov[i] != QUOTE_PROV_UNQUOTED) {
            all_unquoted = false;
            break;
        }
    }
    if (all_unquoted) {
        return expanded;
    }
    /// No length change from `$`-expansion: prov still aligns to `expanded`, so
    /// escape only the quoted (non-U) metacharacters -- a fused `abc"*"def`
    /// keeps its unquoted parts active.
    size_t elen = strlen(expanded);
    if (strlen(text) == elen) {
        char *out = malloc(elen * 2 + 1);
        if (!out) {
            return expanded;
        }
        size_t j = 0;
        for (size_t i = 0; i < elen; i++) {
            if (cond_pattern_meta(expanded[i]) &&
                prov[i] != QUOTE_PROV_UNQUOTED) {
                out[j++] = '\\';
            }
            out[j++] = expanded[i];
        }
        out[j] = '\0';
        free(expanded);
        return out;
    }
    /// Expansion changed the length so per-byte provenance no longer aligns:
    /// protect the whole operand (a lone quoted `"$var"` is uniformly quoted,
    /// so this is exact for it; the mixed-with-unquoted-trailing-glob corner
    /// over-literalizes, the same documented limitation the `[[ ]]` path
    /// holds).
    return cond_escape_all_meta(expanded);
}

static bool pe_operand_op(int op_type, pe_operand_class_t *out_class) {
    switch (op_type) {
    case 0:  /// :-
    case 1:  /// :+
    case 10: /// -
    case 11: /// +
    case 12: /// :=
    case 13: /// =
    case 18: /// :?
    case 19: /// ?
        *out_class = PE_OPERAND_VALUE;
        return true;
    case 2: /// ##
    case 3: /// %%
    case 6: /// #
    case 7: /// %
    case 4: /// ^^ (case-mod pattern restrictor)
    case 5: /// ,,
    case 8: /// ^
    case 9: /// ,
        *out_class = PE_OPERAND_PATTERN;
        return true;
    default: /// 14 substring, 15/16 replace (Commit 2), 17 transform
        return false;
    }
}

static char *pe_process_operand(executor_t *executor, const char *raw,
                                size_t len, pe_operand_class_t cls,
                                bool in_double_quotes) {
    char *text = NULL, *prov = NULL;
    /// NOTE: the shipped lush_dequote_span has no in_double_quotes MODE (the
    /// abandoned #654 branch added one; it is not on master). Every call site
    /// here passes in_double_quotes = false, so the unquoted rules apply and
    /// the parameter only governs tilde eligibility below. A `"${v:-~}"` whose
    /// tilde should stay literal is therefore not yet distinguished -- see the
    /// comment on the parameter.
    (void)in_double_quotes;
    if (!lush_dequote_span(raw, len, &text, &prov, NULL)) {
        /// Degrade to plain expansion on OOM rather than dropping the operand.
        return expand_variables_in_string(executor, raw);
    }
    /// A value operand in an UNQUOTED ${...} is a fresh word: a leading
    /// unquoted
    /// `~` tilde-expands. Inside a `"..."` string it is not a word, so `~`
    /// stays literal (the dequote already keeps single quotes / bare
    /// backslashes; the prov map drives the rest of the double-quote handling).
    bool allow_tilde = !in_double_quotes && cls == PE_OPERAND_VALUE;
    char *expanded =
        expand_quoted_string_prov(executor, text, false, prov, allow_tilde);
    if (cls == PE_OPERAND_PATTERN) {
        expanded = pe_glob_suppress(expanded, text, prov);
    }
    free(text);
    free(prov);
    return expanded;
}

/// Quote-remove + expand a REPLACE operand (`${v/pat/repl}`, `${v//pat/repl}`).
///
/// Unlike a value or pattern operand this one carries TWO halves, so it cannot
/// go through pe_process_operand: quote removal has to happen per half, AFTER
/// the separator is located.
///
/// The separator is the first `/` in the RAW operand that is not backslash-
/// escaped -- quoting does NOT protect it. That is the bash and zsh consensus,
/// verified: `v=a/b/c; ${v//"/"/-}` leaves the value untouched in both (the
/// separator is the `/` INSIDE the quotes, making the pattern a lone `"`),
/// while the idiomatic `${v//\//-}` replaces. Scanning the dequoted text
/// instead would find a different separator and silently diverge from both.
///
/// Each half is then dequoted and expanded on its own; only the pattern half is
/// glob-suppressed, so `${v//"a*"/Q}` matches the text `a*` while `${v//a*/Q}`
/// still globs. The halves are recombined into the `pattern/replacement`
/// spelling apply_param_operator expects, escaping any `/` produced INSIDE the
/// pattern as `\/` -- the escape its split already understands -- so the round
/// trip cannot re-split in the wrong place.
/// Remove one level of quoting from an INDEXED-array subscript before it is
/// evaluated as arithmetic. The associative path canonicalizes its key through
/// subscript_normalize_key; the indexed path has no such step, and since the
/// tokenizer began handing `${...}` over verbatim the quote bytes of
/// `${arr["k"]}` reach the arithmetic evaluator, which cannot parse them. Only
/// quote removal happens here -- `$`-expansion is the arithmetic evaluator's
/// own first pass, so doing it here too would expand twice. Returns an owned
/// string; falls back to a copy of the input on allocation failure.
static char *pe_dequote_subscript(const char *raw) {
    char *text = NULL, *prov = NULL;
    if (!lush_dequote_span(raw, strlen(raw), &text, &prov, NULL)) {
        return strdup(raw);
    }
    free(prov);
    return text;
}

static char *pe_process_replace_operand(executor_t *executor, const char *raw,
                                        size_t len) {
    size_t sep = len;
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == '\\' && i + 1 < len) {
            i++;
            continue;
        }
        if (raw[i] == '/') {
            sep = i;
            break;
        }
    }
    char *pat =
        pe_process_operand(executor, raw, sep, PE_OPERAND_PATTERN, false);
    char *rep = (sep < len)
                    ? pe_process_operand(executor, raw + sep + 1, len - sep - 1,
                                         PE_OPERAND_VALUE, false)
                    : NULL;
    if (!pat) {
        free(rep);
        return strdup("");
    }
    size_t plen = strlen(pat), rlen = rep ? strlen(rep) : 0;
    char *out = malloc(plen * 2 + rlen + 2);
    if (!out) {
        free(pat), free(rep);
        return strdup("");
    }
    size_t j = 0;
    for (size_t i = 0; i < plen; i++) {
        if (pat[i] == '/') {
            out[j++] = '\\';
        }
        out[j++] = pat[i];
    }
    if (rep) {
        out[j++] = '/';
        memcpy(out + j, rep, rlen);
        j += rlen;
    }
    out[j] = '\0';
    free(pat), free(rep);
    return out;
}

static char *cond_expand_rhs_pattern(executor_t *executor, node_t *rhs) {
    char *expanded = cond_expand_operand(executor, rhs);

    /// Single-quoted operand: every character is literal.
    if (rhs->type == NODE_STRING_LITERAL) {
        return cond_escape_all_meta(expanded);
    }

    /// Double-quoted (possibly a fused mixed-quote word): the per-character
    /// quote_prov map is aligned to the source. When the source expanded
    /// without changing length (no `$`/`` ` `` expansion, no dequoting) escape
    /// per character -- only the quoted (non-U) metacharacters, so a fused
    /// `abc"*"def` keeps its unquoted parts active. Otherwise the operand is a
    /// double-quoted string whose content is protected: escape every
    /// metacharacter (a lone quoted `"$var"` is uniformly protected, so this is
    /// exact for it).
    if (rhs->type == NODE_STRING_EXPANDABLE) {
        const char *prov = rhs->quote_prov;
        const char *src = rhs->val.str;
        size_t elen = strlen(expanded);
        if (prov && src && strlen(src) == elen) {
            char *out = malloc(elen * 2 + 1);
            if (!out) {
                return expanded;
            }
            size_t j = 0;
            for (size_t i = 0; i < elen; i++) {
                if (cond_pattern_meta(expanded[i]) &&
                    prov[i] != QUOTE_PROV_UNQUOTED) {
                    out[j++] = '\\';
                }
                out[j++] = expanded[i];
            }
            out[j] = '\0';
            free(expanded);
            return out;
        }
        return cond_escape_all_meta(expanded);
    }

    /// Unquoted operand (NODE_VAR / arithmetic / command sub): metacharacters
    /// are active -- including those introduced by an unquoted `$var`
    /// expansion (the curated consensus) and the extglob/regex parens the
    /// parser gathered as an all-unquoted pattern. The one literal case is a
    /// backslash-escaped metacharacter written directly in the source (`a\*`),
    /// which expand_arg_node strips: rebuild from the source keeping
    /// `\<meta>` literal. A source that mixes a backslash with an expansion is
    /// exotic and is left as the plain (active) expansion.
    if (rhs->val.str && strchr(rhs->val.str, '\\') &&
        !strchr(rhs->val.str, '$') && !strchr(rhs->val.str, '`')) {
        free(expanded);
        const char *s = rhs->val.str;
        size_t n = strlen(s);
        char *out = malloc(n * 2 + 1);
        if (!out) {
            return strdup(s);
        }
        size_t j = 0;
        for (size_t i = 0; i < n; i++) {
            if (s[i] == '\\' && s[i + 1] != '\0') {
                char c = s[i + 1];
                if (cond_pattern_meta(c)) {
                    out[j++] = '\\';
                }
                out[j++] = c;
                i++;
            } else {
                out[j++] = s[i];
            }
        }
        out[j] = '\0';
        return out;
    }
    return expanded;
}

/// Test whether -v's operand names a set variable or a set array element.
///   -v NAME       -- true if any binding (scalar or array) named NAME is set.
///   -v NAME[KEY]  -- true if the associative key / indexed slot is set.
/// Bash semantics: -v on an unset element or unset scalar is false (#97).
static bool cond_var_set(const char *operand) {
    if (!operand) {
        return false;
    }
    char *arg = strdup(operand);
    if (!arg) {
        return false;
    }
    /// Trim trailing whitespace defensively (operand is scalar-expanded).
    char *end = arg + strlen(arg);
    while (end > arg && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    bool result = false;
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
                    result = symtable_array_get_assoc(array, key) != NULL;
                } else {
                    char *endp = NULL;
                    long idx = strtol(key, &endp, 10);
                    if (endp && *endp == '\0') {
                        result = symtable_array_get_index(array, idx) != NULL;
                    }
                }
            }
        }
    } else {
        /// Bare NAME: true for any kind of binding (scalar OR array).
        lush_value_view_t view = {0};
        result = symtable_lookup(arg, &view);
        lush_value_view_clear(&view);
    }
    free(arg);
    return result;
}

/// Evaluate a NODE_COND_UNARY: `op operand`.
static bool cond_eval_unary(executor_t *executor, node_t *node) {
    const char *op = node->val.str;
    node_t *word = node->first_child;
    if (!op || !word) {
        return false;
    }
    char *operand = cond_expand_operand(executor, word);
    bool result;
    if (strcmp(op, "-z") == 0) {
        result = (operand[0] == '\0');
    } else if (strcmp(op, "-n") == 0) {
        result = (operand[0] != '\0');
    } else if (strcmp(op, "-v") == 0) {
        result = cond_var_set(operand);
    } else if (strcmp(op, "-o") == 0) {
        result = shell_is_option_set(operand);
    } else {
        /// File tests -e/-f/-d/.../-O/-G (and unimplemented -t/-a/-N/-R ->
        /// false).
        result = extended_test_file_test(op, operand);
    }
    free(operand);
    return result;
}

/// Evaluate a NODE_COND_BINARY: `lhs op rhs`.
static bool cond_eval_binary(executor_t *executor, node_t *node) {
    const char *op = node->val.str;
    node_t *lnode = node->first_child;
    node_t *rnode = lnode ? lnode->next_sibling : NULL;
    if (!op || !lnode || !rnode) {
        return false;
    }
    char *lhs = cond_expand_operand(executor, lnode);
    /// The ==/!= RHS is a glob PATTERN whose quoted/escaped metacharacters must
    /// be literal (issue #515), so it is expanded with the provenance-aware
    /// escape. Every other operator -- including the =~ regex, whose
    /// metacharacter set differs -- takes the plain scalar RHS.
    char *rhs = (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0)
                    ? cond_expand_rhs_pattern(executor, rnode)
                    : cond_expand_operand(executor, rnode);
    bool result = false;
    if (strcmp(op, "=~") == 0) {
        result = extended_test_regex_match(executor, lhs, rhs);
    } else if (strcmp(op, "==") == 0) {
        result = extended_test_pattern_match(lhs, rhs);
    } else if (strcmp(op, "!=") == 0) {
        result = !extended_test_pattern_match(lhs, rhs);
    } else if (strcmp(op, "<") == 0) {
        result = (strcmp(lhs, rhs) < 0);
    } else if (strcmp(op, ">") == 0) {
        result = (strcmp(lhs, rhs) > 0);
    } else if (strcmp(op, "-eq") == 0) {
        result = (atoll(lhs) == atoll(rhs));
    } else if (strcmp(op, "-ne") == 0) {
        result = (atoll(lhs) != atoll(rhs));
    } else if (strcmp(op, "-lt") == 0) {
        result = (atoll(lhs) < atoll(rhs));
    } else if (strcmp(op, "-le") == 0) {
        result = (atoll(lhs) <= atoll(rhs));
    } else if (strcmp(op, "-gt") == 0) {
        result = (atoll(lhs) > atoll(rhs));
    } else if (strcmp(op, "-ge") == 0) {
        result = (atoll(lhs) >= atoll(rhs));
    } else if (strcmp(op, "-nt") == 0) {
        struct stat s1, s2;
        result = (stat(lhs, &s1) == 0 && stat(rhs, &s2) == 0 &&
                  s1.st_mtime > s2.st_mtime);
    } else if (strcmp(op, "-ot") == 0) {
        struct stat s1, s2;
        result = (stat(lhs, &s1) == 0 && stat(rhs, &s2) == 0 &&
                  s1.st_mtime < s2.st_mtime);
    } else if (strcmp(op, "-ef") == 0) {
        struct stat s1, s2;
        result = (stat(lhs, &s1) == 0 && stat(rhs, &s2) == 0 &&
                  s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino);
    }
    free(lhs);
    free(rhs);
    return result;
}

/// Recursively evaluate one conditional-expression node. OR/AND short-circuit
/// via C's && / ||; NOT inverts; BINARY/UNARY dispatch on the operator; any
/// other node is a bare word-operand tested for truthiness (non-empty after
/// scalar expansion).
static bool cond_eval(executor_t *executor, node_t *node) {
    if (!node) {
        return false;
    }
    switch (node->type) {
    case NODE_COND_OR: {
        node_t *l = node->first_child;
        node_t *r = l ? l->next_sibling : NULL;
        return cond_eval(executor, l) || cond_eval(executor, r);
    }
    case NODE_COND_AND: {
        node_t *l = node->first_child;
        node_t *r = l ? l->next_sibling : NULL;
        return cond_eval(executor, l) && cond_eval(executor, r);
    }
    case NODE_COND_NOT:
        return !cond_eval(executor, node->first_child);
    case NODE_COND_UNARY:
        return cond_eval_unary(executor, node);
    case NODE_COND_BINARY:
        return cond_eval_binary(executor, node);
    default: {
        char *v = cond_expand_operand(executor, node);
        bool result = (v[0] != '\0');
        free(v);
        return result;
    }
    }
}

static int execute_extended_test(executor_t *executor, node_t *test_node) {
    if (!test_node) {
        return 1;
    }
    /// Empty `[[ ]]` (no children) is false.
    node_t *expr = test_node->first_child;
    bool result = expr ? cond_eval(executor, expr) : false;
    executor->exit_status = result ? 0 : 1;
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
/// Report a scalar->array kind-transition error (issue #621) -- the mirror of
/// the §3.9 list->scalar E1134. Emitted only under strict value typing (lush
/// mode), when an array element write or append would implicitly re-kind an
/// existing scalar into a list. Requests a POSIX exit like the §3.9 gate.
static void report_scalar_kind_error(executor_t *executor,
                                     source_location_t loc, const char *name) {
    shell_error_t *err = shell_error_create(
        SHELL_ERR_TYPE_MISMATCH, SHELL_SEVERITY_ERROR, loc,
        "type mismatch: cannot apply an array subscript to scalar '%s'", name);
    if (err) {
        char sugg[192];
        snprintf(sugg, sizeof(sugg),
                 "'%s' holds a scalar value; declare it a list first "
                 "(declare -a %s) or unset it before indexing.",
                 name, name);
        shell_error_set_suggestion(err, sugg);
        shell_error_display(err, stderr, isatty(STDERR_FILENO));
        shell_error_free(err);
        executor->has_error = true;
    } else {
        executor_error_report(
            executor, SHELL_ERR_TYPE_MISMATCH, loc,
            "type mismatch: cannot apply an array subscript to scalar '%s'",
            name);
    }
    executor_request_posix_exit(executor, 1);
}

/*
 * Reject an assignment whose subscript is the aggregate selector [@]/[*]
 * (issue #627). [@]/[*] select the whole array (all elements) in read
 * contexts (${a[@]}, ${#a[*]}); they are not a single writable element
 * address, so they are not a valid assignment target -- uniformly for indexed
 * and associative arrays (bash+zsh reject the indexed form; zsh rejects the
 * associative form too, while bash reinterprets it as a literal key, which
 * would make [@] mean "all" on read but "the @ key" on write -- an overload
 * lush declines). Non-fatal, mirroring the readonly-target reject: the
 * assignment fails with a non-zero status and the script continues. Shared
 * (prototype in executor.h) by all three write entry points: element
 * assignment and array-literal element here, and the declare/typeset/local/
 * readonly compound literal in builtin_bind_array_literal.
 */
void report_aggregate_subscript_error(executor_t *executor,
                                      source_location_t loc,
                                      const char *subscript) {
    shell_error_t *err = shell_error_create(
        SHELL_ERR_INVALID_SUBSCRIPT, SHELL_SEVERITY_ERROR, loc,
        "invalid array subscript '[%s]': the aggregate selector is not a "
        "writable element",
        subscript);
    if (err) {
        shell_error_set_suggestion(
            err,
            "[@] and [*] select all elements for reading and cannot be an "
            "assignment target; replace the whole array with 'name=(...)', or "
            "assign to a specific index or key. A literal @ or * associative "
            "key "
            "needs a variable subscript: k='@'; name[$k]=value.");
        shell_error_display(err, stderr, isatty(STDERR_FILENO));
        shell_error_free(err);
        executor->has_error = true;
    } else {
        executor_error_report(executor, SHELL_ERR_INVALID_SUBSCRIPT, loc,
                              "invalid array subscript '[%s]'", subscript);
    }
}

/// An indexed-array subscript that does not evaluate to an integer (e.g. a
/// non-numeric key on an array that was never `declare -A`'d). Rust-style
/// targeted diagnostic that points at the likely intent -- an associative key
/// -- replacing the bare "Invalid array index" string. Paired with a rollback
/// of any array the failing assignment just auto-created (#631 Phase 2c).
static void report_invalid_index_error(executor_t *executor,
                                       source_location_t loc,
                                       const char *subscript) {
    shell_error_t *err = shell_error_create(
        SHELL_ERR_INVALID_SUBSCRIPT, SHELL_SEVERITY_ERROR, loc,
        "invalid array subscript '[%s]': an indexed-array subscript must "
        "evaluate to an integer",
        subscript);
    if (err) {
        char suggestion[256];
        snprintf(
            suggestion, sizeof(suggestion),
            "'[%s]' is not a valid arithmetic index; to key on it as a "
            "string, declare the array associative first: declare -A name.",
            subscript);
        shell_error_set_suggestion(err, suggestion);
        shell_error_display(err, stderr, isatty(STDERR_FILENO));
        shell_error_free(err);
        executor->has_error = true;
    } else {
        executor_error_report(executor, SHELL_ERR_INVALID_SUBSCRIPT, loc,
                              "invalid array subscript '[%s]'", subscript);
    }
}

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

    /// Readonly enforcement: if the array name is bound to an existing
    /// readonly array, refuse both element writes (arr[idx]=value) and
    /// bulk-literal rebinds (arr=(...)). The bin_declare / readonly
    /// paths set SYMVAR_READONLY on the array's own flags field;
    /// element-level writes from this function bypass
    /// symtable_set_array_element's enforcement, so the check has to
    /// run here too. A readonly SCALAR promoted to an array is refused
    /// too -- its readonly bit lives in the symvar flags, not array->flags,
    /// so check both. Match the scalar diagnostic for consistency.
    if ((symtable_array_get_flags(var_name) & SYMVAR_READONLY) ||
        (symtable_get_flags(executor->symtable, var_name) & SYMVAR_READONLY)) {
        executor_error_report(executor, SHELL_ERR_READONLY_VAR,
                              assign_node->loc, "%s: readonly variable",
                              var_name);
        return 1;
    }

    /// Check if this is an array literal assignment: arr=(a b c)
    if (first_child->type == NODE_ARRAY_LITERAL) {
        /// Check if variable was already declared as associative array
        array_value_t *existing = symtable_get_array(var_name);
        bool is_associative = existing && existing->is_associative;

        /// Create appropriate array type (preserving associative if declared)
        array_value_t *array = symtable_array_create(is_associative);
        if (!array) {
            set_executor_error(executor, "Failed to create array");
            return 1;
        }

        /// Process each element in the literal
        int index = 0;
        node_t *elem = first_child->first_child;

        while (elem) {
            if (elem->val.str) {
                const char *elem_str = elem->val.str;

                /// Check for [index]=value syntax
                if (elem_str[0] == '[') {
                    /// Parse [index]=value
                    const char *bracket_end = strchr(elem_str, ']');
                    if (bracket_end && bracket_end[1] == '=') {
                        /// Extract index/key
                        size_t idx_len = bracket_end - elem_str - 1;
                        char *idx_str = malloc(idx_len + 1);
                        if (idx_str) {
                            strncpy(idx_str, elem_str + 1, idx_len);
                            idx_str[idx_len] = '\0';

                            /// a=([@]=x) is the array-literal spelling of
                            /// a[@]=x: the aggregate selector is not a writable
                            /// element target, so reject it here too (issue
                            /// #627, uniform in every context). Fail the whole
                            /// literal -- the fresh array is discarded and the
                            /// existing binding, stored only after this loop,
                            /// is left untouched -- matching the element path.
                            if (strcmp(idx_str, "@") == 0 ||
                                strcmp(idx_str, "*") == 0) {
                                report_aggregate_subscript_error(
                                    executor, assign_node->loc, idx_str);
                                free(idx_str);
                                symtable_array_free(array);
                                return 1;
                            }

                            /// Get value after ]=
                            const char *value = bracket_end + 2;

                            /// Expand value using full expansion (handles
                            /// $'...' ANSI-C quoting)
                            char *expanded = expand_if_needed(executor, value);
                            const char *final_value =
                                expanded ? expanded : value;

                            if (is_associative) {
                                /// Use string key directly for associative
                                /// arrays
                                symtable_array_set_assoc(array, idx_str,
                                                         final_value);
                            } else {
                                /// Evaluate index as arithmetic for indexed
                                /// arrays
                                arithm_clear_error();
                                char *idx_result = arithm_expand_with_executor(
                                    executor, idx_str);
                                long long idx_val = -1;
                                if (idx_result && !arithm_error_is_flagged()) {
                                    idx_val = strtoll(idx_result, NULL, 10);
                                }
                                free(idx_result);

                                if (idx_val >= 0) {
                                    /// Explicit non-negative index: store at
                                    /// the full-width index (set_index is
                                    /// int64, issue #618 -- no (int)
                                    /// truncation) and advance the positional
                                    /// counter to follow when it fits the
                                    /// counter's width.
                                    symtable_array_set_index(array, idx_val,
                                                             final_value);
                                    if (idx_val <= INT_MAX) {
                                        index = (int)idx_val;
                                    }
                                } else {
                                    /// Unparseable / arithmetic error /
                                    /// negative falls back to the running
                                    /// positional index (pre-existing
                                    /// behavior).
                                    symtable_array_set_index(array, index,
                                                             final_value);
                                }
                            }

                            free(idx_str);
                            if (expanded) {
                                free(expanded);
                            }
                        }
                    }
                } else {
                    /// Splice list-kinded expansions into the array
                    /// builder. Array construction `name=( ... )` is a
                    /// collection-accepting context per the lush value-
                    /// model: a list-yielding expansion like
                    /// `${arr[@]}` or `${(s/:/)str}` here means "insert
                    /// these N elements at the current index", not "the
                    /// joined string occupies one slot."
                    ///
                    /// `try_expand_vector_arg` already recognises bare-
                    /// array, `${arr[@]}`, and `$@`/`$*` shapes; when
                    /// it succeeds we splice the resulting vector and
                    /// skip the per-element expansion path. The
                    /// quoted form `arr=("${other[@]}")` -- which used
                    /// to fail with E1133 ("list in scalar position")
                    /// -- now succeeds via this branch.
                    if (!is_associative) {
                        char **vec = NULL;
                        int vcount = 0;
                        if (try_expand_vector_arg(executor, elem, &vec, &vcount,
                                                  /*positional_only=*/false)) {
                            for (int vi = 0; vi < vcount; vi++) {
                                if (vec[vi]) {
                                    symtable_array_set_index(array, index,
                                                             vec[vi]);
                                    index++;
                                    free(vec[vi]);
                                }
                            }
                            free(vec);
                            elem = elem->next_sibling;
                            continue;
                        }
                    }

                    /// Regular element without [key]=value syntax
                    if (is_associative) {
                        /// Zsh-style: arr=(key1 val1 key2 val2 ...)
                        /// Alternating key-value pairs
                        char *expanded_key = expand_arg_node(executor, elem);
                        const char *key =
                            expanded_key ? expanded_key : elem_str;

                        /// Get next element as value
                        node_t *value_elem = elem->next_sibling;
                        if (value_elem && value_elem->val.str) {
                            char *expanded_val =
                                expand_arg_node(executor, value_elem);
                            const char *val = expanded_val
                                                  ? expanded_val
                                                  : value_elem->val.str;

                            symtable_array_set_assoc(array, key, val);

                            if (expanded_val)
                                free(expanded_val);
                            elem = value_elem; /// Skip the value element
                        }
                        if (expanded_key)
                            free(expanded_key);
                    } else {
                        /// Indexed array - assign to next index. Expand through
                        /// the command-word dispatch so a quoted element goes
                        /// through the double-quote expander (no tilde -- a
                        /// `"~/y"` element stays literal) while an unquoted
                        /// element keeps expand_if_needed's tilde/glob path
                        /// (#498). Handles $'...' ANSI-C via the LITERAL
                        /// branch.
                        char *expanded = expand_arg_node(executor, elem);
                        const char *final_value =
                            expanded ? expanded : elem_str;

                        /// Only word split for unquoted elements (NODE_VAR)
                        /// Quoted strings (NODE_STRING_LITERAL,
                        /// NODE_STRING_EXPANDABLE) should be stored as single
                        /// elements even if they contain spaces
                        bool is_quoted = (elem->type == NODE_STRING_LITERAL ||
                                          elem->type == NODE_STRING_EXPANDABLE);

                        /// Brace expansion on unquoted indexed-array
                        /// elements: `arr=(/tmp/{a,b}/sub)` must yield
                        /// two elements, matching bash/zsh behavior.
                        /// Each brace-expansion result becomes its own
                        /// array element regardless of internal spaces
                        /// (the brace expander has already partitioned
                        /// the source into discrete words).
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

                        /// A fused glob qualifier (`arr=("$f"(Nm-1))`)
                        /// filters the quoted element's literal value; per
                        /// SEMANTICS 3.6 the quote already fixed it as a
                        /// scalar, so the qualifier is an existence/attribute
                        /// test, not a re-glob. Gated to the glob-qualifier
                        /// modes; elsewhere the element stays literal.
                        if (elem->glob_qualified &&
                            shell_mode_allows(FEATURE_GLOB_QUALIFIERS)) {
                            int glob_count = 0;
                            char **glob_results =
                                apply_glob_qualifier_to_literal(final_value,
                                                                &glob_count);
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

                        /// Pathname (glob) expansion on unquoted
                        /// indexed-array elements: `arr=(*.txt)` must
                        /// list the matching files, not iterate the
                        /// literal pattern. expand_glob_pattern handles
                        /// zsh glob qualifiers, extglob, nullglob, and
                        /// `set -f` internally.
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

                        /// Word-split the expanded value via the canonical
                        /// ifs_field_split: respects live IFS (the old
                        /// hardcoded space-only split missed `\t`/`\n`) and
                        /// honors non-whitespace-IFS empty-field semantics
                        /// strtok cannot express. Quoted elements are not
                        /// split.
                        if (!is_quoted) {
                            char *ifs_owned =
                                symtable_get(executor->symtable, "IFS");
                            const char *ifs = ifs_owned ? ifs_owned : " \t\n";
                            int field_count = 0;
                            char **fields =
                                ifs_field_split(final_value, ifs, &field_count);
                            free(ifs_owned);
                            if (fields && field_count > 0) {
                                for (int fi = 0; fi < field_count; fi++) {
                                    if (fields[fi] && *fields[fi]) {
                                        symtable_array_set_index(array, index,
                                                                 fields[fi]);
                                        index++;
                                    }
                                    free(fields[fi]);
                                }
                                free(fields);
                            } else {
                                free(fields);
                                /// Null-word removal: an unquoted element that
                                /// expanded to the empty string contributes
                                /// zero array elements (`arr=($x)` with x=""
                                /// yields an empty array). A non-empty but
                                /// unsplittable value (whitespace-only, or a
                                /// split-OOM fallback) is still stored so it
                                /// is not silently dropped.
                                if (final_value[0] != '\0') {
                                    symtable_array_set_index(array, index,
                                                             final_value);
                                    index++;
                                }
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

        /// Store the array in the symbol table. A bare `a=(...)` resolves
        /// scope like a scalar assignment (#614): inside a function it updates
        /// an existing outer binding in place or, if unbound, creates the array
        /// in the global scope rather than the transient function frame.
        if (symtable_assign_array(var_name, array) != 0) {
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

    /// Array element assignment: arr[n]=value
    /// First child is subscript, second child is value
    node_t *subscript_node = first_child;
    node_t *value_node = first_child->next_sibling;

    if (!subscript_node || !subscript_node->val.str) {
        set_executor_error(executor, "Missing array subscript");
        return 1;
    }

    const char *subscript = subscript_node->val.str;
    const char *value = value_node ? value_node->val.str : "";

    /// The aggregate selector [@]/[*] denotes the whole array (all elements)
    /// for reading; it is not a single writable element address, so it is not
    /// a valid assignment target -- uniformly for indexed and associative
    /// arrays (issue #627). Reject the literal @ or * before any value-expand
    /// or scalar->array promotion side effect, so `s[@]=x` never promotes a
    /// scalar. The check is syntactic (matching how reads recognize the
    /// aggregate); a dynamic subscript (`name[$k]=v` with k=@) is a distinct
    /// literal-key write and is left alone. Runs after the readonly guard
    /// above, so a readonly target reports readonly first (#621 precedence).
    if (strcmp(subscript, "@") == 0 || strcmp(subscript, "*") == 0) {
        report_aggregate_subscript_error(executor, assign_node->loc, subscript);
        return 1;
    }

    /// Check for append operation (value starts with "+=")
    bool is_append = false;
    if (value && strlen(value) >= 2 && value[0] == '+' && value[1] == '=') {
        is_append = true;
        value += 2; /// Skip "+=" prefix
    }

    /// Expand value
    char *expanded_value = expand_variable(executor, value);
    const char *final_value = expanded_value ? expanded_value : value;

    /// Get or create the array. Only a truly-fresh binding may be rolled back
    /// when the index turns out invalid (#631 Phase 2c): `m["a b"]=9` on an
    /// UNDECLARED name would otherwise leave a phantom empty indexed array
    /// behind. A scalar being promoted to an array (SCALAR_PROMO_PRESERVE) must
    /// NOT be rolled back -- unsetting the name would destroy the user's scalar
    /// on the error path -- and a pre-existing array is never touched.
    bool rollback_on_fail = false;
    array_value_t *array = symtable_get_array(var_name);
    if (!array) {
        /// A scalar->array kind transition (issue #621): strict value typing
        /// (lush mode) refuses the implicit re-kind; a relaxed mode promotes
        /// non-lossily, keeping the former scalar as the base element. An
        /// unbound name is a fresh array (no kind change).
        scalar_promo_t promo = symtable_scalar_promotion(var_name);
        if (promo == SCALAR_PROMO_REFUSE) {
            if (expanded_value)
                free(expanded_value);
            report_scalar_kind_error(executor, assign_node->loc, var_name);
            return 1;
        }
        /// Roll back only an unbound-name fresh array; never a promoted scalar.
        rollback_on_fail = (promo == SCALAR_PROMO_NONE);
        /// Create new array if it doesn't exist
        array = symtable_array_create(false);
        if (!array) {
            if (expanded_value)
                free(expanded_value);
            set_executor_error(executor, "Failed to create array");
            return 1;
        }
        /// Seed the base index BEFORE the store overwrites the scalar binding.
        if (promo == SCALAR_PROMO_PRESERVE) {
            symtable_seed_promoted_scalar(var_name, array);
        }
        /// A fresh `a[i]=v` resolves scope like a scalar assignment (#614):
        /// created in the enclosing/global scope, not the function frame.
        if (symtable_assign_array(var_name, array) != 0) {
            symtable_array_free(array);
            if (expanded_value)
                free(expanded_value);
            set_executor_error(executor, "Failed to store array");
            return 1;
        }
    }

    /// Handle subscript - a string key (associative) or a numeric index. The
    /// aggregate selector [@]/[*] was rejected up front (issue #627).
    if (array->is_associative) {
        /// Associative array - use subscript as string key. Canonicalize the
        /// raw interior (remove one level of quoting/escaping, $-expand) so a
        /// key written m["a b"]=v / m[a\ b]=v is stored under the same bytes a
        /// later ${m[a b]} reads back (#631).
        char *expanded_subscript =
            subscript_normalize_key(executor, subscript, strlen(subscript));
        const char *key = expanded_subscript ? expanded_subscript : subscript;

        if (is_append) {
            /// Append to existing element
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
        /// Indexed array - evaluate subscript as arithmetic expression
        arithm_clear_error();
        char *idx_raw = pe_dequote_subscript(subscript);
        char *idx_result = arithm_expand_with_executor(
            executor, idx_raw ? idx_raw : subscript);
        free(idx_raw);

        if (!idx_result || arithm_error_is_flagged()) {
            if (idx_result)
                free(idx_result);
            if (expanded_value)
                free(expanded_value);
            /// Roll back a just-auto-created array so a failed non-integer
            /// index leaves no phantom binding, then report a targeted error.
            if (rollback_on_fail) {
                symtable_unset_global(var_name);
            }
            report_invalid_index_error(executor, assign_node->loc, subscript);
            return 1;
        }

        long long idx = strtoll(idx_result, NULL, 10);
        free(idx_result);

        /// Adjust for 1-indexed arrays (zsh mode)
        /// When FEATURE_ARRAY_ZERO_INDEXED is false, user index 1 maps to
        /// internal 0
        if (!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED)) {
            if (idx <= 0) {
                if (expanded_value)
                    free(expanded_value);
                /// Same phantom-array rollback as the non-integer path.
                if (rollback_on_fail) {
                    symtable_unset_global(var_name);
                }
                set_executor_error(executor,
                                   "Array index must be positive in zsh mode");
                return 1;
            }
            idx--; /// Convert 1-indexed to 0-indexed internally
        }

        int set_rc = 0;
        if (is_append) {
            /// Append to existing element
            const char *existing = symtable_array_get_index(array, idx);
            if (existing) {
                size_t new_len = strlen(existing) + strlen(final_value) + 1;
                char *combined = malloc(new_len);
                if (combined) {
                    strcpy(combined, existing);
                    strcat(combined, final_value);
                    set_rc = symtable_array_set_index(array, idx, combined);
                    free(combined);
                }
            } else {
                set_rc = symtable_array_set_index(array, idx, final_value);
            }
        } else {
            set_rc = symtable_array_set_index(array, idx, final_value);
        }

        /// A from-end negative subscript that resolves below 0 is out of range.
        /// Report it (matching the arithmetic path) rather than silently
        /// no-oping, so the plain surface has the same error/status (issue
        /// #618). A positive subscript is a native 64-bit key and never fails
        /// here.
        if (set_rc < 0) {
            executor_error_report_with_help(
                executor, SHELL_ERR_INVALID_SUBSCRIPT, assign_node->loc,
                "the subscript is out of range for this array",
                "array subscript %lld out of range for '%s'", idx, var_name);
            if (expanded_value)
                free(expanded_value);
            return 1;
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

    /// Readonly enforcement, mirroring execute_array_assignment: a bare
    /// `arr+=(...)` must refuse when arr is a readonly array (whose readonly
    /// bit is in array->flags) or a readonly scalar being promoted (bit in the
    /// symvar flags). Without this the in-place append path silently extends a
    /// readonly array and the create path could clobber a readonly scalar.
    if ((symtable_array_get_flags(var_name) & SYMVAR_READONLY) ||
        (symtable_get_flags(executor->symtable, var_name) & SYMVAR_READONLY)) {
        executor_error_report(executor, SHELL_ERR_READONLY_VAR,
                              append_node->loc, "%s: readonly variable",
                              var_name);
        return 1;
    }

    /// Get existing array or create new one
    array_value_t *array = symtable_get_array(var_name);
    bool new_array = false;

    if (!array) {
        /// A scalar->array kind transition (issue #621): strict value typing
        /// (lush mode) refuses the implicit re-kind; a relaxed mode promotes
        /// non-lossily, keeping the former scalar as the base element (appended
        /// elements then land at index 1+). An unbound name is a fresh array.
        scalar_promo_t promo = symtable_scalar_promotion(var_name);
        if (promo == SCALAR_PROMO_REFUSE) {
            report_scalar_kind_error(executor, append_node->loc, var_name);
            return 1;
        }
        /// Create new array if it doesn't exist
        array = symtable_array_create(false);
        if (!array) {
            set_executor_error(executor, "Failed to create array");
            return 1;
        }
        /// Seed the base index BEFORE the append loop and the store.
        if (promo == SCALAR_PROMO_PRESERVE) {
            symtable_seed_promoted_scalar(var_name, array);
        }
        new_array = true;
    }

    /// Process each element in the literal and append
    node_t *elem = first_child->first_child;

    while (elem) {
        if (elem->val.str) {
            const char *elem_str = elem->val.str;

            /// Expand value if needed
            char *expanded = expand_variable(executor, elem_str);
            const char *final_value = expanded ? expanded : elem_str;

            /// Append to array using symtable_array_append
            symtable_array_append(array, final_value);

            if (expanded) {
                free(expanded);
            }
        }
        elem = elem->next_sibling;
    }

    /// Store the array if newly created. A fresh `a+=(...)` resolves scope
    /// like a scalar assignment (#614): enclosing/global, not the frame.
    if (new_array) {
        if (symtable_assign_array(var_name, array) != 0) {
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
    /// Close all tracked file descriptors first
    for (int i = 0; i < executor->procsub_fd_count; i++) {
        if (executor->procsub_fds[i] >= 0) {
            close(executor->procsub_fds[i]);
        }
    }
    /// Wait for all child processes to prevent zombies and terminal issues.
    /// Route through executor_wait_foreground so an EINTR (the bare wait had
    /// no retry, leaking a zombie) is retried and a hangup is honored.
    for (int i = 0; i < executor->procsub_fd_count; i++) {
        if (executor->procsub_pids[i] > 0) {
            executor_wait_foreground(executor->procsub_pids[i], NULL);
        }
    }
    executor->procsub_fd_count = 0;
}

char *expand_process_substitution(executor_t *executor, node_t *proc_sub) {
    if (!executor || !proc_sub) {
        return NULL;
    }

    /// Check if feature is enabled
    if (!shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION)) {
        set_executor_error(executor, "Process substitution not enabled");
        return NULL;
    }

    bool is_input = (proc_sub->type == NODE_PROC_SUB_IN); /// <(cmd)

    /// Create a pipe for communication
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
        /// Child process - execute the command. Process substitution is
        /// asynchronous: mark the context so it ignores SIGINT per the POSIX
        /// async-list rule, then reset the inherited hangup/fault handlers
        /// (#375).
        executor->async_context = true;
        reset_subshell_signals();
        if (is_input) {
            /// <(cmd): command writes to pipe, parent reads
            close(pipefd[0]); /// Close read end
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            /// Disconnect stdin from terminal to prevent stealing input
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        } else {
            /// >(cmd): parent writes to pipe, command reads
            close(pipefd[1]); /// Close write end
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
        }

        /// Execute the command list in the process substitution
        /// Create a child executor
        executor_t *child_executor = executor_new();
        if (!child_executor) {
            subshell_cleanup();
            _exit(1);
        }

        /// Copy function definitions to child
        copy_function_definitions(child_executor, executor);

        /// Execute each command in the process substitution
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

    /// Parent process
    char *path = NULL;
    int kept_fd = -1;

    if (is_input) {
        /// <(cmd): We need to provide a readable path
        /// Close write end, keep read end
        close(pipefd[1]);
        kept_fd = pipefd[0];

        /// Use /dev/fd/N mechanism if available (macOS and Linux)
        path = malloc(32);
        if (path) {
            snprintf(path, 32, "/dev/fd/%d", kept_fd);
        }
    } else {
        /// >(cmd): We need to provide a writable path
        /// Close read end, keep write end
        close(pipefd[0]);
        kept_fd = pipefd[1];

        path = malloc(32);
        if (path) {
            snprintf(path, 32, "/dev/fd/%d", kept_fd);
        }
    }

    /// Track this fd and pid for cleanup after command execution
    /// This prevents fd leaks and zombie processes with nested process
    /// substitutions
    if (kept_fd >= 0 && executor->procsub_fd_count < 32) {
        executor->procsub_fds[executor->procsub_fd_count] = kept_fd;
        executor->procsub_pids[executor->procsub_fd_count] = pid;
        executor->procsub_fd_count++;
    }

    return path;
}

/* ============================================================================
 * HOOK FUNCTIONS (Zsh-Specific)
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

    /// Check if hook functions are enabled
    if (!shell_mode_allows(FEATURE_HOOK_FUNCTIONS)) {
        return 0;
    }

    /// Prevent recursive hook calls
    if (g_in_hook_execution) {
        return 0;
    }

    /// Look up the hook function
    function_def_t *func = find_function(executor, hook_name);
    if (!func) {
        return 0; /// Hook not defined, that's fine
    }

    /// Set recursion guard
    g_in_hook_execution = true;

    /// Build argv for the function call
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

    /// Call the function (no AST node for hook invocations)
    int result = execute_function_call(executor, hook_name, argv, argc,
                                       SOURCE_LOC_UNKNOWN);

    /// Handle return code translation (200-455 range is internal return signal)
    if (result >= 200 && result <= 455) {
        result = result - 200;
    }

    /// Clear recursion guard
    g_in_hook_execution = false;

    return result;
}

int executor_run_function(executor_t *executor, const char *fn_name, int argc,
                          char **argv) {
    if (!executor || !fn_name || argc < 1 || !argv) {
        return 0;
    }
    /// Autoload-style semantics: an undefined function silently
    /// produces no effect, matching how compdef bindings reference
    /// function names that may be loaded later (or never).
    if (!find_function(executor, fn_name)) {
        return 0;
    }
    int result = execute_function_call(executor, fn_name, argv, argc,
                                       SOURCE_LOC_UNKNOWN);
    /// Mirror executor_call_hook's translation of the internal
    /// return-signal range so a `return N` from the function surfaces
    /// the user's intended exit code, not the encoded signal.
    if (result >= 200 && result <= 455) {
        result = result - 200;
    }
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

/// Deep-copy an array_value_t. The source remains owned by its
/// originator (typically the caller's scope binding); the returned
/// copy is independent and owned by the caller of this helper. Returns
/// NULL on allocation failure.
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
        /// Walk the insertion-ordered map so the copy preserves the
        /// original key ordering (SEMANTICS.md 4.2).
        ht_enum_t *e = ht_strstr_enum_create(src->assoc_map);
        if (e) {
            const char *key, *value;
            while (ht_strstr_enum_next(e, &key, &value)) {
                if (symtable_array_set_assoc(dup, key, value ? value : "") !=
                    0) {
                    ht_strstr_enum_destroy(e);
                    symtable_array_free(dup);
                    return NULL;
                }
            }
            ht_strstr_enum_destroy(e);
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
    /// body was deep-copied via copy_ast_chain at registration time
    /// (see execute_typed_fn_decl); the registry owns the copy.
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

/// Decode the NODE_FN_DECL encoded val.str of the form
///   "name\x1F<return_kind>\x1F<p1>:<k1>\x1F<p2>:<k2>..."
/// into a typed_fn_t record. Returns NULL on malformed encoding.
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
        /// No parameters.
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

/// Register (or replace) a typed function declaration. Mirrors the
/// POSIX function-table semantics: redeclaring the same name replaces
/// the prior record.
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
    /// Deep-copy the body so the registry survives the source AST
    /// being freed at the end of this batch (executor_execute_command_line
    /// frees `ast` after dispatch). copy_ast_chain produces a tree
    /// independent of the parser's allocations.
    if (node->first_child) {
        fn->body = copy_ast_chain(node->first_child);
        if (!fn->body) {
            typed_fn_free(fn);
            executor_error_report(executor, SHELL_ERR_FUNCTION_ERROR, node->loc,
                                  "failed to copy typed-function body");
            return 1;
        }
    }

    /// Capture the declaration-site scope as the lexical-closure parent.
    /// For a top-level `fn`, this is the global scope, which the symtable
    /// never pops -- the borrowed pointer is valid for the manager's
    /// lifetime. For an `fn` declared inside another scope, the captured
    /// pointer is only valid until that enclosing scope pops; once
    /// re-decl of an inner fn from an already-popped enclosing scope
    /// becomes a real pattern, the registry will need a stronger
    /// lifetime guarantee. Today's typical case (top-level fn) is safe.
    fn->captured_scope = symtable_capture_scope_for_lexical(executor->symtable);

    /// Replace existing record under the same name, if any.
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

/// Evaluate one NODE_FN_CALL argument expression to a kind-tagged
/// value. Argument forms accepted today: scalar literals (the parser
/// stored the arg text in val.str of a NODE_COMMAND); `$var` references
/// resolved to the var's current value+kind; nested NODE_FN_CALL
/// returning a typed value.
///
/// The caller owns the returned view (scalar_value is strdup'd; array
/// is borrowed from the symtable's current store and remains valid
/// until the call site's scope walk concludes).
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
        /// Nested typed-fn call as an argument expression.
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

    /// NODE_VAR carries a variable reference -- either a bare $name
    /// / ${name} or a kind-sigil form @name / %name (when
    /// FEATURE_KIND_SIGILS is on; see docs/features/typed-functions.md
    /// for the call-site sigil discipline). Strip whichever prefix is
    /// present and look the underlying name up kind-aware. The
    /// returned view preserves the symtable's actual kind tag, so a
    /// list or map binding crosses the call boundary as-is rather
    /// than being flattened to a string. Issue #207: failing to
    /// strip @/% meant `elements(@arr)` looked up the literal name
    /// `@arr`, missed, and reported the arg as an empty scalar --
    /// triggering an E1133 against the declared list kind on the
    /// docs' own example.
    if (arg->type == NODE_VAR) {
        const char *var_name = text;
        char name_buf[256];
        if (var_name[0] == '$' || var_name[0] == '@' || var_name[0] == '%') {
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
        /// Unset variable -- treat as empty scalar, matching POSIX.
        out->kind = LUSH_VALUE_SCALAR;
        out->scalar_value = strdup("");
        return out->scalar_value ? 0 : 1;
    }

    /// Single-quoted strings carry their text literally; no expansion.
    if (arg->type == NODE_STRING_LITERAL) {
        out->kind = LUSH_VALUE_SCALAR;
        out->scalar_value = strdup(text);
        return out->scalar_value ? 0 : 1;
    }

    /// Bare words, double-quoted strings (NODE_STRING_EXPANDABLE), and
    /// arithmetic numerals all pass through the standard word-expansion
    /// path. This is where `"hello $name!"` style interpolation gets
    /// resolved correctly.
    char *expanded = expand_if_needed(executor, text);
    out->kind = LUSH_VALUE_SCALAR;
    out->scalar_value = expanded ? expanded : strdup("");
    return out->scalar_value ? 0 : 1;
}

/// Bind one parameter (name + declared kind) to an argument value in
/// the current (just-pushed) function scope. Raises a type-mismatch
/// error if the kinds disagree.
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
        /// Deep-copy: the function scope owns its parameter binding.
        /// The caller's array remains intact when this scope pops.
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

/// Execute a NODE_FN_CALL. On a non-void return, sets
/// executor->typed_fn_return_pending = true and stashes the value in
/// executor->typed_fn_return_value for the caller (either NODE_LET_FN
/// or a nested call) to consume. Returns 0 on success / non-void
/// return, non-zero on error.
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

    /// Count arguments and verify arity before pushing scope.
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

    /// Evaluate arguments BEFORE pushing scope so $var references
    /// resolve in the caller's scope (the typed-fn body should not see
    /// them as locals; they are values passed in).
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

    /// Push a SCOPE_LEXICAL frame parented at the captured declaration
    /// site rather than the dynamic caller, so free names in the body
    /// resolve through the closure environment. If the fn was declared
    /// at top level the captured scope is the global scope, which
    /// behaves identically to dynamic scoping for global variables --
    /// the distinction surfaces when a POSIX-form caller has its own
    /// locals; those are now invisible to the typed-fn body.
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

    /// Push a debug frame and mark it lexical so `debug stack` renders
    /// the discipline. The push only happens when the debug subsystem
    /// is enabled -- mirrors how execute_command guards its push.
    bool debug_frame_pushed = false;
    if (g_debug_context && g_debug_context->enabled) {
        debug_push_frame(g_debug_context, callee, NULL, (int)node->loc.line);
        debug_mark_current_frame_lexical(g_debug_context);
        debug_frame_pushed = true;
    }

    /// Execute body. Any NODE_FN_RETURN inside surfaces as
    /// SHELL_FN_RETURN_STATUS, which we consume here.
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

    /// Validate the captured return against the declared return kind.
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

/// Top-level dispatch for a NODE_FN_CALL appearing as a statement (its
/// return value is discarded). Used when the user writes `name(args)`
/// or `name args` at command position.
static int execute_typed_fn_call(executor_t *executor, node_t *node) {
    int rc = execute_typed_fn_call_node(executor, node);
    /// Discard any return value the call produced.
    lush_value_view_clear(&executor->typed_fn_return_value);
    executor->typed_fn_return_pending = false;
    return rc;
}

/// `let name = call(args)` capture form. Invokes the call, then binds
/// the LHS in the current scope (kind-aware).
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
        /// The captured return view borrows from the callee's scope.
        /// That scope has already popped by the time we get here, but
        /// executor->typed_fn_return_value held the borrow; the
        /// underlying array is still valid because the unwind moved
        /// ownership semantics through the view. Take a deep copy so
        /// the caller's binding is independent.
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

/// NODE_FN_RETURN handler. Evaluates the optional return expression to
/// a kind-tagged value, stashes it in executor->typed_fn_return_value,
/// and unwinds via SHELL_FN_RETURN_STATUS.
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
