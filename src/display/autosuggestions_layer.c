/**
 * @file autosuggestions_layer.c
 * @brief Autosuggestions Layer - Fish-like Autosuggestions Integration
 *
 * Part of the Lush Shell Layered Display Architecture.
 * Provides inline command suggestions based on history and completions.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * ============================================================================
 *
 * AUTOSUGGESTIONS LAYER IMPLEMENTATION
 *
 * This module implements the autosuggestions layer for the Lush layered
 * display system. It provides Fish-like autosuggestions that integrate
 * seamlessly with the existing display architecture, eliminating the display
 * corruption issues caused by direct terminal control sequences.
 *
 * Key Features:
 * - Seamless integration with layered display architecture
 * - Intelligent suggestion generation with caching
 * - Safe display operations coordinated with other layers
 * - Enterprise-grade performance monitoring
 * - Professional styling and configuration options
 * - Zero display corruption through proper layer coordination
 */

#include "display/autosuggestions_layer.h"
#include "display_integration.h"
#include "lle/adaptive_terminal_integration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// ============================================================================
// INTERNAL CONSTANTS AND MACROS
// ============================================================================

#define AUTOSUGGESTIONS_LAYER_CACHE_CLEANUP_THRESHOLD 50
#define AUTOSUGGESTIONS_LAYER_MIN_SUGGESTION_LENGTH 1
#define AUTOSUGGESTIONS_LAYER_MAX_CONTEXT_LENGTH 512

// Performance timing macros
#define START_TIMING()                                                         \
    struct timespec start_time;                                                \
    clock_gettime(CLOCK_MONOTONIC, &start_time);

#define END_TIMING(duration_ns)                                                \
    struct timespec end_time;                                                  \
    clock_gettime(CLOCK_MONOTONIC, &end_time);                                 \
    duration_ns = (end_time.tv_sec - start_time.tv_sec) * 1000000000L +        \
                  (end_time.tv_nsec - start_time.tv_nsec);

// ============================================================================
// INTERNAL FUNCTIONS
// ============================================================================

/**
 * @brief Get current timestamp in nanoseconds
 * @return Current monotonic timestamp in nanoseconds, or 0 on failure
 */
static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return ts.tv_sec * 1000000000L + ts.tv_nsec;
    }
    return 0;
}

/**
 * @brief Set layer error and update metrics
 * @param layer Pointer to the autosuggestions layer
 * @param error Error code to set
 */
static void set_layer_error(autosuggestions_layer_t *layer,
                            autosuggestions_layer_error_t error) {
    if (!layer)
        return;

    layer->last_error = error;
    if (error != AUTOSUGGESTIONS_LAYER_SUCCESS) {
        layer->metrics.display_errors++;
    }
}

/**
 * @brief Validate layer pointer for initialization
 * @param layer Pointer to the autosuggestions layer to validate
 * @return true if valid for initialization, false otherwise
 */
static bool validate_layer_for_init(const autosuggestions_layer_t *layer) {
    // During initialization, don't check initialized flag to avoid circular
    // dependency
    return layer && layer->event_system && layer->terminal_control;
}

static bool validate_layer(const autosuggestions_layer_t *layer) {
    return layer && layer->initialized;
}

/**
 * @brief Calculate cache hash for input string
 * @param input Input string to hash
 * @return 32-bit hash value, or 0 if input is NULL
 */
MAYBE_UNUSED
static uint32_t calculate_cache_hash(const char *input) {
    if (!input)
        return 0;

    uint32_t hash = 5381;
    for (const char *p = input; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)*p;
    }
    return hash;
}

/**
 * @brief Find cache entry for input string
 * @param layer Pointer to the autosuggestions layer
 * @param input Input string to search for in cache
 * @return Pointer to cache entry if found, NULL otherwise
 */
