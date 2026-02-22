/**
 * @file prompt_layer.c
 * @brief Prompt Layer - Universal Prompt Rendering System
 *
 * Part of the Lush Shell Layered Display Architecture.
 * Handles prompt generation, caching, and theme integration.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * ============================================================================
 *
 * PROMPT LAYER IMPLEMENTATION
 *
 * This file implements the prompt layer for the Lush Display System.
 * The prompt layer provides universal prompt rendering that works with ANY
 * prompt structure without requiring parsing or modification.
 *
 * Key Features:
 * - Universal prompt structure compatibility
 * - Integration with existing theme system (6 professional themes)
 * - Event-driven communication with foundation layers
 * - High-performance rendering with intelligent caching
 * - Memory-safe prompt content management
 * - Real-time theme switching support
 *
 * Architecture Integration:
 * - Uses Layer 2 (Terminal Control) for ANSI sequences
 * - Uses Layer 1 (Base Terminal) for terminal capabilities
 * - Uses Layer 0 (Event Communication) for layer coordination
 * - Integrates with existing theme system (src/themes.c)
 * - Integrates with existing prompt generation (src/prompt.c)
 */

#include "prompt_layer.h"
#include "lle/lle_shell_integration.h"
#include "lle/prompt/composer.h"
#include "lle/prompt/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// ============================================================================
// INTERNAL CONSTANTS AND MACROS
// ============================================================================

// Magic numbers for memory corruption detection
#define PROMPT_LAYER_MAGIC_HEADER 0x50524F4D // "PROM"
#define PROMPT_LAYER_MAGIC_FOOTER 0x505447   // "PTG" (PrompT laYer)

// Hash calculation constants
#define HASH_INITIAL_VALUE 0x811C9DC5
#define HASH_PRIME 0x01000193

// Performance monitoring
#define NSEC_PER_SEC 1000000000L
#define MSEC_TO_NSEC 1000000L

// Cache validation timeouts
#define THEME_CHECK_INTERVAL_MS 50
#define CONTENT_REFRESH_INTERVAL_MS 100

