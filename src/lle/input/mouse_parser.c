/**
 * @file mouse_parser.c
 * @brief Terminal Mouse Event Parser
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Comprehensive mouse event parsing supporting multiple formats:
 * - X10 compatible mode (ESC[M<btn><x><y>)
 * - SGR extended mode (ESC[<btn;x;y>M/m)
 * - UTF-8 coordinate encoding
 *
 * Handles button press/release, movement, drag, wheel events,
 * and multi-click detection (double/triple click).
 *
 * Spec 06: Input Parsing - Phase 5
 */

#include "lle/error_handling.h"
#include "lle/input_parsing.h"
#include <string.h>
#include <time.h>

/**
 * @brief Get current time in microseconds
 *
 * @return Current monotonic time in microseconds
 */
static uint64_t get_current_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

/**
 * @brief Initialize mouse parser
 *
 * @param parser Output pointer for created parser
 * @param terminal_caps Terminal capabilities for feature detection
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_mouse_parser_init(lle_mouse_parser_t **parser,
                                   lle_terminal_capabilities_t *terminal_caps,
                                   lle_memory_pool_t *memory_pool) {
    if (!parser) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_mouse_parser_t *new_parser = lle_pool_alloc(sizeof(lle_mouse_parser_t));
    if (!new_parser) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(new_parser, 0, sizeof(lle_mouse_parser_t));

    new_parser->terminal_caps = terminal_caps;
    new_parser->memory_pool = memory_pool;
    new_parser->mouse_tracking_enabled = false;
    new_parser->tracking_mode = LLE_MOUSE_TRACKING_NONE;
    new_parser->button_event_tracking = false;
    new_parser->motion_event_tracking = false;
    new_parser->focus_event_tracking = false;

    new_parser->last_x = 0;
    new_parser->last_y = 0;
    new_parser->pressed_buttons = LLE_MOUSE_BUTTON_NONE;
    new_parser->modifiers = LLE_KEY_MOD_NONE;
    new_parser->last_click_time = 0;
    new_parser->click_count = 0;

    new_parser->mouse_events_parsed = 0;
    new_parser->invalid_mouse_sequences = 0;

    *parser = new_parser;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy mouse parser
 *
 * @param parser Parser to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_mouse_parser_destroy(lle_mouse_parser_t *parser) {
    if (!parser) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_pool_free(parser);
    return LLE_SUCCESS;
}

/**
 * @brief Parse X10 compatible mouse sequence
 *
 * Format: ESC[M<btn><x><y>
 * - btn: button byte (32 + button_code + modifiers)
 * - x, y: coordinate bytes (32 + coordinate, limited to 223)
 *
 * @param parser Mouse parser instance
 * @param data Raw sequence data
 * @param data_len Length of sequence data
 * @param event Output for parsed event
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t parse_x10_sequence(lle_mouse_parser_t *parser,
                                       const char *data, size_t data_len,
                                       lle_mouse_event_info_t *event) {
    if (!parser || !data || data_len < 6 || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Verify prefix: ESC[M
    if (data[0] != '\x1B' || data[1] != '[' || data[2] != 'M') {
        return LLE_ERROR_INVALID_FORMAT;
    }

    /// Extract button byte
    unsigned char btn = (unsigned char)data[3];
    unsigned char x_byte = (unsigned char)data[4];
    unsigned char y_byte = (unsigned char)data[5];

    /// Decode button
    int button_code = (btn - 32) & 0x03;
    bool is_motion = (btn - 32) & 0x20;
    bool is_wheel = (btn - 32) & 0x40;

    /// Decode modifiers
    event->modifiers = LLE_KEY_MOD_NONE;
    if ((btn - 32) & 0x04)
        event->modifiers |= LLE_KEY_MOD_SHIFT;
    if ((btn - 32) & 0x08)
        event->modifiers |= LLE_KEY_MOD_ALT;
    if ((btn - 32) & 0x10)
        event->modifiers |= LLE_KEY_MOD_CTRL;

    /// Decode coordinates (1-based, subtract 32 offset and convert to 0-based)
    event->x = (x_byte - 32 - 1);
    event->y = (y_byte - 32 - 1);

    /// Determine event type and button
    if (is_wheel) {
        event->type = LLE_MOUSE_EVENT_WHEEL;
        event->button = (button_code == 0) ? LLE_MOUSE_BUTTON_WHEEL_UP
                                           : LLE_MOUSE_BUTTON_WHEEL_DOWN;
        event->wheel_delta = (button_code == 0) ? 1 : -1;
    } else if (is_motion) {
        event->type = LLE_MOUSE_EVENT_MOVE;
        event->button = LLE_MOUSE_BUTTON_NONE;

        /// Check if dragging
        if (parser->pressed_buttons != LLE_MOUSE_BUTTON_NONE) {
            event->type = LLE_MOUSE_EVENT_DRAG;
            event->button = parser->pressed_buttons;
        }
    } else if (button_code == 3) {
        /// Button release
        event->type = LLE_MOUSE_EVENT_RELEASE;
        event->button = parser->pressed_buttons;
        parser->pressed_buttons = LLE_MOUSE_BUTTON_NONE;
    } else {
        /// Button press
        event->type = LLE_MOUSE_EVENT_PRESS;
        event->button =
            (lle_mouse_button_t)(button_code + 1); /// Convert 0-2 to 1-3
        parser->pressed_buttons = event->button;
    }

    event->timestamp = get_current_time_us();

    /// Update last position
    parser->last_x = event->x;
    parser->last_y = event->y;

    return LLE_SUCCESS;
}

/**
 * @brief Parse SGR extended mouse sequence
 *
 * Format: ESC[<btn;x;y>M (press) or ESC[<btn;x;y>m (release)
 * - btn: button code with modifiers
 * - x, y: decimal coordinates (no limit)
 * - M: press, m: release
 *
 * @param parser Mouse parser instance
 * @param data Raw sequence data
 * @param data_len Length of sequence data
 * @param event Output for parsed event
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t parse_sgr_sequence(lle_mouse_parser_t *parser,
                                       const char *data, size_t data_len,
                                       lle_mouse_event_info_t *event) {
    if (!parser || !data || data_len < 9 || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Verify prefix: ESC[<
    if (data[0] != '\x1B' || data[1] != '[' || data[2] != '<') {
        return LLE_ERROR_INVALID_FORMAT;
    }

    /// Parse button;x;y
    int btn = 0, x = 0, y = 0;
    const char *ptr = data + 3;
    const char *end = data + data_len;

    /// Parse button
    while (ptr < end && *ptr >= '0' && *ptr <= '9') {
        btn = btn * 10 + (*ptr - '0');
        ptr++;
    }

    if (ptr >= end || *ptr != ';') {
        return LLE_ERROR_INVALID_FORMAT;
    }
    ptr++; /// Skip semicolon

    /// Parse x
    while (ptr < end && *ptr >= '0' && *ptr <= '9') {
        x = x * 10 + (*ptr - '0');
        ptr++;
    }

    if (ptr >= end || *ptr != ';') {
        return LLE_ERROR_INVALID_FORMAT;
    }
    ptr++; /// Skip semicolon

    /// Parse y
    while (ptr < end && *ptr >= '0' && *ptr <= '9') {
        y = y * 10 + (*ptr - '0');
        ptr++;
    }

    if (ptr >= end) {
        return LLE_ERROR_INVALID_FORMAT;
    }

    /// Check terminator (M for press, m for release)
    bool is_release = (*ptr == 'm');
    if (*ptr != 'M' && *ptr != 'm') {
        return LLE_ERROR_INVALID_FORMAT;
    }

    /// Decode button
    int button_code = btn & 0x03;
    bool is_motion = btn & 0x20;
    bool is_wheel = btn & 0x40;

    /// Decode modifiers
    event->modifiers = LLE_KEY_MOD_NONE;
    if (btn & 0x04)
        event->modifiers |= LLE_KEY_MOD_SHIFT;
    if (btn & 0x08)
        event->modifiers |= LLE_KEY_MOD_ALT;
    if (btn & 0x10)
        event->modifiers |= LLE_KEY_MOD_CTRL;

    /// Set coordinates (SGR uses 1-based, convert to 0-based)
    event->x = x - 1;
    event->y = y - 1;

    /// Determine event type and button
    if (is_wheel) {
        event->type = LLE_MOUSE_EVENT_WHEEL;
        event->button = (button_code == 0) ? LLE_MOUSE_BUTTON_WHEEL_UP
                                           : LLE_MOUSE_BUTTON_WHEEL_DOWN;
        event->wheel_delta = (button_code == 0) ? 1 : -1;
    } else if (is_motion) {
        event->type = LLE_MOUSE_EVENT_MOVE;
        event->button = LLE_MOUSE_BUTTON_NONE;

        /// Check if dragging
        if (parser->pressed_buttons != LLE_MOUSE_BUTTON_NONE) {
            event->type = LLE_MOUSE_EVENT_DRAG;
            event->button = parser->pressed_buttons;
        }
    } else if (is_release) {
        event->type = LLE_MOUSE_EVENT_RELEASE;
        event->button = (parser->pressed_buttons != LLE_MOUSE_BUTTON_NONE)
                            ? parser->pressed_buttons
                            : (lle_mouse_button_t)(button_code + 1);
        parser->pressed_buttons = LLE_MOUSE_BUTTON_NONE;
    } else {
        event->type = LLE_MOUSE_EVENT_PRESS;
        event->button = (lle_mouse_button_t)(button_code + 1);
        parser->pressed_buttons = event->button;
    }

    event->timestamp = get_current_time_us();

    /// Update last position
    parser->last_x = event->x;
    parser->last_y = event->y;

    return LLE_SUCCESS;
}

/**
 * @brief Detect multi-click events (double/triple click)
 *
 * @param parser Mouse parser instance
 * @param event Event to check and update
 */
