/**
 * @file debug_core.c
 * @brief Core Debug System Implementation
 *
 * Provides the foundational debugging infrastructure including context
 * management, output control, AST visualization, and integration with
 * the shell executor for interactive debugging sessions.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "debug.h"
#include "errors.h"
#include "executor.h"
#include "lush.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/** @brief Global debug context instance */
debug_context_t *g_debug_context = NULL;

/**
 * @brief Initialize a new debug context
 * @return Pointer to the newly created debug context, or NULL on failure
 */
debug_context_t *debug_init(void) {
    debug_context_t *ctx = malloc(sizeof(debug_context_t));
    if (!ctx) {
        return NULL;
    }

    // Initialize all fields to default values
    ctx->level = DEBUG_NONE;
    ctx->mode = DEBUG_MODE_NORMAL;
    ctx->enabled = false;
    ctx->step_mode = false;
    ctx->trace_execution = false;
    ctx->profile_enabled = false;
    ctx->analysis_enabled = false;

    // Execution state
    ctx->current_frame = NULL;
    ctx->stack_depth = 0;
    ctx->step_target_depth = INT_MAX;

    // Breakpoints
    ctx->breakpoints = NULL;
    ctx->next_breakpoint_id = 1;

    // Profiling
    ctx->profile_data = NULL;
    ctx->timing_enabled = false;

    // Analysis
    ctx->analysis_issues = NULL;
    ctx->issue_count = 0;

    // Output control - default to stderr for debug output
    ctx->debug_output = stderr;
    ctx->profile_output = stderr;
    ctx->analysis_output = stderr;

    // Configuration defaults
    ctx->show_variables = true;
    ctx->show_stack_trace = true;
    ctx->show_timing = false;
    ctx->highlight_syntax = true;
    ctx->max_stack_depth = 100;

    // Statistics
    ctx->total_commands = 0;
    ctx->total_time_ns = 0;
    clock_gettime(CLOCK_MONOTONIC, &ctx->session_start);

    // Initialize execution context (for loop debugging fix)
    ctx->execution_context.in_loop = false;
    ctx->execution_context.loop_variable = NULL;
    ctx->execution_context.loop_variable_value = NULL;
    ctx->execution_context.loop_body_start_line = 0;
    ctx->execution_context.loop_iteration = 0;
    ctx->execution_context.loop_node = NULL;

    // Set global context
    g_debug_context = ctx;

    return ctx;
}

/**
 * @brief Clean up and free a debug context
 * @param ctx Debug context to clean up
 */
void debug_cleanup(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    // Clean up stack frames
    while (ctx->current_frame) {
        debug_pop_frame(ctx);
    }

    // Clean up breakpoints
    debug_clear_breakpoints(ctx);

    // Clean up profile data
    profile_data_t *profile = ctx->profile_data;
    while (profile) {
        profile_data_t *next = profile->next;
        free(profile->function_name);
        free(profile->file_path);
        free(profile);
        profile = next;
    }

    // Clean up analysis issues
    debug_clear_analysis_issues(ctx);

    // Clean up execution context (for loop debugging fix)
    free(ctx->execution_context.loop_variable);
    free(ctx->execution_context.loop_variable_value);
    ctx->execution_context.loop_variable = NULL;
    ctx->execution_context.loop_variable_value = NULL;
    ctx->execution_context.in_loop = false;
    ctx->execution_context.loop_node = NULL;
    ctx->execution_context.loop_iteration = 0;
    ctx->execution_context.loop_body_start_line = 0;

    // Close output files if they're not stderr
    if (ctx->debug_output != stderr) {
        fclose(ctx->debug_output);
    }
    if (ctx->profile_output != stderr) {
        fclose(ctx->profile_output);
    }
    if (ctx->analysis_output != stderr) {
        fclose(ctx->analysis_output);
    }

    // Clear global context if this is it
    if (g_debug_context == ctx) {
        g_debug_context = NULL;
    }

    free(ctx);
}

/**
 * @brief Set the debug level
 * @param ctx Debug context
 * @param level Debug level to set
 */
