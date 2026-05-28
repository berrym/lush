/**
 * @file shell_error.h
 * @brief Unified error management system for lush shell
 *
 * Provides structured error reporting with source locations, context chains,
 * multi-error collection, and rich user feedback. Inspired by Rust's compiler
 * error messages.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef SHELL_ERROR_H
#define SHELL_ERROR_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ============================================================================
 * Source Location Tracking
 * ============================================================================
 */

/**
 * @brief Source location tracking structure
 *
 * Lightweight structure to track source positions through the pipeline.
 * Designed to be embedded in tokens, AST nodes, and error contexts.
 */
typedef struct source_location {
    const char *filename; ///< Script filename, "<stdin>", or "<cmdline>"
    size_t line;          ///< Line number (1-based, 0 = unknown)
    size_t column;        ///< Column number (1-based, 0 = unknown)
    size_t offset;        ///< Byte offset in source
    size_t length;        ///< Length of source span (for highlighting)
} source_location_t;

/**
 * @brief Invalid/unknown source location sentinel
 */
#define SOURCE_LOC_UNKNOWN                                                     \
    ((source_location_t){                                                      \
        .filename = NULL, .line = 0, .column = 0, .offset = 0, .length = 0})

/**
 * @brief Check if a source location is valid (has line info)
 */
#define SOURCE_LOC_VALID(loc) ((loc).line > 0)

/* ============================================================================
 * Error Codes
 * ============================================================================
 */

/**
 * @brief Unified shell error codes
 *
 * Hierarchical error code system covering all error categories:
 * - Parse errors (1000-1099)
 * - Runtime errors (1100-1199)
 * - Builtin errors (1200-1299)
 * - Expansion errors (1300-1399)
 * - System errors (1400-1499)
 * - Internal errors (1500-1599)
 */
