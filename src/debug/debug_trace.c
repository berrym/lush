/**
 * @file debug_trace.c
 * @brief Execution Tracing and Variable Inspection
 *
 * Provides execution tracing for commands, builtins, and functions,
 * along with stack frame management and variable inspection capabilities
 * for interactive debugging sessions.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "debug.h"
#include "errors.h"
#include "executor.h"
#include "node.h"
#include "symtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Map a symtable-side value classification to lush's user-facing type
 * vocabulary -- Scalar / List / Map -- per SEMANTICS.md. The
 * is_associative flag is meaningful only for SYMVAR_ARRAY entries:
 * lush arrays carry that flag on array_value_t, and the same enum
 * value SYMVAR_ARRAY covers both indexed (List) and associative (Map)
 * arrays. */
static const char *debug_var_type_label(symvar_type_t type,
                                        bool is_associative) {
    switch (type) {
    case SYMVAR_STRING:
    case SYMVAR_INTEGER:
        return "Scalar";
    case SYMVAR_ARRAY:
        return is_associative ? "Map" : "List";
    case SYMVAR_FUNCTION:
        return "Func";
    case SYMVAR_NAMEREF:
        return "Nameref";
    }
    return "?";
}

/* Callback printing one variable from a scope's vars_ht entry. The
 * vars_ht stores only scalar-shaped types (arrays live in separate
 * global storage, reached via symtable_enumerate_arrays). */
static void debug_local_var_print_cb(const char *name, const char *value,
                                     symvar_type_t type, void *userdata) {
    debug_context_t *ctx = (debug_context_t *)userdata;
    debug_printf(ctx, "  %-12s %-7s \"%s\"\n", name,
                 debug_var_type_label(type, false), value);
}

/* Callback printing one array entry from symtable_enumerate_arrays. */
static void debug_array_print_cb(const char *name, array_value_t *array,
                                 void *userdata) {
    debug_context_t *ctx = (debug_context_t *)userdata;
    if (!array) {
        return;
    }
    debug_printf(ctx, "  %-12s %-7s (%zu element%s)\n", name,
                 debug_var_type_label(SYMVAR_ARRAY, array->is_associative),
                 array->count, array->count == 1 ? "" : "s");
}

/**
 * @brief Trace execution of an AST node
 * @param ctx Debug context
 * @param node AST node being executed
 * @param file Source file name
 * @param line Line number in source file
 */
void debug_trace_node(debug_context_t *ctx, node_t *node, const char *file,
                      int line) {
    if (!ctx || !ctx->enabled || !ctx->trace_execution || !node) {
        return;
    }

    char *desc = debug_get_node_description(node);
    debug_printf(ctx, "TRACE: %s:%d - %s\n", file ? file : "unknown", line,
                 desc);
    free(desc);

    // Show timing if enabled
    if (ctx->show_timing) {
        long current_time = debug_get_time_ns();
        char time_str[64];
        debug_format_time(current_time -
                              ctx->session_start.tv_sec * 1000000000L -
                              ctx->session_start.tv_nsec,
                          time_str, sizeof(time_str));
        debug_printf(ctx, "  Time: %s\n", time_str);
    }

    // Show variables if enabled and we are inside a non-global scope.
    if (ctx->show_variables &&
        symtable_current_scope_type(symtable_manager()) != SCOPE_GLOBAL) {
        debug_printf(ctx, "  Variables in scope:\n");
        symtable_enumerate_current_scope_vars(symtable_manager(),
                                              debug_local_var_print_cb, ctx);
    }

    ctx->total_commands++;
}

/**
 * @brief Trace execution of a command
 * @param ctx Debug context
 * @param command Command name
 * @param argv Argument vector
 * @param argc Argument count
 */
void debug_trace_command(debug_context_t *ctx, const char *command, char **argv,
                         int argc) {
    if (!ctx || !ctx->enabled || !ctx->trace_execution || !command) {
        return;
    }

    debug_printf(ctx, "COMMAND: %s", command);

    // Show arguments
    if (argv && argc > 1) {
        debug_printf(ctx, " with args: ");
        for (int i = 1; i < argc; i++) {
            fprintf(ctx->debug_output, "'%s'", argv[i]);
            if (i < argc - 1) {
                fprintf(ctx->debug_output, " ");
            }
        }
        fprintf(ctx->debug_output, "\n");
    } else {
        fprintf(ctx->debug_output, "\n");
    }

    fflush(ctx->debug_output);
}

