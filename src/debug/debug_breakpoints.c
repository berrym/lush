/**
 * @file debug_breakpoints.c
 * @brief Breakpoint Management and Interactive Debugging
 *
 * Provides breakpoint creation, management, and interactive debugging
 * capabilities including step execution, condition evaluation, and
 * user input handling for the shell debugger.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "debug.h"
#include "errors.h"
#include "executor.h"
#include "lle/lle_readline.h"
#include "node.h"
#include "shell_mode.h"
#include "symtable.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/**
 * @brief Add a new breakpoint
 * @param ctx Debug context
 * @param file Source file for breakpoint
 * @param line Line number for breakpoint
 * @param condition Optional condition expression
 * @return Breakpoint ID on success, -1 on failure
 */
int debug_add_breakpoint(debug_context_t *ctx, const char *file, int line,
                         const char *condition) {
    if (!ctx || !file || line <= 0) {
        return -1;
    }

    breakpoint_t *bp = malloc(sizeof(breakpoint_t));
    if (!bp) {
        debug_printf(ctx, "ERROR: Failed to allocate breakpoint\n");
        return -1;
    }

    /// Initialize breakpoint
    bp->id = ctx->next_breakpoint_id++;
    bp->file = strdup(file);
    bp->line = line;
    bp->condition = condition ? strdup(condition) : NULL;
    bp->hit_count = 0;
    bp->enabled = true;
    bp->next = ctx->breakpoints;

    /// Add to list
    ctx->breakpoints = bp;

    debug_printf(ctx, "Breakpoint %d set at %s:%d\n", bp->id, file, line);
    if (condition) {
        debug_printf(ctx, "  Condition: %s\n", condition);
    }

    return bp->id;
}

/**
 * @brief Remove a breakpoint by ID
 * @param ctx Debug context
 * @param id Breakpoint ID to remove
 * @return true if removed, false if not found
 */
bool debug_remove_breakpoint(debug_context_t *ctx, int id) {
    if (!ctx || id <= 0) {
        return false;
    }

    breakpoint_t **current = &ctx->breakpoints;
    while (*current) {
        if ((*current)->id == id) {
            breakpoint_t *bp = *current;
            *current = bp->next;

            debug_printf(ctx, "Breakpoint %d removed from %s:%d\n", bp->id,
                         bp->file, bp->line);

            free(bp->file);
            free(bp->condition);
            free(bp);
            return true;
        }
        current = &(*current)->next;
    }

    debug_printf(ctx, "Breakpoint %d not found\n", id);
    return false;
}

/**
 * @brief Enable or disable a breakpoint
 * @param ctx Debug context
 * @param id Breakpoint ID
 * @param enable true to enable, false to disable
 * @return true if successful, false if breakpoint not found
 */
bool debug_enable_breakpoint(debug_context_t *ctx, int id, bool enable) {
    if (!ctx || id <= 0) {
        return false;
    }

    breakpoint_t *bp = ctx->breakpoints;
    while (bp) {
        if (bp->id == id) {
            bp->enabled = enable;
            debug_printf(ctx, "Breakpoint %d %s\n", id,
                         enable ? "enabled" : "disabled");
            return true;
        }
        bp = bp->next;
    }

    debug_printf(ctx, "Breakpoint %d not found\n", id);
    return false;
}

/**
 * @brief Check if execution should stop at a breakpoint
 * @param ctx Debug context
 * @param file Current source file
 * @param line Current line number
 * @return true if breakpoint hit, false otherwise
 */