typedef enum shell_error_code {
    SHELL_OK = 0,

    /// Parse errors (1000-1099)
    SHELL_ERR_PARSE_BASE = 1000,
    SHELL_ERR_UNEXPECTED_TOKEN,  ///< Got 'X' but expected 'Y'
    SHELL_ERR_UNEXPECTED_EOF,    ///< Unexpected end of input
    SHELL_ERR_UNCLOSED_QUOTE,    ///< Unterminated string
    SHELL_ERR_UNCLOSED_SUBST,    ///< Unterminated $() or ${}
    SHELL_ERR_UNCLOSED_CONTROL,  ///< Missing fi, done, esac, etc.
    SHELL_ERR_INVALID_REDIRECT,  ///< Invalid redirection syntax
    SHELL_ERR_INVALID_FUNCTION,  ///< Invalid function definition
    SHELL_ERR_INVALID_ARRAY,     ///< Invalid array syntax
    SHELL_ERR_RESERVED_WORD,     ///< Reserved word in wrong context
    SHELL_ERR_HEREDOC_DELIMITER, ///< Invalid heredoc delimiter
    SHELL_ERR_PARSE_MAX = 1099,

    /// Runtime errors (1100-1199)
    SHELL_ERR_RUNTIME_BASE = 1100,
    SHELL_ERR_COMMAND_NOT_FOUND,              ///< Command not in PATH
    SHELL_ERR_PERMISSION_DENIED,              ///< Permission denied
    SHELL_ERR_FILE_NOT_FOUND,                 ///< File does not exist
    SHELL_ERR_NOT_EXECUTABLE,                 ///< File is not executable
    SHELL_ERR_DIVISION_BY_ZERO,               ///< Arithmetic division by zero
    SHELL_ERR_MODULO_BY_ZERO,                 ///< Arithmetic modulo by zero
    SHELL_ERR_ARITH_INVALID_ASSIGN_TARGET,    ///< Assignment to non-lvalue
    SHELL_ERR_ARITH_INVALID_INCREMENT_TARGET, ///< ++ on non-lvalue
    SHELL_ERR_ARITH_INVALID_DECREMENT_TARGET, ///< -- on non-lvalue
    SHELL_ERR_ARITH_NEGATIVE_EXPONENT,        ///< ** with negative exponent
    SHELL_ERR_ARITH_STACK_OVERFLOW,           ///< Operator/operand stack full
    SHELL_ERR_ARITH_STACK_UNDERFLOW,          ///< Operator/operand stack empty
    SHELL_ERR_ARITH_MISMATCHED_TERNARY,       ///< Unmatched ? :
    SHELL_ERR_ARITH_MISMATCHED_PARENS,        ///< Unmatched ( )
    SHELL_ERR_INTEGER_OVERFLOW,               ///< Arithmetic overflow
    SHELL_ERR_INVALID_SUBSCRIPT,              ///< Invalid array subscript
    SHELL_ERR_READONLY_VAR,          ///< Attempt to modify readonly variable
    SHELL_ERR_LOOP_CONTROL,          ///< break/continue outside loop
    SHELL_ERR_RETURN_OUTSIDE_FUNC,   ///< return outside function
    SHELL_ERR_BAD_SUBSTITUTION,      ///< Invalid ${} substitution
    SHELL_ERR_UNBOUND_VARIABLE,      ///< Unset variable with set -u
    SHELL_ERR_PIPE_FAILED,           ///< Failed to create pipe
    SHELL_ERR_PIPELINE_STAGE_FAILED, ///< A pipeline stage exited non-zero
                                     ///< (emitted by pipeline-diagnostic mode)
    SHELL_ERR_FORK_FAILED,           ///< Failed to fork process
    SHELL_ERR_EXEC_FAILED,           ///< Failed to exec command
    SHELL_ERR_FEATURE_DISABLED,      ///< Shell feature not enabled
    SHELL_ERR_MALFORMED_CONSTRUCT,   ///< Malformed control structure
    SHELL_ERR_LOOP_LIMIT,            ///< Loop iteration limit exceeded
    SHELL_ERR_SCOPE_ERROR,           ///< Failed to create/pop scope
    SHELL_ERR_FUNCTION_ERROR,        ///< Function definition/call error
    SHELL_ERR_BAD_FD,                ///< Bad file descriptor
    SHELL_ERR_FD_UNAVAILABLE, ///< No available file descriptor for allocation
    SHELL_ERR_TYPE_MISMATCH, ///< List/map value in a scalar slot, or whole-word
                             ///< constraint violated -- per SEMANTICS.md
                             ///< section 3.9
    SHELL_ERR_RUNTIME_MAX = 1199,

    /// Builtin errors (1200-1299)
    SHELL_ERR_BUILTIN_BASE = 1200,
    SHELL_ERR_INVALID_OPTION,     ///< Unknown option
    SHELL_ERR_MISSING_ARGUMENT,   ///< Required argument missing
    SHELL_ERR_TOO_MANY_ARGUMENTS, ///< Too many arguments
    SHELL_ERR_INVALID_ARGUMENT,   ///< Invalid argument value
    SHELL_ERR_DIRECTORY_STACK,    ///< Directory stack error
    SHELL_ERR_HISTORY_ERROR,      ///< History subsystem error
    SHELL_ERR_ALIAS_ERROR,        ///< Alias subsystem error
    SHELL_ERR_JOB_NOT_FOUND,      ///< No such job
    SHELL_ERR_TRAP_ERROR,         ///< Invalid trap specification
    SHELL_ERR_BUILTIN_MAX = 1299,

    /// Expansion errors (1300-1399)
    SHELL_ERR_EXPANSION_BASE = 1300,
    SHELL_ERR_BAD_PATTERN,             ///< Invalid glob pattern
    SHELL_ERR_NO_MATCH,                ///< No glob matches (with failglob)
    SHELL_ERR_BRACE_EXPANSION,         ///< Invalid brace expansion
    SHELL_ERR_ARITHMETIC_SYNTAX,       ///< Invalid arithmetic expression
    SHELL_ERR_REGEX_SYNTAX,            ///< Invalid regex in [[ =~ ]]
    SHELL_ERR_PROCESS_SUBST,           ///< Process substitution failed
    SHELL_ERR_PARAMETER_NULL_OR_UNSET, ///< ${var:?} or ${var?} on null/unset
                                       ///< parameter (POSIX required-parameter
                                       ///< check)
    SHELL_ERR_EXPANSION_MAX = 1399,

    /// System errors (1400-1499)
    SHELL_ERR_SYSTEM_BASE = 1400,
    SHELL_ERR_OUT_OF_MEMORY,  ///< Memory allocation failed
    SHELL_ERR_IO_ERROR,       ///< I/O error
    SHELL_ERR_SIGNAL_ERROR,   ///< Signal handling error
    SHELL_ERR_RESOURCE_LIMIT, ///< Resource limit exceeded
    SHELL_ERR_SYSTEM_MAX = 1499,

    /// Internal errors (1500-1599)
    SHELL_ERR_INTERNAL_BASE = 1500,
    SHELL_ERR_ASSERTION,             ///< Internal assertion failed
    SHELL_ERR_STATE_CORRUPTION,      ///< Internal state corruption
    SHELL_ERR_NOT_IMPLEMENTED,       ///< Feature not implemented
    SHELL_ERR_SUBSYSTEM_INIT_FAILED, ///< A startup-time subsystem (display,
                                     ///< history, completion, etc.) failed to
                                     ///< initialize. The message identifies the
                                     ///< subsystem and any inner error code
                                     ///< returned by it.
    SHELL_ERR_INTERNAL_MAX = 1599,

} shell_error_code_t;

