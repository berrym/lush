/**
 * @file executor.h
 * @brief POSIX shell command execution engine
 *
 * Handles AST execution including commands, pipelines, control structures,
 * functions, and job control. Works with the tokenizer and parser.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "node.h"
#include "shell_error.h"
#include "symtable.h"

#include <stdbool.h>
#include <sys/types.h>

/** Maximum depth of error context stack */
#define EXECUTOR_CONTEXT_STACK_MAX 16

/// Function parameter definition
typedef struct function_param {
    char *name;                  ///< Parameter name
    char *default_value;         ///< Default value (NULL if required)
    bool is_required;            ///< True if parameter is required
    struct function_param *next; ///< Next parameter in list
} function_param_t;

/// Function definition storage
typedef struct function_def {
    char *name;                ///< Function name
    node_t *body;              ///< Function body AST
    function_param_t *params;  ///< Parameter list (NULL for no params)
    int param_count;           ///< Number of parameters
    struct function_def *next; ///< Next function in list
} function_def_t;

/// Job states
typedef enum { JOB_RUNNING, JOB_STOPPED, JOB_DONE } job_state_t;

/// Process in a job
typedef struct process {
    pid_t pid;
    char *command;
    int status;
    struct process *next;
} process_t;

/// Job control structure
typedef struct job {
    int job_id;
    pid_t pgid;
    job_state_t state;
    bool foreground;
    bool no_sighup; ///< If true, job won't receive SIGHUP on shell exit
    process_t *processes;
    char *command_line;
    struct job *next;
} job_t;

/// Loop control states
typedef enum {
    LOOP_NORMAL,  ///< Normal execution
    LOOP_BREAK,   ///< Break out of loop
    LOOP_CONTINUE ///< Continue to next iteration
} loop_control_t;

