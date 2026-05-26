/**
 * @file shell_error.c
 * @brief Unified error management system implementation
 *
 * Implements structured error reporting with source locations, context chains,
 * multi-error collection, and Rust-style error display.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "shell_error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * ANSI Color Codes
 * ============================================================================
 */

#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_RED "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE "\033[34m"
#define ANSI_CYAN "\033[36m"
#define ANSI_MAGENTA "\033[35m"

/* ============================================================================
 * Error Code String Tables
 * ============================================================================
 */

/**
 * @brief Get error code as formatted string (e.g., "E1001")
 */
const char *shell_error_code_str(shell_error_code_t code) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "E%04d", (int)code);
    return buf;
}

/**
 * @brief Get human-readable error category
 */
const char *shell_error_category(shell_error_code_t code) {
    if (code == SHELL_OK) {
        return "success";
    } else if (code >= SHELL_ERR_PARSE_BASE && code <= SHELL_ERR_PARSE_MAX) {
        return "parse error";
    } else if (code >= SHELL_ERR_RUNTIME_BASE &&
               code <= SHELL_ERR_RUNTIME_MAX) {
        return "runtime error";
    } else if (code >= SHELL_ERR_BUILTIN_BASE &&
               code <= SHELL_ERR_BUILTIN_MAX) {
        return "builtin error";
    } else if (code >= SHELL_ERR_EXPANSION_BASE &&
               code <= SHELL_ERR_EXPANSION_MAX) {
        return "expansion error";
    } else if (code >= SHELL_ERR_SYSTEM_BASE && code <= SHELL_ERR_SYSTEM_MAX) {
        return "system error";
    } else if (code >= SHELL_ERR_INTERNAL_BASE &&
               code <= SHELL_ERR_INTERNAL_MAX) {
        return "internal error";
    }
    return "unknown error";
}

/**
 * @brief Get severity as string
 */
const char *shell_error_severity_str(shell_error_severity_t severity) {
    switch (severity) {
    case SHELL_SEVERITY_NOTE:
        return "note";
    case SHELL_SEVERITY_WARNING:
        return "warning";
    case SHELL_SEVERITY_ERROR:
        return "error";
    case SHELL_SEVERITY_FATAL:
        return "fatal";
    default:
        return "error";
    }
}

/* ============================================================================
 * Error Creation and Management
 * ============================================================================
 */

shell_error_t *shell_error_create(shell_error_code_t code,
                                  shell_error_severity_t severity,
                                  source_location_t loc, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    shell_error_t *error = shell_error_createv(code, severity, loc, fmt, args);
    va_end(args);
    return error;
}

shell_error_t *shell_error_createv(shell_error_code_t code,
                                   shell_error_severity_t severity,
                                   source_location_t loc, const char *fmt,
                                   va_list args) {
    shell_error_t *error = calloc(1, sizeof(shell_error_t));
    if (!error) {
        return NULL;
    }

    error->code = code;
    error->severity = severity;
    error->location = loc;
    error->recoverable = (severity != SHELL_SEVERITY_FATAL);

    /// Set default exit status based on error category
    if (code >= SHELL_ERR_PARSE_BASE && code <= SHELL_ERR_PARSE_MAX) {
        error->exit_status = 2; /// Syntax error
    } else if (code == SHELL_ERR_COMMAND_NOT_FOUND) {
        error->exit_status = 127;
    } else if (code == SHELL_ERR_PERMISSION_DENIED ||
               code == SHELL_ERR_NOT_EXECUTABLE) {
        error->exit_status = 126;
    } else {
        error->exit_status = 1;
    }

    /// Format the message
    if (fmt) {
        va_list args_copy;
        va_copy(args_copy, args);
        int len = vsnprintf(NULL, 0, fmt, args_copy);
        va_end(args_copy);

        if (len > 0) {
            error->message = malloc(len + 1);
            if (error->message) {
                vsnprintf(error->message, len + 1, fmt, args);
            }
        }
    }

    return error;
}

void shell_error_free(shell_error_t *error) {
    while (error) {
        shell_error_t *next = error->next;

        free(error->message);
        free(error->detail);
        free(error->suggestion);
        free(error->source_line);

        /// Free context stack
        for (size_t i = 0; i < error->context_depth; i++) {
            free(error->context_stack[i]);
        }

        /// Free cause chain
        if (error->cause) {
            shell_error_free(error->cause);
        }

        free(error);
        error = next;
    }
}

void shell_error_set_suggestion(shell_error_t *error, const char *suggestion) {
    if (!error || !suggestion)
        return;
    free(error->suggestion);
    error->suggestion = strdup(suggestion);
}