void debug_set_level(debug_context_t *ctx, debug_level_t level) {
    if (!ctx) {
        return;
    }

    ctx->level = level;

    // Enable features based on level
    switch (level) {
    case DEBUG_NONE:
        ctx->enabled = false;
        ctx->trace_execution = false;
        ctx->profile_enabled = false;
        ctx->analysis_enabled = false;
        break;

    case DEBUG_BASIC:
        ctx->enabled = true;
        ctx->trace_execution = false;
        ctx->profile_enabled = false;
        ctx->analysis_enabled = false;
        break;

    case DEBUG_VERBOSE:
        ctx->enabled = true;
        ctx->trace_execution = true;
        ctx->profile_enabled = false;
        ctx->analysis_enabled = false;
        break;

    case DEBUG_TRACE:
        ctx->enabled = true;
        ctx->trace_execution = true;
        ctx->profile_enabled = false;
        ctx->analysis_enabled = true;
        break;

    case DEBUG_PROFILE:
        ctx->enabled = true;
        ctx->trace_execution = true;
        ctx->profile_enabled = true;
        ctx->analysis_enabled = true;
        ctx->timing_enabled = true;
        break;
    }
}

/**
 * @brief Set the debug mode
 * @param ctx Debug context
 * @param mode Debug mode to set
 */
void debug_set_mode(debug_context_t *ctx, debug_mode_t mode) {
    if (!ctx) {
        return;
    }

    ctx->mode = mode;

    switch (mode) {
    case DEBUG_MODE_NORMAL:
        ctx->step_mode = false;
        break;

    case DEBUG_MODE_STEP:
    case DEBUG_MODE_STEP_OVER:
        ctx->step_mode = true;
        /* Default to step-into depth semantics; debug_step_over()
         * narrows step_target_depth when the user asks for it. */
        ctx->step_target_depth = INT_MAX;
        break;

    case DEBUG_MODE_CONTINUE:
        ctx->step_mode = false;
        break;
    }
}

/**
 * @brief Enable or disable debugging
 * @param ctx Debug context
 * @param enable True to enable, false to disable
 */
void debug_enable(debug_context_t *ctx, bool enable) {
    if (!ctx) {
        return;
    }

    ctx->enabled = enable;

    if (enable) {
        debug_printf(ctx, "Debug mode enabled (level: %d)\n", ctx->level);
        debug_printf(ctx, "Debug session started at: %ld.%09ld\n",
                     ctx->session_start.tv_sec, ctx->session_start.tv_nsec);
    } else {
        debug_printf(ctx, "Debug mode disabled\n");
    }
}

/**
 * @brief Get current time in nanoseconds
 * @return Current monotonic time in nanoseconds
 */
long debug_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

/**
 * @brief Format a time duration for display
 * @param ns Time in nanoseconds
 * @param buffer Output buffer for formatted string
 * @param size Size of output buffer
 */
void debug_format_time(long ns, char *buffer, size_t size) {
    if (ns < 1000) {
        snprintf(buffer, size, "%ld ns", ns);
    } else if (ns < 1000000) {
        snprintf(buffer, size, "%.2f us", ns / 1000.0);
    } else if (ns < 1000000000) {
        snprintf(buffer, size, "%.2f ms", ns / 1000000.0);
    } else {
        snprintf(buffer, size, "%.2f s", ns / 1000000000.0);
    }
}

/**
 * @brief Print a debug message with context
 * @param ctx Debug context
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void debug_printf(debug_context_t *ctx, const char *format, ...) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    va_list args;
    va_start(args, format);

    // Add debug prefix
    fprintf(ctx->debug_output, "[DEBUG] ");

    // Add stack depth indication
    for (int i = 0; i < ctx->stack_depth; i++) {
        fprintf(ctx->debug_output, "  ");
    }

    // Print the actual message
    vfprintf(ctx->debug_output, format, args);
    fflush(ctx->debug_output);

    va_end(args);
}

/**
 * @brief Print a separator line
 * @param ctx Debug context
 */