static autosuggestions_cache_entry_t *
find_cache_entry(autosuggestions_layer_t *layer, const char *input) {
    if (!validate_layer(layer) || !input)
        return NULL;

    // uint32_t hash = calculate_cache_hash(input); // Unused for now

    for (int i = 0; i < layer->cache_size; i++) {
        if (layer->cache[i].valid && layer->cache[i].input_text &&
            strcmp(layer->cache[i].input_text, input) == 0) {

            // Update access time and count
            layer->cache[i].last_used_timestamp = get_timestamp_ns();
            layer->cache[i].use_count++;
            layer->metrics.cache_hits++;

            // Enhanced Performance Monitoring: Record cache hit
            display_integration_record_layer_cache_operation("autosuggestions",
                                                             true);

            return &layer->cache[i];
        }
    }

    layer->metrics.cache_misses++;

    // Enhanced Performance Monitoring: Record cache miss
    display_integration_record_layer_cache_operation("autosuggestions", false);

    return NULL;
}

/**
 * @brief Add an entry to the suggestion cache
 * @param layer Pointer to the autosuggestions layer
 * @param input Input string that generated the suggestion
 * @param suggestion Suggestion text to cache
 * @param score Suggestion score/relevance
 * @param generation_time Time taken to generate the suggestion in nanoseconds
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code on failure
 */