/**
 * @brief Trace execution of a builtin command
 * @param ctx Debug context
 * @param builtin Builtin command name
 * @param argv Argument vector
 * @param argc Argument count
 */
void debug_trace_builtin(debug_context_t *ctx, const char *builtin, char **argv,
                         int argc) {
    if (!ctx || !ctx->enabled || !ctx->trace_execution || !builtin) {
        return;
    }

    debug_printf(ctx, "BUILTIN: %s", builtin);

    // Show arguments
    if (argv && argc > 1) {
        debug_printf(ctx, " with args: ");
        for (int i = 1; i < argc; i++) {
            fprintf(ctx->debug_output, "'%s'", argv[i]);
            if (i < argc - 1) {
                fprintf(ctx->debug_output, " ");
            }
        }
        fprintf(ctx->debug_output, "\n");
    } else {
        fprintf(ctx->debug_output, "\n");
    }

    fflush(ctx->debug_output);
}

/**
 * @brief Trace a function call
 * @param ctx Debug context
 * @param function Function name
 * @param argv Argument vector
 * @param argc Argument count
 */
void debug_trace_function_call(debug_context_t *ctx, const char *function,
                               char **argv, int argc) {
    if (!ctx || !ctx->enabled || !ctx->trace_execution || !function) {
        return;
    }

    debug_printf(ctx, "FUNCTION: %s", function);

    // Show arguments
    if (argv && argc > 1) {
        debug_printf(ctx, " with args: ");
        for (int i = 1; i < argc; i++) {
            fprintf(ctx->debug_output, "'%s'", argv[i]);
            if (i < argc - 1) {
                fprintf(ctx->debug_output, " ");
            }
        }
        fprintf(ctx->debug_output, "\n");
    } else {
        fprintf(ctx->debug_output, "\n");
    }

    fflush(ctx->debug_output);
}

/**
 * @brief Push a new stack frame
 * @param ctx Debug context
 * @param function Function name for the frame
 * @param file Source file name
 * @param line Line number
 * @return Pointer to the new frame, or NULL on failure
 */
debug_frame_t *debug_push_frame(debug_context_t *ctx, const char *function,
                                const char *file, int line) {
    if (!ctx || !function) {
        return NULL;
    }

    // Check stack depth limit
    if (ctx->stack_depth >= ctx->max_stack_depth) {
        debug_printf(ctx, "WARNING: Maximum stack depth reached (%d)\n",
                     ctx->max_stack_depth);
        return NULL;
    }

    debug_frame_t *frame = malloc(sizeof(debug_frame_t));
    if (!frame) {
        debug_printf(ctx, "ERROR: Failed to allocate debug frame\n");
        return NULL;
    }

    // Initialize frame
    frame->function_name = strdup(function);
    frame->file_path = file ? strdup(file) : NULL;
    frame->line_number = line;
    frame->current_node = NULL;
    frame->parent = ctx->current_frame;

    // Set timing
    clock_gettime(CLOCK_MONOTONIC, &frame->start_time);
    frame->end_time.tv_sec = 0;
    frame->end_time.tv_nsec = 0;

    // Update context
    ctx->current_frame = frame;
    ctx->stack_depth++;

    if (ctx->trace_execution) {
        debug_printf(ctx, "ENTER: %s (%s:%d) [depth: %d]\n", function,
                     file ? file : "unknown", line, ctx->stack_depth);
    }

    return frame;
}

/**
 * @brief Pop the current stack frame
 * @param ctx Debug context
 */
void debug_pop_frame(debug_context_t *ctx) {
    if (!ctx || !ctx->current_frame) {
        return;
    }

    debug_frame_t *frame = ctx->current_frame;

    // Set end time
    clock_gettime(CLOCK_MONOTONIC, &frame->end_time);

    // Calculate execution time
    long duration_ns =
        (frame->end_time.tv_sec - frame->start_time.tv_sec) * 1000000000L +
        (frame->end_time.tv_nsec - frame->start_time.tv_nsec);

    if (ctx->trace_execution) {
        char time_str[64];
        debug_format_time(duration_ns, time_str, sizeof(time_str));
        debug_printf(ctx, "EXIT: %s (duration: %s) [depth: %d]\n",
                     frame->function_name, time_str, ctx->stack_depth);
    }

    // Update context
    ctx->current_frame = frame->parent;
    ctx->stack_depth--;

    // Update total time
    ctx->total_time_ns += duration_ns;

    // Clean up frame
    free(frame->function_name);
    free(frame->file_path);
    free(frame);
}

