/**
 * @file command_layer.c
 * @brief Command Layer - Real-Time Syntax Highlighting System
 *
 * Part of the Lush Shell Layered Display Architecture.
 * Provides syntax highlighting for command input.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * ============================================================================
 *
 * COMMAND LAYER IMPLEMENTATION
 *
 * This file implements the command layer of the Lush Display System,
 * providing real-time syntax highlighting for command input that works
 * universally with ANY prompt structure.
 *
 * Key Implementation Features:
 * - Real-time syntax highlighting using existing lush functions
 * - Universal compatibility with any prompt structure
 * - High-performance command processing (<5ms updates)
 * - Intelligent caching system for syntax highlighting results
 * - Event-driven communication with prompt layer
 * - Memory-safe command text management
 * - Integration with existing readline and syntax highlighting systems
 *
 * Architecture Integration:
 * - Leverages existing lush_output_colored_line() for syntax highlighting
 * - Coordinates with prompt layer through event system
 * - Maintains independence from prompt structure
 * - Provides positioning information for universal composition
 *
 * Performance Targets:
 * - Command update processing: <5ms
 * - Cache hit rate: >80% for repeated commands
 * - Memory usage: <2MB for command layer
 * - Zero memory leaks (valgrind clean)
 */

#include "display/command_layer.h"
#include "display/base_terminal.h"
#include "display/display_controller.h"
#include "display_integration.h"
#include "lle/adaptive_terminal_integration.h"
#include "lle/prompt/theme.h"
#include "lle/syntax_highlighting.h"

/// Note: Completion menu support moved to display_controller (proper
/// architecture)

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

/// ============================================================================
/// CONSTANTS AND INTERNAL CONFIGURATION
/// ============================================================================

#define COMMAND_LAYER_MAGIC 0x434D444C /// "CMDL" in hex
#define COMMAND_LAYER_VERSION_STRING "1.0.0"

/// Performance monitoring constants
#define NANOSECONDS_PER_SECOND 1000000000ULL
#define NANOSECONDS_PER_MILLISECOND 1000000ULL

/// Cache management constants
#define CACHE_HASH_MULTIPLIER 31
#define CACHE_CLEANUP_THRESHOLD 0.8

/// ============================================================================
/// INTERNAL STRUCTURES AND STATE
/// ============================================================================

/**
 * Syntax highlighting statistics
 */
typedef struct {
    uint64_t tokens_parsed;
    uint64_t regions_created;
    uint64_t cache_lookups;
    uint64_t highlighting_time_ns;
} highlighting_stats_t;

/// Global statistics for monitoring
static highlighting_stats_t g_highlighting_stats = {0};

/// ============================================================================
/// FORWARD DECLARATIONS
/// ============================================================================

/// Core functionality
static command_layer_error_t
perform_syntax_highlighting(command_layer_t *layer);
static command_layer_error_t update_command_metrics(command_layer_t *layer);
static uint64_t get_current_time_ns(void);
static void update_performance_stats(command_layer_t *layer,
                                     uint64_t operation_time_ns);

/// Cache management
static uint32_t calculate_command_hash(const char *command);
static command_cache_entry_t *find_cache_entry(command_layer_t *layer,
                                               const char *command);
static command_layer_error_t add_to_cache(command_layer_t *layer,
                                          const char *command,
                                          const char *highlighted,
                                          const command_metrics_t *metrics);
static void expire_old_cache_entries(command_layer_t *layer);

/// Event handling
static layer_events_error_t handle_layer_event(const layer_event_t *event,
                                               void *user_data);
static command_layer_error_t
publish_command_event(command_layer_t *layer, layer_event_type_t event_type);

/// Validation and utilities
static bool validate_layer_state(command_layer_t *layer);
static void reset_layer_state(command_layer_t *layer);
static size_t safe_string_copy(char *dest, const char *src, size_t dest_size);

/// ============================================================================
/// CORE API IMPLEMENTATION
/// ============================================================================

const char *command_layer_get_version(void) {
    return COMMAND_LAYER_VERSION_STRING;
}

command_layer_t *command_layer_create(void) {
    command_layer_t *layer = calloc(1, sizeof(command_layer_t));
    if (!layer) {
        return NULL;
    }

    /// Initialize magic number for validation
    layer->magic = COMMAND_LAYER_MAGIC;
    layer->initialized = false;
    layer->needs_redraw = false;

    /// Initialize command content
    layer->command_text[0] = '\0';
    layer->highlighted_text[0] = '\0';
    layer->cursor_position = 0;

    /// Initialize syntax highlighting state
    layer->region_count = 0;
    layer->syntax_config.enabled = true;
    layer->syntax_config.use_colors = true;
    layer->syntax_config.highlight_errors = true;
    layer->syntax_config.cache_enabled = true;
    layer->syntax_config.cache_expiry_ms = COMMAND_LAYER_CACHE_EXPIRY_MS;
    layer->syntax_config.max_update_time_ms =
        COMMAND_LAYER_TARGET_UPDATE_TIME_MS;

    /// Initialize performance statistics
    memset(&layer->performance, 0, sizeof(command_performance_t));
    layer->performance.min_update_time_ns = UINT64_MAX;

    /// Initialize cache
    layer->cache_size = 0;
    layer->cache_access_count = 0;
    for (size_t i = 0; i < COMMAND_LAYER_CACHE_SIZE; i++) {
        layer->cache[i].is_valid = false;
    }

    /// Initialize event system integration
    layer->event_system = NULL;
    layer->event_subscription_id = 0;

    /// Initialize prompt layer integration
    layer->prompt_layer = NULL;
    layer->prompt_integration_enabled = false;

    /// Initialize completion menu state
    layer->completion_menu_visible = false;
    layer->completion_menu_content = NULL;
    layer->completion_menu_content_size = 0;
    layer->completion_menu_lines = 0;
    layer->completion_menu_selected_index = 0;

    /// Initialize performance monitoring
    clock_gettime(CLOCK_MONOTONIC, &layer->last_update_time);
    layer->update_sequence_number = 0;

    /// Initialize spec-compliant syntax highlighter (Spec 11)
    layer->spec_highlighter = NULL;
    if (lle_syntax_highlighter_create(&layer->spec_highlighter) != 0) {
        /// Highlighter creation failed - continue without it, fall back to
        /// inline
        layer->spec_highlighter = NULL;
    }

    return layer;
}