/// Execution context for maintaining state
typedef struct executor {
    bool interactive;             ///< Interactive mode flag
    bool debug;                   ///< Debug mode flag
    int exit_status;              ///< Last command exit status
    const char *error_message;    ///< Last error message
    bool has_error;               ///< Error flag
    symtable_manager_t *symtable; ///< Symbol table manager
    function_def_t *functions;    ///< Function definition table
    job_t *jobs;                  ///< Job control list
    int next_job_id;              ///< Next job ID to assign
    pid_t shell_pgid;             ///< Shell process group ID
    loop_control_t loop_control;  ///< Loop control state
    int loop_control_level;       ///< Levels remaining to unwind for `break N`
                                  ///< / `continue N`: 1 = innermost (default),
                                  ///< higher values are decremented at each
                                  ///< loop frame until they reach 1, at which
                                  ///< point the current frame consumes the
                                  ///< signal. Zero when no break/continue is
                                  ///< pending.
    int loop_depth;               ///< Current loop nesting depth

    /// Script execution context for breakpoint matching
    char *current_script_file; ///< Current script file being executed
    bool in_script_execution;  ///< True if executing from script file

    /// Sourced script tracking (Phase 6: return from sourced scripts)
    int source_depth;   ///< Depth of nested source commands (0 = not sourced)
    bool source_return; ///< True if return was called in sourced script

    /// Expansion error tracking
    bool expansion_error;      ///< True if error occurred during expansion
    int expansion_exit_status; ///< Exit status from expansion errors

    /* POSIX-required shell-level exit. A handful of error sites are
     * defined by IEEE 1003.1 to cause a non-interactive shell to exit
     * (e.g. ${var:?word} on null-or-unset, ${var?word} on unset, set -u
     * unbound-variable references). The trigger sites set
     * shell_exit_requested=true and stash the status here; every loop
     * body, command-list, and the top-level REPL honors the flag by
     * short-circuiting. Interactive shells never set the flag -- they
     * print the diagnostic and continue at the next prompt, per spec.
     * Set via executor_request_posix_exit(); never written directly. */
    bool shell_exit_requested;
    int shell_exit_status;

    /* Variable-assignment error abort. A failed assignment to a readonly
     * variable is not an ordinary command failure: per bash/zsh/dash it
     * must abort the current AND-OR list WITHOUT feeding `||`/`&&`, and
     * its diagnostic is a shell-level message reported on the real
     * stderr after the command's transient redirections are torn down
     * (so a `2>/dev/null` on the assignment does not swallow it). The
     * detector (execute_assignment) sets command_abort and stashes the
     * pending diagnostic; the execute_command chokepoint emits it after
     * redirect restore and applies the mode policy (posix: exit a
     * non-interactive shell; other modes: abort the list, continue the
     * script). command_abort is reset per command in
     * execute_command_inner; the AND-OR handlers consume it. */
    bool command_abort;
    char *pending_readonly_var;
    source_location_t pending_readonly_loc;

    /// Error context stack (Phase 3: context-aware error management)
    char *context_stack[EXECUTOR_CONTEXT_STACK_MAX]; ///< "while executing X"
    source_location_t context_locations[EXECUTOR_CONTEXT_STACK_MAX];
    size_t context_depth; ///< Current depth of context stack

    /* Location of the currently-executing simple command. Set at the
     * top of execute_command() from command->loc and restored on exit,
     * giving expansion-subsystem errors a precise source pointer even
     * when no enclosing construct has been pushed onto context_stack
     * (e.g. a bare top-level command from `lush -c '...'`). Read by
     * executor_current_loc(); writers must save+restore so nested
     * calls don't clobber it. SOURCE_LOC_UNKNOWN when no command is
     * currently executing. */
    source_location_t active_loc;

    /* Active completion-result accumulator. NULL outside a
     * completion-function call. Populated by the LLE completion-source
     * bridge before invoking a compdef-bound user function and cleared
     * after. The compadd builtin appends candidates here; it errors
     * when this field is NULL. Treated as opaque on the shell side
     * (real type is lle_completion_result_t, owned by liblle); the
     * shell-LLE bridge in completion_bridge.h provides typed access. */
    void *active_comp_result;

    /// Process substitution fd tracking (for cleanup after command execution)
    int procsub_fds[32];    ///< File descriptors from process substitutions
    pid_t procsub_pids[32]; ///< Child PIDs from process substitutions
    int procsub_fd_count;   ///< Number of tracked fds/pids

    /* Variable-allocated fd registry. Every `exec {var}<file`/`{var}>file`
     * / `{var}>&N` redirection that allocates a new fd (via
     * find_available_fd) appends its number here. `{var}>&-` removes its
     * entry on explicit close. executor_free() closes whatever remains —
     * a script that allocates fds without ever closing them does not leak
     * through shell-exit. Subshells fork()-inherit the array (process-
     * local from then on), so child cleanup never affects parent fds.
     * Dynamic-grown; alloc_fd_cap is the allocation, alloc_fd_count the
     * live entries. Touched only by helpers in redirection.c. */
    int *alloc_fds;
    size_t alloc_fd_count;
    size_t alloc_fd_cap;

    /* Source-text retention for the structured-error system. The
     * executor stashes the input text (and its file-relative starting
     * line) for the current parse/execute batch so any error site —
     * builtin, expansion, redirection, runtime — can attach the
     * original source line via shell_error_set_source_line() and
     * produce the full rust-style `--> file:line:col / N | ... / ^~~~`
     * snippet. Both fields are owned by the caller (executor_execute_
     * command_line stashes pointers; nothing is copied) and are reset
     * to NULL/0 on entry/exit of each batch. Read via the accessor
     * executor_get_source_line(executor, file_line). */
    const char *source_text;
    size_t source_starting_line;

    /**
     * Typed-function (`fn`) registry. Stored separately from
     * `functions` (POSIX form) because the two have different scoping
     * disciplines: typed-fn bodies use lexical (closure) scoping over
     * the captured declaration site, while POSIX-form functions use
     * dynamic scoping through the live call chain. Owned by the
     * executor; freed by executor_free.
     */
    struct typed_fn *typed_fns;

    /**
     * In-flight typed return state. NODE_FN_RETURN populates these
     * and yields a SHELL_FN_RETURN_STATUS unwind code; execute_typed_fn_call
     * consumes them at the call site, kind-checks against the declared
     * return kind, and clears the state. NODE_LET_FN binds the value
     * into the caller's scope. Treat this slot as live only during
     * the call-to-return unwind window.
     */
    bool typed_fn_return_pending;
    lush_value_view_t typed_fn_return_value;
} executor_t;

/** Global executor instance */
extern executor_t *current_executor;

/* ============================================================================
 * Executor Lifecycle
 * ============================================================================
 */

/**
 * @brief Create a new executor with global symbol table
 *
 * @return New executor instance or NULL on failure
 */
executor_t *executor_new(void);

/**
 * @brief Create a new executor with specified symbol table
 *
 * @param symtable Symbol table manager to use
 * @return New executor instance or NULL on failure
 */
executor_t *executor_new_with_symtable(symtable_manager_t *symtable);

/**
 * @brief Free an executor and all associated resources
 *
 * @param executor Executor to free
 */
void executor_free(executor_t *executor);

