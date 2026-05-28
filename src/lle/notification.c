/**
 * @file notification.c
 * @brief LLE Notification System - Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements the notification state management for transient hints.
 */

#include "lle/notification.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * ANSI COLOR CODES
 * ============================================================================
 */

#define ANSI_RESET "\033[0m"
#define ANSI_DIM_GRAY "\033[90m" /// For HINT type
#define ANSI_CYAN "\033[36m"     /// For INFO type
#define ANSI_YELLOW "\033[33m"   /// For WARNING type
#define ANSI_RED "\033[31m"      /// For ERROR type

/* ============================================================================
 * INITIALIZATION AND CLEANUP
 * ============================================================================
 */

void lle_notification_init(lle_notification_state_t *state) {
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->visible = false;
    state->type = LLE_NOTIFICATION_HINT;
    state->trigger_action = LLE_NOTIF_ACTION_NONE;
}

void lle_notification_cleanup(lle_notification_state_t *state) {
    if (!state) {
        return;
    }

    /// Simply clear the state - no dynamic allocations to free
    lle_notification_init(state);
}

/* ============================================================================
 * SHOW AND DISMISS
 * ============================================================================
 */

lle_result_t lle_notification_show(lle_notification_state_t *state,
                                   const char *message,
                                   lle_notification_type_t type) {
    return lle_notification_show_with_trigger(state, message, type,
                                              LLE_NOTIF_ACTION_NONE);
}

lle_result_t lle_notification_show_with_trigger(
    lle_notification_state_t *state, const char *message,
    lle_notification_type_t type,
    lle_notification_trigger_action_t trigger_action) {
    if (!state) {
        return LLE_ERROR_NULL_POINTER;
    }

    if (!message || message[0] == '\0') {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Copy message, truncating if necessary
    size_t msg_len = strlen(message);
    if (msg_len >= LLE_NOTIFICATION_MAX_MESSAGE) {
        msg_len = LLE_NOTIFICATION_MAX_MESSAGE - 1;
    }
    memcpy(state->message, message, msg_len);
    state->message[msg_len] = '\0';

    state->type = type;
    state->visible = true;
    state->trigger_action = trigger_action;

    return LLE_SUCCESS;
}

void lle_notification_dismiss(lle_notification_state_t *state) {
    if (!state) {
        return;
    }

    state->visible = false;
    state->message[0] = '\0';
    state->trigger_action = LLE_NOTIF_ACTION_NONE;
}

/* ============================================================================
 * STATE QUERIES
 * ============================================================================
 */

bool lle_notification_is_visible(const lle_notification_state_t *state) {
    if (!state) {
        return false;
    }
    return state->visible;
}

lle_notification_trigger_action_t
lle_notification_get_trigger_action(const lle_notification_state_t *state) {
    if (!state || !state->visible) {
        return LLE_NOTIF_ACTION_NONE;
    }
    return state->trigger_action;
}

bool lle_notification_should_dismiss_for_action(
    const lle_notification_state_t *state,
    lle_notification_trigger_action_t action) {
    if (!state || !state->visible) {
        return false; /// Nothing to dismiss
    }

    /// If the incoming action matches the trigger, don't dismiss
    /// (suppress-on-repeat)
    if (action != LLE_NOTIF_ACTION_NONE && action == state->trigger_action) {
        return false;
    }

    /// Different action or generic action - dismiss
    return true;
}

/* ============================================================================
 * STYLED OUTPUT
 * ============================================================================
 */

/// @brief Get ANSI color code for notification type
static const char *get_color_code(lle_notification_type_t type) {
    switch (type) {
    case LLE_NOTIFICATION_HINT:
        return ANSI_CYAN; /// Use cyan for visibility during testing
    case LLE_NOTIFICATION_INFO:
        return ANSI_CYAN;
    case LLE_NOTIFICATION_WARNING:
        return ANSI_YELLOW;
    case LLE_NOTIFICATION_ERROR:
        return ANSI_RED;
    default:
        return ANSI_DIM_GRAY;
    }
}

const char *
lle_notification_get_styled_text(const lle_notification_state_t *state,
                                 char *buf, size_t size) {
    if (!state || !buf || size == 0) {
        return NULL;
    }

    if (!state->visible || state->message[0] == '\0') {
        return NULL;
    }

    const char *color = get_color_code(state->type);

    /// Format: for HINT type, prefix with "Tip: ", otherwise just the message
    int written;
    if (state->type == LLE_NOTIFICATION_HINT) {
        written = snprintf(buf, size, "%sTip: %s%s", color, state->message,
                           ANSI_RESET);
    } else {
        written =
            snprintf(buf, size, "%s%s%s", color, state->message, ANSI_RESET);
    }

    if (written < 0 || (size_t)written >= size) {
        /// Truncated or error - ensure null termination
        buf[size - 1] = '\0';
    }

    return buf;
}