/**
 * @brief Error severity levels
 */
typedef enum shell_error_severity {
    SHELL_SEVERITY_NOTE,    ///< Informational note
    SHELL_SEVERITY_WARNING, ///< Warning - continue execution
    SHELL_SEVERITY_ERROR,   ///< Error - command failed
    SHELL_SEVERITY_FATAL,   ///< Fatal - abort execution
} shell_error_severity_t;

/* ============================================================================
 * Error Context Structure
 * ============================================================================
 */

/** Maximum depth of error context stack */
#define SHELL_ERROR_CONTEXT_MAX 8

/**
 * @brief Complete error context with chaining support
 *
 * Captures full context of an error including:
 * - Error code and severity
 * - Source location (file/line/column)
 * - Human-readable message with details
 * - Execution context chain
 * - Suggestions for fixing the error
 */
typedef struct shell_error {
    /// Core error information
    shell_error_code_t code;         ///< Structured error code
    shell_error_severity_t severity; ///< Error severity
    source_location_t location;      ///< Where error occurred

    /// Error message components
    char *message;    ///< Primary error message
    char *detail;     ///< Additional detail (optional)
    char *suggestion; ///< "Did you mean?" suggestion

    /// Source context for display
    char *source_line;      ///< The actual source line text
    size_t highlight_start; ///< Start of error highlight
    size_t highlight_end;   ///< End of error highlight

    /// Error chaining
    struct shell_error *cause; ///< What caused this error
    struct shell_error *next;  ///< Next error in list (multi-error)

    /// Execution context stack
    char *context_stack[SHELL_ERROR_CONTEXT_MAX];
    size_t context_depth; ///< Number of context frames

    /// Recovery information
    bool recoverable; ///< Can execution continue?
    int exit_status;  ///< Suggested exit status

} shell_error_t;

/* ============================================================================
 * Error Collector (Multi-Error Reporting)
 * ============================================================================
 */

/** Default maximum errors before bailout */
#define SHELL_ERROR_MAX_DEFAULT 10

/**
 * @brief Error collector for multi-error reporting
 */
typedef struct shell_error_collector {
    shell_error_t *first; ///< First error in list
    shell_error_t *last;  ///< Last error for O(1) append
    size_t count;         ///< Number of errors collected
    size_t warning_count; ///< Number of warnings
    size_t max_errors;    ///< Maximum errors before bailout
    bool has_fatal;       ///< Contains at least one fatal error

    /// Source buffer for context display
    const char *source_buffer; ///< Full source for line lookup
    size_t source_length;      ///< Length of source buffer
    const char *source_name;   ///< Filename for display

} shell_error_collector_t;

/* ============================================================================
 * Error Creation and Management
 * ============================================================================
 */

/**
 * @brief Create a new error
 *
 * @param code Error code
 * @param severity Error severity
 * @param loc Source location
 * @param fmt Printf-style format string
 * @param ... Format arguments
 * @return New error or NULL on allocation failure
 */
shell_error_t *shell_error_create(shell_error_code_t code,
                                  shell_error_severity_t severity,
                                  source_location_t loc, const char *fmt, ...);

/**
 * @brief Create error with va_list
 */
shell_error_t *shell_error_createv(shell_error_code_t code,
                                   shell_error_severity_t severity,
                                   source_location_t loc, const char *fmt,
                                   va_list args);

/**
 * @brief Free an error and its chain
 *
 * @param error Error to free (NULL safe)
 */
void shell_error_free(shell_error_t *error);

/**
 * @brief Add a suggestion to an error
 *
 * @param error Error to modify
 * @param suggestion Suggestion string (copied)
 */
void shell_error_set_suggestion(shell_error_t *error, const char *suggestion);

/**
 * @brief Add detail to an error
 *
 * @param error Error to modify
 * @param detail Detail string (copied)
 */
void shell_error_set_detail(shell_error_t *error, const char *detail);