bool debug_check_breakpoint(debug_context_t *ctx, const char *file, int line) {
    if (!ctx || !ctx->enabled || !file || line <= 0) {
        return false;
    }

    debug_trace_printf(ctx, "Checking breakpoint at %s:%d\n", file, line);

    breakpoint_t *bp = ctx->breakpoints;
    while (bp) {
        if (bp->enabled && bp->line == line && strcmp(bp->file, file) == 0) {
            bp->hit_count++;

            debug_trace_printf(ctx,
                               "BREAKPOINT MATCHED - entering debug mode\n");
            debug_view_begin_frame(ctx, "BREAKPOINT HIT");
            debug_view_emit_line(ctx, "Breakpoint %d at %s:%d (hit count: %d)",
                                 bp->id, file, line, bp->hit_count);

            /// Evaluate condition if present
            if (bp->condition) {
                bool condition_met =
                    debug_evaluate_condition(ctx, bp->condition);
                debug_view_emit_line(ctx, "Condition: %s -> %s", bp->condition,
                                     condition_met ? "true" : "false");
                if (!condition_met) {
                    debug_view_end_frame(ctx);
                    debug_trace_printf(ctx, "Condition not met, continuing\n");
                    return false; /// Continue execution if condition not met
                }
            }
            debug_view_end_frame(ctx);

            /// Show current context
            debug_show_context(ctx, file, line);

            /// Enter interactive debugging mode
            debug_trace_printf(ctx, "About to enter interactive debug mode\n");
            /// A breakpoint hit leaves the debugger single-stepping
            /// (step-into semantics) until the user says otherwise.
            ctx->step_mode = true;
            ctx->step_target_depth = INT_MAX;
            debug_enter_interactive_mode(ctx);
            debug_trace_printf(ctx, "Exited interactive debug mode\n");

            return true;
        }
        bp = bp->next;
    }

    /// Single-stepping stops here only when this node is not deeper than
    /// the step target -- that is what makes step-over skip nested
    /// function bodies and step-out run on to the caller.
    if (ctx->step_mode && ctx->stack_depth <= ctx->step_target_depth) {
        debug_view_begin_frame(ctx, "STEP");
        debug_view_emit_line(ctx, "At %s:%d", file, line);
        debug_view_end_frame(ctx);
        debug_show_context(ctx, file, line);
        debug_enter_interactive_mode(ctx);
        return true;
    }

    return false;
}

/**
 * @brief List all breakpoints
 * @param ctx Debug context
 */
void debug_list_breakpoints(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    debug_print_header(ctx, "Breakpoints");

    if (!ctx->breakpoints) {
        debug_printf(ctx, "  No breakpoints set\n");
        return;
    }

    breakpoint_t *bp = ctx->breakpoints;
    while (bp) {
        debug_printf(ctx, "  %d: %s:%d %s (hits: %d)\n", bp->id, bp->file,
                     bp->line, bp->enabled ? "enabled" : "disabled",
                     bp->hit_count);

        if (bp->condition) {
            debug_printf(ctx, "      Condition: %s\n", bp->condition);
        }

        bp = bp->next;
    }
}

/**
 * @brief Clear all breakpoints
 * @param ctx Debug context
 */
void debug_clear_breakpoints(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    breakpoint_t *bp = ctx->breakpoints;
    while (bp) {
        breakpoint_t *next = bp->next;
        free(bp->file);
        free(bp->condition);
        free(bp);
        bp = next;
    }

    ctx->breakpoints = NULL;
    ctx->next_breakpoint_id = 1;

    debug_printf(ctx, "All breakpoints cleared\n");
}

/**
 * @brief Step into the next statement
 * @param ctx Debug context
 */
void debug_step_into(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    ctx->mode = DEBUG_MODE_STEP;
    ctx->step_mode = true;
    /// Stop at the next node, at any depth.
    ctx->step_target_depth = INT_MAX;

    debug_printf(ctx, "Stepping into...\n");
}

/**
 * @brief Step over the next statement
 * @param ctx Debug context
 */
void debug_step_over(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    ctx->mode = DEBUG_MODE_STEP_OVER;
    ctx->step_mode = true;
    /// Stop at the next node no deeper than here -- a called function's
    /// body runs to completion without stopping.
    ctx->step_target_depth = ctx->stack_depth;

    debug_printf(ctx, "Stepping over...\n");
}

/**
 * @brief Step out of the current function
 * @param ctx Debug context
 */
void debug_step_out(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    /// Step out: keep stepping, but suppressed until the stack unwinds
    /// past the current command frame -- then stop in the caller.
    ctx->mode = DEBUG_MODE_STEP_OVER;
    ctx->step_mode = true;
    ctx->step_target_depth = ctx->stack_depth - 1;

    debug_printf(ctx, "Stepping out...\n");
}