// Debug output macro
#ifdef PROMPT_LAYER_DEBUG
#define DEBUG_PRINT(fmt, ...)                                                  \
    fprintf(stderr, "[PROMPT_LAYER] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Get active theme name from LLE theme registry
 * @return Name of the active theme, or "default" if none available
 */
static const char *get_active_theme_name(void) {
    if (g_lle_integration && g_lle_integration->prompt_composer &&
        g_lle_integration->prompt_composer->themes) {
        const lle_theme_t *theme = lle_theme_registry_get_active(
            g_lle_integration->prompt_composer->themes);
        if (theme) {
            return theme->name;
        }
    }
    return "default";
}

/**
 * @brief Check if a theme is available from LLE
 * @return true if a theme is available, false otherwise
 */
static bool is_theme_available(void) {
    return (g_lle_integration && g_lle_integration->prompt_composer &&
            g_lle_integration->prompt_composer->themes &&
            lle_theme_registry_get_active(
                g_lle_integration->prompt_composer->themes) != NULL);
}

/**
 * @brief Calculate FNV-1a hash for content validation
 * @param content Content string to hash
 * @return 64-bit hash value, or 0 if content is NULL
 */
static uint64_t calculate_content_hash(const char *content) {
    if (!content)
        return 0;

    uint64_t hash = HASH_INITIAL_VALUE;
    const unsigned char *data = (const unsigned char *)content;

    while (*data) {
        hash ^= *data++;
        hash *= HASH_PRIME;
    }

    return hash;
}

/**
 * @brief Get current timestamp with nanosecond precision
 * @return Current timestamp in nanoseconds from monotonic clock
 */
static uint64_t get_current_time_ns(void) {
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}

/**
 * @brief Calculate time difference in nanoseconds
 * @param start_ns Start timestamp in nanoseconds
 * @param end_ns End timestamp in nanoseconds
 * @return Difference between end and start in nanoseconds
 */
static uint64_t time_diff_ns(uint64_t start_ns, uint64_t end_ns) {
    return end_ns - start_ns;
}

/**
 * @brief Validate layer memory integrity using magic numbers
 * @param layer Prompt layer to validate
 * @return true if memory is valid, false if corrupted or NULL
 */
static bool validate_layer_memory(const prompt_layer_t *layer) {
    if (!layer)
        return false;
    return (layer->magic_header == PROMPT_LAYER_MAGIC_HEADER &&
            layer->magic_footer == PROMPT_LAYER_MAGIC_FOOTER);
}

/**
 * @brief Calculate prompt metrics from content string
 * @param content Prompt content to analyze
 * @param metrics Output structure to store calculated metrics
 */
static void calculate_prompt_metrics(const char *content,
                                     prompt_metrics_t *metrics) {
    if (!content || !metrics)
        return;

    memset(metrics, 0, sizeof(*metrics));

    const char *line_start = content;
    const char *current = content;
    int current_line_width = 0;
    bool in_ansi_sequence = false;

    while (*current) {
        /* Skip readline's prompt markers: \001 (RL_PROMPT_START_IGNORE) and
         * \002 (RL_PROMPT_END_IGNORE) */
        if (*current == '\001' || *current == '\002') {
            /* Don't count these control characters */
            current++;
            continue;
        }

        if (*current == '\033') {
            in_ansi_sequence = true;
            metrics->has_ansi_sequences = true;
        } else if (in_ansi_sequence) {
            /* While in ANSI sequence, check if this is the terminator */
            if ((*current >= 'A' && *current <= 'Z') ||
                (*current >= 'a' && *current <= 'z')) {
                /* ANSI sequences end with a letter (A-Z or a-z) */
                in_ansi_sequence = false;
            }
            /* Don't count ANY characters while in_ansi_sequence (including
             * terminators) */
        } else {
            /* Not in ANSI sequence - count this character */
            if (*current == '\n') {
                metrics->line_count++;
                if (current_line_width > metrics->max_line_width) {
                    metrics->max_line_width = current_line_width;
                }
                current_line_width = 0;
                line_start = current + 1;
            } else {
                /* Only count UTF-8 character start bytes, not continuation
                 * bytes */
                /* UTF-8 continuation bytes have the form 10xxxxxx (0x80-0xBF)
                 */
                unsigned char byte = (unsigned char)*current;
                if ((byte & 0xC0) != 0x80) {
                    /* This is a character start byte (ASCII or UTF-8 lead byte)
                     */
                    current_line_width++;

                    // Check for Unicode characters
                    if (byte > 127) {
                        metrics->has_unicode = true;
                    }
                }
                /* Skip UTF-8 continuation bytes - don't increment counter */
            }
        }
        current++;
    }

    // Handle final line if no trailing newline
    if (current > line_start) {
        metrics->line_count++;
        if (current_line_width > metrics->max_line_width) {
            metrics->max_line_width = current_line_width;
        }
    }

    // Ensure at least one line
    if (metrics->line_count == 0) {
        metrics->line_count = 1;
    }

    metrics->is_multiline = (metrics->line_count > 1);
    metrics->total_visual_width = metrics->max_line_width;

    // Estimate command position (best effort)
    // Command always starts after the LAST line, so use current_line_width
    metrics->estimated_command_row = metrics->line_count;
    metrics->estimated_command_column =
        current_line_width + 1; /* +1 for 1-indexed columns */
}

/**
 * @brief Find cached entry for content and theme combination
 * @param layer Prompt layer containing cache
 * @param content Raw prompt content to search for
 * @param theme_name Theme name to match (may be NULL)
 * @return Pointer to matching cache entry, or NULL if not found
 */
static prompt_cache_entry_t *find_cache_entry(prompt_layer_t *layer,
                                              const char *content,
                                              const char *theme_name) {
    if (!layer || !content)
        return NULL;

    uint64_t content_hash = calculate_content_hash(content);
    uint64_t theme_hash = theme_name ? calculate_content_hash(theme_name) : 0;
    uint64_t now = get_current_time_ns();

    for (int i = 0; i < PROMPT_LAYER_CACHE_SIZE; i++) {
        prompt_cache_entry_t *entry = &layer->cache[i];

        if (entry->is_valid && entry->content_hash == content_hash &&
            entry->theme_hash == theme_hash) {

            // Check cache expiry
            uint64_t age_ms =
                time_diff_ns(entry->creation_time_ns, now) / MSEC_TO_NSEC;
            if (age_ms < PROMPT_LAYER_CACHE_EXPIRY_MS) {
                return entry;
            } else {
                // Expire old entry
                entry->is_valid = false;
            }
        }
    }

    return NULL;
}

/**
 * @brief Create new cache entry with rendered content
 * @param layer Prompt layer containing cache
 * @param content Raw prompt content to cache
 * @param rendered_content Rendered prompt content to cache
 * @param theme_name Theme name for this entry (may be NULL)
 * @param metrics Calculated metrics for this content
 * @return Pointer to created cache entry, or NULL on failure
 */
static prompt_cache_entry_t *
create_cache_entry(prompt_layer_t *layer, const char *content,
                   const char *rendered_content, const char *theme_name,
                   const prompt_metrics_t *metrics) {
    if (!layer || !content || !rendered_content)
        return NULL;

    prompt_cache_entry_t *entry = &layer->cache[layer->cache_next_index];
    layer->cache_next_index =
        (layer->cache_next_index + 1) % PROMPT_LAYER_CACHE_SIZE;

    // Free existing content
    if (entry->raw_content) {
        free(entry->raw_content);
        entry->raw_content = NULL;
    }
    if (entry->rendered_content) {
        free(entry->rendered_content);
        entry->rendered_content = NULL;
    }
    if (entry->theme_name) {
        free(entry->theme_name);
        entry->theme_name = NULL;
    }

    // Allocate and copy new content
    entry->raw_content = strdup(content);
    entry->rendered_content = strdup(rendered_content);
    entry->theme_name = theme_name ? strdup(theme_name) : NULL;

    if (!entry->raw_content || !entry->rendered_content) {
        // Allocation failed - invalidate entry
        entry->is_valid = false;
        return NULL;
    }

    entry->content_hash = calculate_content_hash(content);
    entry->theme_hash = theme_name ? calculate_content_hash(theme_name) : 0;
    entry->creation_time_ns = get_current_time_ns();
    entry->metrics = *metrics;
    entry->is_valid = true;

    return entry;
}

/**
 * @brief Clear all cache entries and free associated memory
 * @param layer Prompt layer containing cache to clear
 */
static void clear_cache(prompt_layer_t *layer) {
    if (!layer)
        return;

    for (int i = 0; i < PROMPT_LAYER_CACHE_SIZE; i++) {
        prompt_cache_entry_t *entry = &layer->cache[i];
        if (entry->raw_content) {
            free(entry->raw_content);
            entry->raw_content = NULL;
        }
        if (entry->rendered_content) {
            free(entry->rendered_content);
            entry->rendered_content = NULL;
        }
        if (entry->theme_name) {
            free(entry->theme_name);
            entry->theme_name = NULL;
        }
        entry->is_valid = false;
    }
    layer->cache_next_index = 0;
}

/**
 * @brief Update performance statistics after a render operation
 * @param layer Prompt layer to update statistics for
 * @param render_time_ns Time taken for render in nanoseconds
 * @param cache_hit Whether this was a cache hit
 */
static void update_performance_stats(prompt_layer_t *layer,
                                     uint64_t render_time_ns, bool cache_hit) {
    if (!layer)
        return;

    prompt_performance_t *perf = &layer->performance;

    perf->render_count++;
    perf->total_render_time_ns += render_time_ns;

    if (cache_hit) {
        perf->cache_hits++;
    } else {
        perf->cache_misses++;
    }

    // Update min/max times
    if (perf->render_count == 1 || render_time_ns < perf->min_render_time_ns) {
        perf->min_render_time_ns = render_time_ns;
    }
    if (render_time_ns > perf->max_render_time_ns) {
        perf->max_render_time_ns = render_time_ns;
    }

    // Calculate running average
    perf->avg_render_time_ns = perf->total_render_time_ns / perf->render_count;

    // Store in recent times circular buffer
    perf->recent_render_times[perf->recent_times_index] = render_time_ns;
    perf->recent_times_index =
        (perf->recent_times_index + 1) % PROMPT_LAYER_METRICS_HISTORY_SIZE;

    perf->last_render_time_ns = get_current_time_ns();
    perf->last_metrics_update_ns = perf->last_render_time_ns;
}

/**
 * @brief Event callback handler for theme changes
 * @param event Layer event that triggered this callback
 * @param user_data User data (prompt_layer_t pointer)
 * @return LAYER_EVENTS_SUCCESS on success, error code otherwise
 */
static layer_events_error_t
handle_theme_change_event(const layer_event_t *event, void *user_data) {
    (void)event; /* Event type already validated by dispatcher */
    prompt_layer_t *layer = (prompt_layer_t *)user_data;
    if (!layer || !validate_layer_memory(layer))
        return LAYER_EVENTS_ERROR_INVALID_PARAM;

    DEBUG_PRINT("Theme change event received");

    // Clear cache to force re-rendering with new theme
    clear_cache(layer);
    layer->content_dirty = true;
    layer->theme_context.theme_available = false; // Force theme re-validation

    layer->events_context.events_received++;
    layer->performance.theme_switches++;

    return LAYER_EVENTS_SUCCESS;
}

/**
 * @brief Event callback handler for content refresh requests
 * @param event Layer event that triggered this callback
 * @param user_data User data (prompt_layer_t pointer)
 * @return LAYER_EVENTS_SUCCESS on success, error code otherwise
 */
static layer_events_error_t
handle_content_refresh_event(const layer_event_t *event, void *user_data) {
    (void)event; /* Event type already validated by dispatcher */
    prompt_layer_t *layer = (prompt_layer_t *)user_data;
    if (!layer || !validate_layer_memory(layer))
        return LAYER_EVENTS_ERROR_INVALID_PARAM;

    DEBUG_PRINT("Content refresh event received");

    layer->content_dirty = true;
    layer->metrics_dirty = true;

    layer->events_context.events_received++;

    return LAYER_EVENTS_SUCCESS;
}

/**
 * @brief Render prompt content with current theme
 * @param layer Prompt layer to render content for
 * @return PROMPT_LAYER_SUCCESS on success, error code otherwise
 */
static prompt_layer_error_t render_prompt_content(prompt_layer_t *layer) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->raw_content) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    uint64_t start_time = get_current_time_ns();

    // Get current theme information from LLE
    const char *theme_name = get_active_theme_name();

    // Check cache first
    prompt_cache_entry_t *cached =
        find_cache_entry(layer, layer->raw_content, theme_name);
    if (cached) {
        // Cache hit - use cached content
        if (layer->rendered_content) {
            free(layer->rendered_content);
        }
        layer->rendered_content = strdup(cached->rendered_content);
        layer->current_metrics = cached->metrics;

        uint64_t end_time = get_current_time_ns();
        uint64_t render_time = time_diff_ns(start_time, end_time);
        update_performance_stats(layer, render_time, true);

        DEBUG_PRINT("Cache hit for theme '%s'", theme_name);
        return PROMPT_LAYER_SUCCESS;
    }

    // Cache miss - need to render
    DEBUG_PRINT("Cache miss - rendering with theme '%s'", theme_name);

    char rendered_buffer[PROMPT_LAYER_MAX_CONTENT_SIZE];

    // Use raw_content directly - it's already been rendered by the appropriate
    // prompt system (Spec 25 composer or legacy theme system)
    // Do NOT re-render here as that would override Spec 25 prompts with legacy
    strncpy(rendered_buffer, layer->raw_content, sizeof(rendered_buffer) - 1);
    rendered_buffer[sizeof(rendered_buffer) - 1] = '\0';
    DEBUG_PRINT("Using raw content as prompt");

    // Calculate metrics
    calculate_prompt_metrics(rendered_buffer, &layer->current_metrics);

    // Update rendered content
    if (layer->rendered_content) {
        free(layer->rendered_content);
    }
    layer->rendered_content = strdup(rendered_buffer);

    if (!layer->rendered_content) {
        return PROMPT_LAYER_ERROR_MEMORY_ALLOCATION;
    }

    // Create cache entry
    create_cache_entry(layer, layer->raw_content, rendered_buffer, theme_name,
                       &layer->current_metrics);

    // Update performance statistics
    uint64_t end_time = get_current_time_ns();
    uint64_t render_time = time_diff_ns(start_time, end_time);
    update_performance_stats(layer, render_time, false);

    layer->content_dirty = false;
    layer->metrics_dirty = false;

    return PROMPT_LAYER_SUCCESS;
}

