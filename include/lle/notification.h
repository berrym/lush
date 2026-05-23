/**
 * @file notification.h
 * @brief LLE Notification System - Transient hints and messages
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides a generic notification system for displaying transient, unobtrusive
 * hints below the command line. Notifications auto-dismiss on user action and
 * can be explicitly dismissed with ESC or Ctrl-G.
 *
 * Primary use case: Notifying users in multiline mode about Ctrl+P/Ctrl+N
 * for history navigation when pressing up/down at buffer boundaries.
 */

#ifndef LLE_NOTIFICATION_H
#define LLE_NOTIFICATION_H

#include "error_handling.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

/** Maximum length for notification message text */
#define LLE_NOTIFICATION_MAX_MESSAGE 256

/** Maximum length for styled output buffer */
#define LLE_NOTIFICATION_MAX_STYLED 512

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================
 */

/**
 * @brief Notification type for visual styling
 */
typedef enum {
    LLE_NOTIFICATION_HINT,    /**< Dim gray - unobtrusive hints */
    LLE_NOTIFICATION_INFO,    /**< Cyan - informational messages */
    LLE_NOTIFICATION_WARNING, /**< Yellow - warning messages */
    LLE_NOTIFICATION_ERROR    /**< Red - error messages */
} lle_notification_type_t;

/**
 * @brief Action types that can trigger notifications
 *
 * Used for suppress-on-repeat logic: if the same action triggers a
 * notification twice in a row, don't dismiss on the second trigger.
 */
typedef enum {
    LLE_NOTIF_ACTION_NONE = 0,
    LLE_NOTIF_ACTION_UP_ARROW,   /**< Up arrow at first line */
    LLE_NOTIF_ACTION_DOWN_ARROW, /**< Down arrow at last line */
} lle_notification_trigger_action_t;

/**
 * @brief Notification state structure
 *
 * Holds the current notification state including message, type, visibility,
 * and trigger information for suppress-on-repeat logic.
 */
typedef struct lle_notification_state {
    char
        message[LLE_NOTIFICATION_MAX_MESSAGE]; /**< Notification message text */
    lle_notification_type_t type;              /**< Visual style type */
    bool visible; /**< Whether notification is currently visible */
    lle_notification_trigger_action_t
        trigger_action; /**< Action that triggered this notification */
} lle_notification_state_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ============================================================================
 */

/**
 * @brief Initialize notification state
 *
 * Sets all fields to default/cleared state.
 *
 * @param state Notification state to initialize
 */
void lle_notification_init(lle_notification_state_t *state);

/**
 * @brief Clean up notification state
 *
 * Clears the notification state. Safe to call on already-cleaned state.
 *
 * @param state Notification state to clean up
 */
void lle_notification_cleanup(lle_notification_state_t *state);

/**
 * @brief Show a notification with the given message and type
 *
 * Displays a transient notification. The notification will remain visible
 * until dismissed by user action (ESC, Ctrl-G, or any input that isn't
 * a repeat of the triggering action).
 *
 * @param state Notification state
 * @param message Message text to display
 * @param type Visual style type (HINT, INFO, WARNING, ERROR)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_notification_show(lle_notification_state_t *state,
                                   const char *message,
                                   lle_notification_type_t type);

/**
 * @brief Show notification with trigger action tracking
 *
 * Same as lle_notification_show() but also records which action triggered
 * the notification. This enables suppress-on-repeat logic where repeating
 * the same action doesn't dismiss the notification.
 *
 * @param state Notification state
 * @param message Message text to display
 * @param type Visual style type
 * @param trigger_action Action that triggered this notification
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_notification_show_with_trigger(
    lle_notification_state_t *state, const char *message,
    lle_notification_type_t type,
    lle_notification_trigger_action_t trigger_action);

/**
 * @brief Dismiss the current notification
 *
 * Hides the notification. Safe to call even if no notification is visible.
 *
 * @param state Notification state
 */
void lle_notification_dismiss(lle_notification_state_t *state);

/**
 * @brief Check if a notification is currently visible
 *
 * @param state Notification state
 * @return true if a notification is visible, false otherwise
 */
bool lle_notification_is_visible(const lle_notification_state_t *state);

/**
 * @brief Get styled notification text with ANSI color codes
 *
 * Formats the notification message with appropriate ANSI escape codes
 * based on the notification type:
 * - HINT: dim gray (\033[90m)
 * - INFO: cyan (\033[36m)
 * - WARNING: yellow (\033[33m)
 * - ERROR: red (\033[31m)
 *
 * Format: "Tip: <message>" for HINT type, just "<message>" for others.
 *
 * @param state Notification state
 * @param buf Buffer to receive styled text
 * @param size Size of buffer
 * @return Pointer to buf on success, NULL if notification not visible or error
 */
const char *
lle_notification_get_styled_text(const lle_notification_state_t *state,
                                 char *buf, size_t size);

/**
 * @brief Check if an action should dismiss the notification
 *
 * Returns true if the action is different from the trigger action,
 * meaning the notification should be dismissed. Returns false if
 * the action matches the trigger (suppress-on-repeat).
 *
 * @param state Notification state
 * @param action Action being performed
 * @return true if notification should be dismissed, false to keep it
 */
bool lle_notification_should_dismiss_for_action(
    const lle_notification_state_t *state,
    lle_notification_trigger_action_t action);

/**
 * @brief Get the trigger action for the current notification
 *
 * @param state Notification state
 * @return The trigger action, or LLE_NOTIF_ACTION_NONE if no notification
 */
lle_notification_trigger_action_t
lle_notification_get_trigger_action(const lle_notification_state_t *state);

#ifdef __cplusplus
}
#endif

#endif // LLE_NOTIFICATION_H