/**
 * @brief Set source line for display
 *
 * @param error Error to modify
 * @param line Source line text (copied)
 * @param highlight_start Column where highlight starts
 * @param highlight_end Column where highlight ends
 */
void shell_error_set_source_line(shell_error_t *error, const char *line,
                                 size_t highlight_start, size_t highlight_end);

/**
 * @brief Chain an error as the cause of another
 *
 * @param error Error being caused
 * @param cause The causing error (takes ownership)
 */
void shell_error_set_cause(shell_error_t *error, shell_error_t *cause);

/* ============================================================================
 * Error Collector Operations
 * ============================================================================
 */

/**
 * @brief Create a new error collector
 *
 * @param source Source buffer for context display
 * @param source_len Length of source buffer
 * @param source_name Filename for display
 * @param max_errors Maximum errors before bailout (0 for default)
 * @return New collector or NULL on allocation failure
 */
shell_error_collector_t *shell_error_collector_new(const char *source,
                                                   size_t source_len,
                                                   const char *source_name,
                                                   size_t max_errors);

/**
 * @brief Free an error collector and all its errors
 *
 * @param collector Collector to free (NULL safe)
 */
void shell_error_collector_free(shell_error_collector_t *collector);

/**
 * @brief Add an error to the collector
 *
 * @param collector Collector to add to
 * @param error Error to add (takes ownership)
 */
void shell_error_collector_add(shell_error_collector_t *collector,
                               shell_error_t *error);

/**
 * @brief Check if collector has reached error limit
 *
 * @param collector Collector to check
 * @return true if should stop collecting
 */
bool shell_error_collector_full(shell_error_collector_t *collector);

/**
 * @brief Check if collector has any errors (not just warnings)
 *
 * @param collector Collector to check
 * @return true if has at least one error
 */
bool shell_error_collector_has_errors(shell_error_collector_t *collector);

/**
 * @brief Get a source line from the buffer
 *
 * @param collector Collector with source buffer
 * @param line_num Line number (1-based)
 * @return Allocated string with line content, or NULL
 */
char *shell_error_collector_get_line(shell_error_collector_t *collector,
                                     size_t line_num);

/* ============================================================================
 * Error Display
 * ============================================================================
 */

/**
 * @brief Display a single error with source context
 *
 * Produces Rust-style error output:
 *
 * error[E1001]: unexpected token
 *   --> script.sh:15:23
 *    |
 * 15 |     if [ $x -eq 5; then
 *    |                   ^ expected ']' before ';'
 *    |
 *    = help: the test command '[' requires a closing ']'
 *
 * @param error Error to display
 * @param out Output stream
 * @param use_color Whether to use ANSI colors
 */
void shell_error_display(shell_error_t *error, FILE *out, bool use_color);

/**
 * @brief Display all errors from collector with summary
 *
 * @param collector Collector with errors
 * @param out Output stream
 * @param use_color Whether to use ANSI colors
 */
void shell_error_display_all(shell_error_collector_t *collector, FILE *out,
                             bool use_color);

/**
 * @brief Get error code as string (e.g., "E1001")
 *
 * @param code Error code
 * @return Static string with code name
 */
const char *shell_error_code_str(shell_error_code_t code);

/**
 * @brief Get human-readable error category
 *
 * @param code Error code
 * @return Static string describing error category
 */
const char *shell_error_category(shell_error_code_t code);

/**
 * @brief Get severity as string
 *
 * @param severity Severity level
 * @return Static string ("error", "warning", "note", "fatal")
 */
const char *shell_error_severity_str(shell_error_severity_t severity);

/* ============================================================================
 * Context Stack Operations
 * ============================================================================
 */

/**
 * @brief Push a context onto an error's context stack
 *
 * Used to build "while doing X, in Y" chains.
 *
 * @param error Error to modify
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void shell_error_push_context(shell_error_t *error, const char *fmt, ...);

/* ============================================================================
 * Convenience Macros
 * ============================================================================
 */

/**
 * @brief Create source location from current code position
 */
#define SOURCE_LOC_HERE()                                                      \
    ((source_location_t){.filename = __FILE__,                                 \
                         .line = __LINE__,                                     \
                         .column = 0,                                          \
                         .offset = 0,                                          \
                         .length = 0})

/**
 * @brief Quick error creation with current location (for internal errors)
 */
#define SHELL_ERROR_INTERNAL(code, fmt, ...)                                   \
    shell_error_create((code), SHELL_SEVERITY_FATAL, SOURCE_LOC_HERE(), (fmt), \
                       ##__VA_ARGS__)

#endif /// SHELL_ERROR_H