command_layer_error_t command_layer_init(command_layer_t *layer,
                                         layer_event_system_t *events) {
    if (!layer || layer->magic != COMMAND_LAYER_MAGIC) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    if (!events) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    /// Store event system reference
    layer->event_system = events;

    /// Subscribe to relevant events
    /// Temporarily disable theme subscription to isolate issue
    ///     layer_events_error_t event_result = layer_events_subscribe(
    ///         events,
    ///         LAYER_EVENT_THEME_CHANGED,
    ///         LAYER_ID_COMMAND_LAYER,
    ///         handle_layer_event,
    ///         layer,
    ///         LAYER_EVENT_PRIORITY_HIGH
    ///     );
    ///
    ///     if (event_result != LAYER_EVENTS_SUCCESS) {
    ///         return COMMAND_LAYER_ERROR_EVENT_SYSTEM;
    ///     }

    /// Subscribe to prompt layer events if available
    layer_events_error_t event_result = layer_events_subscribe(
        events, LAYER_EVENT_CONTENT_CHANGED, LAYER_ID_COMMAND_LAYER,
        handle_layer_event, layer, LAYER_EVENT_PRIORITY_NORMAL);

    if (event_result != LAYER_EVENTS_SUCCESS) {
        return COMMAND_LAYER_ERROR_EVENT_SYSTEM;
    }

    layer->initialized = true;

    /// Publish initialization complete event
    publish_command_event(layer, LAYER_EVENT_INITIALIZATION_COMPLETE);

    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_set_command(command_layer_t *layer,
                                                const char *command_text,
                                                size_t cursor_pos) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    if (!command_text) {
        return COMMAND_LAYER_ERROR_NULL_POINTER;
    }

    size_t command_length = strlen(command_text);
    if (command_length >= COMMAND_LAYER_MAX_COMMAND_SIZE) {
        return COMMAND_LAYER_ERROR_COMMAND_TOO_LARGE;
    }

    if (cursor_pos > command_length) {
        return COMMAND_LAYER_ERROR_INVALID_CURSOR_POS;
    }

    uint64_t start_time = get_current_time_ns();

    /// Check if command has changed
    bool command_changed = (strcmp(layer->command_text, command_text) != 0);
    bool cursor_changed = (layer->cursor_position != cursor_pos);

    /// Always need initial render even if buffer is empty
    bool is_first_render = (layer->update_sequence_number == 0);

    /// Check if completion menu or notification state changed (even if
    /// command/cursor didn't). When menu/notification is shown/hidden, we need
    /// redraw even if command text unchanged.
    bool menu_changed = false;
    bool notification_changed = false;
    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        menu_changed = display_controller_check_and_clear_menu_changed(dc);
        notification_changed =
            display_controller_check_and_clear_notification_changed(dc);
    }

    if (!command_changed && !cursor_changed && !is_first_render &&
        !menu_changed && !notification_changed) {
        /// No change, just update performance stats with minimal time
        update_performance_stats(layer, get_current_time_ns() - start_time);
        return COMMAND_LAYER_SUCCESS;
    }

    /// Update command text and cursor position
    safe_string_copy(layer->command_text, command_text,
                     sizeof(layer->command_text));
    layer->cursor_position = cursor_pos;
    layer->needs_redraw = true;
    layer->update_sequence_number++;

    /// Check cache first for performance
    command_cache_entry_t *cached = NULL;
    if (layer->syntax_config.cache_enabled && command_changed) {
        cached = find_cache_entry(layer, command_text);
        layer->performance.cache_hits += (cached != NULL) ? 1 : 0;
        layer->performance.cache_misses += (cached == NULL) ? 1 : 0;

        /// Record layer-specific cache operation
        display_integration_record_layer_cache_operation("command_layer",
                                                         (cached != NULL));
    }

    if (cached && cached->is_valid) {
        /// Use cached result
        safe_string_copy(layer->highlighted_text, cached->highlighted_text,
                         sizeof(layer->highlighted_text));
        layer->metrics = cached->metrics;
        layer->region_count =
            0; /// Reset regions as they're embedded in highlighted text
    } else {
        /// Perform syntax highlighting
        command_layer_error_t result = perform_syntax_highlighting(layer);
        if (result != COMMAND_LAYER_SUCCESS) {
            update_performance_stats(layer, get_current_time_ns() - start_time);
            return result;
        }

        /// Update metrics
        result = update_command_metrics(layer);
        if (result != COMMAND_LAYER_SUCCESS) {
            update_performance_stats(layer, get_current_time_ns() - start_time);
            return result;
        }

        /// Add to cache if enabled
        if (layer->syntax_config.cache_enabled && command_changed) {
            add_to_cache(layer, command_text, layer->highlighted_text,
                         &layer->metrics);
        }
    }

    /// Update cursor position in metrics
    layer->metrics.cursor_position = cursor_pos;

    uint64_t operation_time = get_current_time_ns() - start_time;
    update_performance_stats(layer, operation_time);

    /// Publish content changed event if command changed
    if (command_changed) {
        publish_command_event(layer, LAYER_EVENT_CONTENT_CHANGED);
    }

    /// Publish cursor moved event if cursor changed
    if (cursor_changed) {
        publish_command_event(layer, LAYER_EVENT_CURSOR_MOVED);
    }

    /// Publish redraw needed event (triggers display_controller to render)
    /// This was previously done by command_layer_update(), but that caused
    /// redundant syntax highlighting. Now we do it here after highlighting.
    publish_command_event(layer, LAYER_EVENT_REDRAW_NEEDED);

    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_get_highlighted_text(command_layer_t *layer,
                                                         char *output,
                                                         size_t output_size) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    if (!output || output_size == 0) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    size_t highlighted_length = strlen(layer->highlighted_text);
    if (highlighted_length >= output_size) {
        return COMMAND_LAYER_ERROR_BUFFER_TOO_SMALL;
    }

    safe_string_copy(output, layer->highlighted_text, output_size);
    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_set_cursor_position(command_layer_t *layer,
                                                        size_t cursor_pos) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    size_t command_length = strlen(layer->command_text);
    if (cursor_pos > command_length) {
        return COMMAND_LAYER_ERROR_INVALID_CURSOR_POS;
    }

    if (layer->cursor_position != cursor_pos) {
        layer->cursor_position = cursor_pos;
        layer->metrics.cursor_position = cursor_pos;
        layer->needs_redraw = true;

        /// Publish cursor moved event
        publish_command_event(layer, LAYER_EVENT_CURSOR_MOVED);
    }

    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_get_metrics(command_layer_t *layer,
                                                command_metrics_t *metrics) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    if (!metrics) {
        return COMMAND_LAYER_ERROR_NULL_POINTER;
    }

    *metrics = layer->metrics;
    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_update(command_layer_t *layer) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    uint64_t start_time = get_current_time_ns();

    /// Force syntax highlighting update
    command_layer_error_t result = perform_syntax_highlighting(layer);
    if (result != COMMAND_LAYER_SUCCESS) {
        return result;
    }

    /// Update metrics
    result = update_command_metrics(layer);
    if (result != COMMAND_LAYER_SUCCESS) {
        return result;
    }

    layer->needs_redraw = true;
    layer->update_sequence_number++;

    uint64_t operation_time = get_current_time_ns() - start_time;
    update_performance_stats(layer, operation_time);

    /// Publish redraw needed event
    publish_command_event(layer, LAYER_EVENT_REDRAW_NEEDED);

    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_clear(command_layer_t *layer) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    /// Clear command content
    layer->command_text[0] = '\0';
    layer->highlighted_text[0] = '\0';
    layer->cursor_position = 0;
    layer->region_count = 0;
    layer->needs_redraw = true;

    /// Reset metrics
    memset(&layer->metrics, 0, sizeof(command_metrics_t));

    /// Publish content changed event
    publish_command_event(layer, LAYER_EVENT_CONTENT_CHANGED);

    return COMMAND_LAYER_SUCCESS;
}