void debug_print_separator(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    fprintf(ctx->debug_output, "[DEBUG] ");
    for (int i = 0; i < 60; i++) {
        fprintf(ctx->debug_output, "-");
    }
    fprintf(ctx->debug_output, "\n");
    fflush(ctx->debug_output);
}

/**
 * @brief Print a header with title
 * @param ctx Debug context
 * @param title Header title text
 */
void debug_print_header(debug_context_t *ctx, const char *title) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    debug_print_separator(ctx);
    debug_printf(ctx, "%s\n", title);
    debug_print_separator(ctx);
}

/**
 * @brief Set output file for debug messages
 * @param ctx Debug context
 * @param filename Path to output file
 */
void debug_set_output_file(debug_context_t *ctx, const char *filename) {
    if (!ctx || !filename) {
        return;
    }

    // Close existing file if it's not stderr
    if (ctx->debug_output != stderr) {
        fclose(ctx->debug_output);
    }

    // Open new file
    ctx->debug_output = fopen(filename, "w");
    if (!ctx->debug_output) {
        ctx->debug_output = stderr; // Fallback to stderr
        debug_printf(
            ctx,
            "Warning: Could not open debug output file '%s', using stderr\n",
            filename);
    } else {
        debug_printf(ctx, "Debug output redirected to: %s\n", filename);
    }
}

/**
 * @brief Set output file for profile data
 * @param ctx Debug context
 * @param filename Path to output file
 */
void debug_set_profile_output_file(debug_context_t *ctx, const char *filename) {
    if (!ctx || !filename) {
        return;
    }

    if (ctx->profile_output != stderr) {
        fclose(ctx->profile_output);
    }

    ctx->profile_output = fopen(filename, "w");
    if (!ctx->profile_output) {
        ctx->profile_output = stderr;
        debug_printf(
            ctx,
            "Warning: Could not open profile output file '%s', using stderr\n",
            filename);
    } else {
        debug_printf(ctx, "Profile output redirected to: %s\n", filename);
    }
}

/**
 * @brief Set output file for analysis results
 * @param ctx Debug context
 * @param filename Path to output file
 */
void debug_set_analysis_output_file(debug_context_t *ctx,
                                    const char *filename) {
    if (!ctx || !filename) {
        return;
    }

    if (ctx->analysis_output != stderr) {
        fclose(ctx->analysis_output);
    }

    ctx->analysis_output = fopen(filename, "w");
    if (!ctx->analysis_output) {
        ctx->analysis_output = stderr;
        debug_printf(
            ctx,
            "Warning: Could not open analysis output file '%s', using stderr\n",
            filename);
    } else {
        debug_printf(ctx, "Analysis output redirected to: %s\n", filename);
    }
}

/**
 * @brief Get a human-readable description of an AST node
 * @param node AST node to describe
 * @return Newly allocated string with description (caller must free)
 */
