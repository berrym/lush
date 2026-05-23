/**
 * @file lle_shell_event_hub.h
 * @brief LLE Shell Event Hub - Central routing for shell lifecycle events
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * This module provides the shell event hub that coordinates shell-level events
 * (directory changes, pre/post command) and routes them to registered handlers.
 * It acts as a thin coordinator layer on top of the existing LLE event system.
 *
 * Shell lifecycle events:
 * - Directory changed: Fired when cd changes the working directory
 * - Pre-command: Fired just before command execution
 * - Post-command: Fired after command completes with exit code and duration
 *
 * Architecture:
 * - Handlers registered via lle_shell_event_hub_register()
 * - Events fired via lle_fire_*() convenience functions
 * - Also propagates to LLE event system (LLE_EVENT_DIRECTORY_CHANGED, etc.)
 *
 * Specification: docs/lle_specification/26_initialization_system_complete.md
 * Date: 2025-01-16
 */

#ifndef LLE_SHELL_EVENT_HUB_H
#define LLE_SHELL_EVENT_HUB_H

#include "lle/error_handling.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * SHELL EVENT TYPES
 * ============================================================================
 */

/**
 * @brief Shell event type enumeration
 *
 * These map to the existing LLE event system types:
 * - LLE_SHELL_EVENT_DIRECTORY_CHANGED -> LLE_EVENT_DIRECTORY_CHANGED (0xD000)
 * - LLE_SHELL_EVENT_PRE_COMMAND -> LLE_EVENT_PRE_COMMAND (0xD001)
 * - LLE_SHELL_EVENT_POST_COMMAND -> LLE_EVENT_POST_COMMAND (0xD002)
 */
typedef enum lle_shell_event_type {
    LLE_SHELL_EVENT_DIRECTORY_CHANGED = 0, /**< Working directory changed */
    LLE_SHELL_EVENT_PRE_COMMAND = 1,       /**< Before command execution */
    LLE_SHELL_EVENT_POST_COMMAND = 2,      /**< After command execution */
    LLE_SHELL_EVENT_TYPE_COUNT = 3         /**< Number of event types */
} lle_shell_event_type_t;

/* ============================================================================
 * EVENT DATA STRUCTURES
 * ============================================================================
 */

/**
 * @brief Directory changed event data
 *
 * Fired when the shell's working directory changes (e.g., via cd builtin).
 * Used to trigger cache invalidation for directory-dependent data like
 * git status, autosuggestions, and prompt segments.
 */
typedef struct lle_directory_changed_event {
    const char *old_dir; /**< Previous working directory (may be NULL) */
    const char *new_dir; /**< New working directory (must not be NULL) */
} lle_directory_changed_event_t;

/**
 * @brief Pre-command event data
 *
 * Fired just before a command is executed. Allows subsystems to record
 * state for transient prompt display and command timing.
 */
typedef struct lle_pre_command_event {
    const char *command; /**< Command about to be executed */
    bool is_background;  /**< True if command will run in background (&) */
} lle_pre_command_event_t;

/**
 * @brief Post-command event data
 *
 * Fired after a command completes execution. Provides exit code and duration
 * for prompt status display, history annotation, and timing segments.
 */
typedef struct lle_post_command_event {
    const char *command;  /**< Command that was executed */
    int exit_code;        /**< Command exit code (0 = success) */
    uint64_t duration_us; /**< Execution duration in microseconds */
} lle_post_command_event_t;

/* ============================================================================
 * HANDLER CALLBACK
 * ============================================================================
 */

/**
 * @brief Shell event handler callback signature
 *
 * @param event_data Pointer to event-specific data structure:
 *                   - lle_directory_changed_event_t* for DIRECTORY_CHANGED
 *                   - lle_pre_command_event_t* for PRE_COMMAND
 *                   - lle_post_command_event_t* for POST_COMMAND
 * @param user_data  User-provided context pointer from registration
 */
typedef void (*lle_shell_event_handler_t)(void *event_data, void *user_data);

/* ============================================================================
 * SHELL EVENT HUB STRUCTURE
 * ============================================================================
 */

/** Maximum handlers per event type */
#define LLE_SHELL_EVENT_MAX_HANDLERS 8

/**
 * @brief Handler registration entry
 */
typedef struct lle_shell_handler_entry {
    lle_shell_event_handler_t handler; /**< Handler callback function */
    void *user_data;                   /**< User context for callback */
    const char *name;                  /**< Handler name for debugging */
} lle_shell_handler_entry_t;

/**
 * @brief Shell event hub structure
 *
 * Central coordinator for shell lifecycle events. Maintains registered
 * handlers for each event type and tracks shell state for event generation.
 */
