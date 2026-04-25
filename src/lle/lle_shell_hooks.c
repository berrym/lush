/**
 * @file lle_shell_hooks.c
 * @brief LLE Shell Hooks - User-facing hook function bridge implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Bridges the LLE Shell Event Hub with user-defined shell functions.
 * When shell events fire (pre_command, post_command, directory_changed),
 * this module calls the corresponding user-defined hook functions
 * (preexec, precmd, chpwd) if they are defined.
 */

#include "lle/lle_shell_hooks.h"
#include "executor.h"
#include "lle/lle_shell_event_hub.h"
#include "lle/lle_shell_integration.h"
#include "lush.h"
#include "shell_mode.h"
#include "symtable.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * STATIC STATE
 * ============================================================================
 */

/** Whether hooks are initialized */
static bool g_hooks_initialized = false;

/** Current hook being executed (for recursion detection) */
static lle_hook_type_t g_current_hook = LLE_HOOK_COUNT;

/** Hook call statistics */
static size_t g_hook_call_counts[LLE_HOOK_COUNT] = {0};

/** Last time periodic hook was called (for PERIOD-based timing) */
static time_t g_last_periodic_call = 0;

/** Hook function names */
static const char *g_hook_names[LLE_HOOK_COUNT] = {
    [LLE_HOOK_PRECMD] = "precmd",
    [LLE_HOOK_PREEXEC] = "preexec",
    [LLE_HOOK_CHPWD] = "chpwd",
    [LLE_HOOK_PERIODIC] = "periodic",
};

/** Hook array names (Zsh compatibility: precmd_functions, etc.) */
static const char *g_hook_array_names[LLE_HOOK_COUNT] = {
    [LLE_HOOK_PRECMD] = "precmd_functions",
    [LLE_HOOK_PREEXEC] = "preexec_functions",
    [LLE_HOOK_CHPWD] = "chpwd_functions",
    [LLE_HOOK_PERIODIC] = "periodic_functions",
};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/**
 * @brief Call a hook function via the executor
 *
 * @param hook_type Type of hook to call
 * @param arg Optional argument (command string for preexec)
 * @return Exit status of hook, or 0 if not defined
 */
static int call_hook_function(lle_hook_type_t hook_type, const char *arg) {
    if (hook_type >= LLE_HOOK_COUNT) {
        return 0;
    }

    // Get the global executor
    executor_t *executor = get_global_executor();
    if (!executor) {
        return 0;
    }

    const char *hook_name = g_hook_names[hook_type];
    if (!hook_name) {
        return 0;
    }

    // Use executor's hook calling mechanism (handles recursion guard
    // internally)
    int result = executor_call_hook(executor, hook_name, arg);

    // Update statistics
    g_hook_call_counts[hook_type]++;

    return result;
}

/**
 * @brief Helper to call all functions in a specific array
 *
 * @param executor Executor instance
 * @param array_name Name of the array to iterate
 * @param arg Optional argument (command string for preexec)
 * @return Number of functions called
 */
static int call_functions_in_array(executor_t *executor, const char *array_name,
                                   const char *arg) {
    if (!executor || !array_name) {
        return 0;
    }

    array_value_t *hook_array = symtable_get_array(array_name);
    if (!hook_array) {
        return 0;
    }

    size_t count = 0;
    char **func_names = symtable_array_get_values(hook_array, &count);
    if (!func_names || count == 0) {
        return 0;
    }

    int called = 0;

    for (size_t i = 0; i < count; i++) {
        const char *func_name = func_names[i];
        if (func_name && func_name[0] != '\0') {
            executor_call_hook(executor, func_name, arg);
            called++;
        }
    }

    for (size_t i = 0; i < count; i++) {
        free(func_names[i]);
    }
    free(func_names);

    return called;
}

/**
 * @brief Call all functions in hook arrays (Zsh compatibility)
 *
 * Zsh supports hook arrays like precmd_functions, preexec_functions, etc.
 * Each element in the array is a function name to be called.
 *
 * When FEATURE_SIMPLE_HOOK_ARRAYS is enabled, also checks the simple
 * hook name (e.g., "precmd") as an array, allowing precmd+=(fn) syntax.
 *
 * @param hook_type Type of hook (determines which array to check)
 * @param arg Optional argument (command string for preexec)
 * @return Number of functions called, or 0 if no arrays exist
 */