/// ============================================================================
/// NOTE: Completion menu integration moved to display_controller
/// All menu-related functions removed - proper architecture now in place
/// ============================================================================

command_layer_error_t command_layer_cleanup(command_layer_t *layer) {
    if (!layer || layer->magic != COMMAND_LAYER_MAGIC) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    /// Unsubscribe from events if event system is available
    if (layer->event_system && layer->initialized) {
        layer_events_unsubscribe(layer->event_system, LAYER_EVENT_THEME_CHANGED,
                                 LAYER_ID_COMMAND_LAYER);
        layer_events_unsubscribe(layer->event_system,
                                 LAYER_EVENT_CONTENT_CHANGED,
                                 LAYER_ID_COMMAND_LAYER);
    }

    /// Clear cache
    for (size_t i = 0; i < COMMAND_LAYER_CACHE_SIZE; i++) {
        layer->cache[i].is_valid = false;
    }
    layer->cache_size = 0;

    /// Free completion menu content
    if (layer->completion_menu_content) {
        free(layer->completion_menu_content);
        layer->completion_menu_content = NULL;
        layer->completion_menu_content_size = 0;
    }
    layer->completion_menu_visible = false;
    layer->completion_menu_lines = 0;
    layer->completion_menu_selected_index = 0;

    /// Reset state
    reset_layer_state(layer);
    layer->initialized = false;

    return COMMAND_LAYER_SUCCESS;
}

void command_layer_destroy(command_layer_t *layer) {
    if (!layer) {
        return;
    }

    /// Cleanup if not already done
    if (layer->initialized) {
        command_layer_cleanup(layer);
    }

    /// Destroy spec highlighter
    if (layer->spec_highlighter) {
        lle_syntax_highlighter_destroy(layer->spec_highlighter);
        layer->spec_highlighter = NULL;
    }

    /// Clear magic number
    layer->magic = 0;

    /// Free memory
    free(layer);
}

