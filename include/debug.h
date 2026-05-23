/**
 * @file debug.h
 * @brief Shell debugging and profiling infrastructure
 *
 * Provides comprehensive debugging facilities including step execution,
 * breakpoints, variable inspection, profiling, and execution tracing.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef DEBUG_H
#define DEBUG_H

#include "node.h"
#include "symtable.h"

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

// Forward declarations
struct executor;
typedef struct executor executor_t;

/**
 * @brief Debug levels for different types of debugging information
 */
typedef enum {
    DEBUG_NONE = 0,    /**< No debugging output */
    DEBUG_BASIC = 1,   /**< Basic debugging output */
    DEBUG_VERBOSE = 2, /**< Verbose debugging output */
    DEBUG_TRACE = 3,   /**< Trace-level debugging output */
    DEBUG_PROFILE = 4  /**< Profiling information */
} debug_level_t;

/**
 * @brief Debug execution modes
 */
typedef enum {
    DEBUG_MODE_NORMAL,    /**< Normal execution */
    DEBUG_MODE_STEP,      /**< Step-by-step execution */
    DEBUG_MODE_STEP_OVER, /**< Step over function calls */
    DEBUG_MODE_CONTINUE   /**< Continue to next breakpoint */
} debug_mode_t;

/**
 * @brief Breakpoint structure
 *
 * Represents a debugger breakpoint with optional condition.
 */
typedef struct breakpoint {
    int id;                  /**< Unique breakpoint ID */
    char *file;              /**< Source file name */
    int line;                /**< Line number */
    char *condition;         /**< Optional condition expression */
    int hit_count;           /**< Number of times hit */
    bool enabled;            /**< Whether breakpoint is enabled */
    struct breakpoint *next; /**< Next breakpoint in list */
} breakpoint_t;

/**
 * @brief Debug stack frame for tracking execution context
 */
typedef struct debug_frame {
    char *function_name;        /**< Name of current function */
    char *file_path;            /**< Path to source file */
    int line_number;            /**< Current line number */
    node_t *current_node;       /**< Current AST node */
    struct debug_frame *parent; /**< Parent frame */
    struct timespec start_time; /**< Frame start time */
    struct timespec end_time;   /**< Frame end time */
    /**
     * Scoping discipline of this frame. True for typed-function (`fn`)
     * frames whose body resolves free names through a captured
     * declaration-site scope; false for POSIX-form functions and
     * top-level frames whose lookup walks the dynamic call chain.
     * Rendered as `[lexical]` / `[dynamic]` in `debug stack` per
     * PHILOSOPHY section 7. Set at debug_push_frame time.
     */
    bool is_lexical;
} debug_frame_t;

/**
 * @brief Performance profiling data
 */
typedef struct profile_data {
    char *function_name;       /**< Function name */
    char *file_path;           /**< Source file path */
    long total_time_ns;        /**< Total time in nanoseconds */
    int call_count;            /**< Number of calls */
    long min_time_ns;          /**< Minimum call time */
    long max_time_ns;          /**< Maximum call time */
    struct profile_data *next; /**< Next profile entry */
} profile_data_t;

/**
 * @brief Script analysis issue
 */
typedef struct analysis_issue {
    char *file_path;  /**< File with issue */
    int line_number;  /**< Line number of issue */
    char *severity;   /**< "error", "warning", "info" */
    char *category;   /**< "syntax", "performance", "security", "style" */
    char *message;    /**< Issue description */
    char *suggestion; /**< Suggested fix */
    struct analysis_issue *next; /**< Next issue */
} analysis_issue_t;

/**
 * @brief Main debug context structure
 *
 * Contains all debugging state and configuration.
 */