char *debug_get_node_description(node_t *node) {
    if (!node) {
        return strdup("(null)");
    }

    char *desc = malloc(256);
    if (!desc) {
        return NULL;
    }

    switch (node->type) {
    case NODE_COMMAND:
        snprintf(desc, 256, "COMMAND: %s",
                 node->val.str ? node->val.str : "(empty)");
        break;
    case NODE_PIPE:
        snprintf(desc, 256, "PIPE");
        break;
    case NODE_IF:
        snprintf(desc, 256, "IF");
        break;
    case NODE_WHILE:
        snprintf(desc, 256, "WHILE");
        break;
    case NODE_FOR:
        snprintf(desc, 256, "FOR");
        break;
    case NODE_FOR_ARITH:
        snprintf(desc, 256, "FOR_ARITH (C-style)");
        break;
    case NODE_CASE:
        snprintf(desc, 256, "CASE");
        break;
    case NODE_FUNCTION:
        snprintf(desc, 256, "FUNCTION: %s",
                 node->val.str ? node->val.str : "(anonymous)");
        break;
    case NODE_BRACE_GROUP:
        snprintf(desc, 256, "BRACE_GROUP");
        break;
    case NODE_SUBSHELL:
        snprintf(desc, 256, "SUBSHELL");
        break;
    case NODE_LOGICAL_AND:
        snprintf(desc, 256, "LOGICAL_AND");
        break;
    case NODE_LOGICAL_OR:
        snprintf(desc, 256, "LOGICAL_OR");
        break;
    case NODE_ARITH_CMD:
        snprintf(desc, 256, "ARITH_CMD: (( %s ))",
                 node->val.str ? node->val.str : "");
        break;
    case NODE_ARRAY_LITERAL:
        snprintf(desc, 256, "ARRAY_LITERAL (%zu elements)", node->children);
        break;
    case NODE_ARRAY_ACCESS:
        snprintf(desc, 256, "ARRAY_ACCESS: %s",
                 node->val.str ? node->val.str : "");
        break;
    case NODE_ARRAY_ASSIGN:
        snprintf(desc, 256, "ARRAY_ASSIGN: %s",
                 node->val.str ? node->val.str : "");
        break;
    case NODE_EXTENDED_TEST:
        snprintf(desc, 256, "EXTENDED_TEST: [[ %s ]]",
                 node->val.str ? node->val.str : "");
        break;
    case NODE_PROC_SUB_IN:
        snprintf(desc, 256, "PROC_SUB_IN: <(%s)",
                 node->val.str ? node->val.str : "");
        break;
    case NODE_PROC_SUB_OUT:
        snprintf(desc, 256, "PROC_SUB_OUT: >(%s)",
                 node->val.str ? node->val.str : "");
        break;
    case NODE_COPROC:
        snprintf(desc, 256, "COPROC: %s", node->val.str ? node->val.str : "");
        break;
    case NODE_CASE_ITEM: {
        const char *term_str = ";;";
        const char *pattern = node->val.str ? node->val.str : "";
        // Pattern format: "<terminator_char><pattern>"
        if (pattern[0] >= '0' && pattern[0] <= '2') {
            if (pattern[0] == '1') {
                term_str = ";&";
            } else if (pattern[0] == '2') {
                term_str = ";;&";
            }
            pattern++; // Skip prefix
        }
        snprintf(desc, 256, "CASE_ITEM: %s [%s]", pattern, term_str);
        break;
    }
    case NODE_SELECT:
        snprintf(desc, 256, "SELECT: %s", node->val.str ? node->val.str : "");
        break;
    case NODE_TIME:
        snprintf(desc, 256, "TIME%s",
                 (node->val_type == VAL_SINT && node->val.sint == 1) ? " -p"
                                                                     : "");
        break;
    case NODE_ANON_FUNCTION:
        snprintf(desc, 256, "ANON_FUNCTION () { ... }");
        break;
    default:
        snprintf(desc, 256, "UNKNOWN_NODE_TYPE_%d", node->type);
        break;
    }

    return desc;
}

/**
 * @brief Print an AST node with indentation
 * @param ctx Debug context
 * @param node AST node to print
 * @param indent Indentation level
 */
void debug_print_node(debug_context_t *ctx, node_t *node, int indent) {
    if (!ctx || !ctx->enabled || !node) {
        return;
    }

    // Print indentation
    for (int i = 0; i < indent; i++) {
        fprintf(ctx->debug_output, "  ");
    }

    char *desc = debug_get_node_description(node);
    fprintf(ctx->debug_output, "[DEBUG] %s\n", desc);
    free(desc);

    // Print children
    if (node->first_child) {
        debug_print_node(ctx, node->first_child, indent + 1);
    }

    // Print siblings
    if (node->next_sibling) {
        debug_print_node(ctx, node->next_sibling, indent);
    }
}

/**
 * @brief Print an entire AST
 * @param ctx Debug context
 * @param ast Root of the AST to print
 */
void debug_print_ast(debug_context_t *ctx, node_t *ast) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    debug_print_header(ctx, "Abstract Syntax Tree");
    debug_print_node(ctx, ast, 0);
}