/**
 * @brief Continue execution until next breakpoint
 * @param ctx Debug context
 */
void debug_continue(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    ctx->mode = DEBUG_MODE_CONTINUE;
    ctx->step_mode = false;

    debug_printf(ctx, "Continuing execution...\n");
}

/**
 * @brief Handle user input during interactive debugging
 * @param ctx Debug context
 * @param input User input string
 */
bool debug_handle_user_input(debug_context_t *ctx, const char *input) {
    if (!ctx || !input) {
        return false;
    }

    /// Resume execution after this command? continue / step / next /
    /// finish / quit / empty say yes; inspection commands say no.
    bool resume = false;

    /// Remove newline and trim whitespace
    char *cmd = strdup(input);
    char *newline = strchr(cmd, '\n');
    if (newline) {
        *newline = '\0';
    }

    /// Skip leading whitespace
    char *trimmed = cmd;
    while (*trimmed == ' ' || *trimmed == '\t') {
        trimmed++;
    }

    /// Handle empty input (default to continue)
    if (strlen(trimmed) == 0) {
        debug_printf(ctx, "Continuing execution...\n");
        ctx->step_mode = false;
        ctx->mode = DEBUG_MODE_CONTINUE;
        free(cmd);
        return true;
    }

    /// Parse and handle commands
    if (strcmp(trimmed, "c") == 0 || strcmp(trimmed, "continue") == 0) {
        debug_continue(ctx);
        resume = true;
    } else if (strcmp(trimmed, "s") == 0 || strcmp(trimmed, "step") == 0) {
        debug_step_into(ctx);
        resume = true;
    } else if (strcmp(trimmed, "n") == 0 || strcmp(trimmed, "next") == 0) {
        debug_step_over(ctx);
        resume = true;
    } else if (strcmp(trimmed, "f") == 0 || strcmp(trimmed, "finish") == 0) {
        debug_step_out(ctx);
        resume = true;
    } else if (strcmp(trimmed, "bt") == 0 ||
               strcmp(trimmed, "backtrace") == 0) {
        debug_show_stack(ctx);
    } else if (strcmp(trimmed, "up") == 0) {
        debug_stack_up(ctx);
    } else if (strcmp(trimmed, "down") == 0) {
        debug_stack_down(ctx);
    } else if (strcmp(trimmed, "l") == 0 || strcmp(trimmed, "list") == 0) {
        debug_list_breakpoints(ctx);
    } else if (strcmp(trimmed, "where") == 0) {
        debug_show_current_location(ctx);
    } else if (strncmp(trimmed, "p ", 2) == 0 ||
               strncmp(trimmed, "print ", 6) == 0) {
        /// Variable inspection
        char *var = trimmed + (trimmed[1] == ' ' ? 2 : 6);
        debug_inspect_variable(ctx, var);
    } else if (strncmp(trimmed, "type ", 5) == 0 ||
               strncmp(trimmed, "t ", 2) == 0) {
        /// Variable type query -- short answer (Scalar / List / Map).
        char *var = trimmed + (trimmed[1] == ' ' ? 2 : 5);
        debug_show_variable_type(ctx, var);
    } else if (strncmp(trimmed, "set ", 4) == 0) {
        /// Variable assignment
        char *assignment = trimmed + 4;
        debug_set_variable(ctx, assignment);
    } else if (strncmp(trimmed, "watch ", 6) == 0) {
        /// Add variable to watch list
        char *var = trimmed + 6;
        debug_watch_variable(ctx, var);
    } else if (strncmp(trimmed, "eval ", 5) == 0) {
        /// Evaluate expression
        char *expr = trimmed + 5;
        debug_evaluate_expression(ctx, expr);
    } else if (strcmp(trimmed, "vars") == 0) {
        debug_inspect_all_variables(ctx);
    } else if (strcmp(trimmed, "mode") == 0) {
        /// Show current shell mode
        debug_printf(ctx, "Shell mode: %s\n",
                     shell_mode_name(shell_mode_get()));
        debug_printf(ctx, "Strict mode: %s\n",
                     shell_mode_is_strict() ? "enabled" : "disabled");
    } else if (strncmp(trimmed, "mode ", 5) == 0) {
        /// Set shell mode
        char *mode_name = trimmed + 5;
        while (*mode_name == ' ')
            mode_name++; /// Skip whitespace
        shell_mode_t new_mode;
        if (strcmp(mode_name, "posix") == 0 || strcmp(mode_name, "sh") == 0) {
            new_mode = SHELL_MODE_POSIX;
        } else if (strcmp(mode_name, "bash") == 0) {
            new_mode = SHELL_MODE_BASH;
        } else if (strcmp(mode_name, "zsh") == 0) {
            new_mode = SHELL_MODE_ZSH;
        } else if (strcmp(mode_name, "lush") == 0) {
            new_mode = SHELL_MODE_LUSH;
        } else {
            debug_printf(ctx,
                         "Unknown mode: %s (valid: posix, bash, zsh, lush)\n",
                         mode_name);
            free(cmd);
            return false;
        }
        if (shell_mode_set(new_mode)) {
            debug_printf(ctx, "Shell mode set to: %s\n",
                         shell_mode_name(new_mode));
        } else {
            debug_printf(ctx, "Cannot change mode (strict mode enabled)\n");
        }
    } else if (strcmp(trimmed, "features") == 0) {
        /// List all features and their status
        debug_printf(ctx, "Shell features (mode: %s):\n",
                     shell_mode_name(shell_mode_get()));
        for (int i = 0; i < FEATURE_COUNT; i++) {
            const char *name = shell_feature_name((shell_feature_t)i);
            bool enabled = shell_mode_allows((shell_feature_t)i);
            debug_printf(ctx, "  %s: %s\n", name, enabled ? "ON" : "OFF");
        }
    } else if (strncmp(trimmed, "feature ", 8) == 0) {
        /// Query or toggle a specific feature
        char *feat_name = trimmed + 8;
        while (*feat_name == ' ')
            feat_name++;
        shell_feature_t feat;
        bool invert = false;
        if (shell_feature_parse(feat_name, &feat, &invert)) {
            bool effective = shell_mode_allows(feat) ^ invert;
            debug_printf(ctx, "Feature '%s': %s\n", feat_name,
                         effective ? "ON" : "OFF");
        } else {
            debug_printf(ctx, "Unknown feature: %s\n", feat_name);
        }
    } else if (strcmp(trimmed, "help") == 0 || strcmp(trimmed, "h") == 0) {
        debug_print_help(ctx);
    } else if (strcmp(trimmed, "q") == 0 || strcmp(trimmed, "quit") == 0) {
        debug_printf(ctx, "Continuing execution...\n");
        ctx->step_mode = false;
        ctx->mode = DEBUG_MODE_CONTINUE;
        resume = true;
    } else {
        debug_printf(ctx, "Unknown command: '%s' (type 'help' for commands)\n",
                     trimmed);
    }

    free(cmd);
    return resume;
}