/// ============================================================================
/// SYNTAX HIGHLIGHTING IMPLEMENTATION
/// ============================================================================

/**
 * @brief Primary syntax highlighting using spec-compliant system (Spec 11)
 *
 * Delegates entirely to the LLE spec-compliant syntax highlighter which
 * handles tokenization and ANSI color rendering. Falls back to plain text
 * if the highlighter is unavailable.
 *
 * @param layer Command layer to perform highlighting on
 * @return COMMAND_LAYER_SUCCESS on success, error code otherwise
 */
static command_layer_error_t
perform_syntax_highlighting(command_layer_t *layer) {
    if (!layer->syntax_config.enabled || !layer->syntax_config.use_colors) {
        /// If highlighting is disabled, just copy the command text
        safe_string_copy(layer->highlighted_text, layer->command_text,
                         sizeof(layer->highlighted_text));
        layer->region_count = 0;
        return COMMAND_LAYER_SUCCESS;
    }

    uint64_t start_time = get_current_time_ns();

    /// Initialize highlighting output
    layer->highlighted_text[0] = '\0';
    layer->region_count = 0;

    if (!layer->command_text[0]) {
        /// Empty command, nothing to highlight
        return COMMAND_LAYER_SUCCESS;
    }

    /// Use spec-compliant highlighter (Spec 11)
    if (layer->spec_highlighter) {
        size_t command_len = strlen(layer->command_text);

        /// Tokenize using spec highlighter
        int token_count = lle_syntax_highlight(
            layer->spec_highlighter, layer->command_text, command_len);

        if (token_count >= 0) {
            /// Use spec highlighter's render function which applies its own
            /// colors based on the LLE theme system
            int rendered = lle_syntax_render_ansi(
                layer->spec_highlighter, layer->command_text,
                layer->highlighted_text, sizeof(layer->highlighted_text));

            if (rendered >= 0) {
                uint64_t highlighting_time = get_current_time_ns() - start_time;
                g_highlighting_stats.highlighting_time_ns += highlighting_time;
                g_highlighting_stats.tokens_parsed += (uint64_t)token_count;
                return COMMAND_LAYER_SUCCESS;
            }
        }
        /// Spec highlighting returned error - fall through to plain text
    }

    /// No spec highlighter available or it failed - just copy plain text
    /// This is a graceful degradation: syntax highlighting is cosmetic,
    /// so we show unhighlighted text rather than failing
    safe_string_copy(layer->highlighted_text, layer->command_text,
                     sizeof(layer->highlighted_text));
    return COMMAND_LAYER_SUCCESS;
}

/// ============================================================================
/// CACHE MANAGEMENT
/// ============================================================================

/**
 * @brief Calculate hash of command string for cache lookup
 * @param command Command string to hash
 * @return 32-bit hash value
 */
static uint32_t calculate_command_hash(const char *command) {
    uint32_t hash = 0;
    for (const char *p = command; *p; p++) {
        hash = hash * CACHE_HASH_MULTIPLIER + (unsigned char)*p;
    }
    return hash;
}

/**
 * @brief Find cache entry for a command string
 * @param layer Command layer containing cache
 * @param command Command string to look up
 * @return Pointer to cache entry if found, NULL otherwise
 */
static command_cache_entry_t *find_cache_entry(command_layer_t *layer,
                                               const char *command) {
    if (!layer->syntax_config.cache_enabled || !command) {
        return NULL;
    }

    uint32_t hash = calculate_command_hash(command);
    uint64_t current_time = get_current_time_ns();

    for (size_t i = 0; i < layer->cache_size; i++) {
        command_cache_entry_t *entry = &layer->cache[i];

        if (!entry->is_valid) {
            continue;
        }

        /// Check if cache entry has expired
        uint64_t age_ns = current_time - entry->timestamp_ns;
        if (age_ns > (layer->syntax_config.cache_expiry_ms *
                      NANOSECONDS_PER_MILLISECOND)) {
            entry->is_valid = false;
            continue;
        }

        /// Check if hash and command match
        if (entry->hash == hash && strcmp(entry->command_text, command) == 0) {
            return entry;
        }
    }

    return NULL;
}

/**
 * @brief Add command and highlighted text to cache
 * @param layer Command layer containing cache
 * @param command Original command string
 * @param highlighted Syntax-highlighted command string
 * @param metrics Command metrics to cache
 * @return COMMAND_LAYER_SUCCESS on success, error code otherwise
 */
static command_layer_error_t add_to_cache(command_layer_t *layer,
                                          const char *command,
                                          const char *highlighted,
                                          const command_metrics_t *metrics) {
    if (!layer->syntax_config.cache_enabled || !command || !highlighted ||
        !metrics) {
        return COMMAND_LAYER_SUCCESS; /// Not an error, just disabled
    }

    /// Find empty slot or oldest entry
    command_cache_entry_t *entry = NULL;
    uint64_t oldest_time = UINT64_MAX;
    size_t oldest_index = 0;

    for (size_t i = 0; i < COMMAND_LAYER_CACHE_SIZE; i++) {
        if (!layer->cache[i].is_valid) {
            entry = &layer->cache[i];
            break;
        }

        if (layer->cache[i].timestamp_ns < oldest_time) {
            oldest_time = layer->cache[i].timestamp_ns;
            oldest_index = i;
        }
    }

    /// If no empty slot found, use oldest entry
    if (!entry) {
        entry = &layer->cache[oldest_index];
    } else if (layer->cache_size < COMMAND_LAYER_CACHE_SIZE) {
        layer->cache_size++;
    }

    /// Store cache entry
    safe_string_copy(entry->command_text, command, sizeof(entry->command_text));
    safe_string_copy(entry->highlighted_text, highlighted,
                     sizeof(entry->highlighted_text));
    entry->metrics = *metrics;
    entry->timestamp_ns = get_current_time_ns();
    entry->hash = calculate_command_hash(command);
    entry->is_valid = true;

    return COMMAND_LAYER_SUCCESS;
}