typedef struct debug_context {
    debug_level_t level;   /**< Current debug level */
    debug_mode_t mode;     /**< Current execution mode */
    bool enabled;          /**< Debug mode enabled */
    bool step_mode;        /**< Single-step mode active */
    bool trace_execution;  /**< Trace execution enabled */
    bool profile_enabled;  /**< Profiling enabled */
    bool analysis_enabled; /**< Script analysis enabled */

    // Execution state
    debug_frame_t *current_frame; /**< Current stack frame */
    int stack_depth;              /**< Current stack depth */
    int step_target_depth;        /**< Single-stepping stops only when
                                       stack_depth <= this. INT_MAX for
                                       step-into (stop everywhere), the
                                       current depth for step-over, and
                                       depth-1 for step-out. */

    // Execution context preservation (for loop debugging fix)
    struct {
        bool in_loop;              /**< Currently inside a loop */
        char *loop_variable;       /**< Current loop variable name */
        char *loop_variable_value; /**< Current iteration value */
        int loop_iteration;        /**< Current iteration number */
        node_t *loop_node;         /**< AST node of current loop */
        int loop_body_start_line;  /**< Line number where loop body starts */
    } execution_context;           /**< Execution context for loop debugging */

    // Breakpoints
    breakpoint_t *breakpoints; /**< List of breakpoints */
    int next_breakpoint_id;    /**< Next breakpoint ID to assign */

    // Profiling
    profile_data_t *profile_data; /**< Profiling data */
    bool timing_enabled;          /**< Timing collection enabled */

    // Analysis
    analysis_issue_t *analysis_issues; /**< List of analysis issues */
    int issue_count;                   /**< Number of issues found */

    // Output control
    FILE *debug_output;    /**< Debug output stream */
    FILE *profile_output;  /**< Profile output stream */
    FILE *analysis_output; /**< Analysis output stream */

    // Configuration
    bool show_variables;   /**< Show variable values */
    bool show_stack_trace; /**< Show stack traces */
    bool show_timing;      /**< Show timing information */
    bool highlight_syntax; /**< Syntax highlighting in output */
    int max_stack_depth;   /**< Maximum stack depth to display */

    // Statistics
    long total_commands;           /**< Total commands executed */
    long total_time_ns;            /**< Total execution time */
    struct timespec session_start; /**< Session start time */
} debug_context_t;

/** @brief Global debug context */
extern debug_context_t *g_debug_context;

/* ============================================================================
 * Core Debug System Functions
 * ============================================================================
 */

/**
 * @brief Initialize the debug system
 *
 * Creates and initializes a new debug context.
 *
 * @return New debug context or NULL on failure
 */
debug_context_t *debug_init(void);

/**
 * @brief Clean up the debug system
 *
 * Frees all resources associated with the debug context.
 *
 * @param ctx Debug context to clean up
 */
void debug_cleanup(debug_context_t *ctx);

/**
 * @brief Set the debug level
 *
 * @param ctx Debug context
 * @param level New debug level
 */
void debug_set_level(debug_context_t *ctx, debug_level_t level);

/**
 * @brief Set the debug execution mode
 *
 * @param ctx Debug context
 * @param mode New execution mode
 */
void debug_set_mode(debug_context_t *ctx, debug_mode_t mode);

/**
 * @brief Enable or disable debugging
 *
 * @param ctx Debug context
 * @param enable True to enable, false to disable
 */
void debug_enable(debug_context_t *ctx, bool enable);

/* ============================================================================
 * Execution Tracing
 * ============================================================================
 */

/**
 * @brief Trace an AST node execution
 *
 * @param ctx Debug context
 * @param node Node being executed
 * @param file Source file name
 * @param line Line number
 */
void debug_trace_node(debug_context_t *ctx, node_t *node, const char *file,
                      int line);

/**
 * @brief Trace a command execution
 *
 * @param ctx Debug context
 * @param command Command name
 * @param argv Command arguments
 * @param argc Argument count
 */
void debug_trace_command(debug_context_t *ctx, const char *command, char **argv,
                         int argc);

/**
 * @brief Trace a builtin command execution
 *
 * @param ctx Debug context
 * @param builtin Builtin command name
 * @param argv Command arguments
 * @param argc Argument count
 */
void debug_trace_builtin(debug_context_t *ctx, const char *builtin, char **argv,
                         int argc);

/**
 * @brief Trace a function call
 *
 * @param ctx Debug context
 * @param function Function name
 * @param argv Function arguments
 * @param argc Argument count
 */
void debug_trace_function_call(debug_context_t *ctx, const char *function,
                               char **argv, int argc);

/* ============================================================================
 * Stack Frame Management
 * ============================================================================
 */

/**
 * @brief Push a new stack frame
 *
 * @param ctx Debug context
 * @param function Function name
 * @param file Source file name
 * @param line Line number
 * @return New frame or NULL on failure
 */
debug_frame_t *debug_push_frame(debug_context_t *ctx, const char *function,
                                const char *file, int line);