// ============================================================================
// LIFECYCLE FUNCTIONS
// ============================================================================

prompt_layer_t *prompt_layer_create(void) {
    prompt_layer_t *layer = calloc(1, sizeof(prompt_layer_t));
    if (!layer) {
        DEBUG_PRINT("Failed to allocate memory for prompt layer");
        return NULL;
    }

    // Set magic numbers for memory corruption detection
    layer->magic_header = PROMPT_LAYER_MAGIC_HEADER;
    layer->magic_footer = PROMPT_LAYER_MAGIC_FOOTER;

    // Initialize timestamps
    layer->creation_time_ns = get_current_time_ns();
    layer->last_update_time_ns = layer->creation_time_ns;

    // Initialize performance metrics
    layer->performance.min_render_time_ns = UINT64_MAX;

    // Set initial state
    layer->initialized = false;
    layer->enabled = false;
    layer->content_dirty = true;
    layer->metrics_dirty = true;

    layer->allocated_size = sizeof(prompt_layer_t);

    DEBUG_PRINT("Prompt layer created successfully");
    return layer;
}

prompt_layer_error_t prompt_layer_init(prompt_layer_t *layer,
                                       layer_event_system_t *events) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!events) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (layer->initialized) {
        return PROMPT_LAYER_SUCCESS; // Already initialized
    }

    DEBUG_PRINT("Initializing prompt layer");

    // Initialize event system integration
    layer->events_context.events = events;
    layer->events_context.events_initialized = true;

    // Subscribe to relevant events
    // TODO: Event subscription will be implemented when event handlers are
    // ready
    layer_events_error_t event_result;

    // Subscribe to theme change events
    event_result = layer_events_subscribe(
        events, LAYER_EVENT_THEME_CHANGED, LAYER_ID_PROMPT_LAYER,
        handle_theme_change_event, layer, LAYER_EVENT_PRIORITY_HIGH);

    if (event_result == LAYER_EVENTS_SUCCESS) {
        layer->events_context.subscription_count++;
        DEBUG_PRINT("Subscribed to theme change events");
    }

    // Subscribe to content refresh events
    event_result = layer_events_subscribe(
        events, LAYER_EVENT_CONTENT_CHANGED, LAYER_ID_PROMPT_LAYER,
        handle_content_refresh_event, layer, LAYER_EVENT_PRIORITY_NORMAL);

    if (event_result == LAYER_EVENTS_SUCCESS) {
        layer->events_context.subscription_count++;
        DEBUG_PRINT("Subscribed to content refresh events");
    }

    // Initialize theme context
    layer->theme_context.theme_available = is_theme_available();
    if (layer->theme_context.theme_available) {
        const char *theme_name = get_active_theme_name();
        layer->theme_context.current_theme_name = strdup(theme_name);
        layer->theme_context.theme_hash = calculate_content_hash(theme_name);
    }
    layer->theme_context.last_theme_check_ns = get_current_time_ns();

    // Set initialization complete
    layer->initialized = true;
    layer->enabled = true;
    layer->last_update_time_ns = get_current_time_ns();

    DEBUG_PRINT("Prompt layer initialized successfully");
    return PROMPT_LAYER_SUCCESS;
}