/**
 * @brief Expire old cache entries based on age
 * @param layer Command layer containing cache to clean
 */
MAYBE_UNUSED
static void expire_old_cache_entries(command_layer_t *layer) {
    uint64_t current_time = get_current_time_ns();
    uint64_t expiry_time_ns =
        layer->syntax_config.cache_expiry_ms * NANOSECONDS_PER_MILLISECOND;

    for (size_t i = 0; i < layer->cache_size; i++) {
        command_cache_entry_t *entry = &layer->cache[i];

        if (entry->is_valid) {
            uint64_t age_ns = current_time - entry->timestamp_ns;
            if (age_ns > expiry_time_ns) {
                entry->is_valid = false;
            }
        }
    }
}

/// ============================================================================
/// METRICS AND PERFORMANCE
/// ============================================================================

/**
 * @brief Update command metrics from current layer state
 * @param layer Command layer to update metrics for
 * @return COMMAND_LAYER_SUCCESS on success, error code otherwise
 */
static command_layer_error_t update_command_metrics(command_layer_t *layer) {
    command_metrics_t *metrics = &layer->metrics;

    /// Basic metrics
    metrics->command_length = strlen(layer->command_text);
    metrics->visual_length = strlen(layer->highlighted_text);
    metrics->token_count = layer->region_count;
    metrics->cursor_position = layer->cursor_position;

    /// Error detection
    metrics->error_count = 0;
    metrics->has_syntax_errors = false;
    for (size_t i = 0; i < layer->region_count; i++) {
        if (layer->highlight_regions[i].token_type == COMMAND_TOKEN_ERROR) {
            metrics->error_count++;
            metrics->has_syntax_errors = true;
        }
    }

    /// Multiline detection
    metrics->is_multiline_command = (strchr(layer->command_text, '\n') != NULL);

    /// Display positioning (will be updated by composition engine)
    metrics->estimated_display_column = 0;
    metrics->estimated_display_row = 0;

    /// If prompt layer integration is available, get positioning info
    if (layer->prompt_integration_enabled && layer->prompt_layer) {
        prompt_metrics_t prompt_metrics;
        if (prompt_layer_get_metrics(layer->prompt_layer, &prompt_metrics) ==
            PROMPT_LAYER_SUCCESS) {
            metrics->estimated_display_column =
                prompt_metrics.estimated_command_column;
            metrics->estimated_display_row =
                prompt_metrics.estimated_command_row;
        }
    }

    return COMMAND_LAYER_SUCCESS;
}

/**
 * @brief Get current timestamp in nanoseconds
 * @return Current time in nanoseconds from monotonic clock
 */
static uint64_t get_current_time_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * NANOSECONDS_PER_SECOND + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Update performance statistics after an operation
 * @param layer Command layer to update stats for
 * @param operation_time_ns Time taken for operation in nanoseconds
 */
static void update_performance_stats(command_layer_t *layer,
                                     uint64_t operation_time_ns) {
    command_performance_t *perf = &layer->performance;

    perf->update_count++;
    perf->total_processing_time_ns += operation_time_ns;

    /// Update min/max times
    if (operation_time_ns < perf->min_update_time_ns) {
        perf->min_update_time_ns = operation_time_ns;
    }
    if (operation_time_ns > perf->max_update_time_ns) {
        perf->max_update_time_ns = operation_time_ns;
    }

    /// Calculate average
    perf->avg_update_time_ns =
        perf->total_processing_time_ns / perf->update_count;

    /// Update last update time
    clock_gettime(CLOCK_MONOTONIC, &layer->last_update_time);
}

/// ============================================================================
/// EVENT HANDLING
/// ============================================================================

/**
 * @brief Handle layer events from event system
 * @param event Layer event to handle
 * @param user_data User data (command_layer_t pointer)
 * @return LAYER_EVENTS_SUCCESS on success, error code otherwise
 */
static layer_events_error_t handle_layer_event(const layer_event_t *event,
                                               void *user_data) {
    command_layer_t *layer = (command_layer_t *)user_data;

    if (!validate_layer_state(layer) || !event) {
        return LAYER_EVENTS_ERROR_INVALID_PARAM;
    }

    switch (event->type) {
    case LAYER_EVENT_THEME_CHANGED:
        /// Theme changes are handled by the LLE spec-compliant highlighter
        layer->needs_redraw = true;
        publish_command_event(layer, LAYER_EVENT_REDRAW_NEEDED);
        break;

    case LAYER_EVENT_CONTENT_CHANGED:
        /// Prompt layer content changed, update positioning
        if (layer->prompt_integration_enabled) {
            update_command_metrics(layer);
            layer->needs_redraw = true;
        }
        break;

    case LAYER_EVENT_SIZE_CHANGED:
        /// Terminal size changed, may affect display
        layer->needs_redraw = true;
        publish_command_event(layer, LAYER_EVENT_REDRAW_NEEDED);
        break;

    default:
        /// Ignore other events
        break;
    }

    return LAYER_EVENTS_SUCCESS;
}

