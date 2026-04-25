/**
 * @file key_detector.c
 * @brief Terminal Key Sequence Detection and Mapping
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Comprehensive key detection system that maps terminal escape sequences
 * to specific keys and key combinations. Supports function keys, cursor keys,
 * editing keys, and modifier combinations across multiple terminal types.
 *
 * Spec 06: Input Parsing - Phase 4
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
 * @brief Key sequence mapping table entry
 */
typedef struct {
    const char *sequence;          /**< Escape sequence bytes */
    size_t sequence_len;           /**< Length of sequence */
    lle_key_type_t type;           /**< Key type classification */
    uint32_t keycode;              /**< Key code value */
    lle_key_modifiers_t modifiers; /**< Modifier flags */
    const char *key_name;          /**< Human-readable key name */
} key_mapping_entry_t;

/**
 * Comprehensive key mapping table for common terminal types.
 * Based on xterm, VT100, and modern terminal emulators.
 */
static const key_mapping_entry_t key_mappings[] = {
    /* Function keys (xterm) */
    {"\x1BOP", 3, LLE_KEY_TYPE_FUNCTION, 1, LLE_KEY_MOD_NONE, "F1"},
    {"\x1BOQ", 3, LLE_KEY_TYPE_FUNCTION, 2, LLE_KEY_MOD_NONE, "F2"},
    {"\x1BOR", 3, LLE_KEY_TYPE_FUNCTION, 3, LLE_KEY_MOD_NONE, "F3"},
    {"\x1BOS", 3, LLE_KEY_TYPE_FUNCTION, 4, LLE_KEY_MOD_NONE, "F4"},
    {"\x1B[15~", 5, LLE_KEY_TYPE_FUNCTION, 5, LLE_KEY_MOD_NONE, "F5"},
    {"\x1B[17~", 5, LLE_KEY_TYPE_FUNCTION, 6, LLE_KEY_MOD_NONE, "F6"},
    {"\x1B[18~", 5, LLE_KEY_TYPE_FUNCTION, 7, LLE_KEY_MOD_NONE, "F7"},
    {"\x1B[19~", 5, LLE_KEY_TYPE_FUNCTION, 8, LLE_KEY_MOD_NONE, "F8"},
    {"\x1B[20~", 5, LLE_KEY_TYPE_FUNCTION, 9, LLE_KEY_MOD_NONE, "F9"},
    {"\x1B[21~", 5, LLE_KEY_TYPE_FUNCTION, 10, LLE_KEY_MOD_NONE, "F10"},
    {"\x1B[23~", 5, LLE_KEY_TYPE_FUNCTION, 11, LLE_KEY_MOD_NONE, "F11"},
    {"\x1B[24~", 5, LLE_KEY_TYPE_FUNCTION, 12, LLE_KEY_MOD_NONE, "F12"},

    /* Cursor keys (normal mode) */
    {"\x1B[A", 3, LLE_KEY_TYPE_CURSOR, 'A', LLE_KEY_MOD_NONE, "Up"},
    {"\x1B[B", 3, LLE_KEY_TYPE_CURSOR, 'B', LLE_KEY_MOD_NONE, "Down"},
    {"\x1B[C", 3, LLE_KEY_TYPE_CURSOR, 'C', LLE_KEY_MOD_NONE, "Right"},
    {"\x1B[D", 3, LLE_KEY_TYPE_CURSOR, 'D', LLE_KEY_MOD_NONE, "Left"},

    /* Cursor keys (application mode) */
    {"\x1BOA", 3, LLE_KEY_TYPE_CURSOR, 'A', LLE_KEY_MOD_NONE, "Up"},
    {"\x1BOB", 3, LLE_KEY_TYPE_CURSOR, 'B', LLE_KEY_MOD_NONE, "Down"},
    {"\x1BOC", 3, LLE_KEY_TYPE_CURSOR, 'C', LLE_KEY_MOD_NONE, "Right"},
    {"\x1BOD", 3, LLE_KEY_TYPE_CURSOR, 'D', LLE_KEY_MOD_NONE, "Left"},

    /* Navigation keys */
    {"\x1B[H", 3, LLE_KEY_TYPE_CURSOR, 'H', LLE_KEY_MOD_NONE, "Home"},
    {"\x1B[F", 3, LLE_KEY_TYPE_CURSOR, 'F', LLE_KEY_MOD_NONE, "End"},
    {"\x1B[1~", 4, LLE_KEY_TYPE_CURSOR, '1', LLE_KEY_MOD_NONE, "Home"},
    {"\x1B[4~", 4, LLE_KEY_TYPE_CURSOR, '4', LLE_KEY_MOD_NONE, "End"},
    {"\x1B[5~", 4, LLE_KEY_TYPE_CURSOR, '5', LLE_KEY_MOD_NONE, "PageUp"},
    {"\x1B[6~", 4, LLE_KEY_TYPE_CURSOR, '6', LLE_KEY_MOD_NONE, "PageDown"},

    /* Editing keys */
    {"\x1B[2~", 4, LLE_KEY_TYPE_EDITING, '2', LLE_KEY_MOD_NONE, "Insert"},
    {"\x1B[3~", 4, LLE_KEY_TYPE_EDITING, '3', LLE_KEY_MOD_NONE, "Delete"},
    {"\x7F", 1, LLE_KEY_TYPE_EDITING, 0x7F, LLE_KEY_MOD_NONE, "Backspace"},
    {"\x08", 1, LLE_KEY_TYPE_EDITING, 0x08, LLE_KEY_MOD_NONE, "Backspace"},

    /* Special keys */
    {"\x09", 1, LLE_KEY_TYPE_SPECIAL, 0x09, LLE_KEY_MOD_NONE, "Tab"},
    {"\x0D", 1, LLE_KEY_TYPE_SPECIAL, 0x0D, LLE_KEY_MOD_NONE, "Enter"},
    {"\x1B\x0D", 2, LLE_KEY_TYPE_SPECIAL, 0x0D, LLE_KEY_MOD_ALT, "Alt+Enter"},
    {"\x1B", 1, LLE_KEY_TYPE_SPECIAL, 0x1B, LLE_KEY_MOD_NONE, "Escape"},

    /* Modified cursor keys (Shift) */
    {"\x1B[1;2A", 6, LLE_KEY_TYPE_CURSOR, 'A', LLE_KEY_MOD_SHIFT, "Shift+Up"},
    {"\x1B[1;2B", 6, LLE_KEY_TYPE_CURSOR, 'B', LLE_KEY_MOD_SHIFT, "Shift+Down"},
    {"\x1B[1;2C", 6, LLE_KEY_TYPE_CURSOR, 'C', LLE_KEY_MOD_SHIFT,
     "Shift+Right"},
    {"\x1B[1;2D", 6, LLE_KEY_TYPE_CURSOR, 'D', LLE_KEY_MOD_SHIFT, "Shift+Left"},

    /* Modified cursor keys (Alt) */
    {"\x1B[1;3A", 6, LLE_KEY_TYPE_CURSOR, 'A', LLE_KEY_MOD_ALT, "Alt+Up"},
    {"\x1B[1;3B", 6, LLE_KEY_TYPE_CURSOR, 'B', LLE_KEY_MOD_ALT, "Alt+Down"},
    {"\x1B[1;3C", 6, LLE_KEY_TYPE_CURSOR, 'C', LLE_KEY_MOD_ALT, "Alt+Right"},
    {"\x1B[1;3D", 6, LLE_KEY_TYPE_CURSOR, 'D', LLE_KEY_MOD_ALT, "Alt+Left"},

    /* Modified cursor keys (Ctrl) */
    {"\x1B[1;5A", 6, LLE_KEY_TYPE_CURSOR, 'A', LLE_KEY_MOD_CTRL, "Ctrl+Up"},
    {"\x1B[1;5B", 6, LLE_KEY_TYPE_CURSOR, 'B', LLE_KEY_MOD_CTRL, "Ctrl+Down"},
    {"\x1B[1;5C", 6, LLE_KEY_TYPE_CURSOR, 'C', LLE_KEY_MOD_CTRL, "Ctrl+Right"},
    {"\x1B[1;5D", 6, LLE_KEY_TYPE_CURSOR, 'D', LLE_KEY_MOD_CTRL, "Ctrl+Left"},

    /* Control characters */
    {"\x01", 1, LLE_KEY_TYPE_CONTROL, 'A', LLE_KEY_MOD_CTRL, "Ctrl+A"},
    {"\x02", 1, LLE_KEY_TYPE_CONTROL, 'B', LLE_KEY_MOD_CTRL, "Ctrl+B"},
    {"\x03", 1, LLE_KEY_TYPE_CONTROL, 'C', LLE_KEY_MOD_CTRL, "Ctrl+C"},
    {"\x04", 1, LLE_KEY_TYPE_CONTROL, 'D', LLE_KEY_MOD_CTRL, "Ctrl+D"},
    {"\x05", 1, LLE_KEY_TYPE_CONTROL, 'E', LLE_KEY_MOD_CTRL, "Ctrl+E"},
    {"\x06", 1, LLE_KEY_TYPE_CONTROL, 'F', LLE_KEY_MOD_CTRL, "Ctrl+F"},
    {"\x07", 1, LLE_KEY_TYPE_CONTROL, 'G', LLE_KEY_MOD_CTRL, "Ctrl+G"},
    {"\x0B", 1, LLE_KEY_TYPE_CONTROL, 'K', LLE_KEY_MOD_CTRL, "Ctrl+K"},
    {"\x0C", 1, LLE_KEY_TYPE_CONTROL, 'L', LLE_KEY_MOD_CTRL, "Ctrl+L"},
    {"\x0E", 1, LLE_KEY_TYPE_CONTROL, 'N', LLE_KEY_MOD_CTRL, "Ctrl+N"},
    {"\x0F", 1, LLE_KEY_TYPE_CONTROL, 'O', LLE_KEY_MOD_CTRL, "Ctrl+O"},
    {"\x10", 1, LLE_KEY_TYPE_CONTROL, 'P', LLE_KEY_MOD_CTRL, "Ctrl+P"},
    {"\x11", 1, LLE_KEY_TYPE_CONTROL, 'Q', LLE_KEY_MOD_CTRL, "Ctrl+Q"},
    {"\x12", 1, LLE_KEY_TYPE_CONTROL, 'R', LLE_KEY_MOD_CTRL, "Ctrl+R"},
    {"\x13", 1, LLE_KEY_TYPE_CONTROL, 'S', LLE_KEY_MOD_CTRL, "Ctrl+S"},
    {"\x14", 1, LLE_KEY_TYPE_CONTROL, 'T', LLE_KEY_MOD_CTRL, "Ctrl+T"},
    {"\x15", 1, LLE_KEY_TYPE_CONTROL, 'U', LLE_KEY_MOD_CTRL, "Ctrl+U"},
    {"\x16", 1, LLE_KEY_TYPE_CONTROL, 'V', LLE_KEY_MOD_CTRL, "Ctrl+V"},
    {"\x17", 1, LLE_KEY_TYPE_CONTROL, 'W', LLE_KEY_MOD_CTRL, "Ctrl+W"},
    {"\x18", 1, LLE_KEY_TYPE_CONTROL, 'X', LLE_KEY_MOD_CTRL, "Ctrl+X"},
    {"\x19", 1, LLE_KEY_TYPE_CONTROL, 'Y', LLE_KEY_MOD_CTRL, "Ctrl+Y"},
    {"\x1A", 1, LLE_KEY_TYPE_CONTROL, 'Z', LLE_KEY_MOD_CTRL, "Ctrl+Z"},

    /* Meta/Alt + letter keys (lowercase) */
    {"\x1B"
     "f",
     2, LLE_KEY_TYPE_REGULAR, 'f', LLE_KEY_MOD_ALT, "Alt+F"},
    {"\x1B"
     "b",
     2, LLE_KEY_TYPE_REGULAR, 'b', LLE_KEY_MOD_ALT, "Alt+B"},
    {"\x1B"
     "c",
     2, LLE_KEY_TYPE_REGULAR, 'c', LLE_KEY_MOD_ALT, "Alt+C"},
    {"\x1B"
     "d",
     2, LLE_KEY_TYPE_REGULAR, 'd', LLE_KEY_MOD_ALT, "Alt+D"},
    {"\x1B"
     "l",
     2, LLE_KEY_TYPE_REGULAR, 'l', LLE_KEY_MOD_ALT, "Alt+L"},
    {"\x1B"
     "u",
     2, LLE_KEY_TYPE_REGULAR, 'u', LLE_KEY_MOD_ALT, "Alt+U"},

    /* Meta/Alt + special characters */
    {"\x1B"
     "<",
     2, LLE_KEY_TYPE_REGULAR, '<', LLE_KEY_MOD_ALT, "Alt+<"},
    {"\x1B"
     ">",
     2, LLE_KEY_TYPE_REGULAR, '>', LLE_KEY_MOD_ALT, "Alt+>"},
};