/**
 * @brief Mark the current frame as lexically scoped.
 *
 * Called by the typed-function executor right after pushing the
 * frame for a `fn` call. The `debug stack` renderer reads this flag
 * to annotate the frame with `[lexical]` instead of the default
 * `[dynamic]`, so a debugger user can see at a glance which scoping
 * discipline each frame is using (PHILOSOPHY section 7).
 *
 * @param ctx Debug context (no-op if NULL or no current frame)
 */
void debug_mark_current_frame_lexical(debug_context_t *ctx);

/**
 * @brief Pop the current stack frame
 *
 * @param ctx Debug context
 */
void debug_pop_frame(debug_context_t *ctx);

/**
 * @brief Update the current frame's AST node
 *
 * @param ctx Debug context
 * @param node New current node
 */
void debug_update_frame_node(debug_context_t *ctx, node_t *node);

/**
 * @brief Display the current call stack
 *
 * @param ctx Debug context
 */
void debug_show_stack(debug_context_t *ctx);

/* ============================================================================
 * Breakpoint Management
 * ============================================================================
 */

/**
 * @brief Add a breakpoint
 *
 * @param ctx Debug context
 * @param file Source file name
 * @param line Line number
 * @param condition Optional condition expression (NULL for unconditional)
 * @return Breakpoint ID or -1 on failure
 */
int debug_add_breakpoint(debug_context_t *ctx, const char *file, int line,
                         const char *condition);

/**
 * @brief Remove a breakpoint by ID
 *
 * @param ctx Debug context
 * @param id Breakpoint ID
 * @return true if removed, false if not found
 */
bool debug_remove_breakpoint(debug_context_t *ctx, int id);

/**
 * @brief Enable or disable a breakpoint
 *
 * @param ctx Debug context
 * @param id Breakpoint ID
 * @param enable True to enable, false to disable
 * @return true on success, false if not found
 */
bool debug_enable_breakpoint(debug_context_t *ctx, int id, bool enable);

/**
 * @brief Check if a breakpoint should trigger
 *
 * @param ctx Debug context
 * @param file Current file
 * @param line Current line
 * @return true if breakpoint hit, false otherwise
 */
bool debug_check_breakpoint(debug_context_t *ctx, const char *file, int line);

/**
 * @brief List all breakpoints
 *
 * @param ctx Debug context
 */
void debug_list_breakpoints(debug_context_t *ctx);

/**
 * @brief Clear all breakpoints
 *
 * @param ctx Debug context
 */
void debug_clear_breakpoints(debug_context_t *ctx);

/* ============================================================================
 * Variable Inspection
 * ============================================================================
 */

/**
 * @brief Inspect a single variable
 *
 * @param ctx Debug context
 * @param name Variable name
 */
void debug_inspect_variable(debug_context_t *ctx, const char *name);

/**
 * @brief Inspect all variables in scope
 *
 * @param ctx Debug context
 */
void debug_inspect_all_variables(debug_context_t *ctx);

/**
 * @brief Show only the user-facing type of a variable
 *
 * Resolves the variable in the same order as debug_inspect_variable
 * (array store first, then the scope chain) and prints just the
 * Scalar / List / Map / Func / Nameref label per SEMANTICS.md, with
 * the element count for arrays. Reports "not set" when no binding is
 * found in any reachable scope or the array store. For the
 * (lush-debug) prompt's `type <name>` / `t <name>` command.
 *
 * @param ctx Debug context
 * @param name Variable name (with or without $ prefix)
 */
void debug_show_variable_type(debug_context_t *ctx, const char *name);

/**
 * @brief Add a variable to the watch list
 *
 * @param ctx Debug context
 * @param name Variable name to watch
 */
void debug_watch_variable(debug_context_t *ctx, const char *name);

/**
 * @brief Show changes to watched variables
 *
 * @param ctx Debug context
 */
void debug_show_variable_changes(debug_context_t *ctx);

/* ============================================================================
 * Step Execution
 * ============================================================================
 */

/**
 * @brief Step into the next instruction
 *
 * @param ctx Debug context
 */
void debug_step_into(debug_context_t *ctx);

/**
 * @brief Step over the next function call
 *
 * @param ctx Debug context
 */
void debug_step_over(debug_context_t *ctx);

/**
 * @brief Step out of the current function
 *
 * @param ctx Debug context
 */
void debug_step_out(debug_context_t *ctx);

/**
 * @brief Continue execution to next breakpoint
 *
 * @param ctx Debug context
 */
void debug_continue(debug_context_t *ctx);

/* ============================================================================
 * Performance Profiling
 * ============================================================================
 */

