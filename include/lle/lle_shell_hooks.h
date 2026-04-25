/**
 * @file lle_shell_hooks.h
 * @brief LLE Shell Hooks - User-facing hook function bridge
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides the bridge between LLE's Shell Event Hub and user-defined shell
 * functions. When users define functions like precmd(), preexec(), or chpwd(),
 * this module ensures they are called at the appropriate times.
 *
 * Hook Function Reference:
 * - precmd()   - Called before each prompt display (after command completes)
 * - preexec()  - Called before command execution, receives command as $1
 * - chpwd()    - Called after directory change (cd), PWD/OLDPWD are set
 * - periodic() - Called every PERIOD seconds (if PERIOD variable is set)
 *
 * Hook Arrays (Zsh Compatibility):
 * In addition to single hook functions, users can define arrays of function
 * names that will all be called for each hook event:
 * - precmd_functions  - Array of function names called after precmd()
 * - preexec_functions - Array of function names called after preexec()
 * - chpwd_functions   - Array of function names called after chpwd()
 *
 * Example:
 *   my_hook() { echo "from array hook"; }
 *   precmd_functions=(my_hook)  # or precmd_functions+=(my_hook)
 *
 * This module registers handlers with the Shell Event Hub that look up and
 * call user-defined shell functions when events fire.
 *
 * @see lle_shell_event_hub.h for the underlying event system
 * @see shell_mode.h for FEATURE_HOOK_FUNCTIONS feature flag
 */

#ifndef LLE_SHELL_HOOKS_H
#define LLE_SHELL_HOOKS_H

#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * HOOK TYPES
 * ============================================================================
 */

/**
 * @brief Shell hook function types
 *
 * Enumeration of user-definable hook functions that are called automatically
 * at specific points in the shell's execution lifecycle.
 */
typedef enum {
    LLE_HOOK_PRECMD,   /**< Called before prompt display (after command) */
    LLE_HOOK_PREEXEC,  /**< Called before command execution */
    LLE_HOOK_CHPWD,    /**< Called after directory change */
    LLE_HOOK_PERIODIC, /**< Called periodically based on PERIOD variable */
    LLE_HOOK_COUNT     /**< Number of hook types */
} lle_hook_type_t;

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize the shell hooks bridge
 *
 * Registers event handlers with the Shell Event Hub that will call user-defined
 * shell functions (precmd, preexec, chpwd) when appropriate events fire.
 *
 * This function should be called once during shell initialization, after the
 * Shell Event Hub is created but before the shell enters its main loop.
 *
 * Does nothing if:
 * - FEATURE_HOOK_FUNCTIONS is disabled
 * - Shell Event Hub is not initialized
 * - Already initialized
 */
void lle_shell_hooks_init(void);

/**
 * @brief Clean up the shell hooks bridge
 *
 * Unregisters all hook handlers from the Shell Event Hub. Called during
 * shell shutdown.
 */
void lle_shell_hooks_cleanup(void);

/**
 * @brief Check if shell hooks are initialized
 *
 * @return true if hooks are registered and active
 */
bool lle_shell_hooks_initialized(void);

/* ============================================================================
 * HOOK QUERY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Check if a hook function is defined
 *
 * Checks if the user has defined a shell function with the given hook name.
 *
 * @param hook_type Type of hook to check
 * @return true if the hook function is defined
 */
bool lle_shell_hook_defined(lle_hook_type_t hook_type);

/**
 * @brief Get the function name for a hook type
 *
 * Returns the shell function name that corresponds to a hook type.
 *
 * @param hook_type Type of hook
 * @return Function name string (e.g., "precmd"), or NULL for invalid type
 */
const char *lle_shell_hook_name(lle_hook_type_t hook_type);

/* ============================================================================
 * MANUAL HOOK INVOCATION
 * ============================================================================
 */

/**
 * @brief Manually call a hook function
 *
 * Calls the specified hook function if it is defined. This is useful for
 * testing or for triggering hooks from scripts.
 *
 * For PREEXEC hooks, provide the command string as argv[1].
 * For other hooks, argv can be NULL or empty.
 *
 * @param hook_type Type of hook to call
 * @param argc Number of arguments (0 for most hooks, 2 for preexec)
 * @param argv Argument vector (argv[0] = hook name, argv[1] = command for
 * preexec)
 * @return Exit status of the hook function, or 0 if not defined
 */
int lle_shell_hook_call(lle_hook_type_t hook_type, int argc, char **argv);

/**
 * @brief Call a hook function by name
 *
 * Alternative interface for calling hooks by their string name.
 *
 * @param hook_name Hook function name (e.g., "precmd", "preexec", "chpwd")
 * @param argc Number of arguments
 * @param argv Argument vector
 * @return Exit status of the hook function, or -1 if hook name is invalid
 */
int lle_shell_hook_call_by_name(const char *hook_name, int argc, char **argv);

/* ============================================================================
 * HOOK STATISTICS
 * ============================================================================
 */

/**
 * @brief Get the number of times a hook has been called
 *
 * @param hook_type Type of hook
 * @return Number of times the hook has been called
 */
size_t lle_shell_hook_call_count(lle_hook_type_t hook_type);

/**
 * @brief Reset hook call statistics
 *
 * Clears all hook call counters. Useful for testing.
 */
void lle_shell_hook_reset_stats(void);

/* ============================================================================
 * RECURSION PROTECTION
 * ============================================================================
 */

/**
 * @brief Check if we're currently inside a hook
 *
 * Returns true if we're in the middle of executing a hook function.
 * Used to prevent recursive hook calls (e.g., precmd running a command
 * that would trigger another precmd).
 *
 * @return true if currently executing a hook
 */
bool lle_shell_hook_in_progress(void);

/**
 * @brief Get the currently executing hook type
 *
 * @return Current hook type, or LLE_HOOK_COUNT if not in a hook
 */
lle_hook_type_t lle_shell_hook_current(void);

#endif /* LLE_SHELL_HOOKS_H */
