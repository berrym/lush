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
#include "shell_mode.h"
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
#include "lush.h"
#include "node.h"
#include "parser.h"
#include "redirection.h"
#include "signals.h"
#include "strings.h"
#include "symtable.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glob.h>
#include <pwd.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
static int execute_function_call(executor_t *executor,
                                 const char *function_name, char **argv,
                                 int argc);
static bool is_function_defined(executor_t *executor,
                                const char *function_name);
static function_def_t *find_function(executor_t *executor,
                                     const char *function_name);
static int store_function(executor_t *executor, const char *function_name,
                          node_t *body, function_param_t *params,
                          int param_count);
static int validate_function_parameters(function_def_t *func, char **argv,
                                        int argc);
static node_t *copy_ast_node(node_t *node);
static node_t *copy_ast_chain(node_t *node);
static int execute_if(executor_t *executor, node_t *if_node);
static int execute_while(executor_t *executor, node_t *while_node);
static int execute_until(executor_t *executor, node_t *until_node);
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
static int execute_external_command(executor_t *executor, char **argv);
static int execute_external_command_with_redirection(executor_t *executor,
                                                     char **argv,
                                                     bool redirect_stderr);
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
static int execute_external_command_with_setup(executor_t *executor,
                                               char **argv,
                                               bool redirect_stderr,
                                               node_t *command);
static int execute_builtin_command(executor_t *executor, char **argv);
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
static char **expand_brace_pattern(const char *pattern, int *expanded_count);
static bool needs_brace_expansion(const char *str);
static void initialize_job_control(executor_t *executor);
static char *expand_arithmetic(executor_t *executor, const char *arith_text);
static char *expand_command_substitution(executor_t *executor,
                                         const char *cmd_text);