/**
 * @brief Start profiling
 *
 * @param ctx Debug context
 */
void debug_profile_start(debug_context_t *ctx);

/**
 * @brief Stop profiling
 *
 * @param ctx Debug context
 */
void debug_profile_stop(debug_context_t *ctx);

/**
 * @brief Record function entry for profiling
 *
 * @param ctx Debug context
 * @param function Function name
 */
void debug_profile_function_enter(debug_context_t *ctx, const char *function);

/**
 * @brief Record function exit for profiling
 *
 * @param ctx Debug context
 * @param function Function name
 */
void debug_profile_function_exit(debug_context_t *ctx, const char *function);

/**
 * @brief Generate profiling report
 *
 * @param ctx Debug context
 */
void debug_profile_report(debug_context_t *ctx);

/**
 * @brief Reset profiling data
 *
 * @param ctx Debug context
 */
void debug_profile_reset(debug_context_t *ctx);

/* ============================================================================
 * Script Analysis
 * ============================================================================
 */

/**
 * @brief Analysis mode for script checking
 *
 * Determines what level of output and functionality is available.
 */
typedef enum {
    ANALYSIS_MODE_FULL, /**< Full analysis: info + warnings + errors */
    ANALYSIS_MODE_LINT, /**< Lint mode: warnings + errors only, supports fixes
                         */
} analysis_mode_t;

/**
 * @brief Analyze a script for issues (full mode)
 *
 * Performs full analysis including info, warnings, and errors.
 * Use this for understanding a script's characteristics.
 *
 * @param ctx Debug context
 * @param script_path Path to script file
 */
void debug_analyze_script(debug_context_t *ctx, const char *script_path);

/**
 * @brief Lint a script for actionable issues
 *
 * Performs lint analysis showing only warnings and errors.
 * Optionally applies automatic fixes.
 *
 * @param ctx Debug context
 * @param script_path Path to script file
 * @param fix Apply safe fixes
 * @param unsafe_fixes Also apply unsafe fixes (requires fix=true)
 * @param dry_run Preview fixes without applying
 * @return Number of unfixed issues remaining
 */
int debug_lint_script(debug_context_t *ctx, const char *script_path, bool fix,
                      bool unsafe_fixes, bool dry_run);

/**
 * @brief Add an analysis issue
 *
 * @param ctx Debug context
 * @param file Source file
 * @param line Line number
 * @param severity Issue severity
 * @param category Issue category
 * @param message Issue message
 * @param suggestion Suggested fix
 */
void debug_add_analysis_issue(debug_context_t *ctx, const char *file, int line,
                              const char *severity, const char *category,
                              const char *message, const char *suggestion);

/**
 * @brief Display analysis report
 *
 * @param ctx Debug context
 */
void debug_show_analysis_report(debug_context_t *ctx);

/**
 * @brief Display analysis report with mode filtering
 *
 * @param ctx Debug context
 * @param mode Analysis mode (FULL shows all, LINT filters info items)
 */
void debug_show_analysis_report_filtered(debug_context_t *ctx,
                                         analysis_mode_t mode);

/**
 * @brief Clear all analysis issues
 *
 * @param ctx Debug context
 */
void debug_clear_analysis_issues(debug_context_t *ctx);

/* ============================================================================
 * Function Introspection
 * ============================================================================
 */

/**
 * @brief List all defined functions
 *
 * @param ctx Debug context
 */
void debug_list_functions(debug_context_t *ctx);

/**
 * @brief Show details of a function
 *
 * @param ctx Debug context
 * @param function_name Function name
 */
void debug_show_function(debug_context_t *ctx, const char *function_name);

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

/**
 * @brief Print an AST node with indentation
 *
 * @param ctx Debug context
 * @param node Node to print
 * @param indent Indentation level
 */
void debug_print_node(debug_context_t *ctx, node_t *node, int indent);

/**
 * @brief Print an entire AST
 *
 * @param ctx Debug context
 * @param ast Root of AST
 */
void debug_print_ast(debug_context_t *ctx, node_t *ast);

/**
 * @brief Get a description of a node type
 *
 * @param node Node to describe
 * @return Description string
 */
char *debug_get_node_description(node_t *node);

/**
 * @brief Get current time in nanoseconds
 *
 * @return Time in nanoseconds
 */
long debug_get_time_ns(void);

/**
 * @brief Format a time value for display
 *
 * @param ns Time in nanoseconds
 * @param buffer Output buffer
 * @param size Buffer size
 */