/**
 * @brief Navigate up one stack frame
 * @param ctx Debug context
 */
void debug_stack_up(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    debug_printf(ctx, "Stack navigation not yet implemented\n");
    /// TODO: Implement stack frame navigation
}

/**
 * @brief Navigate down one stack frame
 * @param ctx Debug context
 */
void debug_stack_down(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    debug_printf(ctx, "Stack navigation not yet implemented\n");
    /// TODO: Implement stack frame navigation
}

/**
 * @brief Show the current execution location
 * @param ctx Debug context
 */
void debug_show_current_location(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->current_frame) {
        debug_printf(ctx, "Current location: %s",
                     ctx->current_frame->function_name);
        if (ctx->current_frame->file_path) {
            debug_printf(ctx, " at %s:%d\n", ctx->current_frame->file_path,
                         ctx->current_frame->line_number);
        } else {
            debug_printf(ctx, "\n");
        }
        debug_show_stack(ctx);
    } else {
        debug_printf(ctx, "No current execution context\n");
    }
}

/**
 * @brief Set a variable value during debugging
 * @param ctx Debug context
 * @param assignment Variable assignment string (name=value)
 */
void debug_set_variable(debug_context_t *ctx, const char *assignment) {
    if (!ctx || !assignment) {
        return;
    }

    debug_printf(ctx, "Variable assignment: %s\n", assignment);
    debug_printf(ctx, "(Variable setting not yet implemented)\n");
    /// TODO: Implement variable assignment
}