static int call_hook_array(lle_hook_type_t hook_type, const char *arg) {
    if (hook_type >= LLE_HOOK_COUNT) {
        return 0;
    }

    executor_t *executor = get_global_executor();
    if (!executor) {
        return 0;
    }

    int called = 0;

    // First, check the standard array name (e.g., "precmd_functions")
    const char *array_name = g_hook_array_names[hook_type];
    if (array_name) {
        called += call_functions_in_array(executor, array_name, arg);
    }

    // If FEATURE_SIMPLE_HOOK_ARRAYS is enabled, also check the simple
    // hook name as an array (e.g., "precmd" array for precmd+=(fn) syntax)
    if (shell_mode_allows(FEATURE_SIMPLE_HOOK_ARRAYS)) {
        const char *simple_name = g_hook_names[hook_type];
        if (simple_name) {
            called += call_functions_in_array(executor, simple_name, arg);
        }
    }

    return called;
}

/**
 * @brief Check and call periodic hook if PERIOD has elapsed
 *
 * In Zsh, if the PERIOD variable is set to a positive integer, the
 * periodic() function is called every PERIOD seconds before each prompt.
 * This is useful for tasks like checking mail or updating status.
 */
static void check_periodic_hook(void) {
    // Get the PERIOD variable
    char *period_str = symtable_get_global("PERIOD");
    if (!period_str) {
        return;
    }

    // Parse the period value
    long period = strtol(period_str, NULL, 10);
    free(period_str);

    if (period <= 0) {
        return;
    }

    // Check if enough time has elapsed
    time_t now = time(NULL);
    if (g_last_periodic_call == 0) {
        // First call - initialize but don't trigger immediately
        g_last_periodic_call = now;
        return;
    }

    if ((now - g_last_periodic_call) >= period) {
        // Time to call periodic hook
        g_last_periodic_call = now;

        g_current_hook = LLE_HOOK_PERIODIC;
        call_hook_function(LLE_HOOK_PERIODIC, NULL);
        call_hook_array(LLE_HOOK_PERIODIC, NULL);
        g_current_hook = LLE_HOOK_COUNT;
    }
}

/**
 * @brief Execute PROMPT_COMMAND (Bash compatibility)
 *
 * Bash 5.1+ supports PROMPT_COMMAND as either:
 * - A string: executed as a command before each prompt
 * - An array: each element executed in order before each prompt
 *
 * This function checks for both forms and executes accordingly.
 * Only active when FEATURE_PROMPT_COMMAND is enabled.
 */
static void execute_prompt_command(void) {
    // Check feature flag
    if (!shell_mode_allows(FEATURE_PROMPT_COMMAND)) {
        return;
    }

    executor_t *executor = get_global_executor();
    if (!executor) {
        return;
    }

    // First check if PROMPT_COMMAND exists as an array (Bash 5.1+ style)
    array_value_t *cmd_array = symtable_get_array("PROMPT_COMMAND");
    if (cmd_array) {
        size_t count = 0;
        char **commands = symtable_array_get_values(cmd_array, &count);
        if (commands && count > 0) {
            for (size_t i = 0; i < count; i++) {
                if (commands[i] && commands[i][0] != '\0') {
                    // Execute each command in the array
                    executor_execute_command_line(executor, commands[i]);
                }
            }
            // Free the commands array
            for (size_t i = 0; i < count; i++) {
                free(commands[i]);
            }
            free(commands);
        }
        return; // Array takes precedence
    }

    // Fall back to string form (traditional Bash style)
    char *cmd_str = symtable_get_global("PROMPT_COMMAND");
    if (cmd_str && cmd_str[0] != '\0') {
        executor_execute_command_line(executor, cmd_str);
        free(cmd_str);
    }
}

/* ============================================================================
 * EVENT HANDLERS
 * ============================================================================
 */

/**
 * @brief Handler for POST_COMMAND events -> precmd hook
 *
 * Called after a command completes, before the next prompt is displayed.
 * Executes in order:
 * 1. PROMPT_COMMAND (Bash compatibility, if enabled)
 * 2. precmd function and precmd_functions array (Zsh style, if enabled)
 * 3. periodic hook (if PERIOD has elapsed)
 */