/**
 * @brief Clear all analysis issues from context
 * @param ctx Debug context
 */
void debug_clear_analysis_issues(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        analysis_issue_t *next = issue->next;
        free(issue->file_path);
        free(issue->severity);
        free(issue->category);
        free(issue->message);
        free(issue->suggestion);
        free(issue);
        issue = next;
    }

    ctx->analysis_issues = NULL;
    ctx->issue_count = 0;
}

/**
 * @brief List all defined functions
 * @param ctx Debug context
 */
void debug_list_functions(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    // Access the current executor to get function definitions
    if (!current_executor || !current_executor->functions) {
        printf("No functions defined.\n");
        return;
    }

    printf("Defined functions:\n");

    function_def_t *func = current_executor->functions;
    int count = 0;

    while (func) {
        count++;
        printf("  %d. %s\n", count, func->name);
        func = func->next;
    }

    if (count == 0) {
        printf("  (none)\n");
    } else {
        printf("\nTotal: %d function%s\n", count, count == 1 ? "" : "s");
        printf("Use 'debug function <name>' to see function details.\n");
    }
}

/**
 * @brief Show details of a specific function
 * @param ctx Debug context
 * @param function_name Name of the function to show
 */
void debug_show_function(debug_context_t *ctx, const char *function_name) {
    if (!ctx || !function_name) {
        return;
    }

    // Access the current executor to find the function
    if (!current_executor) {
        printf("No executor context available.\n");
        return;
    }

    // Find the function in the executor's function list
    function_def_t *func = current_executor->functions;
    while (func) {
        if (strcmp(func->name, function_name) == 0) {
            break;
        }
        func = func->next;
    }

    if (!func) {
        printf("Function '%s' not found.\n", function_name);
        printf("Use 'debug functions' to list all defined functions.\n");
        return;
    }

    // Display function information
    printf("Function: %s\n", func->name);
    printf("========================================\n");

    // Display parameter information
    if (func->params) {
        printf("Parameters:\n");
        function_param_t *param = func->params;
        int param_num = 1;

        while (param) {
            printf("  %d. %s", param_num++, param->name);
            if (param->is_required) {
                printf(" (required)");
            } else {
                printf(" (optional");
                if (param->default_value) {
                    printf(", default: \"%s\"", param->default_value);
                }
                printf(")");
            }
            printf("\n");
            param = param->next;
        }
        printf("\n");
    } else {
        printf("Parameters: (none defined - accepts any arguments)\n\n");
    }

    if (!func->body) {
        printf("Body: (empty function)\n");
    } else {
        printf("Body: (AST representation)\n");

        // Print the AST structure for the function body
        printf("AST Structure:\n");
        debug_print_node(ctx, func->body, 2);
    }

    // Display return value information
    printf("Return Values:\n");
    printf("  Exit Status: 0-255 (via 'return [code]')\n");
    printf("  String Value: any text (via 'return_value \"text\"')\n");
    printf("  Stdout Capture: $(function_name args) captures output\n");
    printf("  Return Value Capture: $(function_name args) captures "
           "return_value if set\n");
    printf("\n");

    printf("Debug Integration:\n");
    printf("  - Function calls traced in debug mode\n");
    printf("  - Return values inspectable via command substitution\n");
    printf("  - Parameter validation during execution\n");
    printf("  - Performance profiling available\n");
    printf("\n");

    printf("========================================\n");
    if (func->params) {
        printf("Usage: %s", func->name);
        function_param_t *param = func->params;
        while (param) {
            if (param->is_required) {
                printf(" <%s>", param->name);
            } else {
                printf(" [%s]", param->name);
            }
            param = param->next;
        }
        printf("\n");
    } else {
        printf("Usage: %s [arguments...]\n", func->name);
    }

    printf("\nReturn Value Examples:\n");
    printf("  %s args               # Execute function\n", func->name);
    printf("  result=$(%s args)     # Capture output or return_value\n",
           func->name);
    printf("  echo $?               # Check exit status after call\n");
}