/**
 * @brief Evaluate an expression during debugging
 * @param ctx Debug context
 * @param expression Expression to evaluate
 */
void debug_evaluate_expression(debug_context_t *ctx, const char *expression) {
    if (!ctx || !expression) {
        return;
    }

    debug_printf(ctx, "Evaluating: %s\n", expression);
    debug_printf(ctx, "(Expression evaluation not yet implemented)\n");
    /// TODO: Implement expression evaluator
}

/**
 * @brief Show source context around current line
 * @param ctx Debug context
 * @param file Source file name
 * @param line Current line number
 */
void debug_show_context(debug_context_t *ctx, const char *file, int line) {
    if (!ctx || !file) {
        return;
    }

    debug_printf(ctx, "\nContext at %s:%d:\n", file, line);

    /// Try to read and show file context
    FILE *f = fopen(file, "r");
    if (f) {
        char buffer[256];
        int current_line = 1;
        int start_line = (line > 3) ? line - 2 : 1;
        int end_line = line + 2;

        while (fgets(buffer, sizeof(buffer), f) && current_line <= end_line) {
            if (current_line >= start_line) {
                char marker = (current_line == line) ? '>' : ' ';
                debug_printf(ctx, "  %c %3d: %s", marker, current_line, buffer);
            }
            current_line++;
        }
        fclose(f);
    } else {
        debug_printf(ctx, "  (source file not available)\n");
    }

    /// Show current stack frame info
    if (ctx->current_frame) {
        debug_printf(ctx, "\nCurrent function: %s\n",
                     ctx->current_frame->function_name);
    }
}

/**
 * @brief Enter interactive debugging mode
 * @param ctx Debug context
 */
void debug_enter_interactive_mode(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    debug_printf(
        ctx, "\nEntering interactive debug mode. Type 'help' for commands.\n");

    /// Show loop context if in loop
    if (ctx->execution_context.in_loop &&
        ctx->execution_context.loop_variable) {
        debug_printf(ctx, "Currently in loop: variable '%s' = '%s'\n",
                     ctx->execution_context.loop_variable,
                     ctx->execution_context.loop_variable_value ?: "unknown");
    }

    /// Show available commands
    debug_printf(
        ctx,
        "Common commands: c/continue, s/step, n/next, vars, help, q/quit\n");

    /// Read commands through LLE -- the single owner of terminal and
    /// user interaction. This is a sequential lle_readline() call: the
    /// primary prompt's readline returned long ago (the break fires
    /// during executor execution), so it is a normal supported use,
    /// not a re-entrant one. LLE owns the terminal lifecycle, so there
    /// is no fgets/termios contention with the line editor -- the bug
    /// that froze sessions. lle_readline() returns NULL on EOF,
    /// interrupt, or when LLE is unavailable (e.g. a non-interactive
    /// run with no controlling terminal); in every such case the right
    /// move is to stop prompting and let execution continue.
    fflush(ctx->debug_output);
    /// Read commands until one resumes execution. Inspection commands
    /// (vars, print, backtrace, ...) loop back for another prompt;
    /// continue / step / next / finish / quit exit so the executor
    /// proceeds. EOF -- no controlling terminal, or Ctrl-D -- is
    /// treated as continue.
    for (;;) {
        char *line = lle_readline_no_history("(lush-debug) ");
        if (!line) {
            debug_printf(ctx, "\nContinuing execution...\n");
            ctx->step_mode = false;
            break;
        }
        bool resume = debug_handle_user_input(ctx, line);
        free(line);
        if (resume) {
            break;
        }
    }

    debug_printf(ctx, "Exited interactive debug mode\n");
    /// Don't clean up execution context here - let the loop manage its own
    /// context
}

/**
 * @brief Evaluate a breakpoint condition
 * @param ctx Debug context
 * @param condition Condition expression to evaluate
 * @return true if condition is met, false otherwise
 */