#define KEY_MAPPING_COUNT (sizeof(key_mappings) / sizeof(key_mappings[0]))

/**
 * @brief Initialize key sequence map
 *
 * @param map Output pointer for created map
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t init_key_sequence_map(lle_key_sequence_map_t **map,
                                          lle_memory_pool_t *memory_pool) {
    if (!map || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_key_sequence_map_t *new_map =
        lle_pool_alloc(sizeof(lle_key_sequence_map_t));
    if (!new_map) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Allocate entries array
    new_map->capacity = KEY_MAPPING_COUNT;
    new_map->entry_count = 0;
    new_map->memory_pool = memory_pool;

    new_map->entries = lle_pool_alloc(sizeof(lle_key_sequence_map_entry_t) *
                                      new_map->capacity);
    if (!new_map->entries) {
        lle_pool_free(new_map);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Populate map from static table
    for (size_t i = 0; i < KEY_MAPPING_COUNT; i++) {
        const key_mapping_entry_t *src = &key_mappings[i];
        lle_key_sequence_map_entry_t *dst = &new_map->entries[i];

        // Copy sequence
        if (src->sequence_len > LLE_MAX_KEY_SEQUENCE_LENGTH) {
            continue; // Skip if too long
        }

        memcpy(dst->sequence, src->sequence, src->sequence_len);
        dst->sequence_length = src->sequence_len;

        // Fill key info
        dst->key_info.type = src->type;
        dst->key_info.keycode = src->keycode;
        dst->key_info.modifiers = src->modifiers;
        strncpy(dst->key_info.key_name, src->key_name,
                sizeof(dst->key_info.key_name) - 1);
        dst->key_info.key_name[sizeof(dst->key_info.key_name) - 1] = '\0';
        dst->key_info.is_repeat = false;
        dst->key_info.timestamp = 0;

        new_map->entry_count++;
    }

    *map = new_map;
    return LLE_SUCCESS;
}

/**
 * @brief Match sequence to key in mapping table
 *
 * @param map Key sequence map
 * @param sequence Sequence bytes to match
 * @param sequence_len Length of sequence
 * @param key_info Output for matched key info
 * @return Match type (exact, prefix, ambiguous, or none)
 */