prompt_layer_error_t prompt_layer_cleanup(prompt_layer_t *layer) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    DEBUG_PRINT("Cleaning up prompt layer");

    // Disable layer
    layer->enabled = false;

    // Unsubscribe from events if initialized
    if (layer->events_context.events_initialized &&
        layer->events_context.events) {
        // Note: In a real implementation, we would need unsubscribe functions
        // For now, just clear the event context
        layer->events_context.events = NULL;
        layer->events_context.subscription_count = 0;
        layer->events_context.events_initialized = false;
    }

    // Clear cache
    clear_cache(layer);

    // Free content strings
    if (layer->raw_content) {
        free(layer->raw_content);
        layer->raw_content = NULL;
    }

    if (layer->rendered_content) {
        free(layer->rendered_content);
        layer->rendered_content = NULL;
    }

    // Free theme context
    if (layer->theme_context.current_theme_name) {
        free(layer->theme_context.current_theme_name);
        layer->theme_context.current_theme_name = NULL;
    }

    // Reset state
    layer->initialized = false;
    layer->content_dirty = true;
    layer->metrics_dirty = true;

    DEBUG_PRINT("Prompt layer cleanup complete");
    return PROMPT_LAYER_SUCCESS;
}

void prompt_layer_destroy(prompt_layer_t *layer) {
    if (!layer)
        return;

    DEBUG_PRINT("Destroying prompt layer");

    // Validate memory integrity before destruction
    if (!validate_layer_memory(layer)) {
        DEBUG_PRINT("WARNING: Memory corruption detected during destruction");
    }

    // Cleanup if not already done
    if (layer->initialized) {
        prompt_layer_cleanup(layer);
    }

    // Clear magic numbers
    layer->magic_header = 0;
    layer->magic_footer = 0;

    // Free the structure
    free(layer);

    DEBUG_PRINT("Prompt layer destroyed");
}