static node_t *copy_node_simple(node_t *original);
static void copy_function_definitions(executor_t *dest, executor_t *src);
char *expand_if_needed(executor_t *executor, const char *text);
static char *expand_quoted_string(executor_t *executor, const char *str);
static char *expand_ansi_c_string(const char *str, size_t len);
static bool is_assignment(const char *text);
static int execute_assignment(executor_t *executor, const char *assignment);
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
 * @brief Check if environment variable modification is allowed in privileged mode
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
static void subshell_cleanup(void) {
    free_global_symtable();
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
    executor->current_script_line = 0;
    executor->in_script_execution = false;
    executor->expansion_error = false;
    executor->expansion_exit_status = 0;
    executor->loop_control = LOOP_NORMAL;
    executor->loop_depth = 0;
    executor->source_depth = 0;

    /* Initialize error context stack (Phase 3) */
    executor->context_depth = 0;
    for (size_t i = 0; i < EXECUTOR_CONTEXT_STACK_MAX; i++) {
        executor->context_stack[i] = NULL;
        executor->context_locations[i] = SOURCE_LOC_UNKNOWN;
    }

    /* Initialize process substitution fd tracking */
    executor->procsub_fd_count = 0;
    memset(executor->procsub_fds, -1, sizeof(executor->procsub_fds));
    memset(executor->procsub_pids, 0, sizeof(executor->procsub_pids));

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
    executor->current_script_line = 0;
    executor->in_script_execution = false;
    executor->expansion_error = false;
    executor->expansion_exit_status = 0;
    executor->loop_control = LOOP_NORMAL;
    executor->loop_depth = 0;
    executor->source_depth = 0;

    /* Initialize error context stack (Phase 3) */
    executor->context_depth = 0;
    for (size_t i = 0; i < EXECUTOR_CONTEXT_STACK_MAX; i++) {
        executor->context_stack[i] = NULL;
        executor->context_locations[i] = SOURCE_LOC_UNKNOWN;
    }

    /* Initialize process substitution fd tracking */
    executor->procsub_fd_count = 0;
    memset(executor->procsub_fds, -1, sizeof(executor->procsub_fds));
    memset(executor->procsub_pids, 0, sizeof(executor->procsub_pids));

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

        // Free script context
        free(executor->current_script_file);

        /* Free error context stack (Phase 3) */
        executor_clear_context(executor);

        free(executor);
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
 * @brief Set script execution context for debugging
 *
 * @param executor Executor context
 * @param script_file Script file path (NULL to clear)
 * @param line_number Current line number in script
 */
void executor_set_script_context(executor_t *executor, const char *script_file,
                                 int line_number) {
    if (!executor) {
        return;
    }

    // Free existing script file name
    free(executor->current_script_file);

    // Set new script context
    executor->current_script_file = script_file ? strdup(script_file) : NULL;
    executor->current_script_line = line_number;
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
    executor->current_script_line = 0;
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
 * @brief Get current script line number
 *
 * @param executor Executor context
 * @return Line number or 0 if not in script
 */
int executor_get_current_script_line(executor_t *executor) {
    return executor ? executor->current_script_line : 0;
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
 * ============================================================================ */

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

    /* Create the error */
    va_list args;
    va_start(args, fmt);
    shell_error_t *error = shell_error_createv(code, SHELL_SEVERITY_ERROR,
                                                loc, fmt, args);
    va_end(args);

    if (!error) {
        /* Fallback to legacy error system */
        set_executor_error(executor, "runtime error");
        return;
    }

    /* Add context stack to error */
    for (size_t i = 0; i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
        if (executor->context_stack[i]) {
            shell_error_push_context(error, "%s", executor->context_stack[i]);
        }
    }

    /* Display the error immediately */
    shell_error_display(error, stderr, isatty(STDERR_FILENO));

    /* Set legacy error state for compatibility - use NULL since error was already displayed */
    executor->has_error = true;
    executor->error_message = NULL;  /* Already displayed via structured system */

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

    /* Create the error */
    va_list args;
    va_start(args, fmt);
    shell_error_t *error = shell_error_createv(code, SHELL_SEVERITY_ERROR,
                                                loc, fmt, args);
    va_end(args);

    if (!error) {
        /* Fallback to legacy error system */
        set_executor_error(executor, "runtime error");
        return;
    }

    /* Add context stack to error */
    for (size_t i = 0; i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
        if (executor->context_stack[i]) {
            shell_error_push_context(error, "%s", executor->context_stack[i]);
        }
    }

    /* Display the error immediately */
    shell_error_display(error, stderr, isatty(STDERR_FILENO));

    /* Set legacy error state for compatibility - use NULL since error was already displayed */
    executor->has_error = true;
    executor->error_message = NULL;  /* Already displayed via structured system */

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

    /* Create the error */
    shell_error_t *error = shell_error_create(SHELL_ERR_COMMAND_NOT_FOUND,
                                               SHELL_SEVERITY_ERROR,
                                               loc,
                                               "%s: command not found",
                                               command);
    if (!error) {
        /* Fallback to simple error message */
        fprintf(stderr, "lush: %s: command not found\n", command);
        return;
    }

    /* Add context stack to error */
    for (size_t i = 0; i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
        if (executor->context_stack[i]) {
            shell_error_push_context(error, "%s", executor->context_stack[i]);
        }
    }

    /* Get suggestions from autocorrect (builtins + PATH with fast pre-filter) */
    correction_results_t results;
    int num_suggestions = autocorrect_find_suggestions(executor, command, &results);

    if (num_suggestions > 0) {
        /* Build suggestion string */
        char suggestion[256];
        if (num_suggestions == 1) {
            snprintf(suggestion, sizeof(suggestion),
                     "did you mean '%s'?", results.suggestions[0].command);
        } else {
            int show_count = num_suggestions > 3 ? 3 : num_suggestions;
            int pos = snprintf(suggestion, sizeof(suggestion), "did you mean ");
            for (int i = 0; i < show_count && pos < (int)sizeof(suggestion) - 20; i++) {
                if (i > 0) {
                    if (i == show_count - 1) {
                        pos += snprintf(suggestion + pos, sizeof(suggestion) - pos, ", or ");
                    } else {
                        pos += snprintf(suggestion + pos, sizeof(suggestion) - pos, ", ");
                    }
                }
                pos += snprintf(suggestion + pos, sizeof(suggestion) - pos,
                               "'%s'", results.suggestions[i].command);
            }
            snprintf(suggestion + pos, sizeof(suggestion) - pos, "?");
        }
        shell_error_set_suggestion(error, suggestion);
    }

    /* Always free autocorrect results (original_command is allocated even with no suggestions) */
    autocorrect_free_results(&results);

    /* Display the error */
    shell_error_display(error, stderr, isatty(STDERR_FILENO));

    /* Set legacy error state */
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
int executor_execute_command_line(executor_t *executor, const char *input) {
    if (!executor || !input) {
        return 1;
    }

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
                    i++; // Skip the newline too (loop will increment past backslash)
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
    parser_t *parser = parser_new_with_source(parse_input, source_name);
    if (!parser) {
        set_executor_error(executor, "Failed to create parser");
        free(processed_input);
        return 1;
    }

    node_t *ast = parser_parse(parser);

    // Check syntax check mode (set -n) - parse but don't execute
    if (shell_opts.syntax_check) {
        if (parser_has_error(parser)) {
            /* Display structured errors if available */
            parser_display_errors(parser, stderr, isatty(STDERR_FILENO));
            /* Set executor error for legacy compatibility (may be NULL with new system) */
            const char *legacy_err = parser_error(parser);
            if (legacy_err) {
                set_executor_error(executor, legacy_err);
            }
            parser_free(parser);
            free(processed_input);
            return 2; // Syntax error
        }
        parser_free(parser);
        free(processed_input);
        return 0; // Syntax check successful
    }

    if (parser_has_error(parser)) {
        /* Display structured errors if available */
        parser_display_errors(parser, stderr, isatty(STDERR_FILENO));
        /* Set executor error for legacy compatibility (may be NULL with new system) */
        const char *legacy_err = parser_error(parser);
        if (legacy_err) {
            set_executor_error(executor, legacy_err);
        }
        parser_free(parser);
        free(processed_input);
        return 1;
    }

    if (!ast) {
        parser_free(parser);
        free(processed_input);
        return 0; // Empty command
    }

    int result = executor_execute(executor, ast);

    free_node_tree(ast);
    parser_free(parser);
    free(processed_input);

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

    // Check for breakpoints using script context
    if (executor->in_script_execution && executor->current_script_file) {
        // Handle loop-aware line tracking
        if (g_debug_context && g_debug_context->execution_context.in_loop) {
            // If this is the first statement in a loop body, save the line
            // number
            if (g_debug_context->execution_context.loop_body_start_line == 0 &&
                node->type == NODE_COMMAND) {
                g_debug_context->execution_context.loop_body_start_line =
                    executor->current_script_line;
            }
        }

        DEBUG_BREAKPOINT_CHECK(executor->current_script_file,
                               executor->current_script_line);

        // Only increment line number for simple sequential commands, not
        // control structures
        if (node->type == NODE_COMMAND) {
            executor->current_script_line++;
        }
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

        last_result = execute_node(executor, current);

        // Check for loop control (break/continue) - stop executing list
        if (executor->loop_control != LOOP_NORMAL) {
            return last_result;
        }

        // Flush stdout to prevent pipeline from picking up residual output
        fflush(stdout);

        // Update exit status after each command in the sequence
        set_exit_status(last_result);

        if (executor->debug) {
            printf("DEBUG: Command result: %d\n", last_result);
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
static int execute_command(executor_t *executor, node_t *command) {
    if (!command || command->type != NODE_COMMAND) {
        return 1;
    }

    // Reset expansion error flags for this command
    executor->expansion_error = false;
    executor->expansion_exit_status = 0;

    // Check for assignment
    if (command->val.str && is_assignment(command->val.str)) {
        return execute_assignment(executor, command->val.str);
    }

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

    // Privileged mode security check
    if (argc > 0 && !is_privileged_command_allowed(argv[0])) {
        fprintf(stderr, "lush: %s: restricted command in privileged mode\n",
                argv[0]);
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
        result = execute_function_call(executor, filtered_argv[0],
                                       filtered_argv, filtered_argc);
    } else if (is_builtin_command(filtered_argv[0])) {
        // For builtin commands with stdout redirections, check if stdout is
        // captured. Only fork for "pure" builtins that don't modify shell state.
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

            result = execute_builtin_command(executor, filtered_argv);

            // Flush output streams after builtin execution
            // This ensures output appears immediately, especially under valgrind/piping
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

                /* Auto-cd to the directory */
                if (chdir(argv[0]) == 0) {
                    /* Successfully changed directory, update PWD */
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
                    result = 0; /* Success */
                } else {
                    /* Failed to change directory, show error */
                    shell_error_t *error = shell_error_create(
                        SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR,
                        SOURCE_LOC_UNKNOWN, "cd: %s: %s", argv[0], strerror(errno));
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
            // 3. interactive prompts are enabled (otherwise no point in searching)
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
                                result = execute_builtin_command(executor,
                                                                 filtered_argv);
                                fflush(stdout);
                                fflush(stderr);
                            } else if (is_function_defined(executor,
                                                           filtered_argv[0])) {
                                result = execute_function_call(
                                    executor, filtered_argv[0], filtered_argv,
                                    filtered_argc);
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

    // Free filtered argv if it was separately allocated (from redirect or alias expansion)
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
 * @brief Execute a pipeline of commands
 *
 * Implements a two-command pipeline with proper pipe setup.
 * Supports pipefail mode where failure in any command causes
 * pipeline failure.
 *
 * @param executor Executor context
 * @param pipeline Pipeline node containing commands
 * @return Exit status (last command's status, or first failure with pipefail)
 */
static int execute_pipeline(executor_t *executor, node_t *pipeline) {
    if (!pipeline || pipeline->type != NODE_PIPE) {
        return 1;
    }

    // Push error context for structured error reporting
    executor_push_context(executor, pipeline->loc, "in pipeline");

    // For now, implement simple two-command pipeline
    // A more complete implementation would handle N-command pipelines

    node_t *left = pipeline->first_child;
    node_t *right = left ? left->next_sibling : NULL;

    if (!left || !right) {
        executor_error_add(executor, SHELL_ERR_MALFORMED_CONSTRUCT,
                           pipeline->loc, "malformed pipeline");
        executor_pop_context(executor);
        return 1;
    }

    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        executor_error_add(executor, SHELL_ERR_PIPE_FAILED,
                           pipeline->loc, "failed to create pipe: %s", strerror(errno));
        executor_pop_context(executor);
        return 1;
    }

    pid_t left_pid = fork();
    if (left_pid == -1) {
        executor_error_add(executor, SHELL_ERR_FORK_FAILED,
                           pipeline->loc, "failed to fork for pipeline: %s", strerror(errno));
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        executor_pop_context(executor);
        return 1;
    }

    if (left_pid == 0) {
        // Left command: write to pipe
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        // Check if |& was used - also redirect stderr to the pipe
        if (pipeline->val_type == VAL_SINT && pipeline->val.sint == 1) {
            dup2(pipe_fd[1], STDERR_FILENO);
        }
        close(pipe_fd[1]);

        int result = execute_node(executor, left);
        fflush(stdout);
        fflush(stderr);
        subshell_cleanup();
        _exit(result);
    }

    pid_t right_pid = fork();
    if (right_pid == -1) {
        executor_error_add(executor, SHELL_ERR_FORK_FAILED,
                           pipeline->loc, "failed to fork for pipeline: %s", strerror(errno));
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        while (waitpid(left_pid, NULL, 0) == -1 && errno == EINTR)
            ;
        executor_pop_context(executor);
        return 1;
    }

    if (right_pid == 0) {
        // Right command: read from pipe
        close(pipe_fd[1]);
        dup2(pipe_fd[0], STDIN_FILENO);
        close(pipe_fd[0]);

        int result = execute_node(executor, right);
        fflush(stdout);
        fflush(stderr);
        subshell_cleanup();
        _exit(result);
    }

    // Parent: close pipes and wait
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    int left_status, right_status;
    // Wait for children, retrying on EINTR (signal interruption)
    while (waitpid(left_pid, &left_status, 0) == -1 && errno == EINTR)
        ;
    while (waitpid(right_pid, &right_status, 0) == -1 && errno == EINTR)
        ;

    // Extract exit codes - handle signal termination
    int left_exit =
        WIFEXITED(left_status)
            ? WEXITSTATUS(left_status)
            : (WIFSIGNALED(left_status) ? 128 + WTERMSIG(left_status) : 1);
    int right_exit =
        WIFEXITED(right_status)
            ? WEXITSTATUS(right_status)
            : (WIFSIGNALED(right_status) ? 128 + WTERMSIG(right_status) : 1);

    // Pop error context before returning
    executor_pop_context(executor);

    // Pipefail behavior: return failure if ANY command in pipeline fails
    if (is_pipefail_enabled()) {
        if (left_exit != 0) {
            return left_exit;
        }
        if (right_exit != 0) {
            return right_exit;
        }
        return 0;
    }

    // Standard behavior: return exit status of right (last) command
    return right_exit;
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
                           if_node->loc, "malformed if statement - missing then body");
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
        if (!next) break;

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

/**
 * @brief Execute a while loop
 *
 * Executes body while condition returns success (0).
 * Supports break/continue and has a safety limit of 10000 iterations.
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
        // If what we think is body is a redirection, we have malformed structure
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
    int iteration = 0;
    const int max_iterations = 10000; // Safety limit

    /* Push loop context for error reporting (Phase 3) */
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

    while (iteration < max_iterations) {
        // Execute condition
        int condition_result = execute_node(executor, condition);

        if (executor->debug) {
            printf("DEBUG: WHILE iteration %d, condition result: %d\n",
                   iteration, condition_result);
        }

        // If condition fails, exit loop
        if (condition_result != 0) {
            break;
        }

        // Execute body
        last_result = execute_command_chain(executor, body);

        // Check for break/continue
        if (executor->loop_control == LOOP_BREAK) {
            executor->loop_control = LOOP_NORMAL;
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            executor->loop_control = LOOP_NORMAL;
            // Continue to next iteration (just reset and loop again)
        }

        iteration++;
    }

    // Decrement loop depth before returning
    executor->loop_depth--;

    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    /* Pop loop context */
    executor_pop_context(executor);

    if (iteration >= max_iterations) {
        executor_error_add(executor, SHELL_ERR_LOOP_LIMIT,
                           while_node->loc, "while loop exceeded maximum iterations (%d)", max_iterations);
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
    int iteration = 0;
    const int max_iterations = 10000; // Safety limit

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

    while (iteration < max_iterations) {
        // Execute condition
        int condition_result = execute_node(executor, condition);

        if (executor->debug) {
            printf("DEBUG: UNTIL iteration %d, condition result: %d\n",
                   iteration, condition_result);
        }

        // If condition succeeds (returns 0), exit loop
        // This is the key difference from while loop
        if (condition_result == 0) {
            break;
        }

        // Execute body
        last_result = execute_command_chain(executor, body);

        // Check for break/continue
        if (executor->loop_control == LOOP_BREAK) {
            executor->loop_control = LOOP_NORMAL;
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            executor->loop_control = LOOP_NORMAL;
            // Continue to next iteration
        }

        iteration++;
    }

    // Decrement loop depth before returning
    executor->loop_depth--;

    // Restore file descriptors if we set up redirections
    if (has_redirections) {
        restore_file_descriptors(&redir_state);
    }

    // Pop error context
    executor_pop_context(executor);

    if (iteration >= max_iterations) {
        executor_error_add(executor, SHELL_ERR_LOOP_LIMIT,
                           until_node->loc, "until loop exceeded maximum iterations (%d)", max_iterations);
        return 1;
    }

    return last_result;
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
        executor_error_add(executor, SHELL_ERR_SCOPE_ERROR,
                           for_node->loc, "failed to create loop scope");
        return 1;
    }

    // Notify debug system we're entering a loop
    if (g_debug_context && g_debug_context->enabled) {
        debug_enter_loop(g_debug_context, "for", var_name, NULL);
    }

    // Increment loop depth - enables break/continue builtins
    executor->loop_depth++;

    // Push error context for structured error reporting
    executor_push_context(executor, for_node->loc, "in for loop over '%s'", var_name);

    int last_result = 0;

    // Build expanded word list for iteration
    char **expanded_words = NULL;
    int word_count = 0;

    // Process each word in the word list, expanding and splitting
    if (word_list && word_list->first_child) {
        node_t *word = word_list->first_child;
        while (word) {
            if (word->val.str) {
                // Special handling for "$@" to preserve word boundaries
                if (strcmp(word->val.str, "\"$@\"") == 0 ||
                    strcmp(word->val.str, "$@") == 0) {
                    // Handle quoted "$@" - preserve word boundaries
                    // Check if we're in a function scope
                    if (symtable_in_function_scope(executor->symtable)) {
                        // In function scope - use local positional parameters
                        char *argc_str = symtable_get_var(executor->symtable, "#");
                        int func_argc = argc_str ? atoi(argc_str) : 0;
                        free(argc_str);
                        
                        for (int i = 1; i <= func_argc; i++) {
                            char param_name[16];
                            snprintf(param_name, sizeof(param_name), "%d", i);
                            char *param_value = symtable_get_var(executor->symtable, param_name);
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

                                expanded_words[word_count] = strdup(shell_argv[i]);
                                word_count++;
                            }
                        }
                    }
                } else {
                    // Normal expansion and splitting for other words
                    char *expanded = expand_if_needed(executor, word->val.str);
                    if (expanded) {
                        // Check for brace expansion first
                        if (needs_brace_expansion(expanded)) {
                            int brace_count;
                            char **brace_results = expand_brace_pattern(expanded, &brace_count);
                            if (brace_results) {
                                // Add each brace expansion result
                                for (int b = 0; b < brace_count; b++) {
                                    expanded_words = realloc(expanded_words,
                                                             (word_count + 1) * sizeof(char *));
                                    if (!expanded_words) {
                                        set_executor_error(executor,
                                            "Memory allocation failed in for loop");
                                        for (int k = 0; k < brace_count; k++) {
                                            free(brace_results[k]);
                                        }
                                        free(brace_results);
                                        free(expanded);
                                        symtable_pop_scope(executor->symtable);
                                        return 1;
                                    }
                                    expanded_words[word_count] = brace_results[b];
                                    word_count++;
                                }
                                free(brace_results); // Free array, not strings (moved to expanded_words)
                                free(expanded);
                            } else {
                                // Brace expansion failed, use original
                                expanded_words = realloc(expanded_words,
                                                         (word_count + 1) * sizeof(char *));
                                if (expanded_words) {
                                    expanded_words[word_count] = expanded;
                                    word_count++;
                                } else {
                                    free(expanded);
                                }
                            }
                        } else {
                            // No brace expansion needed - check if IFS splitting is enabled
                            if (shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT)) {
                                // IFS splitting enabled - split the expanded string
                                const char *ifs =
                                    symtable_get(executor->symtable, "IFS");
                                if (!ifs) {
                                    ifs = " \t\n"; // Default IFS
                                }

                                // Split the expanded string into individual words
                                char *expanded_copy = strdup(expanded);
                                char *token = strtok(expanded_copy, ifs);

                                while (token) {
                                    // Resize array if needed
                                    expanded_words =
                                        realloc(expanded_words,
                                                (word_count + 1) * sizeof(char *));
                                    if (!expanded_words) {
                                        set_executor_error(
                                            executor,
                                            "Memory allocation failed in for loop");
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
                                // Word splitting disabled (zsh-style) - keep as single word
                                expanded_words = realloc(expanded_words,
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
            word = word->next_sibling;
        }
    }

    // Iterate over expanded words
    for (int i = 0; i < word_count; i++) {
        if (expanded_words[i]) {
            // Set loop variable in global scope (POSIX compliance)
            if (symtable_set_global_var(executor->symtable, var_name,
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

                // Reset line number to loop body start for iterations after the
                // first
                if (i > 0 &&
                    g_debug_context->execution_context.loop_body_start_line >
                        0) {
                    executor->current_script_line =
                        g_debug_context->execution_context.loop_body_start_line;
                }
            }

            // Execute body
            last_result = execute_command_chain(executor, body);

            // Check for break/continue
            if (executor->loop_control == LOOP_BREAK) {
                executor->loop_control = LOOP_NORMAL;
                break;
            } else if (executor->loop_control == LOOP_CONTINUE) {
                executor->loop_control = LOOP_NORMAL;
                // Continue to next iteration
            }
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

    return last_result;
}

/**
 * @brief Execute C-style arithmetic for loop
 *
 * Executes a C-style for loop: for ((init; test; update)); do body; done
 * - Child 0: init expression (evaluated once at start)
 * - Child 1: test expression (evaluated before each iteration, loop continues if non-zero)
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
                           for_arith_node->loc, "C-style for loop missing body");
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
    if (symtable_push_scope(executor->symtable, SCOPE_LOOP, "for-arith-loop") != 0) {
        executor_error_add(executor, SHELL_ERR_SCOPE_ERROR,
                           for_arith_node->loc, "failed to create loop scope");
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

    // Execute init expression (once at the start)
    if (init_node && init_node->val.str && init_node->val.str[0] != '\0') {
        char *init_expanded = expand_if_needed(executor, init_node->val.str);
        if (init_expanded) {
            arithm_clear_error();
            char *result_str = arithm_expand_with_executor(executor, init_expanded);
            if (result_str) {
                free(result_str);
            }
            if (arithm_error_flag && executor->debug) {
                fprintf(stderr, "DEBUG: C-style for init failed: %s\n", init_expanded);
            }
            free(init_expanded);
        }
    }

    // Loop: test, execute body, update
    while (1) {
        // Evaluate test expression (if empty, treat as true - infinite loop)
        if (test_node && test_node->val.str && test_node->val.str[0] != '\0') {
            char *test_expanded = expand_if_needed(executor, test_node->val.str);
            if (test_expanded) {
                arithm_clear_error();
                char *result_str = arithm_expand_with_executor(executor, test_expanded);
                if (!result_str || arithm_error_flag) {
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

        // Check for break/continue
        if (executor->loop_control == LOOP_BREAK) {
            executor->loop_control = LOOP_NORMAL;
            break;
        } else if (executor->loop_control == LOOP_CONTINUE) {
            executor->loop_control = LOOP_NORMAL;
            // Fall through to update expression
        }

        // Execute update expression
        if (update_node && update_node->val.str && update_node->val.str[0] != '\0') {
            char *update_expanded = expand_if_needed(executor, update_node->val.str);
            if (update_expanded) {
                arithm_clear_error();
                char *result_str = arithm_expand_with_executor(executor, update_expanded);
                if (result_str) {
                    free(result_str);
                }
                if (arithm_error_flag && executor->debug) {
                    fprintf(stderr, "DEBUG: C-style for update failed: %s\n", update_expanded);
                }
                free(update_expanded);
            }
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
                           select_node->loc, "select loop missing variable name");
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
                    
                    if (is_quoted || !shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT)) {
                        // Quoted strings or no-word-split mode: keep as single item
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
                        const char *ifs = symtable_get(executor->symtable, "IFS");
                        if (!ifs) {
                            ifs = " \t\n";
                        }

                        char *expanded_copy = strdup(expanded);
                        char *token = strtok(expanded_copy, ifs);

                        while (token) {
                            menu_items = realloc(menu_items,
                                                 (item_count + 1) * sizeof(char *));
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

        // Set loop variable
        if (*input_buf != '\0' && *endptr == '\0' && selection >= 1 &&
            selection <= item_count) {
            // Valid selection
            symtable_set_global_var(executor->symtable, var_name,
                                    menu_items[selection - 1]);
        } else {
            // Invalid or empty input - set variable to empty
            symtable_set_global_var(executor->symtable, var_name, "");
        }

        // Execute body
        node_t *cmd = body;
        while (cmd) {
            last_result = execute_node(executor, cmd);

            // Check for break/continue
            if (executor->loop_control != LOOP_NORMAL) {
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

    bool posix_format = (time_node->val_type == VAL_SINT && time_node->val.sint == 1);
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

    double user_time = (end_usage.ru_utime.tv_sec - start_usage.ru_utime.tv_sec) +
                       (end_usage.ru_utime.tv_usec - start_usage.ru_utime.tv_usec) / 1000000.0;

    double sys_time = (end_usage.ru_stime.tv_sec - start_usage.ru_stime.tv_sec) +
                      (end_usage.ru_stime.tv_usec - start_usage.ru_stime.tv_usec) / 1000000.0;

    // Check for TIMEFORMAT variable (Bash extension)
    const char *timeformat = symtable_get(executor->symtable, "TIMEFORMAT");

    if (posix_format) {
        // POSIX format: real, user, sys in seconds
        fprintf(stderr, "real %.2f\nuser %.2f\nsys %.2f\n",
                real_time, user_time, sys_time);
    } else if (timeformat && *timeformat) {
        // Custom format (simplified - just show the times)
        fprintf(stderr, "\nreal\t%.3fs\nuser\t%.3fs\nsys\t%.3fs\n",
                real_time, user_time, sys_time);
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

    pid_t pid = fork();
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
        close(pipe_to_coproc[1]);  // Close write end
        dup2(pipe_to_coproc[0], STDIN_FILENO);
        close(pipe_to_coproc[0]);

        // Redirect stdout to pipe_from_coproc[1]
        close(pipe_from_coproc[0]);  // Close read end
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
    close(pipe_to_coproc[0]);    // Close read end of input pipe
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

    // Create a new scope for the anonymous function
    if (symtable_push_scope(executor->symtable, SCOPE_FUNCTION, "<anonymous>") != 0) {
        set_executor_error(executor, "Failed to create anonymous function scope");
        return 1;
    }

    // Set positional parameters ($# = 0, no arguments)
    symtable_set_local_var(executor->symtable, "#", "0");

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
        // Skip redirection nodes
        if (!is_redirection_node(child)) {
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
                    char *expanded_arg;

                    // Handle different node types appropriately
                    if (child->type == NODE_STRING_LITERAL) {
                        // Check for ANSI-C quoting $'...'
                        if (child->val.str[0] == '$' && child->val.str[1] == '\'' &&
                            shell_mode_allows(FEATURE_ANSI_QUOTING)) {
                            // Find closing quote and expand
                            size_t len = strlen(child->val.str);
                            if (len >= 3 && child->val.str[len - 1] == '\'') {
                                expanded_arg = expand_ansi_c_string(
                                    child->val.str + 2, len - 3);
                            } else {
                                expanded_arg = strdup(child->val.str);
                            }
                        } else {
                            // Regular single-quoted strings: no expansion at all
                            expanded_arg = strdup(child->val.str);
                        }
                    } else if (child->type == NODE_STRING_EXPANDABLE) {
                        // Double-quoted strings: expand variables but not globs
                        expanded_arg =
                            expand_quoted_string(executor, child->val.str);
                    } else if (child->type == NODE_ARITH_EXP) {
                        // Arithmetic expansion: $((expr))
                        expanded_arg =
                            expand_arithmetic(executor, child->val.str);
                    } else if (child->type == NODE_COMMAND_SUB) {
                        // Command substitution: $(cmd) or `cmd`
                        expanded_arg = expand_command_substitution(
                            executor, child->val.str);
                    } else if (child->type == NODE_PROC_SUB_IN ||
                               child->type == NODE_PROC_SUB_OUT) {
                        // Process substitution: <(cmd) or >(cmd)
                        expanded_arg = expand_process_substitution(
                            executor, child);
                        if (!expanded_arg) {
                            goto cleanup_and_fail;
                        }
                    } else {
                        // Regular variables and other expandable content
                        expanded_arg =
                            expand_if_needed(executor, child->val.str);
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
                        // - Command substitution $(cmd): always split (all shells do this)
                        // - Parameter expansion $var: only split if FEATURE_WORD_SPLIT_DEFAULT enabled
                        // - Quoted strings: never split
                        bool should_word_split = false;
                        if (child->type == NODE_COMMAND_SUB) {
                            // Command substitution always gets word split
                            should_word_split = true;
                        } else if (child->type == NODE_VAR &&
                                   shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT)) {
                            // Parameter expansion only splits if feature enabled
                            should_word_split = true;
                        }
                        // Quoted strings (NODE_STRING_LITERAL, NODE_STRING_EXPANDABLE) never split
                        
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
char *expand_if_needed(executor_t *executor, const char *text) {
    if (!executor || !text) {
        return NULL;
    }

    // Handle strings that contain single quotes - process them specially
    // Single-quoted content should not be expanded (POSIX requirement)
    // BUT: Don't enter this path for command substitution $(...) or `...`
    // which may contain quotes internally
    if (strchr(text, '\'') && 
        !(text[0] == '$' && text[1] == '(') &&
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
                    char *expanded = expand_ansi_c_string(&text[content_start], content_len);
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
                    strncpy(&result[result_pos], &text[content_start], content_len);
                    result_pos += content_len;
                    result[result_pos++] = '\'';
                }
                // i now points to closing quote (or end of string)
            } else if (text[i] == '\'') {
                // Regular single quote - copy content literally until closing quote
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
                    if (text[i] == '"' && (i == 0 || text[i-1] != '\\')) {
                        depth--;
                        if (depth == 0) break;
                    }
                    i++;
                }
                // Extract double-quoted content and expand it
                size_t dq_len = i - dq_start;
                char *dq_content = malloc(dq_len + 1);
                if (dq_content) {
                    strncpy(dq_content, &text[dq_start], dq_len);
                    dq_content[dq_len] = '\0';
                    char *expanded = expand_quoted_string(executor, dq_content);
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
                        if (text[brace_end] == '{') brace_depth++;
                        else if (text[brace_end] == '}') brace_depth--;
                        brace_end++;
                    }
                    i = brace_end - 1;
                } else if (i + 1 < len && text[i + 1] == '(') {
                    // $(cmd) or $((arith)) - find closing paren
                    size_t paren_end = i + 2;
                    int paren_depth = 1;
                    while (paren_end < len && paren_depth > 0) {
                        if (text[paren_end] == '(') paren_depth++;
                        else if (text[paren_end] == ')') paren_depth--;
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
                char *final_result =
                    expand_quoted_string(executor, tilde_expanded);
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
        // position 0, treat as quoted string with multiple expansions
        if (dollar_count > 1 || first_dollar != text) {
            return expand_quoted_string(executor, text);
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
                    char *rest = expand_if_needed(executor, text + quote_end + 1);
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
            return expand_arithmetic(executor, text);
        } else if (strncmp(text, "$(", 2) == 0) {
            return expand_command_substitution(executor, text);
        } else if (strncmp(text, "${", 2) == 0) {
            // ${var} format - check if there's more text after }
            const char *close_brace = strchr(text, '}');
            if (close_brace && close_brace[1] != '\0') {
                // Text continues after ${var}, use quoted string expansion
                return expand_quoted_string(executor, text);
            }
            return expand_variable(executor, text);
        } else {
            // $var format - check if there's more text after variable name
            const char *p = text + 1;  // Skip $
            // Find end of variable name
            if (*p == '?' || *p == '$' || *p == '#' || *p == '*' ||
                *p == '@' || *p == '!' || *p == '-' ||
                (*p >= '0' && *p <= '9')) {
                p++;  // Single character special variable
            } else {
                while (*p && (isalnum(*p) || *p == '_')) {
                    p++;
                }
            }
            // If there's more text after the variable, use quoted string expansion
            if (*p != '\0') {
                return expand_quoted_string(executor, text);
            }
            return expand_variable(executor, text);
        }
    }

    // Check for backtick command substitution
    if (text[0] == '`') {
        return expand_command_substitution(executor, text);
    }

    // Regular text - just duplicate
    return strdup(text);
}

/**
 * @brief Execute an external command
 *
 * Forks and executes an external command using execvp.
 * Handles command hashing for faster subsequent lookups.
 *
 * @param executor Executor context
 * @param argv NULL-terminated argument vector
 * @return Exit status of command (127 for not found, 126 for permission denied)
 */
MAYBE_UNUSED
static int execute_external_command(executor_t *executor, char **argv) {
    return execute_external_command_with_redirection(executor, argv, false);
}

/**
 * @brief Execute external command with optional stderr redirection
 *
 * Forks and executes an external command, optionally redirecting
 * stderr to /dev/null. Handles command hashing and proper signal handling.
 *
 * @param executor Executor context
 * @param argv NULL-terminated argument vector
 * @param redirect_stderr If true, redirect stderr to /dev/null
 * @return Exit status of command
 */
static int execute_external_command_with_redirection(executor_t *executor,
                                                     char **argv,
                                                     bool redirect_stderr) {
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
            report_command_not_found(executor, argv[0], SOURCE_LOC_UNKNOWN);
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

    pid_t pid = fork();
    if (pid == -1) {
        set_executor_error(executor, "Failed to fork");
        return 1;
    }

    if (pid == 0) {
        // Child process
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
            fprintf(stderr, "lush: %s: %s\n", argv[0], strerror(errno));
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

        // Enhanced debug tracing for external commands
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

        last_result = execute_node(executor, command);

        if (executor->debug) {
            printf("DEBUG: Brace group command result: %d\n", last_result);
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
    pid_t pid = fork();
    if (pid == -1) {
        executor_error_add(executor, SHELL_ERR_FORK_FAILED,
                           subshell->loc, "failed to fork for subshell: %s", strerror(errno));
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
 * Uses power-of-2 values for bitmask operations (combined qualifiers like *(.,@))
 */
typedef enum {
    GLOB_QUAL_NONE     = 0,   // No qualifier
    GLOB_QUAL_FILE     = 1,   // (.) - regular files only
    GLOB_QUAL_DIR      = 2,   // (/) - directories only
    GLOB_QUAL_LINK     = 4,   // (@) - symbolic links only
    GLOB_QUAL_EXEC     = 8,   // (*) - executable files
    GLOB_QUAL_READABLE = 16,  // (r) - readable files
    GLOB_QUAL_WRITABLE = 32,  // (w) - writable files
} glob_qualifier_t;

/**
 * @brief Parse and strip glob qualifier from pattern
 *
 * @param pattern Input pattern (may be modified)
 * @param base_pattern Output: pattern without qualifier (must be freed)
 * @return Glob qualifier type
 */
static glob_qualifier_t parse_glob_qualifier(const char *pattern, char **base_pattern) {
    if (!pattern || !base_pattern) {
        *base_pattern = pattern ? strdup(pattern) : NULL;
        return GLOB_QUAL_NONE;
    }

    size_t len = strlen(pattern);
    
    // Check for qualifier pattern: ends with (X) or (X,Y,...) where X,Y are qualifier chars
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
            if (i == min_idx) break;  // Prevent underflow on decrement
        }
        
        if (open_paren) {
            // Parse all qualifier characters between ( and )
            glob_qualifier_t qual = GLOB_QUAL_NONE;
            bool valid_qualifier = true;
            
            for (const char *p = open_paren + 1; p < pattern + len - 1; p++) {
                switch (*p) {
                case '.': qual |= GLOB_QUAL_FILE; break;
                case '/': qual |= GLOB_QUAL_DIR; break;
                case '@': qual |= GLOB_QUAL_LINK; break;
                case '*': qual |= GLOB_QUAL_EXEC; break;
                case 'r': qual |= GLOB_QUAL_READABLE; break;
                case 'w': qual |= GLOB_QUAL_WRITABLE; break;
                case ',': break;  // Separator, ignore
                default:
                    // Unknown character - not a valid glob qualifier
                    valid_qualifier = false;
                    break;
                }
                if (!valid_qualifier) break;
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
static bool matches_glob_qualifier(const char *path, glob_qualifier_t qualifier) {
    if (qualifier == GLOB_QUAL_NONE) {
        return true;
    }
    
    struct stat st;
    if (lstat(path, &st) != 0) {
        return false;
    }
    
    // With combined qualifiers (bitmask), file must match ANY of the type qualifiers
    // For example, *(.,@) matches files OR symlinks
    bool type_match = false;
    bool has_type_qualifier = false;
    
    // Check type qualifiers (file, dir, link, exec) - OR logic
    if (qualifier & GLOB_QUAL_FILE) {
        has_type_qualifier = true;
        if (S_ISREG(st.st_mode)) type_match = true;
    }
    if (qualifier & GLOB_QUAL_DIR) {
        has_type_qualifier = true;
        if (S_ISDIR(st.st_mode)) type_match = true;
    }
    if (qualifier & GLOB_QUAL_LINK) {
        has_type_qualifier = true;
        if (S_ISLNK(st.st_mode)) type_match = true;
    }
    if (qualifier & GLOB_QUAL_EXEC) {
        has_type_qualifier = true;
        if (S_ISREG(st.st_mode) && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
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
        if (access(path, R_OK) != 0) return false;
    }
    if (qualifier & GLOB_QUAL_WRITABLE) {
        if (access(path, W_OK) != 0) return false;
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
    
    // Check for (a|b) alternation - parentheses with | inside, NOT preceded by extglob op
    const char *p = pattern;
    while (*p) {
        if (*p == '(' && (p == pattern || !strchr("?*+@!", *(p - 1)))) {
            // Found ( not preceded by extglob operator - check for | inside
            const char *inner = p + 1;
            int depth = 1;
            while (*inner && depth > 0) {
                if (*inner == '(') depth++;
                else if (*inner == ')') depth--;
                else if (*inner == '|' && depth == 1) return true;
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
    if (!pattern) return NULL;
    
    // Allocate generous buffer
    size_t max_len = strlen(pattern) * 4 + 10;
    char *regex = malloc(max_len);
    if (!regex) return NULL;
    
    char *out = regex;
    *out++ = '^';  // Anchor at start
    
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
                    *out++ = '+';  // ## = one or more
                    p += 2;
                } else {
                    *out++ = '*';  // # = zero or more
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
                    *out++ = '+';  // ## = one or more
                    p += 2;
                } else {
                    *out++ = '*';  // # = zero or more
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
    
    *out++ = '$';  // Anchor at end
    *out = '\0';
    
    return regex;
}

/**
 * @brief Match filename against zsh extglob pattern
 */
static bool match_zsh_extglob(const char *filename, const char *pattern, bool is_negated) {
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
static char **expand_zsh_extglob_pattern(const char *pattern, int *expanded_count) {
    *expanded_count = 0;
    
    if (!pattern) {
        return NULL;
    }
    
    // Check for ^pattern negation
    bool is_negated = (pattern[0] == '^');
    const char *match_pattern = is_negated ? pattern + 1 : pattern;
    
    // Split pattern into directory and filename parts
    char *pattern_copy = strdup(match_pattern);
    if (!pattern_copy) return NULL;
    
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
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Skip hidden files unless pattern starts with .
        if (entry->d_name[0] == '.' && file_pattern[0] != '.') {
            continue;
        }
        
        if (match_zsh_extglob(entry->d_name, file_pattern, is_negated)) {
            // Grow array if needed
            if (result_count >= result_capacity) {
                size_t new_capacity = result_capacity == 0 ? 16 : result_capacity * 2;
                char **new_results = realloc(results, (new_capacity + 1) * sizeof(char *));
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
             *pattern == '@' || *pattern == '!') && *(pattern + 1) == '(') {
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
    if (!pattern) return NULL;
    
    *is_negated = false;
    
    // Allocate generous buffer (pattern can expand significantly)
    size_t max_len = strlen(pattern) * 4 + 10;
    char *regex = malloc(max_len);
    if (!regex) return NULL;
    
    char *out = regex;
    *out++ = '^';  // Anchor at start
    
    const char *p = pattern;
    while (*p) {
        // Check for extglob operators
        if ((*p == '?' || *p == '*' || *p == '+' || *p == '@' || *p == '!') 
            && *(p + 1) == '(') {
            char op = *p;
            p += 2;  // Skip operator and (
            
            // Find matching closing paren
            int depth = 1;
            const char *start = p;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                if (depth > 0) p++;
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
                case '?': *out++ = '?'; break;
                case '*': *out++ = '*'; break;
                case '+': *out++ = '+'; break;
                case '@': /* exactly one, no quantifier */ break;
                case '!': *is_negated = true; break;
            }
            
            p++;  // Skip closing paren
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
    
    *out++ = '$';  // Anchor at end
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
    if (!pattern_copy) return NULL;
    
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
                char **new_results = realloc(results, capacity * sizeof(char *));
                if (!new_results) {
                    for (int i = 0; i < count; i++) free(results[i]);
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
                for (int i = 0; i < count; i++) free(results[i]);
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
    if (!pattern) return false;
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
                                     char ***results, int *count, int *capacity) {
    DIR *dir = opendir(base_dir[0] ? base_dir : ".");
    if (!dir) {
        return 0; /* Not an error - directory might not be readable */
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Skip hidden files unless dotglob is enabled */
        if (entry->d_name[0] == '.' && !shell_mode_allows(FEATURE_DOT_GLOB)) {
            continue;
        }

        /* Build full path */
        char full_path[PATH_MAX];
        if (base_dir[0]) {
            snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
        }

        /* Check if this path matches the remaining pattern */
        if (remaining_pattern && remaining_pattern[0]) {
            /* Build candidate path with remaining pattern */
            char candidate[PATH_MAX];
            int written = snprintf(candidate, sizeof(candidate), "%s/%s", full_path, remaining_pattern);
            if (written < 0 || (size_t)written >= sizeof(candidate)) {
                continue;  /* Path too long, skip this entry */
            }

            /* Use glob to match the remaining pattern */
            glob_t globbuf;
            if (glob(candidate, GLOB_NOSORT, NULL, &globbuf) == 0) {
                for (size_t i = 0; i < globbuf.gl_pathc; i++) {
                    /* Resize array if needed */
                    if (*count >= *capacity) {
                        *capacity = *capacity ? *capacity * 2 : 32;
                        char **new_results = realloc(*results, *capacity * sizeof(char *));
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
            /* No remaining pattern - match the path itself */
            if (*count >= *capacity) {
                *capacity = *capacity ? *capacity * 2 : 32;
                char **new_results = realloc(*results, *capacity * sizeof(char *));
                if (!new_results) {
                    closedir(dir);
                    return -1;
                }
                *results = new_results;
            }
            (*results)[(*count)++] = strdup(full_path);
        }

        /* Recurse into directories */
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (expand_globstar_recursive(full_path, remaining_pattern,
                                          results, count, capacity) < 0) {
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
static char **expand_globstar_pattern(const char *pattern, int *expanded_count) {
    *expanded_count = 0;

    if (!pattern || !has_globstar_pattern(pattern)) {
        return NULL;
    }

    /* Find the ** in the pattern */
    const char *starstar = strstr(pattern, "**");
    if (!starstar) {
        return NULL;
    }

    /* Split into prefix (before **) and suffix (after **) */
    size_t prefix_len = starstar - pattern;
    char *prefix = malloc(prefix_len + 1);
    if (!prefix) return NULL;
    
    strncpy(prefix, pattern, prefix_len);
    prefix[prefix_len] = '\0';

    /* Remove trailing slash from prefix if present */
    if (prefix_len > 0 && prefix[prefix_len - 1] == '/') {
        prefix[prefix_len - 1] = '\0';
    }

    /* Get suffix (after **) */
    const char *suffix = starstar + 2;
    if (*suffix == '/') suffix++; /* Skip leading slash after ** */

    /* Initialize results */
    char **results = NULL;
    int count = 0;
    int capacity = 0;

    /* Start directory */
    const char *start_dir = prefix[0] ? prefix : ".";

    /* First, match the start directory itself with the suffix pattern */
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
                    char **new_results = realloc(results, capacity * sizeof(char *));
                    if (!new_results) {
                        globfree(&globbuf);
                        free(prefix);
                        for (int j = 0; j < count; j++) free(results[j]);
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

    /* Recursively expand through directories */
    if (expand_globstar_recursive(start_dir, suffix[0] ? suffix : NULL,
                                  &results, &count, &capacity) < 0) {
        free(prefix);
        for (int i = 0; i < count; i++) free(results[i]);
        free(results);
        return NULL;
    }

    free(prefix);

    if (count == 0) {
        free(results);
        return NULL;
    }

    /* Add NULL terminator */
    char **final = realloc(results, (count + 1) * sizeof(char *));
    if (final) {
        final[count] = NULL;
        results = final;
    }

    *expanded_count = count;
    return results;
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
        char **globstar_results = expand_globstar_pattern(pattern, expanded_count);
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
    const char *pattern_to_expand = (qualifier != GLOB_QUAL_NONE) ? base_pattern : pattern;
    
    // Try zsh-style extglob expansion first (X#, X##, (a|b), ^pattern)
    if (qualifier == GLOB_QUAL_NONE && has_zsh_extglob_pattern(pattern_to_expand)) {
        // Free base_pattern if it was allocated by parse_glob_qualifier
        free(base_pattern);
        char **zsh_results = expand_zsh_extglob_pattern(pattern_to_expand, expanded_count);
        if (zsh_results && *expanded_count > 0) {
            return zsh_results;
        }
        // Zsh extglob expansion failed or no matches - handle according to nullglob
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
        char **extglob_results = expand_extglob_pattern(pattern_to_expand, expanded_count);
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
    
    // If no glob qualifier was parsed yet, set up base_pattern now
    if (qualifier == GLOB_QUAL_NONE) {
        base_pattern = strdup(pattern);
    }
    
    if (!base_pattern) {
        *expanded_count = 0;
        return NULL;
    }

    glob_t globbuf;
    int glob_result = glob(base_pattern, GLOB_NOSORT, NULL, &globbuf);
    free(base_pattern);

    if (glob_result == GLOB_NOMATCH) {
        // No matches - check nullglob setting
        if (shell_mode_allows(FEATURE_NULL_GLOB)) {
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
            // No matches after filtering - check nullglob
            free(result);
            if (shell_mode_allows(FEATURE_NULL_GLOB)) {
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
        // Check for bash-style extglob patterns: ?(pat), *(pat), +(pat), @(pat), !(pat)
        if (shell_mode_allows(FEATURE_EXTENDED_GLOB)) {
            if ((*p == '?' || *p == '*' || *p == '+' || 
                 *p == '@' || *p == '!') && *(p + 1) == '(') {
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
                    if (*inner == '(') depth++;
                    else if (*inner == ')') depth--;
                    else if (*inner == '|' && depth == 1) return true;
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
    if (!start_str) return NULL;
    
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
        if (end_pad > pad_width) pad_width = end_pad;
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
        if (step < 0) step = -step; // Step is always positive, direction from start/end
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
    
    if (count <= 0 || count > 10000) {
        // Sanity check - don't expand absurdly large ranges
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
        
        for (int i = 0; i < count; i++) {
            if (needs_brace_expansion(result[i])) {
                int sub_count;
                char **sub_results = expand_brace_pattern(result[i], &sub_count);
                if (sub_results) {
                    // Add all sub-results to final
                    char **new_final = realloc(final_results, 
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
                    char **new_final = realloc(final_results, 
                                               (final_count + 1) * sizeof(char *));
                    if (new_final) {
                        final_results = new_final;
                        final_results[final_count++] = result[i];
                    }
                }
            } else {
                char **new_final = realloc(final_results, 
                                           (final_count + 1) * sizeof(char *));
                if (new_final) {
                    final_results = new_final;
                    final_results[final_count++] = result[i];
                }
            }
        }
        
        free(result);
        
        char **terminated = realloc(final_results, (final_count + 1) * sizeof(char *));
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
static char **expand_brace_pattern(const char *pattern, int *expanded_count) {
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

    const char *close = strchr(open + 1, '}');
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

    // Check if this is a range pattern (contains ..)
    if (strstr(content, "..")) {
        char **range_result = expand_brace_range(prefix, content, suffix, expanded_count);
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

    // Count comma-separated items
    int item_count = 1;
    for (const char *p = content; *p; p++) {
        if (*p == ',') {
            item_count++;
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
        // Find next comma or end of string
        while (*comma_pos && *comma_pos != ',') {
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
    
    // Recursively expand any remaining brace patterns in results
    // This handles Cartesian products like {1..2}{a..b}
    if (strchr(suffix, '{')) {
        char **final_results = NULL;
        int final_count = 0;
        
        for (int i = 0; i < item_count; i++) {
            if (needs_brace_expansion(result[i])) {
                int sub_count;
                char **sub_results = expand_brace_pattern(result[i], &sub_count);
                if (sub_results) {
                    // Add all sub-results to final
                    char **new_final = realloc(final_results, 
                                               (final_count + sub_count) * sizeof(char *));
                    if (new_final) {
                        final_results = new_final;
                        for (int j = 0; j < sub_count; j++) {
                            final_results[final_count++] = sub_results[j];
                        }
                        free(sub_results);  // Free array, not strings
                    } else {
                        // Memory error - cleanup and return what we have
                        for (int j = 0; j < sub_count; j++) {
                            free(sub_results[j]);
                        }
                        free(sub_results);
                    }
                    free(result[i]);  // Free original since we expanded it
                } else {
                    // Sub-expansion failed, keep original
                    char **new_final = realloc(final_results, 
                                               (final_count + 1) * sizeof(char *));
                    if (new_final) {
                        final_results = new_final;
                        final_results[final_count++] = result[i];
                    }
                }
            } else {
                // No more braces, keep as-is
                char **new_final = realloc(final_results, 
                                           (final_count + 1) * sizeof(char *));
                if (new_final) {
                    final_results = new_final;
                    final_results[final_count++] = result[i];
                }
            }
        }
        
        free(result);  // Free original array
        
        // Add NULL terminator
        char **terminated = realloc(final_results, (final_count + 1) * sizeof(char *));
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

    pid_t pid = fork();
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
            fprintf(stderr, "lush: %s: %s\n", argv[0], strerror(errno));
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

/* Forward declaration for test builtin */
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
static int execute_builtin_command(executor_t *executor, char **argv) {
    if (!argv || !argv[0]) {
        return 1;
    }

    // Set global executor for job control builtins
    current_executor = executor;

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

            int result = builtins[i].func(argc, argv);

            // Clear global executor
            current_executor = NULL;

            return result;
        }
    }

    // Clear global executor
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
    (void)executor; /* Reserved for executor-aware test evaluation */
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
static int execute_assignment(executor_t *executor, const char *assignment) {
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
        var_len--;  // Exclude the '+' from variable name
    }
    
    char *var_name = malloc(var_len + 1);
    if (!var_name) {
        return 1;
    }

    strncpy(var_name, assignment, var_len);
    var_name[var_len] = '\0';

    // Privileged mode security check for environment variable modifications
    if (!is_privileged_path_modification_allowed(var_name)) {
        fprintf(stderr,
                "lush: %s: cannot modify restricted variable in privileged "
                "mode\n",
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

    // Resolve nameref if the variable is a nameref (max depth 10)
    const char *target_name = var_name;
    char *resolved_to_free = NULL;  // Track if we need to free resolved name
    if (symtable_is_nameref(executor->symtable, var_name)) {
        const char *resolved = symtable_resolve_nameref(executor->symtable,
                                                        var_name, 10);
        if (resolved && resolved != var_name) {
            target_name = resolved;
            resolved_to_free = (char *)resolved;  // May need to free this
        }
    }

    // POSIX compliance: variable assignments are GLOBAL by default
    // Local variables are only created via explicit 'local' builtin
    int result;
    
    if (is_append) {
        // Check if target is an array - append as new element
        array_value_t *array = symtable_get_array(target_name);
        if (array) {
            // Append value as new array element
            symtable_array_append(array, value ? value : "");
            result = 0;
        } else {
            // String append - concatenate to existing value
            char *existing = symtable_get_var(executor->symtable, target_name);
            if (existing && existing[0]) {
                size_t existing_len = strlen(existing);
                size_t value_len = value ? strlen(value) : 0;
                char *combined = malloc(existing_len + value_len + 1);
                if (combined) {
                    strcpy(combined, existing);
                    if (value) {
                        strcat(combined, value);
                    }
                    result = symtable_set_global_var(executor->symtable, target_name, combined);
                    free(combined);
                } else {
                    result = -1;
                }
            } else {
                // No existing value, just set the new value
                result = symtable_set_global_var(executor->symtable, target_name,
                                                 value ? value : "");
            }
        }
    } else {
        result = symtable_set_global_var(executor->symtable, target_name,
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

    // Notify LLE prompt system when PS1, PS2, or PROMPT is set by user code
    if (result == 0 && (strcmp(var_name, "PS1") == 0 ||
                        strcmp(var_name, "PS2") == 0 ||
                        strcmp(var_name, "PROMPT") == 0)) {
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
        // Format: "<terminator_char><pattern>" where terminator_char is '0', '1', or '2'
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
            // Split patterns by | and test each one
            char *pattern_copy = strdup(patterns);
            if (!pattern_copy) {
                free(test_word);
                return 1;
            }

            char *pattern = strtok(pattern_copy, "|");
            while (pattern && !matched) {
                // Expand variables in pattern
                char *expanded_pattern = expand_if_needed(executor, pattern);
                if (expanded_pattern) {
                    if (match_pattern(test_word, expanded_pattern)) {
                        matched = true;
                    }
                    free(expanded_pattern);
                }
                pattern = strtok(NULL, "|");
            }

            free(pattern_copy);
        }

        if (matched) {
            // Execute commands for this case item
            node_t *commands = case_item->first_child;
            while (commands) {
                result = execute_node(executor, commands);
                if (result != 0) {
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
        executor_error_add(executor, SHELL_ERR_FUNCTION_ERROR,
                           node->loc, "function definition missing name");
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
                                 int argc) {
    if (!executor || !function_name) {
        return 1;
    }

    function_def_t *func = find_function(executor, function_name);
    if (!func) {
        set_executor_error(executor, "Function not found");
        return 1;
    }

    // Validate function parameters
    if (validate_function_parameters(func, argv, argc) != 0) {
        set_executor_error(executor, "Function parameter validation failed");
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

    /* Push function context for error reporting (Phase 3) */
    source_location_t func_loc = func->body ? func->body->loc : SOURCE_LOC_UNKNOWN;
    executor_push_context(executor, func_loc, "in function '%s'", function_name);

    // Execute function body (handle multiple commands)
    int result = 0;
    node_t *command = func->body;
    while (command) {
        result = execute_node(executor, command);

        // Check if this is a function return (special code 200-255)
        if (result >= 200 && result <= 255) {
            // Extract the actual return value from the special code
            int actual_return = result - 200;

            /* Pop function context before returning */
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

    /* Pop function context */
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
static int validate_function_parameters(function_def_t *func, char **argv,
                                        int argc) {
    (void)argv; /* Reserved for argument type validation */
    (void)argc; /* Reserved for arity checking */
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
            fprintf(stderr, "Error: Function '%s' requires parameter '%s'\n",
                    func->name, param->name);
            return 1;
        }
        // Optional parameter without argument - will use default
        param = param->next;
    }

    // Check for too many arguments
    if (arg_index < argc) {
        fprintf(stderr,
                "Error: Function '%s' called with %d arguments but only "
                "accepts %d\n",
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
static node_t *copy_ast_node(node_t *node) {
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
 * @brief Extract a substring with offset and length
 *
 * Supports negative offsets (from end of string).
 * Handles bounds checking and returns empty string for invalid ranges.
 *
 * @param str Source string
 * @param offset Starting position (negative for from-end)
 * @param length Number of characters (-1 for rest of string)
 * @return Extracted substring (caller must free)
 */
static char *extract_substring(const char *str, int offset, int length) {
    if (!str) {
        return strdup("");
    }

    int str_len = strlen(str);

    // Handle negative offset (from end)
    if (offset < 0) {
        offset = str_len + offset;
        if (offset < 0) {
            offset = 0;
        }
    }

    // Bounds check
    if (offset >= str_len) {
        return strdup("");
    }

    // Calculate actual length
    int remaining = str_len - offset;
    if (length < 0 || length > remaining) {
        length = remaining;
    }

    char *result = malloc(length + 1);
    if (!result) {
        return strdup("");
    }

    strncpy(result, str + offset, length);
    result[length] = '\0';

    return result;
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

    /* Allocate buffer for Unicode conversion (may need more space) */
    size_t buf_size = len * 4 + 1; /* UTF-8 worst case */
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_toupper_first(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        /* Fallback to simple copy on error */
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

    /* Allocate buffer for Unicode conversion (may need more space) */
    size_t buf_size = len * 4 + 1; /* UTF-8 worst case */
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_tolower_first(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        /* Fallback to simple copy on error */
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
static char *convert_case_all_upper(const char *str) {
    if (!str) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len == 0) {
        return strdup("");
    }

    /* Allocate buffer for Unicode conversion (may need more space) */
    size_t buf_size = len * 4 + 1; /* UTF-8 worst case */
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_toupper(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        /* Fallback to simple copy on error */
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

    /* Allocate buffer for Unicode conversion (may need more space) */
    size_t buf_size = len * 4 + 1; /* UTF-8 worst case */
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    size_t out_len = lle_utf8_tolower(str, len, result, buf_size);
    if (out_len == (size_t)-1) {
        /* Fallback to simple copy on error */
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

    /* Allocate buffer - capitalize shouldn't change length significantly */
    size_t buf_size = len * 4 + 1;  /* UTF-8 worst case */
    char *result = malloc(buf_size);
    if (!result) {
        return strdup("");
    }

    const char *src = str;
    char *dst = result;
    bool word_start = true;

    while (*src) {
        /* Get UTF-8 codepoint length */
        size_t cp_len = 1;
        unsigned char c = (unsigned char)*src;
        if (c >= 0xC0 && c < 0xE0) cp_len = 2;
        else if (c >= 0xE0 && c < 0xF0) cp_len = 3;
        else if (c >= 0xF0) cp_len = 4;

        /* Ensure we don't read past end */
        size_t remaining = strlen(src);
        if (cp_len > remaining) cp_len = remaining;

        if (isspace((unsigned char)*src)) {
            *dst++ = *src++;
            word_start = true;
        } else if (word_start) {
            /* Uppercase the first character of word */
            char temp[8] = {0};
            memcpy(temp, src, cp_len);
            char upper[16] = {0};
            size_t upper_len = lle_utf8_toupper(temp, cp_len, upper, sizeof(upper));
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
            /* Lowercase the rest */
            char temp[8] = {0};
            memcpy(temp, src, cp_len);
            char lower[16] = {0};
            size_t lower_len = lle_utf8_tolower(temp, cp_len, lower, sizeof(lower));
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

    size_t str_len = strlen(str);
    size_t pattern_len = strlen(pattern);
    size_t replacement_len = strlen(replacement);

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
        if (strchr(pattern, '*') || strchr(pattern, '?')) {
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
                            for (size_t longer = try_len + 1; longer <= str_len - i; longer++) {
                                char *longer_substr = malloc(longer + 1);
                                if (longer_substr) {
                                    strncpy(longer_substr, str + i, longer);
                                    longer_substr[longer] = '\0';
                                    if (fnmatch(pattern, longer_substr, 0) == 0) {
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
static char *transform_quote(const char *str) {
    if (!str) {
        return strdup("''");
    }

    // Use $'...' quoting for strings with special characters
    size_t len = strlen(str);
    bool needs_special = false;

    for (size_t i = 0; i < len; i++) {
        if (str[i] < 32 || str[i] == '\'' || str[i] == '\\') {
            needs_special = true;
            break;
        }
    }

    if (needs_special) {
        // Use $'...' format with escape sequences
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
        // Simple single quotes
        size_t result_size = len + 3;
        char *result = malloc(result_size);
        if (!result) {
            return strdup("''");
        }
        snprintf(result, result_size, "'%s'", str);
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
            case 'n': result[j++] = '\n'; i++; break;
            case 't': result[j++] = '\t'; i++; break;
            case 'r': result[j++] = '\r'; i++; break;
            case '\\': result[j++] = '\\'; i++; break;
            case '\'': result[j++] = '\''; i++; break;
            case '"': result[j++] = '"'; i++; break;
            case 'a': result[j++] = '\a'; i++; break;
            case 'b': result[j++] = '\b'; i++; break;
            case 'e': result[j++] = '\033'; i++; break;
            case 'f': result[j++] = '\f'; i++; break;
            case 'v': result[j++] = '\v'; i++; break;
            case 'x': // Hex escape \xHH
                if (i + 3 < len && isxdigit(str[i + 2]) && isxdigit(str[i + 3])) {
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
    size_t result_size = len * 4 + 256;  // Allow for expansion
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
                    if (dot) *dot = '\0';
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
                    if (*base == '\0') base = "/";
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
                    var_end++;  // Single character special variable
                } else {
                    // Regular variable names (alphanumeric + underscore)
                    while (var_end < len &&
                           (isalnum(str[var_end]) || str[var_end] == '_')) {
                        var_end++;
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
static char *parse_parameter_expansion(executor_t *executor,
                                       const char *expansion) {
    if (!expansion) {
        return strdup("");
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

            // Check for 'k' flag (return keys instead of values)
            // This must be handled before inner expansion
            bool want_keys = (strchr(flags, 'k') != NULL);
            
            char *inner_result = NULL;
            
            if (want_keys) {
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
                    if (array) {
                        // Get all keys from array (works for both indexed and associative)
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
                                    if (i > 0) strcat(inner_result, " ");
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
                const char *var_name = rest + 1;  // Skip the #
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
                // Normal expansion
                inner_result = parse_parameter_expansion(executor, rest);
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
                    if (result != inner_result) free(result);
                    result = new_result ? new_result : strdup("");
                    p++;
                    break;

                case 'L':
                    // Lowercase all
                    new_result = convert_case_all_lower(result);
                    if (result != inner_result) free(result);
                    result = new_result ? new_result : strdup("");
                    p++;
                    break;

                case 'C':
                    // Capitalize each word
                    new_result = convert_case_capitalize_words(result);
                    if (result != inner_result) free(result);
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
                                new_result[i] = (result[i] == '\n') ? ' ' : result[i];
                            }
                        }
                        if (result != inner_result) free(result);
                        result = new_result ? new_result : strdup("");
                    }
                    p++;
                    break;

                case 'j':
                    // Join with separator: j:X:
                    if (p[1] == ':') {
                        // Find closing :
                        const char *sep_start = p + 2;
                        const char *sep_end = strchr(sep_start, ':');
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
                                    if (result[i] == ' ') count++;
                                }
                                new_result = malloc(res_len + count * sep_len + 1);
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
                            if (result != inner_result) free(result);
                            result = new_result ? new_result : strdup("");
                            p = sep_end + 1;
                        } else {
                            p++;  // Malformed, skip
                        }
                    } else {
                        p++;  // No separator specified
                    }
                    break;

                case 's':
                    // Split on separator: s:X: - replace X with space
                    if (p[1] == ':') {
                        const char *sep_start = p + 2;
                        const char *sep_end = strchr(sep_start, ':');
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
                                if (sep) free(sep);
                            }
                            if (new_result) {
                                if (result != inner_result) free(result);
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
                                    for (size_t i = 0; i < word_count - 1; i++) {
                                        for (size_t j = i + 1; j < word_count; j++) {
                                            if (strcmp(words[i], words[j]) > 0) {
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
                                        for (size_t i = 0; i < word_count; i++) {
                                            if (i > 0) strcat(new_result, " ");
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
                                if (result != inner_result) free(result);
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
                                    for (size_t i = 0; i < word_count - 1; i++) {
                                        for (size_t j = i + 1; j < word_count; j++) {
                                            if (strcmp(words[i], words[j]) < 0) {
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
                                        for (size_t i = 0; i < word_count; i++) {
                                            if (i > 0) strcat(new_result, " ");
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
                                if (result != inner_result) free(result);
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

                default:
                    // Unknown flag, skip
                    p++;
                    break;
                }
            }

            free(flags);
            if (result == inner_result) {
                return result;  // No transformation applied
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
        if (name_len > 0 && (var_name[name_len - 1] == '*' ||
                             var_name[name_len - 1] == '@')) {
            // Extract prefix (without * or @)
            char *prefix = malloc(name_len);
            if (!prefix) {
                return strdup("");
            }
            strncpy(prefix, var_name, name_len - 1);
            prefix[name_len - 1] = '\0';

            // Get all variable names matching prefix from environment
            // For now, scan environment variables
            size_t prefix_len = strlen(prefix);
            size_t result_size = 256;
            char *result = malloc(result_size);
            if (!result) {
                free(prefix);
                return strdup("");
            }
            result[0] = '\0';
            size_t result_pos = 0;

            extern char **environ;
            for (char **env = environ; *env; env++) {
                if (strncmp(*env, prefix, prefix_len) == 0) {
                    char *eq = strchr(*env, '=');
                    if (eq) {
                        size_t var_len = eq - *env;
                        if (result_pos + var_len + 2 >= result_size) {
                            result_size *= 2;
                            char *new_result = realloc(result, result_size);
                            if (!new_result) {
                                free(result);
                                free(prefix);
                                return strdup("");
                            }
                            result = new_result;
                        }
                        if (result_pos > 0) {
                            result[result_pos++] = ' ';
                        }
                        strncpy(result + result_pos, *env, var_len);
                        result_pos += var_len;
                        result[result_pos] = '\0';
                    }
                }
            }

            free(prefix);
            return result;
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
                    size_t result_size = count * 12;  // Enough for integers
                    char *result = malloc(result_size);
                    if (result) {
                        result[0] = '\0';
                        for (size_t i = 0; i < count; i++) {
                            if (i > 0) strcat(result, " ");
                            strcat(result, keys[i]);
                            free(keys[i]);
                        }
                        free(keys);
                        return result;
                    }
                    for (size_t i = 0; i < count; i++) free(keys[i]);
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
                            if (idx_result && !arithm_error_flag) {
                                int idx = (int)strtoll(idx_result, NULL, 10);
                                free(idx_result);
                                
                                // Adjust for 1-indexed arrays (zsh mode)
                                if (!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED)) {
                                    if (idx <= 0) {
                                        snprintf(result_buf, sizeof(result_buf), "0");
                                    } else {
                                        idx--;  // Convert 1-indexed to 0-indexed
                                        const char *elem =
                                            symtable_array_get_index(array, idx);
                                        snprintf(result_buf, sizeof(result_buf), "%zu",
                                                 elem ? strlen(elem) : 0);
                                    }
                                } else {
                                    const char *elem =
                                        symtable_array_get_index(array, idx);
                                    snprintf(result_buf, sizeof(result_buf), "%zu",
                                             elem ? strlen(elem) : 0);
                                }
                            } else {
                                if (idx_result) free(idx_result);
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

        // Regular variable length: ${#var}
        char *value = symtable_get_var(executor->symtable, var_name);
        if (value) {
            int len = strlen(value);
            free(value);  // Free the strdup'd value after getting length
            char *result = malloc(16);
            if (result) {
                snprintf(result, 16, "%d", len);
            }
            return result ? result : strdup("0");
        }
        return strdup("0");
    }

    // Handle array element access: ${arr[n]}, ${arr[@]}, ${arr[*]}
    const char *bracket = strchr(expansion, '[');
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
                    const char *target = symtable_resolve_nameref(mgr, arr_name, 10);
                    if (target) {
                        resolved_arr_name = target;
                    }
                }
                
                array_value_t *array = symtable_get_array(resolved_arr_name);
                if (array) {
                    char *result = NULL;

                    if (strcmp(subscript, "@") == 0 ||
                        strcmp(subscript, "*") == 0) {
                        // ${arr[@]} or ${arr[*]} - all elements
                        result = symtable_array_expand(array, " ");
                    } else if (array->is_associative) {
                        // Associative array - use subscript as string key
                        char *expanded_subscript = expand_variable(executor, subscript);
                        const char *key = expanded_subscript ? expanded_subscript : subscript;
                        const char *elem = symtable_array_get_assoc(array, key);
                        result = strdup(elem ? elem : "");
                        if (expanded_subscript) free(expanded_subscript);
                    } else {
                        // Indexed array - ${arr[n]} - specific element
                        arithm_clear_error();
                        char *idx_result = arithm_expand(subscript);
                        if (idx_result && !arithm_error_flag) {
                            int idx = (int)strtoll(idx_result, NULL, 10);
                            free(idx_result);
                            
                            // Adjust for 1-indexed arrays (zsh mode)
                            if (!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED)) {
                                if (idx <= 0) {
                                    result = strdup("");
                                } else {
                                    idx--;  // Convert 1-indexed to 0-indexed
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
                            if (idx_result) free(idx_result);
                            result = strdup("");
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
                    // There's more after ] - this might be a parameter expansion
                    // on an unset array element. For now, treat as empty.
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
    const char *operators[] = {
        ":-",  // 0: use default if unset or empty
        ":+",  // 1: use alternative if set and non-empty
        "##",  // 2: remove longest prefix pattern
        "%%",  // 3: remove longest suffix pattern
        "^^",  // 4: uppercase all
        ",,",  // 5: lowercase all
        "#",   // 6: remove shortest prefix pattern
        "%",   // 7: remove shortest suffix pattern
        "^",   // 8: uppercase first
        ",",   // 9: lowercase first
        "-",   // 10: use default if unset
        "+",   // 11: use alternative if set
        ":=",  // 12: assign default if unset or empty
        "=",   // 13: assign default if unset
        ":",   // 14: substring
        "//",  // 15: replace all occurrences
        "/",   // 16: replace first occurrence
        "@",   // 17: transformations
        NULL
    };
    int op_type = -1;

    // Find the first valid operator - prioritize longer operators first
    for (int i = 0; operators[i]; i++) {
        const char *found = strstr(expansion, operators[i]);
        if (found) {
            // Skip single-character operators that are part of longer ones
            if (strlen(operators[i]) == 1) {
                // Check if this single char is part of a longer operator
                bool part_of_longer = false;

                // Check for :- and :+ before processing single :
                if (strcmp(operators[i], ":") == 0) {
                    if ((found > expansion &&
                         (found[-1] == '-' || found[-1] == '+')) ||
                        (found[1] == '-' || found[1] == '+' ||
                         found[1] == '=')) {
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

        // Get variable value
        char *var_value = symtable_get_var(executor->symtable, var_name);
        const char *default_value = op_pos + strlen(operators[op_type]);

        // Expand variables in default value
        char *expanded_default =
            expand_variables_in_string(executor, default_value);

        char *result = NULL;

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

        case 4: // ${var^^} - convert all characters to uppercase
            if (var_value) {
                result = convert_case_all_upper(var_value);
            } else {
                result = strdup("");
            }
            break;

        case 5: // ${var,,} - convert all characters to lowercase
            if (var_value) {
                result = convert_case_all_lower(var_value);
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

        case 8: // ${var^} - convert first character to uppercase
            if (var_value) {
                result = convert_case_first_upper(var_value);
            } else {
                result = strdup("");
            }
            break;

        case 9: // ${var,} - convert first character to lowercase
            if (var_value) {
                result = convert_case_first_lower(var_value);
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
            if (var_value) {
                // expanded_default contains "pattern/replacement"
                // Find the separator between pattern and replacement
                char *sep = strchr(expanded_default, '/');
                if (sep) {
                    size_t pattern_len = sep - expanded_default;
                    char *pattern = malloc(pattern_len + 1);
                    if (pattern) {
                        strncpy(pattern, expanded_default, pattern_len);
                        pattern[pattern_len] = '\0';
                        const char *replacement = sep + 1;
                        result = pattern_substitute(var_value, pattern, replacement, true);
                        free(pattern);
                    } else {
                        result = strdup(var_value);
                    }
                } else {
                    // No replacement, just remove pattern
                    result = pattern_substitute(var_value, expanded_default, "", true);
                }
            } else {
                result = strdup("");
            }
            break;

        case 16: // ${var/pattern/replacement} - replace first occurrence
            if (var_value) {
                // expanded_default contains "pattern/replacement"
                char *sep = strchr(expanded_default, '/');
                if (sep) {
                    size_t pattern_len = sep - expanded_default;
                    char *pattern = malloc(pattern_len + 1);
                    if (pattern) {
                        strncpy(pattern, expanded_default, pattern_len);
                        pattern[pattern_len] = '\0';
                        const char *replacement = sep + 1;
                        result = pattern_substitute(var_value, pattern, replacement, false);
                        free(pattern);
                    } else {
                        result = strdup(var_value);
                    }
                } else {
                    // No replacement, just remove pattern
                    result = pattern_substitute(var_value, expanded_default, "", false);
                }
            } else {
                result = strdup("");
            }
            break;

        case 17: // ${var@op} - transformations
            if (var_value && expanded_default[0]) {
                char op = expanded_default[0];
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
                case 'a': // Attribute flags
                    result = get_variable_attributes(var_name);
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
            if (is_interactive_shell()) flags[pos++] = 'i';
            if (shell_opts.job_control) flags[pos++] = 'm';
            if (shell_opts.exit_on_error) flags[pos++] = 'e';
            if (shell_opts.unset_error) flags[pos++] = 'u';
            if (shell_opts.trace_execution) flags[pos++] = 'x';
            if (shell_opts.verbose) flags[pos++] = 'v';
            if (shell_opts.noclobber) flags[pos++] = 'C';
            if (shell_opts.no_globbing) flags[pos++] = 'f';
            if (shell_opts.syntax_check) flags[pos++] = 'n';
            if (shell_opts.allexport) flags[pos++] = 'a';
            if (shell_opts.notify) flags[pos++] = 'b';
            if (shell_opts.physical_mode) flags[pos++] = 'P';
            if (shell_opts.privileged_mode) flags[pos++] = 'p';
            if (shell_opts.history_mode) flags[pos++] = 'H';
            if (shell_opts.histexpand_mode) flags[pos++] = 'B';
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
                        char *value = symtable_get_var(executor->symtable, param_name);
                        if (value && value[0] != '\0') {
                            return value;  // Already allocated by symtable_get_var
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
    // Note: symtable_get_var returns a strdup'd value, caller must free
    
    // In zsh mode, ${arr} without subscript expands to all elements
    // Check if this is an array first
    array_value_t *array = symtable_get_array(expansion);
    if (array) {
        // Array exists - expand all elements
        char *result = symtable_array_expand(array, " ");
        return result ? result : strdup("");
    }
    
    char *value = symtable_get_var(executor->symtable, expansion);

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
                                  SOURCE_LOC_UNKNOWN,
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

        char *close = strchr(var_name, '}');
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
            var_name[0] == '-' ||
            (var_name[0] >= '0' && var_name[0] <= '9')) {
            name_len = 1;
        } else {
            // Regular variable names (alphanumeric + underscore)
            while (var_name[name_len] &&
                   (isalnum(var_name[name_len]) || var_name[name_len] == '_')) {
                name_len++;
            }
        }

        if (name_len > 0) {
            char *name = malloc(name_len + 1);
            if (name) {
                strncpy(name, var_name, name_len);
                name[name_len] = '\0';

                // Resolve nameref if applicable (max depth 10 to prevent loops)
                const char *resolved_name = name;
                char *resolved_to_free = NULL;  // Track if we need to free
                if (symtable_is_nameref(executor->symtable, name)) {
                    const char *target = symtable_resolve_nameref(
                        executor->symtable, name, 10);
                    if (target && target != name) {
                        resolved_name = target;
                        resolved_to_free = (char *)target;
                    }
                }

                // Look up in modern symbol table using resolved name
                char *value = symtable_get_var(executor->symtable,
                                               resolved_name);
                
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
                        executor_error_report(executor, SHELL_ERR_UNBOUND_VARIABLE,
                                              SOURCE_LOC_UNKNOWN,
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
                        if (is_interactive_shell()) flags[fpos++] = 'i';
                        if (shell_opts.job_control) flags[fpos++] = 'm';
                        if (shell_opts.exit_on_error) flags[fpos++] = 'e';
                        if (shell_opts.unset_error) flags[fpos++] = 'u';
                        if (shell_opts.trace_execution) flags[fpos++] = 'x';
                        if (shell_opts.verbose) flags[fpos++] = 'v';
                        if (shell_opts.noclobber) flags[fpos++] = 'C';
                        if (shell_opts.no_globbing) flags[fpos++] = 'f';
                        if (shell_opts.syntax_check) flags[fpos++] = 'n';
                        if (shell_opts.allexport) flags[fpos++] = 'a';
                        if (shell_opts.notify) flags[fpos++] = 'b';
                        if (shell_opts.physical_mode) flags[fpos++] = 'P';
                        if (shell_opts.privileged_mode) flags[fpos++] = 'p';
                        if (shell_opts.history_mode) flags[fpos++] = 'H';
                        if (shell_opts.histexpand_mode) flags[fpos++] = 'B';
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

    // If arithm_expand returns NULL, there was an error (like division by zero)
    // Report structured error for arithmetic failure
    if (arithm_error_flag && arithm_error_message) {
        executor_error_report(executor, SHELL_ERR_ARITHMETIC_SYNTAX,
                              SOURCE_LOC_UNKNOWN,
                              "arithmetic: %s", arithm_error_message);
    } else {
        executor_error_report(executor, SHELL_ERR_ARITHMETIC_SYNTAX,
                              SOURCE_LOC_UNKNOWN,
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

    // Expand variables in the command before executing it
    char *expanded_command = expand_variables_in_string(executor, command);
    free(command);
    if (!expanded_command) {
        return strdup("");
    }
    command = expanded_command;

    // Create a pipe to capture command output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        free(command);
        return strdup("");
    }

    pid_t pid = fork();
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
        parser_t *parser = parser_new_with_source(command, src_name);
        int result = 127;

        if (parser) {
            node_t *ast = parser_parse(parser);
            if (!parser_has_error(parser) && ast) {
                // Execute in current context (functions are inherited via fork)
                // Use executor_execute to handle command sequences (next_sibling)
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

        // Check for return value marker in output
        const char *return_marker = "__LUSH_RETURN__:";
        const char *end_marker = ":__END__";
        char *marker_pos = strstr(output, return_marker);

        if (marker_pos) {
            // Found return value marker, extract the return value
            char *value_start = marker_pos + strlen(return_marker);
            char *value_end = strstr(value_start, end_marker);

            if (value_end) {
                // Extract the return value
                size_t value_len = value_end - value_start;
                char *return_value = malloc(value_len + 1);
                if (return_value) {
                    strncpy(return_value, value_start, value_len);
                    return_value[value_len] = '\0';

                    // Clean up the original output
                    free(output);
                    return return_value;
                }
            }
        }

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
                        uint32_t codepoint =
                            (uint32_t)strtoul(hex, NULL, 16);
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
                        uint32_t codepoint =
                            (uint32_t)strtoul(hex, NULL, 16);
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
static char *expand_quoted_string(executor_t *executor, const char *str) {
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
        if (str[i] == '$' && i + 1 < len) {
            // NOTE: ANSI-C quoting $'...' is NOT expanded inside double quotes
            // per POSIX/bash behavior. It's only recognized at the outer level.
            // So we skip the $' check here and treat it as a literal $.
            
            // Check for arithmetic expansion $((...))
            if (str[i + 1] == '(' && i + 2 < len && str[i + 2] == '(') {
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
            // Handle escape sequences
            char next_char = str[i + 1];
            char escape_char;

            switch (next_char) {
            case '\\':
                escape_char = '\\';
                break;
            case '"':
                escape_char = '"';
                break;
            case '$':
                escape_char = '$';
                break;
            case '`':
                escape_char = '`';
                break;
            default:
                // POSIX: only \, ", $, ` are valid escapes in double quotes
                // All other backslash sequences are literal
                escape_char = '\\';
                break;
            }

            if (next_char == '\\' || next_char == '"' || next_char == '$' ||
                next_char == '`') {
                // Valid escape sequence, skip both backslash and next char
                if (result_pos >= buffer_size - 1) {
                    buffer_size *= 2;
                    result = realloc(result, buffer_size);
                    if (!result) {
                        return strdup("");
                    }
                }
                result[result_pos++] = escape_char;
                i += 2; // Skip both backslash and next character
            } else {
                // Invalid escape sequence, include backslash and next char
                // literally
                if (result_pos >= buffer_size - 2) {
                    buffer_size *= 2;
                    result = realloc(result, buffer_size);
                    if (!result) {
                        return strdup("");
                    }
                }
                result[result_pos++] = '\\';       // Add backslash
                result[result_pos++] = str[i + 1]; // Add following character
                i += 2; // Skip both backslash and next character
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
        pid_t pid = fork();
        if (pid == -1) {
            fprintf(stderr, "Failed to fork for background process\n");
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

    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "Failed to fork for background job\n");
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
    (void)argv; /* Reserved for job filtering options */
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
        fprintf(stderr, "fg: %d: no such job\n", job_id);
        return 1;
    }

    if (job->state == JOB_DONE) {
        fprintf(stderr, "fg: %d: job has terminated\n", job_id);
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
        fprintf(stderr, "bg: %d: no such job\n", job_id);
        return 1;
    }

    if (job->state != JOB_STOPPED) {
        fprintf(stderr, "bg: %d: job already in background\n", job_id);
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
        if (child->type == NODE_REDIR_OUT ||          // >
            child->type == NODE_REDIR_APPEND ||       // >>
            child->type == NODE_REDIR_BOTH ||         // &>
            child->type == NODE_REDIR_BOTH_APPEND ||  // &>>
            child->type == NODE_REDIR_CLOBBER) {      // >|
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
    if (!name) return false;
    
    // Only these builtins are "pure" - they produce output but don't modify
    // shell state. All others must run in the parent process.
    static const char *pure_builtins[] = {
        "echo", "printf", "true", "false", "test", "[", "type", "which",
        "help", "pwd", "dirs", "times", "kill", "wait", "jobs", "fg", "bg",
        NULL
    };
    
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

    pid_t pid = fork();
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
        int result = execute_builtin_command(executor, argv);
        
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

    // Use the existing arithmetic evaluator with executor context
    // This handles simple variable expansion internally
    arithm_clear_error();
    char *result_str = arithm_expand_with_executor(executor, expr);

    if (!result_str || arithm_error_flag) {
        // Arithmetic error - could be syntax error or division by zero
        // Create structured error with help suggestion
        shell_error_t *error = shell_error_create(SHELL_ERR_ARITHMETIC_SYNTAX,
                                                   SHELL_SEVERITY_ERROR,
                                                   arith_node->loc,
                                                   "arithmetic syntax error in expression: %s", expr);
        if (error) {
            // Build the source line: (( expr ))
            char *source_line = NULL;
            size_t expr_len = strlen(expr);
            if (asprintf(&source_line, "(( %s ))", expr) > 0) {
                shell_error_set_source_line(error, source_line, 3, 3 + expr_len);
                free(source_line);
            }
            // Update location to highlight the expression (column is 1-indexed)
            error->location.column = 4;  // After "(( "
            error->location.length = expr_len;
            // Add context stack from executor
            for (size_t i = 0; i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX; i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s", executor->context_stack[i]);
                }
            }
            // Add specific context for arithmetic command
            shell_error_push_context(error, "evaluating arithmetic command (( %s ))", expr);
            // Add help suggestion
            shell_error_set_suggestion(error,
                "(( )) expects arithmetic expressions, not shell commands");
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
        }
        executor->has_error = true;
        if (result_str) {
            free(result_str);
        }
        free(expanded_expr);
        return 1;
    }

    // Convert result to long long to check if non-zero
    long long result = strtoll(result_str, NULL, 10);
    free(result_str);
    free(expanded_expr); // Safe to free NULL

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
                symtable_set_array_element("BASH_REMATCH", subscript, match_str);
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
            // Check for || first (lower precedence, so we want to split on it first)
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
    while (*expr && isspace(*expr)) expr++;
    char *end = expr + strlen(expr) - 1;
    while (end > expr && isspace(*end)) *end-- = '\0';
    
    if (*expr == '\0') {
        return false;
    }
    
    // Check if entire expression is wrapped in parentheses
    if (*expr == '(' && *(expr + strlen(expr) - 1) == ')') {
        // Check if they match (not just opening and closing from different groups)
        int depth = 0;
        bool matched = true;
        for (char *p = expr; *p; p++) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            if (depth == 0 && *(p+1) != '\0') {
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
        if (*p == '(') paren_depth++;
        else if (*p == ')') paren_depth--;
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
    while (*p && isspace(*p)) p++;
    
    // Check for negation
    bool negate = false;
    if (*p == '!') {
        negate = true;
        p++;
        while (*p && isspace(*p)) p++;
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
        while (*p && isspace(*p)) p++;
        
        // String tests
        if (strcmp(op, "-z") == 0) {
            result = (*p == '\0');
        } else if (strcmp(op, "-n") == 0) {
            result = (*p != '\0');
        } else {
            // File tests - get the path (rest of line, trimmed)
            char *path = strdup(p);
            if (path) {
                char *end = path + strlen(path) - 1;
                while (end > path && isspace(*end)) *end-- = '\0';
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
            if (*scan == '(') paren_depth++;
            else if (*scan == ')') paren_depth--;
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
                } else if (scan[0] == '<' && scan[1] != '<') {
                    op_start = scan;
                    strcpy(op_type, "<");
                    break;
                } else if (scan[0] == '>' && scan[1] != '>') {
                    op_start = scan;
                    strcpy(op_type, ">");
                    break;
                } else if (scan[0] == '-' && isalpha(scan[1])) {
                    if (strncmp(scan, "-eq", 3) == 0 && (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-eq");
                        break;
                    } else if (strncmp(scan, "-ne", 3) == 0 && (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-ne");
                        break;
                    } else if (strncmp(scan, "-lt", 3) == 0 && (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-lt");
                        break;
                    } else if (strncmp(scan, "-le", 3) == 0 && (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-le");
                        break;
                    } else if (strncmp(scan, "-gt", 3) == 0 && (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-gt");
                        break;
                    } else if (strncmp(scan, "-ge", 3) == 0 && (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-ge");
                        break;
                    } else if (strncmp(scan, "-nt", 3) == 0 && (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-nt");
                        break;
                    } else if (strncmp(scan, "-ot", 3) == 0 && (isspace(scan[3]) || scan[3] == '\0')) {
                        op_start = scan;
                        strcpy(op_type, "-ot");
                        break;
                    } else if (strncmp(scan, "-ef", 3) == 0 && (isspace(scan[3]) || scan[3] == '\0')) {
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
                while (end >= lhs && isspace(*end)) *end-- = '\0';
            }
            
            // Extract RHS
            char *rhs_start = op_start + strlen(op_type);
            while (*rhs_start && isspace(*rhs_start)) rhs_start++;
            char *rhs = strdup(rhs_start);
            if (rhs) {
                char *end = rhs + strlen(rhs) - 1;
                while (end >= rhs && isspace(*end)) *end-- = '\0';
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
                if (lhs && rhs && stat(lhs, &st1) == 0 && stat(rhs, &st2) == 0) {
                    result = (st1.st_mtime > st2.st_mtime);
                } else {
                    result = false;
                }
            } else if (strcmp(op_type, "-ot") == 0) {
                // File1 older than file2
                struct stat st1, st2;
                if (lhs && rhs && stat(lhs, &st1) == 0 && stat(rhs, &st2) == 0) {
                    result = (st1.st_mtime < st2.st_mtime);
                } else {
                    result = false;
                }
            } else if (strcmp(op_type, "-ef") == 0) {
                // File1 and file2 are same file (same device and inode)
                struct stat st1, st2;
                if (lhs && rhs && stat(lhs, &st1) == 0 && stat(rhs, &st2) == 0) {
                    result = (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);
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
static int execute_array_assignment(executor_t *executor,
                                    node_t *assign_node) {
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

                            // Expand value using full expansion (handles $'...' ANSI-C quoting)
                            char *expanded = expand_if_needed(executor, value);
                            const char *final_value =
                                expanded ? expanded : value;

                            if (is_associative) {
                                // Use string key directly for associative arrays
                                symtable_array_set_assoc(array, idx_str, final_value);
                            } else {
                                // Evaluate index as arithmetic for indexed arrays
                                arithm_clear_error();
                                char *idx_result = arithm_expand(idx_str);

                                if (idx_result && !arithm_error_flag) {
                                    long long idx_val = strtoll(idx_result, NULL, 10);
                                    if (idx_val >= 0) {
                                        index = (int)idx_val;
                                    }
                                    free(idx_result);
                                }

                                symtable_array_set_index(array, index, final_value);
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
                        char *expanded_key = expand_if_needed(executor, elem_str);
                        const char *key = expanded_key ? expanded_key : elem_str;
                        
                        // Get next element as value
                        node_t *value_elem = elem->next_sibling;
                        if (value_elem && value_elem->val.str) {
                            char *expanded_val = expand_if_needed(executor, value_elem->val.str);
                            const char *val = expanded_val ? expanded_val : value_elem->val.str;
                            
                            symtable_array_set_assoc(array, key, val);
                            
                            if (expanded_val) free(expanded_val);
                            elem = value_elem;  // Skip the value element
                        }
                        if (expanded_key) free(expanded_key);
                    } else {
                        // Indexed array - assign to next index
                        // Expand the element using full expansion (handles $'...' ANSI-C quoting)
                        char *expanded = expand_if_needed(executor, elem_str);
                        const char *final_value = expanded ? expanded : elem_str;

                        // Only word split for unquoted elements (NODE_VAR)
                        // Quoted strings (NODE_STRING_LITERAL, NODE_STRING_EXPANDABLE)
                        // should be stored as single elements even if they contain spaces
                        bool is_quoted = (elem->type == NODE_STRING_LITERAL ||
                                          elem->type == NODE_STRING_EXPANDABLE);

                        // Word split the expanded value if it contains spaces
                        // This handles ${(s:,:)var} and ${(f)var} producing multiple words
                        // but NOT quoted strings like "echo hello"
                        if (!is_quoted && strchr(final_value, ' ') != NULL) {
                            // Split on spaces and add each word as separate element
                            char *copy = strdup(final_value);
                            if (copy) {
                                char *saveptr;
                                char *word = strtok_r(copy, " ", &saveptr);
                                while (word) {
                                    // Skip empty words
                                    if (*word) {
                                        symtable_array_set_index(array, index, word);
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

        if (!idx_result || arithm_error_flag) {
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
        // When FEATURE_ARRAY_ZERO_INDEXED is false, user index 1 maps to internal 0
        if (!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED)) {
            if (idx <= 0) {
                if (expanded_value)
                    free(expanded_value);
                set_executor_error(executor, "Array index must be positive in zsh mode");
                return 1;
            }
            idx--;  // Convert 1-indexed to 0-indexed internally
        }

        if (is_append) {
            // Append to existing element
            const char *existing =
                symtable_array_get_index(array, (int)idx);
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
 * @param proc_sub Process substitution node (NODE_PROC_SUB_IN or NODE_PROC_SUB_OUT)
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

    bool is_input = (proc_sub->type == NODE_PROC_SUB_IN);  // <(cmd)

    // Create a pipe for communication
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        set_executor_error(executor, "Failed to create pipe for process substitution");
        return NULL;
    }

    pid_t pid = fork();
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
            close(pipefd[0]);  // Close read end
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
            close(pipefd[1]);  // Close write end
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
    // This prevents fd leaks and zombie processes with nested process substitutions
    if (kept_fd >= 0 && executor->procsub_fd_count < 32) {
        executor->procsub_fds[executor->procsub_fd_count] = kept_fd;
        executor->procsub_pids[executor->procsub_fd_count] = pid;
        executor->procsub_fd_count++;
    }

    return path;
}

/* ============================================================================
 * HOOK FUNCTIONS (Phase 7: Zsh-Specific)
 * ============================================================================ */

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
        return 0;  // Hook not defined, that's fine
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

    // Call the function
    int result = execute_function_call(executor, hook_name, argv, argc);

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
bool executor_in_hook(void) {
    return g_in_hook_execution;
}