static void hook_precmd_handler(void *event_data, void *user_data) {
    (void)event_data;
    (void)user_data;

    // Prevent recursive calls
    if (executor_in_hook()) {
        return;
    }

    // Execute PROMPT_COMMAND first (Bash compatibility)
    // This runs regardless of FEATURE_HOOK_FUNCTIONS
    execute_prompt_command();

    // Then execute Zsh-style hooks if enabled
    if (shell_mode_allows(FEATURE_HOOK_FUNCTIONS)) {
        g_current_hook = LLE_HOOK_PRECMD;
        call_hook_function(LLE_HOOK_PRECMD, NULL);
        call_hook_array(LLE_HOOK_PRECMD, NULL);
        g_current_hook = LLE_HOOK_COUNT;

        // Check and call periodic hook if PERIOD has elapsed
        check_periodic_hook();
    }
}

/**
 * @brief Handler for PRE_COMMAND events -> preexec hook
 *
 * Called before a command is executed. The command string is passed as $1.
 */
static void hook_preexec_handler(void *event_data, void *user_data) {
    (void)user_data;

    // Check feature flag
    if (!shell_mode_allows(FEATURE_HOOK_FUNCTIONS)) {
        return;
    }

    // Prevent recursive calls
    if (executor_in_hook()) {
        return;
    }

    lle_pre_command_event_t *event = (lle_pre_command_event_t *)event_data;
    const char *command = event ? event->command : NULL;

    g_current_hook = LLE_HOOK_PREEXEC;
    call_hook_function(LLE_HOOK_PREEXEC, command);
    call_hook_array(LLE_HOOK_PREEXEC, command);
    g_current_hook = LLE_HOOK_COUNT;
}

/**
 * @brief Handler for DIRECTORY_CHANGED events -> chpwd hook
 *
 * Called after the working directory changes (e.g., via cd).
 * PWD and OLDPWD are already set by the cd builtin.
 */
static void hook_chpwd_handler(void *event_data, void *user_data) {
    (void)event_data;
    (void)user_data;

    // Check feature flag
    if (!shell_mode_allows(FEATURE_HOOK_FUNCTIONS)) {
        return;
    }

    // Prevent recursive calls
    if (executor_in_hook()) {
        return;
    }

    g_current_hook = LLE_HOOK_CHPWD;
    call_hook_function(LLE_HOOK_CHPWD, NULL);
    call_hook_array(LLE_HOOK_CHPWD, NULL);
    g_current_hook = LLE_HOOK_COUNT;
}

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize the shell hooks bridge
 */
void lle_shell_hooks_init(void) {
    if (g_hooks_initialized) {
        return;
    }

    // Check if any hook feature is enabled
    // PROMPT_COMMAND (Bash) or HOOK_FUNCTIONS (Zsh) both need POST_COMMAND
    // events
    bool need_hooks = shell_mode_allows(FEATURE_HOOK_FUNCTIONS) ||
                      shell_mode_allows(FEATURE_PROMPT_COMMAND);
    if (!need_hooks) {
        return;
    }

    // Get the shell event hub
    if (!g_lle_integration || !g_lle_integration->event_hub) {
        return;
    }

    lle_shell_event_hub_t *hub = g_lle_integration->event_hub;

    // Register POST_COMMAND handler for precmd hooks and PROMPT_COMMAND
    // This runs for both Zsh-style hooks and Bash-style PROMPT_COMMAND
    lle_shell_event_hub_register(hub, LLE_SHELL_EVENT_POST_COMMAND,
                                 hook_precmd_handler, NULL, "precmd-hook");

    // The following only make sense if FEATURE_HOOK_FUNCTIONS is enabled
    if (shell_mode_allows(FEATURE_HOOK_FUNCTIONS)) {
        // preexec is triggered by PRE_COMMAND (before command runs)
        lle_shell_event_hub_register(hub, LLE_SHELL_EVENT_PRE_COMMAND,
                                     hook_preexec_handler, NULL,
                                     "preexec-hook");

        // chpwd is triggered by DIRECTORY_CHANGED
        lle_shell_event_hub_register(hub, LLE_SHELL_EVENT_DIRECTORY_CHANGED,
                                     hook_chpwd_handler, NULL, "chpwd-hook");
    }

    g_hooks_initialized = true;
}

/**
 * @brief Clean up the shell hooks bridge
 */