/**
 * @brief Publish a command layer event to the event system
 * @param layer Command layer publishing the event
 * @param event_type Type of event to publish
 * @return COMMAND_LAYER_SUCCESS on success, error code otherwise
 */
static command_layer_error_t
publish_command_event(command_layer_t *layer, layer_event_type_t event_type) {
    if (!layer->event_system) {
        return COMMAND_LAYER_SUCCESS; /// Not an error if no event system
    }

    /// Use HIGH priority for REDRAW_NEEDED events to match display_controller
    /// subscription
    layer_event_priority_t priority = (event_type == LAYER_EVENT_REDRAW_NEEDED)
                                          ? LAYER_EVENT_PRIORITY_HIGH
                                          : LAYER_EVENT_PRIORITY_NORMAL;

    layer_events_error_t result = layer_events_publish_simple(
        layer->event_system, event_type, LAYER_ID_COMMAND_LAYER, 0, priority);

    return (result == LAYER_EVENTS_SUCCESS) ? COMMAND_LAYER_SUCCESS
                                            : COMMAND_LAYER_ERROR_EVENT_SYSTEM;
}

/// ============================================================================
/// UTILITY FUNCTIONS
/// ============================================================================

/**
 * @brief Validate command layer state and magic number
 * @param layer Command layer to validate
 * @return true if layer is valid and initialized, false otherwise
 */
static bool validate_layer_state(command_layer_t *layer) {
    return (layer != NULL && layer->magic == COMMAND_LAYER_MAGIC &&
            layer->initialized);
}

/**
 * @brief Reset command layer to initial state
 * @param layer Command layer to reset
 */
static void reset_layer_state(command_layer_t *layer) {
    if (!layer) {
        return;
    }

    layer->command_text[0] = '\0';
    layer->highlighted_text[0] = '\0';
    layer->cursor_position = 0;
    layer->region_count = 0;
    layer->needs_redraw = false;

    memset(&layer->metrics, 0, sizeof(command_metrics_t));
    memset(&layer->performance, 0, sizeof(command_performance_t));
    layer->performance.min_update_time_ns = UINT64_MAX;

    layer->cache_size = 0;
    layer->cache_access_count = 0;

    layer->update_sequence_number = 0;
}

/**
 * @brief Safely copy string with buffer size limit
 * @param dest Destination buffer
 * @param src Source string
 * @param dest_size Size of destination buffer
 * @return Number of bytes copied (excluding null terminator)
 */
static size_t safe_string_copy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) {
        return 0;
    }

    size_t src_len = strlen(src);
    size_t copy_len = (src_len < dest_size - 1) ? src_len : dest_size - 1;

    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';

    return copy_len;
}

/// ============================================================================
/// CONFIGURATION AND SYNTAX HIGHLIGHTING API
/// ============================================================================

command_layer_error_t command_layer_set_syntax_enabled(command_layer_t *layer,
                                                       bool enabled) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    if (layer->syntax_config.enabled != enabled) {
        layer->syntax_config.enabled = enabled;
        layer->needs_redraw = true;

        /// Clear cache when enabling/disabling
        command_layer_clear_cache(layer);

        publish_command_event(layer, LAYER_EVENT_STYLE_UPDATED);
    }

    return COMMAND_LAYER_SUCCESS;
}

bool command_layer_is_syntax_enabled(command_layer_t *layer) {
    if (!validate_layer_state(layer)) {
        return false;
    }

    return layer->syntax_config.enabled;
}

command_layer_error_t
command_layer_set_syntax_config(command_layer_t *layer,
                                const command_syntax_config_t *config) {
    if (!validate_layer_state(layer) || !config) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    layer->syntax_config = *config;
    layer->needs_redraw = true;

    /// Clear cache since configuration changed
    command_layer_clear_cache(layer);

    publish_command_event(layer, LAYER_EVENT_STYLE_UPDATED);

    return COMMAND_LAYER_SUCCESS;
}

/// ============================================================================
/// PROMPT LAYER INTEGRATION
/// ============================================================================

command_layer_error_t
command_layer_set_prompt_layer(command_layer_t *layer,
                               prompt_layer_t *prompt_layer) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    layer->prompt_layer = prompt_layer;

    if (prompt_layer && layer->prompt_integration_enabled) {
        /// Update metrics with new prompt information
        update_command_metrics(layer);
        layer->needs_redraw = true;
    }

    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t
command_layer_set_prompt_integration(command_layer_t *layer, bool enabled) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    layer->prompt_integration_enabled = enabled;

    if (enabled && layer->prompt_layer) {
        /// Update metrics when enabling integration
        update_command_metrics(layer);
        layer->needs_redraw = true;
    }

    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_get_display_position(command_layer_t *layer,
                                                         int *column,
                                                         int *row) {
    if (!validate_layer_state(layer) || !column || !row) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    *column = layer->metrics.estimated_display_column;
    *row = layer->metrics.estimated_display_row;

    return COMMAND_LAYER_SUCCESS;
}

/// ============================================================================
/// PERFORMANCE AND MONITORING API
/// ============================================================================