/**
 * @brief Track a variable-allocated file descriptor for shell-exit cleanup
 *
 * Append `fd` to the executor's variable-fd-alloc registry. Called from
 * setup_fd_alloc_redirection() each time `exec {var}<...` / `exec {var}>...`
 * / `exec {var}>&N` opens a new descriptor via find_available_fd(). The fd
 * will be close()d in executor_free() if not explicitly closed sooner via
 * `{var}>&-`. Idempotent on capacity exhaustion: returns without tracking
 * (the fd is still allocated and usable; just won't be auto-cleaned at
 * shell exit).
 *
 * @param executor Executor context
 * @param fd       File descriptor number to track
 */
void executor_track_alloc_fd(executor_t *executor, int fd);

/**
 * @brief Stop tracking a variable-allocated file descriptor
 *
 * Remove `fd` from the registry, if present. Called from
 * setup_fd_alloc_redirection() when `{var}>&-` explicitly closes a fd; the
 * registry should not double-close on shell exit. No-op if the fd was
 * never tracked (e.g. user closed an fd they didn't allocate via {var}<).
 *
 * @param executor Executor context
 * @param fd       File descriptor number to stop tracking
 */
void executor_untrack_alloc_fd(executor_t *executor, int fd);

/* ============================================================================
 * Primary Execution
 * ============================================================================
 */

/**
 * @brief Execute an AST node
 *
 * @param executor Executor context
 * @param ast Abstract syntax tree to execute
 * @return Exit status of executed command
 */
int executor_execute(executor_t *executor, node_t *ast);

/**
 * @brief Parse and execute a command line string
 *
 * The starting_line parameter seeds source-location tracking so errors
 * emitted during parsing or execution carry file-relative line numbers
 * even when `input` is a slice of a larger script.
 *
 * @param executor      Executor context
 * @param input         Command line to execute
 * @param starting_line Line number of the first character of `input`
 *                      within the original source file (1-based; pass 1
 *                      if input is the entire source or origin is
 *                      unknown)
 * @return Exit status of executed command
 */
int executor_execute_command_line(executor_t *executor, const char *input,
                                  size_t starting_line);

/**
 * @brief Fetch the text of a single source line for diagnostic display
 *
 * Returns a freshly-allocated copy of the requested line from the
 * executor's currently-stashed source text. Translates a file-relative
 * line number (as carried in source_location_t fields throughout the
 * AST) into a batch-relative offset using the executor's
 * source_starting_line, then walks the source text counting newlines
 * to extract the line content.
 *
 * Use site: any error emitter that calls shell_error_create() and
 * wants the full rust-style `N | source line / ^~~~` snippet block.
 * Pair with shell_error_set_source_line(error, line, col_start, col_end).
 *
 * Returns NULL if no source text is stashed, the requested line is
 * outside the batch's range, allocation fails, or the executor pointer
 * is NULL. Caller owns the returned string and must free() it.
 *
 * @param executor   Executor context (NULL returns NULL)
 * @param file_line  File-relative 1-based line number (typically
 *                   `loc.line` from a source_location_t)
 * @return Allocated source line text without trailing newline, or NULL
 */
char *executor_get_source_line(executor_t *executor, size_t file_line);

/* ============================================================================
 * Configuration
 * ============================================================================
 */

/**
 * @brief Enable or disable debug mode
 *
 * @param executor Executor context
 * @param debug True to enable debug output
 */
void executor_set_debug(executor_t *executor, bool debug);

/**
 * @brief Set interactive mode flag
 *
 * @param executor Executor context
 * @param interactive True for interactive shell mode
 */
void executor_set_interactive(executor_t *executor, bool interactive);

/**
 * @brief Set the symbol table manager
 *
 * @param executor Executor context
 * @param symtable Symbol table manager to use
 */
void executor_set_symtable(executor_t *executor, symtable_manager_t *symtable);

/* ============================================================================
 * Error Handling
 * ============================================================================
 */

/**
 * @brief Check if executor has an error
 *
 * @param executor Executor context
 * @return True if an error occurred
 */
bool executor_has_error(executor_t *executor);

/**
 * @brief Get the last error message
 *
 * @param executor Executor context
 * @return Error message string or NULL
 */
const char *executor_error(executor_t *executor);

/* ============================================================================
 * Error Context Stack (Phase 3)
 * ============================================================================
 */

/**
 * @brief Push a context frame onto the error context stack
 *
 * Used to build "while doing X, in Y" context chains for runtime errors.
 *
 * @param executor Executor context
 * @param loc Source location of this context
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void executor_push_context(executor_t *executor, source_location_t loc,
                           const char *fmt, ...);

/**
 * @brief Pop a context frame from the error context stack
 *
 * @param executor Executor context
 */