/**
 * @brief Update the current frame's AST node
 * @param ctx Debug context
 * @param node Current AST node
 */
void debug_update_frame_node(debug_context_t *ctx, node_t *node) {
    if (!ctx || !ctx->current_frame) {
        return;
    }

    ctx->current_frame->current_node = node;
}

/**
 * @brief Show the current call stack
 * @param ctx Debug context
 */
void debug_show_stack(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    debug_print_header(ctx, "Call Stack");

    if (!ctx->current_frame) {
        debug_printf(ctx, "  (empty)\n");
        return;
    }

    debug_frame_t *frame = ctx->current_frame;
    int depth = ctx->stack_depth;

    while (frame) {
        debug_printf(ctx, "  #%d: %s", depth, frame->function_name);

        if (frame->file_path) {
            fprintf(ctx->debug_output, " at %s:%d", frame->file_path,
                    frame->line_number);
        }

        // Show timing for current frame
        if (ctx->show_timing && frame == ctx->current_frame) {
            struct timespec current_time;
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            long duration_ns =
                (current_time.tv_sec - frame->start_time.tv_sec) * 1000000000L +
                (current_time.tv_nsec - frame->start_time.tv_nsec);
            char time_str[64];
            debug_format_time(duration_ns, time_str, sizeof(time_str));
            fprintf(ctx->debug_output, " (running: %s)", time_str);
        }

        fprintf(ctx->debug_output, "\n");

        frame = frame->parent;
        depth--;
    }
}

/**
 * @brief Inspect a variable by name
 * @param ctx Debug context
 * @param name Variable name (with or without $ prefix)
 */
void debug_inspect_variable(debug_context_t *ctx, const char *name) {
    if (!ctx || !ctx->enabled || !name) {
        return;
    }

    debug_printf(ctx, "VARIABLE: %s\n", name);

    // Clean variable name (remove $ prefix if present)
    const char *clean_name = (name[0] == '$') ? name + 1 : name;

    if (!current_executor) {
        debug_printf(ctx, "  Error: No executor context available\n");
        return;
    }

    // Arrays first -- they carry the richest type info (List vs Map).
    array_value_t *array = symtable_get_array(clean_name);
    if (array) {
        debug_printf(
            ctx, "  Type:  %s\n",
            debug_var_type_label(SYMVAR_ARRAY, array->is_associative));
        debug_printf(ctx, "  Count: %zu element%s\n", array->count,
                     array->count == 1 ? "" : "s");
        debug_printf(ctx, "  Scope: %s\n",
                     symtable_current_scope_type(symtable_manager()) ==
                             SCOPE_GLOBAL
                         ? "global"
                         : "function");
        return;
    }

    // Scalar via the scope chain: symtable_get_var walks current scope
    // up to global. The return is heap-allocated and must be freed.
    char *owned_value = symtable_get_var(symtable_manager(), clean_name);
    const char *value = owned_value;
    const char *scope = NULL;
    if (value) {
        scope =
            (symtable_current_scope_type(symtable_manager()) != SCOPE_GLOBAL)
                ? "shell (in or above current scope)"
                : "global";
    } else {
        // Environment fallback for unexported shell vars that landed
        // in the process environment.
        value = getenv(clean_name);
        if (value) {
            scope = "environment";
        }
    }

    if (value) {
        debug_printf(ctx, "  Type:  Scalar\n");
        debug_printf(ctx, "  Value: \"%s\"\n", value);
        debug_printf(ctx, "  Length: %zu characters\n", strlen(value));
        debug_printf(ctx, "  Scope: %s\n", scope);

        // Show first few characters if value is very long.
        if (strlen(value) > 100) {
            char preview[104];
            strncpy(preview, value, 100);
            preview[100] = '\0';
            debug_printf(ctx, "  Preview: \"%.100s...\"\n", preview);
        }
        free(owned_value);
        return;
    }
    free(owned_value);

    {
        // Check for special variables
        if (strcmp(clean_name, "?") == 0) {
            const char *exit_status = symtable_get_global("?") ?: "0";
            debug_printf(ctx, "  Value: '%s' (last exit status)\n",
                         exit_status);
            debug_printf(ctx, "  Type: numeric\n");
            debug_printf(ctx, "  Scope: special\n");
        } else if (strcmp(clean_name, "$") == 0) {
            const char *shell_pid = symtable_get_global("$");
            if (!shell_pid) {
                shell_pid = "unknown";
            }
            debug_printf(ctx, "  Value: '%s' (shell PID)\n", shell_pid);
            debug_printf(ctx, "  Type: numeric\n");
            debug_printf(ctx, "  Scope: special\n");
        } else if (strcmp(clean_name, "PWD") == 0) {
            debug_printf(ctx, "  Value: '%s' (current directory)\n",
                         symtable_get_global("PWD") ?: "unknown");
        } else if (strcmp(clean_name, "HOME") == 0) {
            debug_printf(ctx, "  Value: '%s' (home directory)\n",
                         symtable_get_global("HOME") ?: "unknown");
        } else if (strcmp(clean_name, "PATH") == 0) {
            const char *path = symtable_get_global("PATH");
            if (path) {
                debug_printf(ctx, "  Value: '%s'\n", path);
                debug_printf(ctx, "  Type: PATH variable\n");
                // Count PATH entries
                int count = 1;
                for (const char *p = path; *p; p++) {
                    if (*p == ':') {
                        count++;
                    }
                }
                debug_printf(ctx, "  Entries: %d\n", count);
            } else {
                debug_printf(ctx, "  Value: (unset)\n");
            }
        } else {
            debug_printf(ctx, "  Value: (unset or not found)\n");
        }
        debug_printf(ctx, "  Scope: %s\n",
                     ctx->current_frame ? ctx->current_frame->function_name
                                        : "global");
    }
}