void debug_format_time(long ns, char *buffer, size_t size);

/* ============================================================================
 * Output Functions
 * ============================================================================
 */

/**
 * @brief Print user-facing debugger output
 *
 * Indented by current stack depth; no per-line tag. For the interactive
 * debugger UI -- banners, hit messages, the inspection output the user
 * is actually reading. Internal engine trace uses debug_trace_printf().
 *
 * @param ctx Debug context
 * @param format printf-style format string
 * @param ... Format arguments
 */
void debug_printf(debug_context_t *ctx, const char *format, ...);

/**
 * @brief Print internal engine trace, gated on debug level
 *
 * For diagnostic chatter the debugger emits about its own operation --
 * "checking breakpoint", "saving loop context", per-iteration variable
 * updates. Silent unless ctx->level >= DEBUG_TRACE, so normal debugging
 * is not polluted. Output is otherwise identical to debug_printf:
 * indented by stack depth, no per-line tag.
 *
 * @param ctx Debug context
 * @param format printf-style format string
 * @param ... Format arguments
 */
void debug_trace_printf(debug_context_t *ctx, const char *format, ...);

/**
 * @brief Emit one user-facing debugger line with a left gutter
 *
 * Prepends the gutter glyph (UTF-8 "|" or ASCII "|") to make debugger
 * output visually distinct from intermingled script output. Used for
 * breakpoint banners, step indicators, and the interior lines of
 * framed blocks (those are bracketed by debug_view_begin_frame /
 * debug_view_end_frame).
 *
 * @param ctx Debug context
 * @param format printf-style format string (no trailing newline needed)
 * @param ... Format arguments
 */
void debug_view_emit_line(debug_context_t *ctx, const char *format, ...);

/**
 * @brief Open a framed block with an optional title
 *
 * Writes the top border: `+- [title] ---...` (ASCII) or `┌─ [title] ─...`
 * (UTF-8). Pair with debug_view_end_frame.
 *
 * @param ctx Debug context
 * @param title Title to embed in the border, or NULL/"" for an
 *              unlabeled frame.
 */
void debug_view_begin_frame(debug_context_t *ctx, const char *title);

/**
 * @brief Close a framed block opened with debug_view_begin_frame
 *
 * @param ctx Debug context
 */
void debug_view_end_frame(debug_context_t *ctx);

/**
 * @brief Print a separator line
 *
 * @param ctx Debug context
 */
void debug_print_separator(debug_context_t *ctx);

/**
 * @brief Print a section header
 *
 * @param ctx Debug context
 * @param title Header title
 */
void debug_print_header(debug_context_t *ctx, const char *title);

/* ============================================================================
 * Configuration
 * ============================================================================
 */

/**
 * @brief Set debug output file
 *
 * @param ctx Debug context
 * @param filename Output file path
 */
void debug_set_output_file(debug_context_t *ctx, const char *filename);

/**
 * @brief Set profile output file
 *
 * @param ctx Debug context
 * @param filename Output file path
 */
void debug_set_profile_output_file(debug_context_t *ctx, const char *filename);

/**
 * @brief Set analysis output file
 *
 * @param ctx Debug context
 * @param filename Output file path
 */
void debug_set_analysis_output_file(debug_context_t *ctx, const char *filename);

/* ============================================================================
 * Integration Macros
 * ============================================================================
 */

/** @brief Trace AST node execution if debugging enabled */
#define DEBUG_TRACE_NODE(node, file, line)                                     \
    if (g_debug_context && g_debug_context->enabled) {                         \
        debug_trace_node(g_debug_context, node, file, line);                   \
    }

/** @brief Trace command execution if debugging enabled */
#define DEBUG_TRACE_COMMAND(cmd, argv, argc)                                   \
    if (g_debug_context && g_debug_context->enabled) {                         \
        debug_trace_command(g_debug_context, cmd, argv, argc);                 \
    }

/** @brief Record function entry for profiling if enabled */
#define DEBUG_PROFILE_ENTER(func)                                              \
    if (g_debug_context && g_debug_context->profile_enabled) {                 \
        debug_profile_function_enter(g_debug_context, func);                   \
    }

/** @brief Record function exit for profiling if enabled */
#define DEBUG_PROFILE_EXIT(func)                                               \
    if (g_debug_context && g_debug_context->profile_enabled) {                 \
        debug_profile_function_exit(g_debug_context, func);                    \
    }