bool debug_evaluate_condition(debug_context_t *ctx, const char *condition) {
    if (!ctx || !condition) {
        return true; /// No condition means always true
    }

    /// Simple condition evaluation for now
    /// TODO: Implement full expression evaluator

    /// Handle simple variable comparisons
    if (strstr(condition, "==") || strstr(condition, "!=") ||
        strstr(condition, "<") || strstr(condition, ">")) {
        /// For now, just return true for any comparison
        /// In a full implementation, this would parse and evaluate the
        /// expression
        return true;
    }

    /// Handle variable existence checks
    if (condition[0] == '$') {
        /// Check if variable exists
        /// TODO: Implement variable lookup
        return true;
    }

    return true; /// Default to true
}

/**
 * @brief Print debug help information
 * @param ctx Debug context
 */
void debug_print_help(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    debug_print_header(ctx, "Interactive Debug Commands");
    debug_printf(
        ctx, "  c, continue    - Continue execution until next breakpoint\n");
    debug_printf(
        ctx, "  s, step        - Step into next statement (enter functions)\n");
    debug_printf(
        ctx, "  n, next        - Step over next statement (skip functions)\n");
    debug_printf(ctx, "  f, finish      - Step out of current function\n");
    debug_printf(ctx, "  bt, backtrace  - Show call stack\n");
    debug_printf(ctx, "  up             - Move up one stack frame\n");
    debug_printf(ctx, "  down           - Move down one stack frame\n");
    debug_printf(ctx, "  l, list        - List breakpoints\n");
    debug_printf(ctx, "  p <var>        - Print variable value\n");
    debug_printf(ctx, "  set <var>=<val> - Set variable value\n");
    debug_printf(ctx, "  vars           - Show all variables in scope\n");
    debug_printf(ctx, "  watch <var>    - Add variable to watch list\n");
    debug_printf(ctx, "  eval <expr>    - Evaluate expression\n");
    debug_printf(ctx, "  where          - Show current location and context\n");
    debug_printf(ctx, "\n");
    debug_printf(ctx, "Shell Mode Commands:\n");
    debug_printf(ctx, "  mode           - Show current shell mode\n");
    debug_printf(
        ctx, "  mode <name>    - Set shell mode (posix, bash, zsh, lush)\n");
    debug_printf(ctx,
                 "  features       - List all features and their status\n");
    debug_printf(ctx, "  feature <name> - Query a specific feature\n");
    debug_printf(ctx, "\n");
    debug_printf(ctx, "  h, help        - Show this help\n");
    debug_printf(ctx, "  q, quit        - Exit debug mode and continue\n");
    debug_printf(ctx, "\nTip: Use Tab for command completion\n");
}

/**
 * @brief Save execution context for loop debugging
 * @param ctx Debug context
 * @param executor Current executor instance
 * @param node Current AST node
 */
void debug_save_execution_context(debug_context_t *ctx, executor_t *executor,
                                  node_t *node) {
    if (!ctx || !executor || !node) {
        debug_trace_printf(ctx,
                           "debug_save_execution_context: NULL parameter - "
                           "ctx=%p executor=%p node=%p\n",
                           (void *)ctx, (void *)executor, (void *)node);
        return;
    }

    debug_trace_printf(
        ctx, "debug_save_execution_context: Called with node type %d\n",
        node->type);

    /// Detect if we're in a loop
    ctx->execution_context.in_loop =
        (node->type == NODE_FOR || node->type == NODE_WHILE ||
         node->type == NODE_UNTIL);

    if (ctx->execution_context.in_loop) {
        debug_trace_printf(
            ctx, "Saving loop execution context - node type: %d\n", node->type);

        /// Save loop-specific context
        if (node->type == NODE_FOR) {
            /// For now, we'll detect the loop variable from execution context
            /// This is a simplified approach until we can properly parse the
            /// AST
            debug_trace_printf(ctx, "FOR loop detected, saving context\n");
        }

        /// Save reference to loop AST node
        ctx->execution_context.loop_node = node;
    } else {
        debug_trace_printf(ctx, "Not a loop node - type: %d\n", node->type);
    }
}