static lle_key_sequence_match_type_t
match_sequence(const lle_key_sequence_map_t *map, const char *sequence,
               size_t sequence_len, lle_key_info_t *key_info) {
    if (!map || !sequence || sequence_len == 0) {
        return LLE_MATCH_NONE;
    }

    bool has_exact_match = false;
    bool has_prefix_match = false;
    lle_key_sequence_map_entry_t *exact_entry = NULL;

    // Scan for matches
    for (size_t i = 0; i < map->entry_count; i++) {
        lle_key_sequence_map_entry_t *entry = &map->entries[i];

        if (entry->sequence_length == sequence_len) {
            // Check for exact match
            if (memcmp(entry->sequence, sequence, sequence_len) == 0) {
                has_exact_match = true;
                exact_entry = entry;
                // Don't break - check if there are longer sequences with this
                // as prefix
            }
        } else if (entry->sequence_length > sequence_len) {
            // Check if current sequence is a prefix
            if (memcmp(entry->sequence, sequence, sequence_len) == 0) {
                has_prefix_match = true;
            }
        }
    }

    if (has_exact_match) {
        if (has_prefix_match) {
            // Ambiguous - exact match but also prefix of longer sequences
            if (key_info && exact_entry) {
                memcpy(key_info, &exact_entry->key_info,
                       sizeof(lle_key_info_t));
            }
            return LLE_MATCH_AMBIGUOUS;
        } else {
            // Exact match, no ambiguity
            if (key_info && exact_entry) {
                memcpy(key_info, &exact_entry->key_info,
                       sizeof(lle_key_info_t));
            }
            return LLE_MATCH_EXACT;
        }
    } else if (has_prefix_match) {
        // Prefix match - need more data
        return LLE_MATCH_PREFIX;
    }

    return LLE_MATCH_NONE;
}