// ============================================================================
// CONTENT MANAGEMENT FUNCTIONS
// ============================================================================

prompt_layer_error_t prompt_layer_set_content(prompt_layer_t *layer,
                                              const char *content) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!content) {
        return PROMPT_LAYER_ERROR_NULL_POINTER;
    }

    if (!layer->initialized) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    size_t content_len = strlen(content);
    if (content_len >= PROMPT_LAYER_MAX_CONTENT_SIZE) {
        return PROMPT_LAYER_ERROR_CONTENT_TOO_LARGE;
    }

    DEBUG_PRINT("Setting prompt content (%zu bytes)", content_len);

    // Free existing content
    if (layer->raw_content) {
        free(layer->raw_content);
    }

    // Copy new content
    layer->raw_content = strdup(content);
    if (!layer->raw_content) {
        return PROMPT_LAYER_ERROR_MEMORY_ALLOCATION;
    }

    // Mark content as dirty for re-rendering
    layer->content_dirty = true;
    layer->metrics_dirty = true;
    layer->last_update_time_ns = get_current_time_ns();

    return PROMPT_LAYER_SUCCESS;
}

prompt_layer_error_t prompt_layer_get_rendered_content(prompt_layer_t *layer,
                                                       char *output,
                                                       size_t output_size) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!output || output_size < 1) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->initialized || !layer->enabled) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    // Render content if dirty or not yet rendered
    if (layer->content_dirty || !layer->rendered_content) {
        prompt_layer_error_t result = render_prompt_content(layer);
        if (result != PROMPT_LAYER_SUCCESS) {
            return result;
        }
    }

    if (!layer->rendered_content) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    size_t content_len = strlen(layer->rendered_content);
    if (content_len >= output_size) {
        return PROMPT_LAYER_ERROR_BUFFER_TOO_SMALL;
    }

    strcpy(output, layer->rendered_content);

    DEBUG_PRINT("Returned rendered content (%zu bytes)", content_len);
    return PROMPT_LAYER_SUCCESS;
}