static autosuggestions_layer_error_t
add_cache_entry(autosuggestions_layer_t *layer, const char *input,
                const char *suggestion, int score, uint64_t generation_time) {
    if (!validate_layer(layer) || !input || !suggestion) {
        return AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM;
    }

    // Find slot to use (LRU replacement if full)
    int slot = layer->cache_next_index;
    if (layer->cache_size < AUTOSUGGESTIONS_LAYER_MAX_CACHE_ENTRIES) {
        slot = layer->cache_size;
        layer->cache_size++;
    } else {
        // Find LRU entry
        uint64_t oldest_time = UINT64_MAX;
        for (int i = 0; i < layer->cache_size; i++) {
            if (layer->cache[i].last_used_timestamp < oldest_time) {
                oldest_time = layer->cache[i].last_used_timestamp;
                slot = i;
            }
        }
    }

    // Free existing data
    if (layer->cache[slot].input_text) {
        free(layer->cache[slot].input_text);
    }
    if (layer->cache[slot].suggestion_text) {
        free(layer->cache[slot].suggestion_text);
    }

    // Set new data
    layer->cache[slot].input_text = strdup(input);
    layer->cache[slot].suggestion_text = strdup(suggestion);
    layer->cache[slot].suggestion_score = score;
    layer->cache[slot].generation_time_ns = generation_time;
    layer->cache[slot].last_used_timestamp = get_timestamp_ns();
    layer->cache[slot].use_count = 1;
    layer->cache[slot].valid = true;

    layer->cache_next_index =
        (slot + 1) % AUTOSUGGESTIONS_LAYER_MAX_CACHE_ENTRIES;

    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

/**
 * @brief Generate suggestion placeholder (stub function)
 *
 * NOTE: In v1.3.0, suggestions are generated externally by LLE's history system
 * and set via autosuggestions_layer_set_suggestion(). This function is a stub
 * that returns NULL - the actual suggestion generation happens in
 * lle_readline.c which calls autosuggestions_layer_set_suggestion() with
 * history-based suggestions.
 *
 * @param layer Pointer to the autosuggestions layer
 * @param context Context information for suggestion generation
 * @param generation_time_ns Output parameter for generation time
 * @return Always returns NULL (stub implementation)
 */
static char *generate_suggestion(autosuggestions_layer_t *layer,
                                 const autosuggestions_context_t *context,
                                 uint64_t *generation_time_ns) {
    if (!validate_layer(layer) || !context)
        return NULL;

    START_TIMING();

    // In v1.3.0, suggestion generation is handled externally by LLE.
    // LLE's readline implementation calls
    // autosuggestions_layer_set_suggestion() with suggestions from LLE's
    // history system. This function returns NULL; external callers should use
    // set_suggestion().

    END_TIMING(*generation_time_ns);

    return NULL;
}

/**
 * @brief Update performance metrics after suggestion operation
 * @param layer Pointer to the autosuggestions layer
 * @param generation_time_ns Time taken to generate suggestion in nanoseconds
 * @param display_time_ns Time taken to display suggestion in nanoseconds
 */
static void update_performance_metrics(autosuggestions_layer_t *layer,
                                       uint64_t generation_time_ns,
                                       uint64_t display_time_ns) {
    if (!validate_layer(layer))
        return;

    // Update generation time average
    double generation_ms = generation_time_ns / 1000000.0;
    layer->metrics.avg_generation_time_ms =
        (layer->metrics.avg_generation_time_ms *
             layer->metrics.suggestions_generated +
         generation_ms) /
        (layer->metrics.suggestions_generated + 1);

    // Update display time average
    if (display_time_ns > 0) {
        double display_ms = display_time_ns / 1000000.0;
        layer->metrics.avg_display_time_ms =
            (layer->metrics.avg_display_time_ms *
                 layer->metrics.suggestions_displayed +
             display_ms) /
            (layer->metrics.suggestions_displayed + 1);
    }

    // Update cache hit rate
    uint64_t total_requests =
        layer->metrics.cache_hits + layer->metrics.cache_misses;
    if (total_requests > 0) {
        layer->metrics.cache_hit_rate =
            (double)layer->metrics.cache_hits / total_requests;
    }

    layer->metrics.last_performance_update = get_timestamp_ns();
}

/**
 * @brief Compose suggestion display with terminal control sequences
 * @param layer Pointer to the autosuggestions layer
 * @param suggestion Suggestion text to compose
 * @param buffer Output buffer for composed display
 * @param buffer_size Size of output buffer
 * @param bytes_written Output parameter for bytes written to buffer
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code on failure
 */
static autosuggestions_layer_error_t
compose_suggestion_display(autosuggestions_layer_t *layer,
                           const char *suggestion, char *buffer,
                           size_t buffer_size, size_t *bytes_written) {
    if (!validate_layer(layer) || !suggestion || !buffer || !bytes_written) {
        return AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM;
    }

    *bytes_written = 0;

    // Check terminal capabilities
    if (!layer->display_config.enable_color ||
        !terminal_control_has_capability(layer->terminal_control,
                                         TERMINAL_CAP_COLOR_8)) {
        // Plain text display
        int written = snprintf(buffer, buffer_size, "%s", suggestion);
        if (written < 0 || (size_t)written >= buffer_size) {
            return AUTOSUGGESTIONS_LAYER_ERROR_DISPLAY_FAILED;
        }
        *bytes_written = written;
        return AUTOSUGGESTIONS_LAYER_SUCCESS;
    }

    // Generate color sequence
    char color_sequence[64] = {0};
    if (terminal_control_generate_color_sequence(
            layer->terminal_control, layer->display_config.suggestion_color,
            true, // foreground
            color_sequence, sizeof(color_sequence)) <= 0) {
        // Fallback to plain text
        int written = snprintf(buffer, buffer_size, "%s", suggestion);
        if (written < 0 || (size_t)written >= buffer_size) {
            return AUTOSUGGESTIONS_LAYER_ERROR_DISPLAY_FAILED;
        }
        *bytes_written = written;
        return AUTOSUGGESTIONS_LAYER_SUCCESS;
    }

    // Generate reset sequence
    char reset_sequence[32] = {0};
    if (terminal_control_generate_style_sequence(
            layer->terminal_control, TERMINAL_STYLE_NONE, reset_sequence,
            sizeof(reset_sequence)) <= 0) {
        strcpy(reset_sequence, "\033[0m"); // Fallback
    }

    // Compose full display
    int written = snprintf(buffer, buffer_size, "%s%s%s", color_sequence,
                           suggestion, reset_sequence);

    if (written < 0 || (size_t)written >= buffer_size) {
        return AUTOSUGGESTIONS_LAYER_ERROR_DISPLAY_FAILED;
    }

    *bytes_written = written;
    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

// ============================================================================
// CORE LAYER MANAGEMENT
// ============================================================================

autosuggestions_layer_t *
autosuggestions_layer_create(layer_event_system_t *event_system,
                             terminal_control_t *terminal_control) {
    if (!event_system || !terminal_control) {
        return NULL;
    }

    autosuggestions_layer_t *layer = calloc(1, sizeof(autosuggestions_layer_t));
    if (!layer) {
        return NULL;
    }

    // Initialize basic fields
    layer->layer_id = LAYER_ID_AUTOSUGGESTIONS;
    layer->initialized = false;
    layer->enabled = true;
    layer->last_error = AUTOSUGGESTIONS_LAYER_SUCCESS;

    // Store references
    layer->event_system = event_system;
    layer->terminal_control = terminal_control;

    // Initialize state
    layer->current_input = NULL;
    layer->current_suggestion = NULL;
    layer->cursor_position = 0;
    layer->suggestion_displayed = false;
    layer->suggestion_timestamp = 0;

    // Initialize cache
    layer->cache_size = 0;
    layer->cache_next_index = 0;
    memset(layer->cache, 0, sizeof(layer->cache));

    // Initialize performance metrics
    memset(&layer->metrics, 0, sizeof(layer->metrics));
    layer->metrics.last_performance_update = get_timestamp_ns();

    // Initialize coordination flags
    layer->in_display_operation = false;
    layer->needs_refresh = false;

    return layer;
}

autosuggestions_layer_error_t
autosuggestions_layer_init(autosuggestions_layer_t *layer,
                           const autosuggestions_display_config_t *config) {
    if (!layer) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NULL_POINTER;
    }

    if (layer->initialized) {
        return AUTOSUGGESTIONS_LAYER_SUCCESS; // Already initialized
    }

    // Set configuration
    if (config) {
        layer->display_config = *config;
    } else {
        autosuggestions_layer_create_default_config(&layer->display_config);
    }

    // Get terminal capabilities
    layer->terminal_caps =
        terminal_control_get_capabilities(layer->terminal_control);

    // Use LLE for comprehensive terminal capability detection
    lle_terminal_detection_result_t *detection = NULL;
    lle_detect_terminal_capabilities_optimized(&detection);

    // For autosuggestions, we need stdout to be a TTY for display, not
    // necessarily stdin
    if (!detection || !detection->stdout_is_tty) {
        set_layer_error(layer,
                        AUTOSUGGESTIONS_LAYER_ERROR_UNSUPPORTED_TERMINAL);
        return AUTOSUGGESTIONS_LAYER_ERROR_UNSUPPORTED_TERMINAL;
    }

    // Check if terminal has basic capabilities needed for autosuggestions
    // We need colors and unicode support for professional display
    if (!detection->supports_colors || !detection->supports_unicode) {
        set_layer_error(layer,
                        AUTOSUGGESTIONS_LAYER_ERROR_UNSUPPORTED_TERMINAL);
        return AUTOSUGGESTIONS_LAYER_ERROR_UNSUPPORTED_TERMINAL;
    }

    // NOTE: In v1.3.0, suggestion generation is handled externally by LLE's
    // history system. LLE's readline calls
    // autosuggestions_layer_set_suggestion() directly with history-based
    // suggestions. No legacy init needed.

    // Subscribe to layer events
    autosuggestions_layer_error_t error =
        autosuggestions_layer_subscribe_events(layer);
    if (error != AUTOSUGGESTIONS_LAYER_SUCCESS) {
        set_layer_error(layer, error);
        return error;
    }

    layer->initialized = true;
    set_layer_error(layer, AUTOSUGGESTIONS_LAYER_SUCCESS);

    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

autosuggestions_layer_error_t
autosuggestions_layer_destroy(autosuggestions_layer_t **layer) {
    if (!layer || !*layer) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NULL_POINTER;
    }

    autosuggestions_layer_t *l = *layer;

    // Unsubscribe from events
    if (l->initialized) {
        autosuggestions_layer_unsubscribe_events(l);
    }

    // Clear display
    autosuggestions_layer_clear(l);

    // Free current state
    if (l->current_input) {
        free(l->current_input);
    }
    if (l->current_suggestion) {
        free(l->current_suggestion);
    }

    // Free cache entries
    for (int i = 0; i < l->cache_size; i++) {
        if (l->cache[i].input_text) {
            free(l->cache[i].input_text);
        }
        if (l->cache[i].suggestion_text) {
            free(l->cache[i].suggestion_text);
        }
    }

    // Free layer structure
    free(l);
    *layer = NULL;

    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

autosuggestions_layer_error_t
autosuggestions_layer_set_enabled(autosuggestions_layer_t *layer,
                                  bool enabled) {
    if (!validate_layer(layer)) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NOT_INITIALIZED;
    }

    if (layer->enabled != enabled) {
        layer->enabled = enabled;

        if (!enabled) {
            // Clear current display when disabled
            autosuggestions_layer_clear(layer);
        }
    }

    set_layer_error(layer, AUTOSUGGESTIONS_LAYER_SUCCESS);
    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

bool autosuggestions_layer_is_enabled(const autosuggestions_layer_t *layer) {
    return validate_layer(layer) && layer->enabled;
}

// ============================================================================
// SUGGESTION GENERATION AND DISPLAY
// ============================================================================

autosuggestions_layer_error_t
autosuggestions_layer_update(autosuggestions_layer_t *layer,
                             const autosuggestions_context_t *context) {
    if (!validate_layer(layer) || !context) {
        return AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->enabled || layer->in_display_operation) {
        return AUTOSUGGESTIONS_LAYER_SUCCESS;
    }

    layer->in_display_operation = true;
    layer->metrics.total_suggestions_requested++;

    START_TIMING();

    autosuggestions_layer_error_t result = AUTOSUGGESTIONS_LAYER_SUCCESS;

    // Check if we should suggest
    if (!autosuggestions_layer_should_suggest(layer, context)) {
        autosuggestions_layer_clear(layer);
        goto cleanup;
    }

    // Check cache first
    autosuggestions_cache_entry_t *cached =
        find_cache_entry(layer, context->input_line);
    char *suggestion_text = NULL;
    uint64_t generation_time_ns = 0;

    if (cached) {
        suggestion_text = strdup(cached->suggestion_text);
        generation_time_ns = cached->generation_time_ns;
    } else {
        // Generate new suggestion
        suggestion_text =
            generate_suggestion(layer, context, &generation_time_ns);

        if (suggestion_text) {
            // Add to cache
            add_cache_entry(layer, context->input_line, suggestion_text, 100,
                            generation_time_ns);
        }
    }

    // Update current state
    if (layer->current_input) {
        free(layer->current_input);
    }
    layer->current_input = strdup(context->input_line);
    layer->cursor_position = context->cursor_position;

    if (suggestion_text) {
        if (layer->current_suggestion) {
            free(layer->current_suggestion);
        }
        layer->current_suggestion = suggestion_text;
        layer->suggestion_timestamp = get_timestamp_ns();
        layer->suggestion_displayed = true;
        layer->metrics.suggestions_displayed++;

        // Publish change event
        autosuggestions_layer_publish_change(layer, NULL, suggestion_text);
    } else {
        autosuggestions_layer_clear(layer);
    }

cleanup:;
    END_TIMING(generation_time_ns);
    update_performance_metrics(layer, generation_time_ns, 0);

    layer->in_display_operation = false;
    set_layer_error(layer, result);
    return result;
}

autosuggestions_layer_error_t
autosuggestions_layer_clear(autosuggestions_layer_t *layer) {
    if (!validate_layer(layer)) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NOT_INITIALIZED;
    }

    if (layer->suggestion_displayed) {
        const char *old_suggestion = layer->current_suggestion;

        // Clear state
        if (layer->current_suggestion) {
            free(layer->current_suggestion);
            layer->current_suggestion = NULL;
        }

        layer->suggestion_displayed = false;
        layer->suggestion_timestamp = 0;

        // Publish change event
        autosuggestions_layer_publish_change(layer, old_suggestion, NULL);
    }

    set_layer_error(layer, AUTOSUGGESTIONS_LAYER_SUCCESS);
    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

autosuggestions_layer_error_t
autosuggestions_layer_accept(autosuggestions_layer_t *layer,
                             char *accepted_text, size_t buffer_size) {
    if (!validate_layer(layer) || !accepted_text) {
        return AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->suggestion_displayed || !layer->current_suggestion) {
        accepted_text[0] = '\0';
        return AUTOSUGGESTIONS_LAYER_SUCCESS;
    }

    size_t suggestion_len = strlen(layer->current_suggestion);
    if (suggestion_len >= buffer_size) {
        set_layer_error(layer, AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM);
        return AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM;
    }

    strcpy(accepted_text, layer->current_suggestion);
    layer->metrics.suggestions_accepted++;

    // Clear after acceptance
    autosuggestions_layer_clear(layer);

    set_layer_error(layer, AUTOSUGGESTIONS_LAYER_SUCCESS);
    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

const char *autosuggestions_layer_get_current_suggestion(
    const autosuggestions_layer_t *layer) {
    if (!validate_layer(layer) || !layer->suggestion_displayed) {
        return NULL;
    }

    return layer->current_suggestion;
}

autosuggestions_layer_error_t
autosuggestions_layer_set_suggestion(autosuggestions_layer_t *layer,
                                     const char *suggestion) {

    if (!layer) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NULL_POINTER;
    }

    // Handle NULL or empty suggestion as clear
    if (!suggestion || !*suggestion) {
        return autosuggestions_layer_clear(layer);
    }

    // Validate suggestion length
    size_t len = strlen(suggestion);
    if (len > AUTOSUGGESTIONS_LAYER_MAX_SUGGESTION_LENGTH) {
        set_layer_error(layer, AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM);
        return AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM;
    }

    // Free existing suggestion if any
    if (layer->current_suggestion) {
        free(layer->current_suggestion);
        layer->current_suggestion = NULL;
    }

    // Set new suggestion
    layer->current_suggestion = strdup(suggestion);
    if (!layer->current_suggestion) {
        set_layer_error(layer, AUTOSUGGESTIONS_LAYER_ERROR_MEMORY_ALLOCATION);
        return AUTOSUGGESTIONS_LAYER_ERROR_MEMORY_ALLOCATION;
    }

    layer->suggestion_displayed = true;
    layer->suggestion_timestamp = get_timestamp_ns();
    layer->metrics.suggestions_displayed++;

    set_layer_error(layer, AUTOSUGGESTIONS_LAYER_SUCCESS);
    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

bool autosuggestions_layer_has_suggestion(
    const autosuggestions_layer_t *layer) {
    return validate_layer(layer) && layer->suggestion_displayed &&
           layer->current_suggestion;
}

// ============================================================================
// CONFIGURATION AND STYLING
// ============================================================================

autosuggestions_layer_error_t autosuggestions_layer_create_default_config(
    autosuggestions_display_config_t *config) {
    if (!config) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NULL_POINTER;
    }

    config->suggestion_color =
        terminal_control_color_from_basic(TERMINAL_COLOR_BRIGHT_BLACK);
    config->suggestion_style = TERMINAL_STYLE_NONE;
    config->enable_color = true;
    config->enable_styling = false;
    config->display_delay_ms = 0;
    config->fade_timeout_ms = 0;
    config->show_in_multiline = false;
    config->show_for_short_commands = true;

    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

// ============================================================================
// LAYER EVENT INTEGRATION
// ============================================================================

layer_events_error_t
autosuggestions_layer_handle_event(const layer_event_t *event,
                                   void *user_data) {
    autosuggestions_layer_t *layer = (autosuggestions_layer_t *)user_data;

    if (!validate_layer(layer) || !event) {
        return LAYER_EVENTS_ERROR_INVALID_PARAM;
    }

    switch (event->type) {
    case LAYER_EVENT_CONTENT_CHANGED:
        // Command layer content changed - may need to update suggestions
        if (event->source_layer == LAYER_ID_COMMAND_LAYER) {
            layer->needs_refresh = true;
        }
        break;

    case LAYER_EVENT_SIZE_CHANGED:
        // Terminal size changed - update capabilities
        layer->terminal_caps =
            terminal_control_get_capabilities(layer->terminal_control);
        break;

    default:
        return LAYER_EVENTS_ERROR_INVALID_PARAM;
    }

    return LAYER_EVENTS_SUCCESS;
}

autosuggestions_layer_error_t
autosuggestions_layer_publish_change(autosuggestions_layer_t *layer,
                                     const char *old_suggestion
                                     __attribute__((unused)),
                                     const char *new_suggestion) {
    if (!validate_layer(layer)) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NOT_INITIALIZED;
    }

    // Publish content changed event
    if (layer->event_system) {
        const char *content = new_suggestion ? new_suggestion : "";
        size_t content_len = strlen(content);

        layer_events_publish_content_changed(layer->event_system,
                                             LAYER_ID_AUTOSUGGESTIONS, content,
                                             content_len, false);

        /* Also publish REDRAW_NEEDED so display_controller renders the change.
         * This is critical when clearing autosuggestion - display_controller
         * subscribes to REDRAW_NEEDED events to trigger
         * dc_handle_redraw_needed() which handles ghost text cleanup. Without
         * this, clearing autosuggestion doesn't trigger a visual update. */
        layer_events_publish_simple(
            layer->event_system, LAYER_EVENT_REDRAW_NEEDED,
            LAYER_ID_AUTOSUGGESTIONS, 0, // broadcast to all
            LAYER_EVENT_PRIORITY_HIGH);
    }

    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

autosuggestions_layer_error_t
autosuggestions_layer_subscribe_events(autosuggestions_layer_t *layer) {
    if (!validate_layer_for_init(layer)) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NOT_INITIALIZED;
    }

    // Subscribe to content changes from command layer
    layer_events_error_t error = layer_events_subscribe(
        layer->event_system, LAYER_EVENT_CONTENT_CHANGED,
        LAYER_ID_AUTOSUGGESTIONS, autosuggestions_layer_handle_event, layer,
        LAYER_EVENT_PRIORITY_NORMAL);

    if (error != LAYER_EVENTS_SUCCESS) {
        return AUTOSUGGESTIONS_LAYER_ERROR_EVENT_FAILED;
    }

    // Subscribe to size changes
    error = layer_events_subscribe(
        layer->event_system, LAYER_EVENT_SIZE_CHANGED, LAYER_ID_AUTOSUGGESTIONS,
        autosuggestions_layer_handle_event, layer, LAYER_EVENT_PRIORITY_LOW);

    if (error != LAYER_EVENTS_SUCCESS) {
        return AUTOSUGGESTIONS_LAYER_ERROR_EVENT_FAILED;
    }

    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

autosuggestions_layer_error_t
autosuggestions_layer_unsubscribe_events(autosuggestions_layer_t *layer) {
    if (!validate_layer(layer) || !layer->event_system) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NOT_INITIALIZED;
    }

    layer_events_unsubscribe_all(layer->event_system, LAYER_ID_AUTOSUGGESTIONS);

    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

// ============================================================================
// ERROR HANDLING AND UTILITIES
// ============================================================================

autosuggestions_layer_error_t
autosuggestions_layer_get_last_error(const autosuggestions_layer_t *layer) {
    if (!layer) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NULL_POINTER;
    }

    return layer->last_error;
}

const char *
autosuggestions_layer_error_string(autosuggestions_layer_error_t error) {
    switch (error) {
    case AUTOSUGGESTIONS_LAYER_SUCCESS:
        return "Success";
    case AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM:
        return "Invalid parameter";
    case AUTOSUGGESTIONS_LAYER_ERROR_NULL_POINTER:
        return "NULL pointer";
    case AUTOSUGGESTIONS_LAYER_ERROR_MEMORY_ALLOCATION:
        return "Memory allocation failed";
    case AUTOSUGGESTIONS_LAYER_ERROR_NOT_INITIALIZED:
        return "Layer not initialized";
    case AUTOSUGGESTIONS_LAYER_ERROR_TERMINAL_TOO_SMALL:
        return "Terminal too small";
    case AUTOSUGGESTIONS_LAYER_ERROR_GENERATION_FAILED:
        return "Suggestion generation failed";
    case AUTOSUGGESTIONS_LAYER_ERROR_DISPLAY_FAILED:
        return "Display operation failed";
    case AUTOSUGGESTIONS_LAYER_ERROR_CACHE_FULL:
        return "Cache is full";
    case AUTOSUGGESTIONS_LAYER_ERROR_UNSUPPORTED_TERMINAL:
        return "Terminal doesn't support required features";
    case AUTOSUGGESTIONS_LAYER_ERROR_LAYER_CONFLICT:
        return "Conflict with other layers";
    case AUTOSUGGESTIONS_LAYER_ERROR_EVENT_FAILED:
        return "Event handling failed";
    default:
        return "Unknown error";
    }
}

bool autosuggestions_layer_terminal_supported(
    const autosuggestions_layer_t *layer) {
    if (!validate_layer(layer)) {
        return false;
    }

    return terminal_control_has_capability(layer->terminal_control,
                                           TERMINAL_CAP_CURSOR_POSITIONING);
}

void autosuggestions_layer_get_version(int *major, int *minor, int *patch) {
    if (major)
        *major = AUTOSUGGESTIONS_LAYER_VERSION_MAJOR;
    if (minor)
        *minor = AUTOSUGGESTIONS_LAYER_VERSION_MINOR;
    if (patch)
        *patch = AUTOSUGGESTIONS_LAYER_VERSION_PATCH;
}

// ============================================================================
// INTEGRATION HELPERS
// ============================================================================

autosuggestions_layer_error_t
autosuggestions_layer_create_context_from_readline(
    autosuggestions_context_t *context, const char *line_buffer, int cursor_pos,
    int line_end) {

    if (!context) {
        return AUTOSUGGESTIONS_LAYER_ERROR_NULL_POINTER;
    }

    memset(context, 0, sizeof(*context));

    context->input_line = line_buffer;
    context->cursor_position = cursor_pos;
    context->line_length = line_end;
    context->is_multiline_context = false;        // Could be enhanced later
    context->current_directory = getcwd(NULL, 0); // Will need to be freed
    context->shell_context = NULL;

    return AUTOSUGGESTIONS_LAYER_SUCCESS;
}

bool autosuggestions_layer_should_suggest(
    const autosuggestions_layer_t *layer,
    const autosuggestions_context_t *context) {
    if (!validate_layer(layer) || !context) {
        return false;
    }

    // Don't suggest if disabled
    if (!layer->enabled) {
        return false;
    }

    // Don't suggest if no input
    if (!context->input_line || !*context->input_line) {
        return false;
    }

    // Don't suggest if cursor not at end
    if (context->cursor_position != context->line_length) {
        return false;
    }

    // Don't suggest for very short commands unless configured
    if (!layer->display_config.show_for_short_commands &&
        context->line_length < 3) {
        return false;
    }

    // Don't suggest in multiline context unless configured
    if (context->is_multiline_context &&
        !layer->display_config.show_in_multiline) {
        return false;
    }

    // Check terminal size
    if (layer->terminal_caps.terminal_width <
        AUTOSUGGESTIONS_LAYER_MIN_TERMINAL_WIDTH) {
        return false;
    }

    return true;
}

autosuggestions_layer_error_t
autosuggestions_layer_compose_display(autosuggestions_layer_t *layer,
                                      char *buffer, size_t buffer_size,
                                      size_t *bytes_written) {

    if (!validate_layer(layer) || !buffer || !bytes_written) {
        return AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM;
    }

    *bytes_written = 0;

    if (!layer->suggestion_displayed || !layer->current_suggestion) {
        return AUTOSUGGESTIONS_LAYER_SUCCESS;
    }

    return compose_suggestion_display(layer, layer->current_suggestion, buffer,
                                      buffer_size, bytes_written);
}