void shell_error_set_detail(shell_error_t *error, const char *detail) {
    if (!error || !detail)
        return;
    free(error->detail);
    error->detail = strdup(detail);
}

void shell_error_set_source_line(shell_error_t *error, const char *line,
                                 size_t highlight_start, size_t highlight_end) {
    if (!error || !line)
        return;
    free(error->source_line);
    error->source_line = strdup(line);
    error->highlight_start = highlight_start;
    error->highlight_end = highlight_end;
}

void shell_error_set_cause(shell_error_t *error, shell_error_t *cause) {
    if (!error) {
        shell_error_free(cause);
        return;
    }
    if (error->cause) {
        shell_error_free(error->cause);
    }
    error->cause = cause;
}

void shell_error_push_context(shell_error_t *error, const char *fmt, ...) {
    if (!error || !fmt)
        return;
    if (error->context_depth >= SHELL_ERROR_CONTEXT_MAX)
        return;

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (len > 0) {
        char *ctx = malloc(len + 1);
        if (ctx) {
            va_start(args, fmt);
            vsnprintf(ctx, len + 1, fmt, args);
            va_end(args);
            error->context_stack[error->context_depth++] = ctx;
        }
    }
}

/* ============================================================================
 * Error Collector Operations
 * ============================================================================
 */

shell_error_collector_t *shell_error_collector_new(const char *source,
                                                   size_t source_len,
                                                   const char *source_name,
                                                   size_t max_errors) {
    shell_error_collector_t *collector =
        calloc(1, sizeof(shell_error_collector_t));
    if (!collector) {
        return NULL;
    }

    collector->source_buffer = source;
    collector->source_length = source_len;
    collector->source_name = source_name;
    collector->max_errors =
        max_errors > 0 ? max_errors : SHELL_ERROR_MAX_DEFAULT;

    return collector;
}

void shell_error_collector_free(shell_error_collector_t *collector) {
    if (!collector)
        return;

    shell_error_t *error = collector->first;
    while (error) {
        shell_error_t *next = error->next;
        error->next = NULL; /// Prevent double-free in shell_error_free
        shell_error_free(error);
        error = next;
    }

    free(collector);
}

void shell_error_collector_add(shell_error_collector_t *collector,
                               shell_error_t *error) {
    if (!collector || !error) {
        shell_error_free(error);
        return;
    }

    /// Add to linked list
    if (!collector->first) {
        collector->first = error;
        collector->last = error;
    } else {
        collector->last->next = error;
        collector->last = error;
    }

    /// Update counters
    if (error->severity == SHELL_SEVERITY_WARNING) {
        collector->warning_count++;
    } else {
        collector->count++;
    }

    if (error->severity == SHELL_SEVERITY_FATAL) {
        collector->has_fatal = true;
    }
}

bool shell_error_collector_full(shell_error_collector_t *collector) {
    if (!collector)
        return true;
    return collector->count >= collector->max_errors || collector->has_fatal;
}

bool shell_error_collector_has_errors(shell_error_collector_t *collector) {
    return collector && collector->count > 0;
}

char *shell_error_collector_get_line(shell_error_collector_t *collector,
                                     size_t line_num) {
    if (!collector || !collector->source_buffer || line_num == 0) {
        return NULL;
    }

    const char *src = collector->source_buffer;
    size_t len = collector->source_length;
    size_t current_line = 1;
    size_t line_start = 0;

    /// Find the start of the requested line
    for (size_t i = 0; i < len && current_line < line_num; i++) {
        if (src[i] == '\n') {
            current_line++;
            line_start = i + 1;
        }
    }

    if (current_line != line_num) {
        return NULL; /// Line not found
    }

    /// Find the end of the line
    size_t line_end = line_start;
    while (line_end < len && src[line_end] != '\n') {
        line_end++;
    }

    /// Allocate and copy
    size_t line_len = line_end - line_start;
    char *result = malloc(line_len + 1);
    if (result) {
        memcpy(result, src + line_start, line_len);
        result[line_len] = '\0';
    }

    return result;
}

/* ============================================================================
 * Error Display
 * ============================================================================
 */

/**
 * @brief Print colored text if colors enabled
 */