prompt_layer_error_t prompt_layer_get_metrics(prompt_layer_t *layer,
                                              prompt_metrics_t *metrics) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!metrics) {
        return PROMPT_LAYER_ERROR_NULL_POINTER;
    }

    if (!layer->initialized || !layer->enabled) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    // Ensure metrics are up to date
    if (layer->metrics_dirty || layer->content_dirty) {
        prompt_layer_error_t result = render_prompt_content(layer);
        if (result != PROMPT_LAYER_SUCCESS) {
            return result;
        }
    }

    *metrics = layer->current_metrics;
    return PROMPT_LAYER_SUCCESS;
}

// ============================================================================
// THEME INTEGRATION FUNCTIONS
// ============================================================================

prompt_layer_error_t prompt_layer_update_theme(prompt_layer_t *layer) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->initialized) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    uint64_t now = get_current_time_ns();
    uint64_t time_since_check =
        time_diff_ns(layer->theme_context.last_theme_check_ns, now) /
        MSEC_TO_NSEC;

    // Limit theme checks to avoid performance impact
    if (time_since_check < THEME_CHECK_INTERVAL_MS) {
        return PROMPT_LAYER_SUCCESS;
    }

    DEBUG_PRINT("Updating theme integration");

    bool theme_available = is_theme_available();
    bool theme_changed = false;

    if (theme_available) {
        const char *theme_name = get_active_theme_name();
        uint64_t new_theme_hash = calculate_content_hash(theme_name);

        if (!layer->theme_context.theme_available ||
            layer->theme_context.theme_hash != new_theme_hash) {

            // Theme changed or became available
            theme_changed = true;

            if (layer->theme_context.current_theme_name) {
                free(layer->theme_context.current_theme_name);
            }
            layer->theme_context.current_theme_name = strdup(theme_name);
            layer->theme_context.theme_hash = new_theme_hash;
            layer->theme_context.theme_available = true;

            DEBUG_PRINT("Theme changed to '%s'", theme_name);
        }
    } else {
        if (layer->theme_context.theme_available) {
            // Theme became unavailable
            theme_changed = true;
            layer->theme_context.theme_available = false;

            DEBUG_PRINT("Theme became unavailable");
        }
    }

    if (theme_changed) {
        // Clear cache and mark content dirty
        clear_cache(layer);
        layer->content_dirty = true;
        layer->performance.theme_switches++;
    }

    layer->theme_context.last_theme_check_ns = now;
    return PROMPT_LAYER_SUCCESS;
}

prompt_layer_error_t prompt_layer_force_render(prompt_layer_t *layer) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->initialized || !layer->enabled) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    DEBUG_PRINT("Forcing prompt re-render");

    // Clear cache to force fresh rendering
    clear_cache(layer);
    layer->content_dirty = true;
    layer->metrics_dirty = true;

    return render_prompt_content(layer);
}

// ============================================================================
// PERFORMANCE AND MONITORING FUNCTIONS
// ============================================================================

prompt_layer_error_t
prompt_layer_get_performance(prompt_layer_t *layer,
                             prompt_performance_t *performance) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!performance) {
        return PROMPT_LAYER_ERROR_NULL_POINTER;
    }

    *performance = layer->performance;
    return PROMPT_LAYER_SUCCESS;
}

prompt_layer_error_t prompt_layer_reset_performance(prompt_layer_t *layer) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    DEBUG_PRINT("Resetting performance statistics");

    // Reset all counters but preserve cache
    memset(&layer->performance, 0, sizeof(layer->performance));
    layer->performance.min_render_time_ns = UINT64_MAX;
    layer->performance.last_metrics_update_ns = get_current_time_ns();

    return PROMPT_LAYER_SUCCESS;
}