void lle_shell_hooks_cleanup(void) {
    if (!g_hooks_initialized) {
        return;
    }

    // Unregister handlers if event hub is still available
    if (g_lle_integration && g_lle_integration->event_hub) {
        lle_shell_event_hub_t *hub = g_lle_integration->event_hub;

        lle_shell_event_hub_unregister(hub, LLE_SHELL_EVENT_POST_COMMAND,
                                       "precmd-hook");
        lle_shell_event_hub_unregister(hub, LLE_SHELL_EVENT_PRE_COMMAND,
                                       "preexec-hook");
        lle_shell_event_hub_unregister(hub, LLE_SHELL_EVENT_DIRECTORY_CHANGED,
                                       "chpwd-hook");
    }

    g_hooks_initialized = false;
}

/**
 * @brief Check if shell hooks are initialized
 */
bool lle_shell_hooks_initialized(void) { return g_hooks_initialized; }

/* ============================================================================
 * HOOK QUERY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Check if a hook function is defined
 */
bool lle_shell_hook_defined(lle_hook_type_t hook_type) {
    if (hook_type >= LLE_HOOK_COUNT) {
        return false;
    }

    executor_t *executor = get_global_executor();
    if (!executor) {
        return false;
    }

    const char *hook_name = g_hook_names[hook_type];
    if (!hook_name) {
        return false;
    }

    // Use executor_call_hook with a NULL check - if hook returns 0 and
    // no function exists, that's the same as if it ran successfully.
    // We need a way to check if the function exists without calling it.
    // For now, we'll rely on the fact that executor_call_hook checks
    // internally.
    // TODO: Add executor_function_exists() for clean checking

    // Workaround: Check if the function name is in the executor's function
    // table by attempting to call with special marker - but this isn't ideal.
    // For now, just return true if the feature is enabled - the hook will
    // silently do nothing if the function isn't defined.
    return shell_mode_allows(FEATURE_HOOK_FUNCTIONS);
}

/**
 * @brief Get the function name for a hook type
 */
const char *lle_shell_hook_name(lle_hook_type_t hook_type) {
    if (hook_type >= LLE_HOOK_COUNT) {
        return NULL;
    }
    return g_hook_names[hook_type];
}

/* ============================================================================
 * MANUAL HOOK INVOCATION
 * ============================================================================
 */

/**
 * @brief Manually call a hook function
 */
int lle_shell_hook_call(lle_hook_type_t hook_type, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (hook_type >= LLE_HOOK_COUNT) {
        return -1;
    }

    // Check feature flag
    if (!shell_mode_allows(FEATURE_HOOK_FUNCTIONS)) {
        return 0;
    }

    // For preexec, extract command from argv[1] if present
    const char *arg = NULL;
    if (hook_type == LLE_HOOK_PREEXEC && argc > 1 && argv && argv[1]) {
        arg = argv[1];
    }

    return call_hook_function(hook_type, arg);
}

/**
 * @brief Call a hook function by name
 */
int lle_shell_hook_call_by_name(const char *hook_name, int argc, char **argv) {
    if (!hook_name) {
        return -1;
    }

    // Find the hook type
    for (int i = 0; i < LLE_HOOK_COUNT; i++) {
        if (g_hook_names[i] && strcmp(g_hook_names[i], hook_name) == 0) {
            return lle_shell_hook_call((lle_hook_type_t)i, argc, argv);
        }
    }

    return -1; // Unknown hook name
}

/* ============================================================================
 * HOOK STATISTICS
 * ============================================================================
 */

/**
 * @brief Get the number of times a hook has been called
 */
size_t lle_shell_hook_call_count(lle_hook_type_t hook_type) {
    if (hook_type >= LLE_HOOK_COUNT) {
        return 0;
    }
    return g_hook_call_counts[hook_type];
}

/**
 * @brief Reset hook call statistics
 */
void lle_shell_hook_reset_stats(void) {
    for (int i = 0; i < LLE_HOOK_COUNT; i++) {
        g_hook_call_counts[i] = 0;
    }
}

/* ============================================================================
 * RECURSION PROTECTION
 * ============================================================================
 */

/**
 * @brief Check if we're currently inside a hook
 */
bool lle_shell_hook_in_progress(void) {
    return g_current_hook != LLE_HOOK_COUNT || executor_in_hook();
}

/**
 * @brief Get the currently executing hook type
 */
lle_hook_type_t lle_shell_hook_current(void) { return g_current_hook; }