command_layer_error_t
command_layer_get_performance(command_layer_t *layer,
                              command_performance_t *performance) {
    if (!validate_layer_state(layer) || !performance) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    *performance = layer->performance;
    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_reset_performance(command_layer_t *layer) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    memset(&layer->performance, 0, sizeof(command_performance_t));
    layer->performance.min_update_time_ns = UINT64_MAX;

    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_set_cache_enabled(command_layer_t *layer,
                                                      bool enabled) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    layer->syntax_config.cache_enabled = enabled;

    if (!enabled) {
        /// Clear cache when disabling
        command_layer_clear_cache(layer);
    }

    return COMMAND_LAYER_SUCCESS;
}

command_layer_error_t command_layer_clear_cache(command_layer_t *layer) {
    if (!validate_layer_state(layer)) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    for (size_t i = 0; i < COMMAND_LAYER_CACHE_SIZE; i++) {
        layer->cache[i].is_valid = false;
    }
    layer->cache_size = 0;
    layer->performance.cache_hits = 0;
    layer->performance.cache_misses = 0;

    return COMMAND_LAYER_SUCCESS;
}

/// ============================================================================
/// VALIDATION AND DEBUGGING
/// ============================================================================

bool command_layer_validate(command_layer_t *layer) {
    return validate_layer_state(layer);
}

command_layer_error_t command_layer_get_debug_info(command_layer_t *layer,
                                                   char *status_buffer,
                                                   size_t buffer_size) {
    if (!validate_layer_state(layer) || !status_buffer || buffer_size == 0) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    snprintf(
        status_buffer, buffer_size,
        "Command Layer Debug Info:\n"
        "Version: %s\n"
        "Initialized: %s\n"
        "Syntax Enabled: %s\n"
        "Cache Enabled: %s\n"
        "Command Length: %zu\n"
        "Cursor Position: %zu\n"
        "Highlight Regions: %zu\n"
        "Cache Size: %zu\n"
        "Update Count: %lu\n"
        "Cache Hit Rate: %.1f%%\n"
        "Avg Update Time: %lu ns\n"
        "Needs Redraw: %s\n",
        COMMAND_LAYER_VERSION_STRING, layer->initialized ? "Yes" : "No",
        layer->syntax_config.enabled ? "Yes" : "No",
        layer->syntax_config.cache_enabled ? "Yes" : "No",
        layer->metrics.command_length, layer->cursor_position,
        layer->region_count, layer->cache_size,
        (unsigned long)layer->performance.update_count,
        (layer->performance.cache_hits + layer->performance.cache_misses > 0)
            ? (100.0 * layer->performance.cache_hits /
               (layer->performance.cache_hits +
                layer->performance.cache_misses))
            : 0.0,
        (unsigned long)layer->performance.avg_update_time_ns,
        layer->needs_redraw ? "Yes" : "No");

    return COMMAND_LAYER_SUCCESS;
}

/// ============================================================================
/// UTILITY FUNCTIONS
/// ============================================================================

/**
 * @brief Get human-readable error message for error code
 * @param error Error code to get message for
 * @return Human-readable error description string
 */
const char *command_layer_get_error_message(command_layer_error_t error) {
    switch (error) {
    case COMMAND_LAYER_SUCCESS:
        return "Success";
    case COMMAND_LAYER_ERROR_INVALID_PARAM:
        return "Invalid parameter";
    case COMMAND_LAYER_ERROR_NULL_POINTER:
        return "NULL pointer";
    case COMMAND_LAYER_ERROR_MEMORY_ALLOCATION:
        return "Memory allocation failed";
    case COMMAND_LAYER_ERROR_BUFFER_TOO_SMALL:
        return "Buffer too small";
    case COMMAND_LAYER_ERROR_COMMAND_TOO_LARGE:
        return "Command too large";
    case COMMAND_LAYER_ERROR_INVALID_CURSOR_POS:
        return "Invalid cursor position";
    case COMMAND_LAYER_ERROR_CACHE_FULL:
        return "Cache is full";
    case COMMAND_LAYER_ERROR_SYNTAX_ERROR:
        return "Syntax highlighting error";
    case COMMAND_LAYER_ERROR_NOT_INITIALIZED:
        return "Layer not initialized";
    case COMMAND_LAYER_ERROR_EVENT_SYSTEM:
        return "Event system error";
    case COMMAND_LAYER_ERROR_PERFORMANCE_LIMIT:
        return "Performance limit exceeded";
    default:
        return "Unknown error";
    }
}

/// ============================================================================
/// COMPLETION MENU INTEGRATION
/// ============================================================================

/**
 * @brief Set completion menu content to display
 * @param layer Command layer instance
 * @param menu_content Pre-rendered menu content string
 * @param num_lines Number of lines in the menu
 * @param selected_index Currently selected menu item index
 * @return COMMAND_LAYER_SUCCESS on success, error code otherwise
 */