void executor_pop_context(executor_t *executor);

/**
 * @brief Clear all context frames
 *
 * @param executor Executor context
 */
void executor_clear_context(executor_t *executor);

/**
 * @brief Report a structured runtime error with context chain
 *
 * Creates and displays a structured error including the current
 * context stack for "while doing X" information.
 *
 * @param executor Executor context
 * @param code Error code
 * @param loc Source location of error
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void executor_error_report(executor_t *executor, shell_error_code_t code,
                           source_location_t loc, const char *fmt, ...);

/**
 * @brief Reject a mixed-script identifier when the hardening flag is set
 *
 * Author-time guard for the UAX #39 homograph vector: an identifier
 * drawing letters from more than one script (Latin `p` + Cyrillic `а`
 * in `pаsswd`). When FEATURE_REJECT_MIXED_SCRIPT_IDENTS is off (the
 * default in every mode) this is a no-op returning false. When on, a
 * mixed-script @p name reports a structured error at @p loc and returns
 * true so the caller aborts the definition. Call only at author
 * boundaries (assignment, declare/typeset, function, alias); never on
 * the environment-import path.
 *
 * @param executor Executor for error reporting and context stack
 * @param name Candidate identifier name
 * @param loc Source location for the diagnostic
 * @return true if rejected (error already reported); false to proceed
 */
bool executor_reject_mixed_script_ident(executor_t *executor, const char *name,
                                        source_location_t loc);

/* ============================================================================
 * Variable Expansion
 * ============================================================================
 */

/**
 * @brief Expand variables in text if needed
 *
 * @param executor Executor context
 * @param text Text potentially containing variables
 * @return Expanded string (caller must free) or NULL on error
 */
char *expand_if_needed(executor_t *executor, const char *text);

/**
 * @brief Expand a brace pattern into its enumerated branches
 *
 * Given a pattern such as `{a,b,c}/Doc` or `{1..3}-tail`, returns a
 * NULL-terminated heap-allocated array of branch strings. Range
 * expansion (`{1..10}`) and list expansion (`{a,b,c}`) are both
 * supported. When the input contains no brace pattern the returned
 * array has a single element equal to a copy of the input.
 *
 * Caller owns each returned string and the array; both must be freed
 * with free(). The terminating NULL slot is included in the
 * allocation.
 *
 * @param pattern Pattern text (no quote/escape processing performed)
 * @param expanded_count Output for the number of branches produced.
 *        Set to 0 on failure.
 * @return Heap-allocated array of branch strings or NULL on
 *         allocation failure. Single-element array (length-1) on
 *         input with no brace.
 */
char **expand_brace_pattern(const char *pattern, int *expanded_count);

/**
 * @brief Expand a process substitution node
 *
 * Handles <(cmd) and >(cmd) process substitution by forking a child
 * process to execute the command and returning a /dev/fd/N path that
 * can be used as a filename for redirection.
 *
 * @param executor Executor context
 * @param proc_sub Process substitution node (NODE_PROC_SUB_IN or
 * NODE_PROC_SUB_OUT)
 * @return Path string like "/dev/fd/N" (caller must free) or NULL on error
 */
char *expand_process_substitution(executor_t *executor, node_t *proc_sub);

/* ============================================================================
 * Job Control
 * ============================================================================
 */

/**
 * @brief Execute a command in the background
 *
 * @param executor Executor context
 * @param command Command node to execute
 * @return Exit status (0 on success)
 */
int executor_execute_background(executor_t *executor, node_t *command);

/**
 * @brief Add a job to the job list
 *
 * @param executor Executor context
 * @param pgid Process group ID of the job
 * @param command_line Original command line string
 * @return New job structure or NULL on failure
 */
job_t *executor_add_job(executor_t *executor, pid_t pgid,
                        const char *command_line);

/**
 * @brief Update status of all jobs
 *
 * @param executor Executor context
 */
void executor_update_job_status(executor_t *executor);

/**
 * @brief Find a job by ID
 *
 * @param executor Executor context
 * @param job_id Job ID to find
 * @return Job structure or NULL if not found
 */
job_t *executor_find_job(executor_t *executor, int job_id);

/**
 * @brief Remove a job from the job list
 *
 * @param executor Executor context
 * @param job_id Job ID to remove
 */
void executor_remove_job(executor_t *executor, int job_id);

/**
 * @brief Built-in jobs command implementation
 *
 * @param executor Executor context
 * @param argv Command arguments
 * @return Exit status
 */