/**
 * @brief Restore execution context after debugging
 * @param ctx Debug context
 * @param executor Current executor instance
 * @param node Current AST node
 */
void debug_restore_execution_context(debug_context_t *ctx, executor_t *executor,
                                     node_t *node) {
    (void)executor; /// Reserved for execution state restoration
    (void)node;     /// Reserved for node-specific restoration
    if (!ctx) {
        return;
    }

    if (!ctx->execution_context.in_loop) {
        debug_trace_printf(
            ctx, "debug_restore_execution_context: Not in loop context\n");
        return;
    }

    debug_trace_printf(ctx, "Restoring loop execution context\n");

    /// For now, just log that we're restoring context
    /// TODO: Full variable restoration
    debug_trace_printf(ctx, "Loop context restoration (placeholder)\n");
}

/**
 * @brief Clean up execution context
 * @param ctx Debug context
 */
void debug_cleanup_execution_context(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->execution_context.in_loop) {
        debug_trace_printf(ctx, "Cleaning up loop execution context\n");
    }

    free(ctx->execution_context.loop_variable);
    free(ctx->execution_context.loop_variable_value);
    ctx->execution_context.loop_variable = NULL;
    ctx->execution_context.loop_variable_value = NULL;
    ctx->execution_context.in_loop = false;
    ctx->execution_context.loop_node = NULL;
    ctx->execution_context.loop_iteration = 0;
    ctx->execution_context.loop_body_start_line = 0;
}

/**
 * @brief Enter a loop for debugging tracking
 * @param ctx Debug context
 * @param loop_type Type of loop (for, while, until)
 * @param variable Loop variable name
 * @param value Current loop variable value
 */
void debug_enter_loop(debug_context_t *ctx, const char *loop_type,
                      const char *variable, const char *value) {
    if (!ctx || !ctx->enabled) {
        return;
    }

    debug_trace_printf(ctx, "Entering %s loop with variable '%s' = '%s'\n",
                       loop_type ? loop_type : "unknown",
                       variable ? variable : "unknown",
                       value ? value : "unknown");

    ctx->execution_context.in_loop = true;
    /// Save the current line as the loop body start line (will be set to body
    /// start on first iteration)
    ctx->execution_context.loop_body_start_line =
        0; /// Will be set when we hit the first loop body statement

    /// Save loop variable information
    free(ctx->execution_context.loop_variable);
    free(ctx->execution_context.loop_variable_value);

    ctx->execution_context.loop_variable = variable ? strdup(variable) : NULL;
    ctx->execution_context.loop_variable_value = value ? strdup(value) : NULL;
    ctx->execution_context.loop_iteration = 0;
}

/**
 * @brief Update the loop variable during iteration
 * @param ctx Debug context
 * @param variable Variable name
 * @param value New variable value
 */
void debug_update_loop_variable(debug_context_t *ctx, const char *variable,
                                const char *value) {
    if (!ctx || !ctx->enabled || !ctx->execution_context.in_loop) {
        return;
    }

    debug_trace_printf(ctx, "Loop variable update: '%s' = '%s'\n",
                       variable ? variable : "unknown",
                       value ? value : "unknown");

    /// Update the stored loop variable value
    free(ctx->execution_context.loop_variable_value);
    ctx->execution_context.loop_variable_value = value ? strdup(value) : NULL;
    ctx->execution_context.loop_iteration++;
}

/**
 * @brief Exit a loop for debugging tracking
 * @param ctx Debug context
 */
void debug_exit_loop(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled || !ctx->execution_context.in_loop) {
        return;
    }

    debug_trace_printf(ctx, "Exiting loop after %d iterations\n",
                       ctx->execution_context.loop_iteration);

    /// Clean up loop context
    ctx->execution_context.in_loop = false;
    ctx->execution_context.loop_body_start_line = 0;
    free(ctx->execution_context.loop_variable);
    free(ctx->execution_context.loop_variable_value);
    ctx->execution_context.loop_variable = NULL;
    ctx->execution_context.loop_variable_value = NULL;
    ctx->execution_context.loop_iteration = 0;
    ctx->execution_context.loop_node = NULL;
}