/**
 * @brief Initialize key detector
 *
 * @param detector Output pointer for created detector
 * @param terminal_caps Terminal capabilities for feature detection
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_key_detector_init(lle_key_detector_t **detector,
                                   lle_terminal_capabilities_t *terminal_caps,
                                   lle_memory_pool_t *memory_pool) {
    if (!detector || !terminal_caps || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_key_detector_t *new_detector =
        lle_pool_alloc(sizeof(lle_key_detector_t));
    if (!new_detector) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(new_detector, 0, sizeof(lle_key_detector_t));

    new_detector->terminal_caps = terminal_caps;
    new_detector->memory_pool = memory_pool;
    new_detector->sequence_pos = 0;
    new_detector->sequence_start_time = 0;
    new_detector->ambiguous_sequence = false;

    // Initialize sequence map
    lle_result_t result =
        init_key_sequence_map(&new_detector->sequence_map, memory_pool);
    if (result != LLE_SUCCESS) {
        lle_pool_free(new_detector);
        return result;
    }

    *detector = new_detector;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy key detector
 *
 * @param detector Detector to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_key_detector_destroy(lle_key_detector_t *detector) {
    if (!detector) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (detector->sequence_map) {
        if (detector->sequence_map->entries) {
            lle_pool_free(detector->sequence_map->entries);
        }
        lle_pool_free(detector->sequence_map);
    }

    lle_pool_free(detector);
    return LLE_SUCCESS;
}

/**
 * @brief Check if sequence has timed out
 *
 * @param detector Key detector instance
 * @return true if sequence has timed out, false otherwise
 */