/**
 * @brief Structure for passing callback data during variable enumeration
 */
typedef struct {
    debug_context_t *ctx; /**< Debug context for output */
    bool found_any;       /**< Flag indicating if any variables were found */
} debug_var_callback_data_t;

/**
 * @brief Callback function for variable enumeration
 * @param key Variable name
 * @param value Variable value
 * @param userdata Pointer to debug_var_callback_data_t
 */
static void debug_var_enum_callback(const char *key, const char *value,
                                    void *userdata) {
    debug_var_callback_data_t *data = (debug_var_callback_data_t *)userdata;

    if (!key || !value) {
        return;
    }

    data->found_any = true;

    // Parse the serialized value to extract just the actual value
    // Format: value|type|flags|scope_level
    char *clean_value = strdup(value);
    if (clean_value) {
        char *separator = strstr(clean_value, "|");
        if (separator) {
            *separator =
                '\0'; // Terminate at first separator to get clean value
        }

        debug_printf(data->ctx, "  %-12s = '%s'\n", key, clean_value);
        free(clean_value);
    } else {
        debug_printf(data->ctx, "  %-12s = '%s'\n", key, value);
    }
}

/**
 * @brief Inspect all variables in scope
 * @param ctx Debug context
 */
void debug_inspect_all_variables(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    debug_print_header(ctx, "Variable Inspection");

    if (!current_executor) {
        debug_printf(ctx, "No executor context available\n");
        return;
    }

    /* Single source of truth for the current scope: ask the symtable.
     * The debug frame's function_name tracks the executing command,
     * not the scope, so it would mislabel inside builtins, loops, etc. */
    const char *current_scope_name =
        symtable_current_scope_name(symtable_manager());
    debug_printf(ctx, "Current scope: %s\n",
                 current_scope_name ? current_scope_name : "global");
    debug_printf(ctx, "\n");

    // Show local variables when inside any non-global scope (function
    // body, loop body, etc.). Iterates the current scope's vars_ht
    // directly so values shadowed from outer scopes are not included.
    if (symtable_current_scope_type(symtable_manager()) != SCOPE_GLOBAL) {
        debug_printf(ctx, "Local Variables:\n");
        symtable_enumerate_current_scope_vars(symtable_manager(),
                                              debug_local_var_print_cb, ctx);
        debug_printf(ctx, "\n");
    }

    // Enumerate shell variables using callback-based approach
    debug_printf(ctx, "Shell Variables (from symbol table):\n");

    debug_var_callback_data_t callback_data = {ctx, false};

    // Enumerate global variables
    symtable_debug_enumerate_global_vars(debug_var_enum_callback,
                                         &callback_data);

    if (!callback_data.found_any) {
        debug_printf(ctx, "  (no user-defined shell variables found)\n");
    }
    debug_printf(ctx, "\n");

    // Arrays (Lists and Maps) -- not in any scope's vars_ht; lush stores
    // them in separate global array storage. Render with the
    // is_associative-derived type label and element count.
    debug_printf(ctx, "Arrays:\n");
    symtable_enumerate_arrays(debug_array_print_cb, ctx);
    debug_printf(ctx, "\n");

    // Also show commonly accessed system variables for completeness
    debug_printf(ctx, "System Variables:\n");
    const char *common_vars[] = {"PWD", "HOME",   "PATH", "USER", "SHELL", "?",
                                 "$",   "OLDPWD", "PS1",  "PS2",  NULL};
    bool found_any = false;

    for (int i = 0; common_vars[i]; i++) {
        const char *value = symtable_get_global(common_vars[i]);
        if (value) {
            debug_printf(ctx, "  %-12s = '%s'\n", common_vars[i], value);
            found_any = true;
        }
    }

    if (!found_any) {
        debug_printf(ctx, "  (no system variables found)\n");
    }
    debug_printf(ctx, "\n");

    debug_printf(ctx, "Environment Variables (first 10):\n");
    // Show a few key environment variables
    extern char **environ;
    int count = 0;
    for (char **env = environ; *env && count < 10; env++, count++) {
        char *eq = strchr(*env, '=');
        if (eq) {
            *eq = '\0';
            debug_printf(ctx, "  %-12s = '%s'\n", *env, eq + 1);
            *eq = '='; // Restore
        }
    }

    if (environ && *environ) {
        debug_printf(
            ctx,
            "\nUse 'debug print <varname>' to inspect specific variables\n");
        debug_printf(ctx, "Use 'debug stack' to see call stack and context\n");
    }
}