command_layer_error_t
command_layer_set_completion_menu(command_layer_t *layer,
                                  const char *menu_content, int num_lines,
                                  int selected_index) {
    if (!layer || layer->magic != COMMAND_LAYER_MAGIC) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    if (!menu_content || num_lines <= 0) {
        /// Clear menu if invalid input
        return command_layer_clear_completion_menu(layer);
    }

    size_t content_len = strlen(menu_content);
    size_t needed_size = content_len + 1;

    /// Allocate or reallocate buffer if needed
    if (layer->completion_menu_content_size < needed_size) {
        char *new_buffer = realloc(layer->completion_menu_content, needed_size);
        if (!new_buffer) {
            return COMMAND_LAYER_ERROR_MEMORY_ALLOCATION;
        }
        layer->completion_menu_content = new_buffer;
        layer->completion_menu_content_size = needed_size;
    }

    /// Copy menu content
    memcpy(layer->completion_menu_content, menu_content, content_len + 1);

    /// Update state
    layer->completion_menu_visible = true;
    layer->completion_menu_lines = num_lines;
    layer->completion_menu_selected_index = selected_index;
    layer->needs_redraw = true;

    /// Publish redraw event
    if (layer->event_system) {
        layer_event_t event = {.type = LAYER_EVENT_REDRAW_NEEDED,
                               .source_layer = LAYER_ID_COMMAND_LAYER,
                               .timestamp = 0};
        layer_events_publish(layer->event_system, &event);
    }

    return COMMAND_LAYER_SUCCESS;
}

/**
 * @brief Clear and hide the completion menu
 * @param layer Command layer instance
 * @return COMMAND_LAYER_SUCCESS on success, error code otherwise
 */
command_layer_error_t
command_layer_clear_completion_menu(command_layer_t *layer) {
    if (!layer || layer->magic != COMMAND_LAYER_MAGIC) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    bool was_visible = layer->completion_menu_visible;

    layer->completion_menu_visible = false;
    layer->completion_menu_lines = 0;
    layer->completion_menu_selected_index = 0;

    /// Don't free the buffer - keep it for reuse
    /// Just mark as not visible

    if (was_visible) {
        layer->needs_redraw = true;

        /// Publish redraw event
        if (layer->event_system) {
            layer_event_t event = {.type = LAYER_EVENT_REDRAW_NEEDED,
                                   .source_layer = LAYER_ID_COMMAND_LAYER,
                                   .timestamp = 0};
            layer_events_publish(layer->event_system, &event);
        }
    }

    return COMMAND_LAYER_SUCCESS;
}

/**
 * @brief Check if completion menu is currently visible
 * @param layer Command layer instance
 * @return true if menu is visible, false otherwise
 */
bool command_layer_is_menu_visible(const command_layer_t *layer) {
    if (!layer || layer->magic != COMMAND_LAYER_MAGIC) {
        return false;
    }
    return layer->completion_menu_visible;
}

/**
 * @brief Get the current completion menu content
 * @param layer Command layer instance
 * @return Menu content string, or NULL if not visible
 */
const char *command_layer_get_menu_content(const command_layer_t *layer) {
    if (!layer || layer->magic != COMMAND_LAYER_MAGIC) {
        return NULL;
    }
    if (!layer->completion_menu_visible) {
        return NULL;
    }
    return layer->completion_menu_content;
}

/**
 * @brief Get the number of lines in the completion menu
 * @param layer Command layer instance
 * @return Number of menu lines, or 0 if not visible
 */
int command_layer_get_menu_lines(const command_layer_t *layer) {
    if (!layer || layer->magic != COMMAND_LAYER_MAGIC) {
        return 0;
    }
    if (!layer->completion_menu_visible) {
        return 0;
    }
    return layer->completion_menu_lines;
}

/**
 * @brief Set the selected item index in the completion menu
 * @param layer Command layer instance
 * @param selected_index New selected item index
 * @return COMMAND_LAYER_SUCCESS on success, error code otherwise
 */
command_layer_error_t command_layer_set_menu_selection(command_layer_t *layer,
                                                       int selected_index) {
    if (!layer || layer->magic != COMMAND_LAYER_MAGIC) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->completion_menu_visible) {
        return COMMAND_LAYER_ERROR_NOT_INITIALIZED;
    }

    layer->completion_menu_selected_index = selected_index;
    layer->needs_redraw = true;

    return COMMAND_LAYER_SUCCESS;
}

/**
 * @brief Create default syntax highlighting configuration
 * @param config Output buffer for default configuration
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_create_default_config(command_syntax_config_t *config) {
    if (!config) {
        return COMMAND_LAYER_ERROR_INVALID_PARAM;
    }

    config->enabled = true;
    config->use_colors = true;
    config->highlight_errors = true;
    config->cache_enabled = true;
    config->cache_expiry_ms = COMMAND_LAYER_CACHE_EXPIRY_MS;
    config->max_update_time_ms = COMMAND_LAYER_TARGET_UPDATE_TIME_MS;

    /// Color scheme is no longer used - colors are handled by spec highlighter
    memset(&config->color_scheme, 0, sizeof(config->color_scheme));

    return COMMAND_LAYER_SUCCESS;
}

/**
 * @brief Apply LLE theme syntax colors to command layer's spec highlighter
 * @param lle_theme LLE theme with syntax colors
 * @param cmd_layer Command layer to apply colors to
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t command_layer_apply_theme_colors(const lle_theme_t *lle_theme,
                                              command_layer_t *cmd_layer) {
    if (!lle_theme || !cmd_layer) {
        return LLE_ERROR_NULL_POINTER;
    }

    if (!lle_theme->has_syntax_colors) {
        return LLE_SUCCESS;
    }

    if (cmd_layer->spec_highlighter) {
        lle_syntax_highlighter_set_colors(cmd_layer->spec_highlighter,
                                          &lle_theme->syntax_colors);
    }

    return LLE_SUCCESS;
}