/** @brief Check breakpoint if debugging enabled */
#define DEBUG_BREAKPOINT_CHECK(file, line)                                     \
    if (g_debug_context && g_debug_context->enabled) {                         \
        debug_check_breakpoint(g_debug_context, file, line);                   \
    }

/* ============================================================================
 * User Interaction Functions
 * ============================================================================
 */

/**
 * @brief Handle one user debug command at the interactive break prompt
 *
 * @param ctx Debug context
 * @param input User input string
 * @return true if the command resumes execution (continue / step / next /
 *         finish / quit / empty), so the prompt loop should exit; false
 *         for inspection commands, so the loop should prompt again.
 */
bool debug_handle_user_input(debug_context_t *ctx, const char *input);

/**
 * @brief Print debug help information
 *
 * @param ctx Debug context
 */
void debug_print_help(debug_context_t *ctx);

/**
 * @brief Enter interactive debug mode
 *
 * @param ctx Debug context
 */
void debug_enter_interactive_mode(debug_context_t *ctx);

/**
 * @brief Show source context around current location
 *
 * @param ctx Debug context
 * @param file Source file
 * @param line Line number
 */
void debug_show_context(debug_context_t *ctx, const char *file, int line);

/**
 * @brief Show current execution location
 *
 * @param ctx Debug context
 */
void debug_show_current_location(debug_context_t *ctx);

/**
 * @brief Move up the call stack
 *
 * @param ctx Debug context
 */
void debug_stack_up(debug_context_t *ctx);

/**
 * @brief Move down the call stack
 *
 * @param ctx Debug context
 */
void debug_stack_down(debug_context_t *ctx);

/**
 * @brief Set a variable value
 *
 * @param ctx Debug context
 * @param assignment Variable assignment string (name=value)
 */
void debug_set_variable(debug_context_t *ctx, const char *assignment);

/**
 * @brief Evaluate an expression
 *
 * @param ctx Debug context
 * @param expression Expression to evaluate
 */
void debug_evaluate_expression(debug_context_t *ctx, const char *expression);

/**
 * @brief Evaluate a condition expression
 *
 * @param ctx Debug context
 * @param condition Condition to evaluate
 * @return true if condition is true, false otherwise
 */
bool debug_evaluate_condition(debug_context_t *ctx, const char *condition);

/* ============================================================================
 * Execution Context Preservation (Loop Debugging Fix)
 * ============================================================================
 */

/**
 * @brief Save execution context for loop debugging
 *
 * @param ctx Debug context
 * @param executor Executor context
 * @param node Current AST node
 */
void debug_save_execution_context(debug_context_t *ctx, executor_t *executor,
                                  node_t *node);

/**
 * @brief Restore execution context after loop debugging
 *
 * @param ctx Debug context
 * @param executor Executor context
 * @param node Current AST node
 */
void debug_restore_execution_context(debug_context_t *ctx, executor_t *executor,
                                     node_t *node);

/**
 * @brief Clean up execution context
 *
 * @param ctx Debug context
 */
void debug_cleanup_execution_context(debug_context_t *ctx);

/* ============================================================================
 * Loop Context Tracking (Architectural Fix)
 * ============================================================================
 */

/**
 * @brief Enter a loop context
 *
 * @param ctx Debug context
 * @param loop_type Type of loop ("for", "while", "until")
 * @param variable Loop variable name
 * @param value Initial loop variable value
 */
void debug_enter_loop(debug_context_t *ctx, const char *loop_type,
                      const char *variable, const char *value);

/**
 * @brief Update loop variable value
 *
 * @param ctx Debug context
 * @param variable Variable name
 * @param value New value
 */
void debug_update_loop_variable(debug_context_t *ctx, const char *variable,
                                const char *value);

/**
 * @brief Exit a loop context
 *
 * @param ctx Debug context
 */
void debug_exit_loop(debug_context_t *ctx);

/**
 * @brief Check breakpoint with context preservation
 *
 * Enhanced breakpoint check that preserves execution context for loop
 * debugging.
 *
 * @param ctx Debug context
 * @param file Source file
 * @param line Line number
 * @param executor Executor context
 * @param node Current AST node
 * @return true if breakpoint hit, false otherwise
 */
bool debug_check_breakpoint_with_context(debug_context_t *ctx, const char *file,
                                         int line, executor_t *executor,
                                         node_t *node);

#endif // DEBUG_H