prompt_layer_error_t prompt_layer_optimize(prompt_layer_t *layer) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->initialized) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    DEBUG_PRINT("Optimizing prompt layer performance");

    uint64_t now = get_current_time_ns();

    // Clean up expired cache entries
    int expired_count = 0;
    (void)expired_count; /* Reserved for cache cleanup statistics */
    for (int i = 0; i < PROMPT_LAYER_CACHE_SIZE; i++) {
        prompt_cache_entry_t *entry = &layer->cache[i];
        if (entry->is_valid) {
            uint64_t age_ms =
                time_diff_ns(entry->creation_time_ns, now) / MSEC_TO_NSEC;
            if (age_ms >= PROMPT_LAYER_CACHE_EXPIRY_MS) {
                if (entry->raw_content) {
                    free(entry->raw_content);
                    entry->raw_content = NULL;
                }
                if (entry->rendered_content) {
                    free(entry->rendered_content);
                    entry->rendered_content = NULL;
                }
                if (entry->theme_name) {
                    free(entry->theme_name);
                    entry->theme_name = NULL;
                }
                entry->is_valid = false;
                expired_count++;
            }
        }
    }

    DEBUG_PRINT("Optimization complete - expired %d cache entries",
                expired_count);
    return PROMPT_LAYER_SUCCESS;
}

// ============================================================================
// EVENT HANDLING FUNCTIONS
// ============================================================================

prompt_layer_error_t prompt_layer_process_events(prompt_layer_t *layer) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->initialized || !layer->events_context.events_initialized) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    if (!layer->events_context.events) {
        return PROMPT_LAYER_ERROR_EVENT_SYSTEM_FAILURE;
    }

    // Process pending events through the event system
    int result =
        layer_events_process_pending(layer->events_context.events, 32, 50);
    if (result < 0) {
        return PROMPT_LAYER_ERROR_EVENT_SYSTEM_FAILURE;
    }

    layer->events_context.events_processed++;
    return PROMPT_LAYER_SUCCESS;
}

// ============================================================================
// UTILITY AND DIAGNOSTIC FUNCTIONS
// ============================================================================

prompt_layer_error_t prompt_layer_validate(prompt_layer_t *layer) {
    if (!layer) {
        return PROMPT_LAYER_ERROR_NULL_POINTER;
    }

    // Check memory corruption markers
    if (!validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    // Validate initialization state
    if (layer->initialized) {
        if (!layer->events_context.events) {
            return PROMPT_LAYER_ERROR_EVENT_SYSTEM_FAILURE;
        }
    }

    // Validate content consistency
    if (layer->raw_content && !layer->rendered_content &&
        !layer->content_dirty) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    return PROMPT_LAYER_SUCCESS;
}

const char *prompt_layer_error_string(prompt_layer_error_t error) {
    switch (error) {
    case PROMPT_LAYER_SUCCESS:
        return "Success";
    case PROMPT_LAYER_ERROR_INVALID_PARAM:
        return "Invalid parameter";
    case PROMPT_LAYER_ERROR_NULL_POINTER:
        return "NULL pointer";
    case PROMPT_LAYER_ERROR_MEMORY_ALLOCATION:
        return "Memory allocation failed";
    case PROMPT_LAYER_ERROR_BUFFER_TOO_SMALL:
        return "Buffer too small";
    case PROMPT_LAYER_ERROR_CONTENT_TOO_LARGE:
        return "Content too large";
    case PROMPT_LAYER_ERROR_THEME_NOT_AVAILABLE:
        return "Theme not available";
    case PROMPT_LAYER_ERROR_EVENT_SYSTEM_FAILURE:
        return "Event system failure";
    case PROMPT_LAYER_ERROR_RENDERING_FAILURE:
        return "Rendering failure";
    case PROMPT_LAYER_ERROR_INVALID_STATE:
        return "Invalid state";
    case PROMPT_LAYER_ERROR_PERFORMANCE_TIMEOUT:
        return "Performance timeout";
    case PROMPT_LAYER_ERROR_UNKNOWN:
    default:
        return "Unknown error";
    }
}

void prompt_layer_get_version(int *major, int *minor, int *patch) {
    if (major)
        *major = PROMPT_LAYER_VERSION_MAJOR;
    if (minor)
        *minor = PROMPT_LAYER_VERSION_MINOR;
    if (patch)
        *patch = PROMPT_LAYER_VERSION_PATCH;
}

// ============================================================================
// INTEGRATION HELPERS
// ============================================================================

prompt_layer_error_t prompt_layer_generate_from_lush(prompt_layer_t *layer) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->initialized) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    DEBUG_PRINT("Generating prompt from Lush system");

    // Expand PS1 format string via unified prompt engine (Spec 28)
    lle_shell_update_prompt();

    // Get the rendered prompt (not the format string in PS1)
    const char *rendered = lle_shell_get_rendered_prompt();
    if (!rendered || !*rendered) {
        rendered = "$ ";
        DEBUG_PRINT("Rendered prompt empty, using simple prompt");
    }

    // Set the rendered content in the layer
    prompt_layer_error_t result = prompt_layer_set_content(layer, rendered);
    if (result != PROMPT_LAYER_SUCCESS) {
        return result;
    }

    DEBUG_PRINT("Generated prompt from Lush: '%.50s%s'", rendered,
                strlen(rendered) > 50 ? "..." : "");

    return PROMPT_LAYER_SUCCESS;
}