static void detect_multi_click(lle_mouse_parser_t *parser,
                               lle_mouse_event_info_t *event) {
    if (!parser || !event || event->type != LLE_MOUSE_EVENT_PRESS) {
        return;
    }

    uint64_t current_time = event->timestamp;
    uint64_t time_diff = current_time - parser->last_click_time;

    /// Check if within double-click time window (500ms)
    if (time_diff < (LLE_MOUSE_DOUBLE_CLICK_TIME_MS * 1000)) {
        parser->click_count++;
    } else {
        parser->click_count = 1;
    }

    parser->last_click_time = current_time;

    /// Set multi-click flags
    if (parser->click_count == 2) {
        event->double_click = true;
        event->triple_click = false;
    } else if (parser->click_count >= 3) {
        event->double_click = false;
        event->triple_click = true;
    } else {
        event->double_click = false;
        event->triple_click = false;
    }
}

/**
 * @brief Parse mouse sequence
 *
 * Detects format and dispatches to appropriate parser.
 *
 * @param parser Mouse parser instance
 * @param sequence Raw mouse sequence data
 * @param sequence_len Length of sequence data
 * @param event_info Output pointer for parsed event
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_mouse_parser_parse_sequence(lle_mouse_parser_t *parser,
                                const char *sequence, size_t sequence_len,
                                lle_mouse_event_info_t **event_info) {
    if (!parser || !sequence || !event_info) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (sequence_len < 6) {
        parser->invalid_mouse_sequences++;
        return LLE_ERROR_INVALID_FORMAT;
    }

    *event_info = NULL;

    /// Allocate event structure
    lle_mouse_event_info_t *event =
        lle_pool_alloc(sizeof(lle_mouse_event_info_t));
    if (!event) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(event, 0, sizeof(lle_mouse_event_info_t));

    lle_result_t result;

    /// Detect format and parse
    if (sequence_len >= 6 && sequence[0] == '\x1B' && sequence[1] == '[' &&
        sequence[2] == 'M') {
        /// X10 format
        result = parse_x10_sequence(parser, sequence, sequence_len, event);
    } else if (sequence_len >= 9 && sequence[0] == '\x1B' &&
               sequence[1] == '[' && sequence[2] == '<') {
        /// SGR format
        result = parse_sgr_sequence(parser, sequence, sequence_len, event);
    } else {
        /// Unknown format
        lle_pool_free(event);
        parser->invalid_mouse_sequences++;
        return LLE_ERROR_INVALID_FORMAT;
    }

    if (result != LLE_SUCCESS) {
        lle_pool_free(event);
        parser->invalid_mouse_sequences++;
        return result;
    }

    /// Detect multi-click
    detect_multi_click(parser, event);

    parser->mouse_events_parsed++;
    *event_info = event;

    return LLE_SUCCESS;
}

/**
 * @brief Enable/disable mouse tracking
 *
 * @param parser Mouse parser instance
 * @param enabled Whether to enable tracking
 * @param mode Tracking mode to use
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_mouse_parser_set_tracking(lle_mouse_parser_t *parser,
                                           bool enabled,
                                           lle_mouse_tracking_mode_t mode) {
    if (!parser) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    parser->mouse_tracking_enabled = enabled;
    parser->tracking_mode = mode;

    return LLE_SUCCESS;
}

/**
 * @brief Get mouse parser statistics
 *
 * @param parser Parser to query
 * @param events_parsed Output for total events parsed
 * @param invalid_sequences Output for invalid sequence count
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_mouse_parser_get_stats(const lle_mouse_parser_t *parser,
                                        uint64_t *events_parsed,
                                        uint64_t *invalid_sequences) {
    if (!parser || !events_parsed || !invalid_sequences) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *events_parsed = parser->mouse_events_parsed;
    *invalid_sequences = parser->invalid_mouse_sequences;

    return LLE_SUCCESS;
}

/**
 * @brief Reset mouse parser state
 *
 * @param parser Parser to reset
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_mouse_parser_reset(lle_mouse_parser_t *parser) {
    if (!parser) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    parser->last_x = 0;
    parser->last_y = 0;
    parser->pressed_buttons = LLE_MOUSE_BUTTON_NONE;
    parser->modifiers = LLE_KEY_MOD_NONE;
    parser->last_click_time = 0;
    parser->click_count = 0;

    /// Reset statistics
    parser->mouse_events_parsed = 0;
    parser->invalid_mouse_sequences = 0;

    return LLE_SUCCESS;
}

/**
 * @brief Get current mouse state
 *
 * @param parser Parser to query
 * @param x Output for current X position
 * @param y Output for current Y position
 * @param pressed_buttons Output for currently pressed buttons
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_mouse_parser_get_state(const lle_mouse_parser_t *parser,
                                        uint16_t *x, uint16_t *y,
                                        lle_mouse_button_t *pressed_buttons) {
    if (!parser || !x || !y || !pressed_buttons) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *x = parser->last_x;
    *y = parser->last_y;
    *pressed_buttons = parser->pressed_buttons;

    return LLE_SUCCESS;
}