static void print_colored(FILE *out, bool use_color, const char *color,
                          const char *fmt, ...) {
    if (use_color && color) {
        fputs(color, out);
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    if (use_color && color) {
        fputs(ANSI_RESET, out);
    }
}

/**
 * @brief Get color for severity
 */
static const char *severity_color(shell_error_severity_t severity) {
    switch (severity) {
    case SHELL_SEVERITY_NOTE:
        return ANSI_CYAN;
    case SHELL_SEVERITY_WARNING:
        return ANSI_YELLOW;
    case SHELL_SEVERITY_ERROR:
        return ANSI_RED;
    case SHELL_SEVERITY_FATAL:
        return ANSI_RED ANSI_BOLD;
    default:
        return ANSI_RED;
    }
}

void shell_error_display(shell_error_t *error, FILE *out, bool use_color) {
    if (!error || !out)
        return;

    const char *sev_color = severity_color(error->severity);
    const char *sev_str = shell_error_severity_str(error->severity);

    /// Header line: error[E1001]: message
    print_colored(out, use_color, sev_color, "%s", sev_str);
    print_colored(out, use_color, ANSI_BOLD, "[%s]",
                  shell_error_code_str(error->code));
    fprintf(out, ": ");
    print_colored(out, use_color, ANSI_BOLD, "%s\n",
                  error->message ? error->message : "(no message)");

    /// Location line: --> file:line:column
    if (SOURCE_LOC_VALID(error->location)) {
        print_colored(out, use_color, ANSI_BLUE, "  --> ");
        fprintf(out, "%s:%zu:%zu\n",
                error->location.filename ? error->location.filename
                                         : "<unknown>",
                error->location.line,
                error->location.column > 0 ? error->location.column : 1);
    }

    /// Source line with highlighting
    if (error->source_line) {
        size_t line_num = error->location.line;
        int line_width = snprintf(NULL, 0, "%zu", line_num);

        /// Blank line prefix
        print_colored(out, use_color, ANSI_BLUE, "%*s |\n", line_width, "");

        /// Source line
        print_colored(out, use_color, ANSI_BLUE, "%zu | ", line_num);
        fprintf(out, "%s\n", error->source_line);

        /// Highlight line
        print_colored(out, use_color, ANSI_BLUE, "%*s | ", line_width, "");

        size_t col = error->location.column > 0 ? error->location.column : 1;
        for (size_t i = 1; i < col; i++) {
            fputc(' ', out);
        }

        print_colored(out, use_color, sev_color, "^");
        size_t span = error->location.length > 0 ? error->location.length : 1;
        for (size_t i = 1; i < span; i++) {
            print_colored(out, use_color, sev_color, "~");
        }

        /// Inline message
        if (error->detail) {
            fprintf(out, " ");
            print_colored(out, use_color, sev_color, "%s", error->detail);
        }
        fprintf(out, "\n");

        /// Trailing blank
        print_colored(out, use_color, ANSI_BLUE, "%*s |\n", line_width, "");
    }

    /// Context stack
    for (size_t i = 0; i < error->context_depth; i++) {
        print_colored(out, use_color, ANSI_BLUE, "   = ");
        print_colored(out, use_color, ANSI_CYAN, "while: ");
        fprintf(out, "%s\n", error->context_stack[i]);
    }

    /// Suggestion
    if (error->suggestion) {
        print_colored(out, use_color, ANSI_BLUE, "   = ");
        print_colored(out, use_color, ANSI_CYAN, "help: ");
        fprintf(out, "%s\n", error->suggestion);
    }

    /// Cause chain
    if (error->cause) {
        fprintf(out, "\n");
        print_colored(out, use_color, ANSI_BLUE, "   = ");
        print_colored(out, use_color, ANSI_CYAN, "caused by:\n");
        shell_error_display(error->cause, out, use_color);
    }

    fprintf(out, "\n");
}

void shell_error_display_all(shell_error_collector_t *collector, FILE *out,
                             bool use_color) {
    if (!collector || !out)
        return;

    /// Display each error
    for (shell_error_t *err = collector->first; err; err = err->next) {
        /// Try to get source line if not already set
        if (!err->source_line && SOURCE_LOC_VALID(err->location)) {
            char *line =
                shell_error_collector_get_line(collector, err->location.line);
            if (line) {
                shell_error_set_source_line(err, line, err->location.column,
                                            err->location.column +
                                                err->location.length);
                free(line);
            }
        }

        shell_error_display(err, out, use_color);
    }

    /// Summary line
    if (collector->count > 0 || collector->warning_count > 0) {
        print_colored(out, use_color, ANSI_BOLD, "aborting due to ");
        if (collector->count > 0) {
            print_colored(out, use_color, ANSI_RED ANSI_BOLD, "%zu error%s",
                          collector->count, collector->count == 1 ? "" : "s");
        }
        if (collector->count > 0 && collector->warning_count > 0) {
            fprintf(out, " and ");
        }
        if (collector->warning_count > 0) {
            print_colored(out, use_color, ANSI_YELLOW ANSI_BOLD,
                          "%zu warning%s", collector->warning_count,
                          collector->warning_count == 1 ? "" : "s");
        }
        fprintf(out, "\n");
    }
}