static bool has_timed_out(lle_key_detector_t *detector) {
    if (detector->sequence_start_time == 0) {
        return false;
    }

    uint64_t current_time = get_current_time_us();
    uint64_t elapsed = current_time - detector->sequence_start_time;

    return elapsed > LLE_KEY_SEQUENCE_TIMEOUT_US;
}

/**
 * @brief Process key sequence
 *
 * @param detector Key detector instance
 * @param sequence Sequence bytes to process
 * @param sequence_len Length of sequence
 * @param key_info Output pointer for detected key info
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_key_detector_process_sequence(lle_key_detector_t *detector,
                                               const char *sequence,
                                               size_t sequence_len,
                                               lle_key_info_t **key_info) {
    if (!detector || !sequence || !key_info) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *key_info = NULL;

    lle_key_sequence_match_result_t result;
    memset(&result, 0, sizeof(lle_key_sequence_match_result_t));

    // Add sequence to buffer
    if (detector->sequence_pos + sequence_len > LLE_MAX_KEY_SEQUENCE_LENGTH) {
        // Buffer overflow - reset
        detector->sequence_pos = 0;
        detector->sequence_start_time = 0;
        detector->ambiguous_sequence = false;
        return LLE_SUCCESS;
    }

    memcpy(detector->sequence_buffer + detector->sequence_pos, sequence,
           sequence_len);
    detector->sequence_pos += sequence_len;

    // Start timing if first byte
    if (detector->sequence_pos == sequence_len) {
        detector->sequence_start_time = get_current_time_us();
    }

    // Try to match
    lle_key_info_t matched_key;
    lle_key_sequence_match_type_t match_type =
        match_sequence(detector->sequence_map, detector->sequence_buffer,
                       detector->sequence_pos, &matched_key);

    switch (match_type) {
    case LLE_MATCH_EXACT:
        // Exact match found - allocate and return key info
        *key_info = lle_pool_alloc(sizeof(lle_key_info_t));
        if (*key_info) {
            memcpy(*key_info, &matched_key, sizeof(lle_key_info_t));
            (*key_info)->timestamp = get_current_time_us();
            memcpy((*key_info)->sequence, detector->sequence_buffer,
                   detector->sequence_pos);
            (*key_info)->sequence_length = detector->sequence_pos;
        }

        detector->sequences_detected++;
        detector->sequences_resolved++;

        // Reset buffer
        detector->sequence_pos = 0;
        detector->sequence_start_time = 0;
        detector->ambiguous_sequence = false;
        break;

    case LLE_MATCH_AMBIGUOUS:
        // Ambiguous - could match or could be prefix
        detector->ambiguous_sequence = true;
        detector->sequences_detected++;

        // Check for timeout
        if (has_timed_out(detector)) {
            // Timeout - accept the match
            *key_info = lle_pool_alloc(sizeof(lle_key_info_t));
            if (*key_info) {
                memcpy(*key_info, &matched_key, sizeof(lle_key_info_t));
                (*key_info)->timestamp = get_current_time_us();
                memcpy((*key_info)->sequence, detector->sequence_buffer,
                       detector->sequence_pos);
                (*key_info)->sequence_length = detector->sequence_pos;
            }

            detector->ambiguous_timeouts++;
            detector->sequences_resolved++;

            // Reset buffer
            detector->sequence_pos = 0;
            detector->sequence_start_time = 0;
            detector->ambiguous_sequence = false;
        }
        // Otherwise keep waiting for more data or timeout
        break;

    case LLE_MATCH_PREFIX:
        // Prefix match - need more data, keep buffering
        break;

    case LLE_MATCH_NONE:
        // No match - reset
        detector->sequence_pos = 0;
        detector->sequence_start_time = 0;
        detector->ambiguous_sequence = false;
        break;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Get detector statistics
 *
 * @param detector Detector to query
 * @param sequences_detected Output for total sequences detected
 * @param sequences_resolved Output for total sequences resolved
 * @param ambiguous_timeouts Output for ambiguous timeout count
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_key_detector_get_stats(const lle_key_detector_t *detector,
                                        uint64_t *sequences_detected,
                                        uint64_t *sequences_resolved,
                                        uint64_t *ambiguous_timeouts) {
    if (!detector || !sequences_detected || !sequences_resolved ||
        !ambiguous_timeouts) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *sequences_detected = detector->sequences_detected;
    *sequences_resolved = detector->sequences_resolved;
    *ambiguous_timeouts = detector->ambiguous_timeouts;

    return LLE_SUCCESS;
}

/**
 * @brief Reset detector state
 *
 * @param detector Detector to reset
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_key_detector_reset(lle_key_detector_t *detector) {
    if (!detector) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    detector->sequence_pos = 0;
    detector->sequence_start_time = 0;
    detector->ambiguous_sequence = false;
    memset(detector->sequence_buffer, 0, sizeof(detector->sequence_buffer));

    return LLE_SUCCESS;
}

/**
 * @brief Check if detector is waiting for more data
 *
 * @param detector Detector to query
 * @return true if waiting for more data, false otherwise
 */
bool lle_key_detector_is_waiting(const lle_key_detector_t *detector) {
    if (!detector) {
        return false;
    }

    return detector->sequence_pos > 0;
}