int executor_builtin_jobs(executor_t *executor, char **argv);

/**
 * @brief Built-in fg command implementation
 *
 * @param executor Executor context
 * @param argv Command arguments
 * @return Exit status
 */
int executor_builtin_fg(executor_t *executor, char **argv);

/**
 * @brief Built-in bg command implementation
 *
 * @param executor Executor context
 * @param argv Command arguments
 * @return Exit status
 */
int executor_builtin_bg(executor_t *executor, char **argv);

/**
 * @brief Count number of jobs
 *
 * @param executor Executor context
 * @return Number of jobs in job list
 */
int executor_count_jobs(executor_t *executor);

/* ============================================================================
 * Function Parameters
 * ============================================================================
 */

/**
 * @brief Create a function parameter definition
 *
 * @param name Parameter name
 * @param default_value Default value or NULL if required
 * @return New parameter structure or NULL on failure
 */
function_param_t *create_function_param(const char *name,
                                        const char *default_value);

/**
 * @brief Free a function parameter list
 *
 * @param params Parameter list to free
 */
void free_function_params(function_param_t *params);

/* ============================================================================
 * Script Context (Debugging)
 * ============================================================================
 */

/**
 * @brief Set script execution context for breakpoint matching
 *
 * @param executor Executor context
 * @param script_file Script file path (NULL to clear)
 */
void executor_set_script_context(executor_t *executor, const char *script_file);

/**
 * @brief Clear script execution context
 *
 * @param executor Executor context
 */
void executor_clear_script_context(executor_t *executor);

/**
 * @brief Get current script file path
 *
 * @param executor Executor context
 * @return Script file path or NULL
 */
const char *executor_get_current_script_file(executor_t *executor);

/* ============================================================================
 * Security
 * ============================================================================
 */

/**
 * @brief Check if redirection target is allowed in privileged mode
 *
 * @param target Redirection target path
 * @return True if allowed, false otherwise
 */
bool is_privileged_redirection_allowed(const char *target);

/* ============================================================================
 * Hook Functions (Phase 7: Zsh-Specific)
 * ============================================================================
 */

/**
 * @brief Call a hook function if defined
 *
 * Executes a user-defined hook function (precmd, preexec, chpwd, periodic)
 * if it exists. Only active when FEATURE_HOOK_FUNCTIONS is enabled.
 *
 * @param executor Executor context
 * @param hook_name Name of the hook function (e.g., "precmd")
 * @param arg Optional argument to pass to the hook (e.g., command for preexec)
 * @return Exit status of hook function, or 0 if not defined
 */
int executor_call_hook(executor_t *executor, const char *hook_name,
                       const char *arg);

/**
 * @brief Call precmd hook (before prompt display)
 *
 * @param executor Executor context
 * @return Exit status of hook, or 0 if not defined
 */
int executor_call_precmd(executor_t *executor);

/**
 * @brief Call preexec hook (before command execution)
 *
 * @param executor Executor context
 * @param command The command about to be executed
 * @return Exit status of hook, or 0 if not defined
 */
int executor_call_preexec(executor_t *executor, const char *command);

/**
 * @brief Call chpwd hook (after directory change)
 *
 * @param executor Executor context
 * @return Exit status of hook, or 0 if not defined
 */
int executor_call_chpwd(executor_t *executor);

/**
 * @brief Check if currently executing inside a hook
 *
 * Used to prevent recursive hook calls (e.g., precmd running a command
 * that would trigger another precmd).
 *
 * @return true if currently in hook execution
 */
bool executor_in_hook(void);

/* ============================================================================
 * Structured Error Reporting (Phase 4)
 * ============================================================================
 */

/**
 * @brief Add a structured error to the executor's error state
 *
 * Creates and reports a structured error with full context information
 * including the execution context stack. Falls back to legacy error
 * system if structured errors are not available.
 *
 * @param executor Executor context
 * @param code Error code from shell_error_code_t
 * @param loc Source location where error occurred
 * @param fmt Printf-style format string for error message
 * @param ... Format arguments
 */
void executor_error_add(executor_t *executor, shell_error_code_t code,
                        source_location_t loc, const char *fmt, ...);

/**
 * @brief Display a structured runtime error
 *
 * Immediately displays an error with context stack. Used for runtime
 * errors that should be shown as they occur.
 *
 * @param executor Executor context
 * @param code Error code
 * @param loc Source location
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void executor_error_report(executor_t *executor, shell_error_code_t code,
                           source_location_t loc, const char *fmt, ...);

#endif /// EXECUTOR_H