typedef struct lle_shell_event_hub {
    /** Handler arrays indexed by event type */
    lle_shell_handler_entry_t handlers[LLE_SHELL_EVENT_TYPE_COUNT]
                                      [LLE_SHELL_EVENT_MAX_HANDLERS];

    /** Number of registered handlers per event type */
    size_t handler_counts[LLE_SHELL_EVENT_TYPE_COUNT];

    /** Current working directory (for change detection) */
    char current_dir[PATH_MAX];

    /** Command start time for duration calculation */
    uint64_t command_start_time_us;

    /** Current command being executed (for post-command event) */
    char current_command[4096];

    /** Whether hub is initialized */
    bool initialized;

    // Statistics
    uint64_t events_fired;      /**< Total events fired */
    uint64_t directory_changes; /**< Directory change events */
    uint64_t commands_executed; /**< Commands executed (post-command count) */
} lle_shell_event_hub_t;

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Create a new shell event hub
 *
 * Allocates and initializes a shell event hub. The hub starts with no
 * registered handlers and captures the current working directory.
 *
 * @param hub_out Pointer to receive new hub instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_shell_event_hub_create(lle_shell_event_hub_t **hub_out);

/**
 * @brief Destroy a shell event hub
 *
 * Frees all resources associated with the hub. Registered handlers are
 * not called during destruction.
 *
 * @param hub Hub to destroy (NULL safe)
 */
void lle_shell_event_hub_destroy(lle_shell_event_hub_t *hub);

/* ============================================================================
 * HANDLER REGISTRATION
 * ============================================================================
 */

/**
 * @brief Register an event handler
 *
 * Registers a callback to be invoked when the specified event type fires.
 * Handlers are called in registration order. Maximum 8 handlers per type.
 *
 * @param hub        Shell event hub
 * @param event_type Event type to handle
 * @param handler    Callback function
 * @param user_data  Context pointer passed to callback
 * @param name       Handler name for debugging (may be NULL)
 * @return LLE_SUCCESS, LLE_ERROR_INVALID_PARAMETER, or
 * LLE_ERROR_RESOURCE_EXHAUSTED
 */
lle_result_t lle_shell_event_hub_register(lle_shell_event_hub_t *hub,
                                          lle_shell_event_type_t event_type,
                                          lle_shell_event_handler_t handler,
                                          void *user_data, const char *name);

/**
 * @brief Unregister an event handler by name
 *
 * Removes a previously registered handler. Other handlers shift to fill gap.
 *
 * @param hub        Shell event hub
 * @param event_type Event type
 * @param name       Handler name to remove
 * @return LLE_SUCCESS or LLE_ERROR_NOT_FOUND
 */
lle_result_t lle_shell_event_hub_unregister(lle_shell_event_hub_t *hub,
                                            lle_shell_event_type_t event_type,
                                            const char *name);

/* ============================================================================
 * EVENT FIRING (INTERNAL)
 * ============================================================================
 */

/**
 * @brief Fire an event to all registered handlers
 *
 * Internal function that dispatches an event to all handlers for the
 * specified event type. Also propagates to LLE event system.
 *
 * @param hub        Shell event hub
 * @param event_type Event type to fire
 * @param event_data Pointer to event data structure
 */
void lle_shell_event_hub_fire(lle_shell_event_hub_t *hub,
                              lle_shell_event_type_t event_type,
                              void *event_data);

/* ============================================================================
 * CONVENIENCE EVENT FIRING FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Fire a directory changed event
 *
 * Called by bin_cd() after successful chdir(). Triggers cache invalidation
 * for directory-dependent subsystems (git status, prompt, autosuggestions).
 *
 * @param old_dir Previous working directory (NULL to auto-detect from hub)
 * @param new_dir New working directory (NULL to get current via getcwd)
 */
void lle_fire_directory_changed(const char *old_dir, const char *new_dir);

/**
 * @brief Fire a pre-command event
 *
 * Called by main shell loop just before executing a command. Records
 * command start time and allows subsystems to prepare for execution.
 *
 * @param command       Command about to be executed
 * @param is_background True if command will run in background
 */
void lle_fire_pre_command(const char *command, bool is_background);

/**
 * @brief Fire a post-command event
 *
 * Called by main shell loop after command completes. Provides exit code
 * and calculated duration for prompt status and history annotation.
 *
 * @param command     Command that was executed
 * @param exit_code   Command exit code
 * @param duration_us Execution duration in microseconds (0 to auto-calculate)
 */
void lle_fire_post_command(const char *command, int exit_code,
                           uint64_t duration_us);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get current timestamp in microseconds
 *
 * Uses CLOCK_MONOTONIC for reliable duration calculation.
 *
 * @return Current timestamp in microseconds
 */
uint64_t lle_shell_event_get_timestamp_us(void);

/**
 * @brief Get event type name for debugging
 *
 * @param event_type Event type
 * @return Human-readable event type name
 */
const char *lle_shell_event_type_name(lle_shell_event_type_t event_type);

#endif // LLE_SHELL_EVENT_HUB_H