/**
 * @brief Add a variable to the watch list
 * @param ctx Debug context
 * @param name Variable name to watch
 */
void debug_watch_variable(debug_context_t *ctx, const char *name) {
    if (!ctx || !ctx->enabled || !name) {
        return;
    }

    // Clean variable name
    const char *clean_name = (name[0] == '$') ? name + 1 : name;

    debug_printf(ctx, "WATCH: %s\n", clean_name);

    // Resolve the current binding -- arrays carry a richer label than
    // the scope-chain scalar lookup. Either render the type alongside
    // the value, or report that the name is unbound.
    array_value_t *array = symtable_get_array(clean_name);
    if (array) {
        debug_printf(
            ctx, "  Type:  %s\n",
            debug_var_type_label(SYMVAR_ARRAY, array->is_associative));
        debug_printf(ctx, "  Count: %zu element%s\n", array->count,
                     array->count == 1 ? "" : "s");
        debug_printf(ctx, "  Variable is now being watched for changes\n");
    } else {
        char *value = symtable_get_var(symtable_manager(), clean_name);
        if (value) {
            debug_printf(ctx, "  Type:  Scalar\n");
            debug_printf(ctx, "  Value: \"%s\"\n", value);
            debug_printf(ctx, "  Variable is now being watched for changes\n");
            free(value);
        } else {
            debug_printf(ctx, "  Variable '%s' is not currently set\n",
                         clean_name);
            debug_printf(ctx, "  Will watch for when it gets assigned\n");
        }
    }

    // TODO: Implement proper watch list management
    // For now, just acknowledge the watch request
}

void debug_show_variable_type(debug_context_t *ctx, const char *name) {
    if (!ctx || !ctx->enabled || !name) {
        return;
    }

    const char *clean_name = (name[0] == '$') ? name + 1 : name;

    array_value_t *array = symtable_get_array(clean_name);
    if (array) {
        debug_printf(
            ctx, "%s: %s (%zu element%s)\n", clean_name,
            debug_var_type_label(SYMVAR_ARRAY, array->is_associative),
            array->count, array->count == 1 ? "" : "s");
        return;
    }

    char *value = symtable_get_var(symtable_manager(), clean_name);
    if (value) {
        debug_printf(ctx, "%s: Scalar\n", clean_name);
        free(value);
        return;
    }

    debug_printf(ctx, "%s: not set\n", clean_name);
}

/**
 * @brief Show variable changes since last check
 * @param ctx Debug context
 */
void debug_show_variable_changes(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    debug_printf(ctx, "Variable Changes Monitor:\n");
    debug_printf(ctx, "  (Advanced change tracking not yet implemented)\n");
    debug_printf(ctx, "  Use 'p <varname>' to check current values\n");
    debug_printf(ctx,
                 "  Use 'watch <varname>' to start monitoring a variable\n");
}