prompt_layer_error_t prompt_layer_run_tests(prompt_layer_t *layer) {
    if (!layer || !validate_layer_memory(layer)) {
        return PROMPT_LAYER_ERROR_INVALID_PARAM;
    }

    if (!layer->initialized) {
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    DEBUG_PRINT("Running prompt layer tests");

    prompt_layer_error_t result;
    char output[PROMPT_LAYER_MAX_CONTENT_SIZE];
    prompt_metrics_t metrics;
    uint64_t start_time, end_time;

    // Test 1: Simple prompt
    result = prompt_layer_set_content(layer, "$ ");
    if (result != PROMPT_LAYER_SUCCESS) {
        DEBUG_PRINT("Test 1 failed: %s", prompt_layer_error_string(result));
        return result;
    }

    start_time = get_current_time_ns();
    result = prompt_layer_get_rendered_content(layer, output, sizeof(output));
    end_time = get_current_time_ns();

    if (result != PROMPT_LAYER_SUCCESS) {
        DEBUG_PRINT("Test 1 failed: %s", prompt_layer_error_string(result));
        return result;
    }

    uint64_t render_time = time_diff_ns(start_time, end_time);
    if (render_time > PROMPT_LAYER_TARGET_RENDER_TIME_MS * MSEC_TO_NSEC) {
        DEBUG_PRINT("Test 1 failed: render time %lu ns exceeds target",
                    render_time);
        return PROMPT_LAYER_ERROR_PERFORMANCE_TIMEOUT;
    }

    DEBUG_PRINT("Test 1 passed: simple prompt (%lu ns)", render_time);

    // Test 2: Complex prompt
    result = prompt_layer_set_content(layer, "[user@host ~/path]$ ");
    if (result != PROMPT_LAYER_SUCCESS) {
        DEBUG_PRINT("Test 2 failed: %s", prompt_layer_error_string(result));
        return result;
    }

    result = prompt_layer_get_metrics(layer, &metrics);
    if (result != PROMPT_LAYER_SUCCESS) {
        DEBUG_PRINT("Test 2 failed: %s", prompt_layer_error_string(result));
        return result;
    }

    if (metrics.line_count != 1 || metrics.max_line_width < 10) {
        DEBUG_PRINT("Test 2 failed: invalid metrics");
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    DEBUG_PRINT("Test 2 passed: complex prompt");

    // Test 3: Multiline prompt
    result = prompt_layer_set_content(layer, "┌─[user@host]─[~/path]\n└─$ ");
    if (result != PROMPT_LAYER_SUCCESS) {
        DEBUG_PRINT("Test 3 failed: %s", prompt_layer_error_string(result));
        return result;
    }

    result = prompt_layer_get_metrics(layer, &metrics);
    if (result != PROMPT_LAYER_SUCCESS) {
        DEBUG_PRINT("Test 3 failed: %s", prompt_layer_error_string(result));
        return result;
    }

    if (!metrics.is_multiline || metrics.line_count != 2) {
        DEBUG_PRINT("Test 3 failed: multiline not detected correctly");
        return PROMPT_LAYER_ERROR_INVALID_STATE;
    }

    DEBUG_PRINT("Test 3 passed: multiline prompt");

    // Test 4: Performance test with cache
    for (int i = 0; i < 10; i++) {
        start_time = get_current_time_ns();
        result =
            prompt_layer_get_rendered_content(layer, output, sizeof(output));
        end_time = get_current_time_ns();

        if (result != PROMPT_LAYER_SUCCESS) {
            DEBUG_PRINT("Test 4 failed: %s", prompt_layer_error_string(result));
            return result;
        }

        render_time = time_diff_ns(start_time, end_time);
        if (i > 0 &&
            render_time > PROMPT_LAYER_TARGET_RENDER_TIME_MS * MSEC_TO_NSEC) {
            DEBUG_PRINT(
                "Test 4 failed: cached render time %lu ns exceeds target",
                render_time);
            return PROMPT_LAYER_ERROR_PERFORMANCE_TIMEOUT;
        }
    }

    DEBUG_PRINT("Test 4 passed: performance and caching");

    // Test 5: Theme integration
    result = prompt_layer_update_theme(layer);
    if (result != PROMPT_LAYER_SUCCESS) {
        DEBUG_PRINT("Test 5 failed: %s", prompt_layer_error_string(result));
        return result;
    }

    DEBUG_PRINT("Test 5 passed: theme integration");

    DEBUG_PRINT("All prompt layer tests passed!");
    return PROMPT_LAYER_SUCCESS;
}