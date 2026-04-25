/**
 * @file composition_engine.c
 * @brief Composition Engine - Intelligent Layer Combination System
 *
 * Part of the Lush Shell Layered Display Architecture.
 * Combines display layers with proper z-ordering and optimization.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * ============================================================================
 *
 * COMPOSITION ENGINE IMPLEMENTATION
 *
 * This file implements the composition engine that intelligently combines
 * prompt and command layers without interference, enabling universal prompt
 * compatibility with real-time syntax highlighting.
 *
 * Key Features:
 * - Intelligent prompt structure analysis (non-invasive)
 * - Smart positioning and conflict resolution
 * - Universal compatibility with any prompt structure
 * - High-performance composition with caching
 * - Event-driven coordination between layers
 *
 * Strategic Innovation:
 * This engine completes the revolutionary shell display technology by enabling
 * the universal combination of professional themes with real-time syntax
 * highlighting for ANY prompt structure.
 */

#include "display/composition_engine.h"
#include "display/base_terminal.h"
#include "display/screen_buffer.h"
#include "display_integration.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

// ============================================================================
// FORWARD DECLARATIONS (Multiline Support)
// ============================================================================

/**
 * Represents a single line in a multiline command
 */
typedef struct {
    const char *content; // Pointer to start of line content
    size_t length;       // Length in bytes (not including \n)
    size_t byte_offset;  // Byte offset from start of command
} command_line_info_t;

// Forward declarations for multiline composition helpers
static size_t split_command_lines(const char *command_text,
                                  command_line_info_t *lines, size_t max_lines);

static composition_engine_error_t
build_continuation_prompts(composition_engine_t *engine,
                           const char *primary_prompt, const char *command_text,
                           const command_line_info_t *lines, size_t line_count,
                           char (*prompts)[256], size_t prompt_size);

static composition_engine_error_t coordinate_screen_buffer_rendering(
    composition_engine_t *engine, const command_line_info_t *lines,
    size_t line_count, char (*prompts)[256], char *output, size_t output_size);

static composition_engine_error_t translate_cursor_to_screen_position(
    const char *command_text, size_t cursor_byte_offset,
    const command_line_info_t *lines, size_t line_count, char (*prompts)[256],
    int terminal_width, size_t *out_line, size_t *out_column);

// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Get current timestamp in nanoseconds
 * @return Current timestamp in nanoseconds
 */
static uint64_t get_timestamp_ns(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
}

/**
 * @brief Calculate simple hash of string content using djb2 algorithm
 * @param content Content string to hash
 * @param hash_output Buffer to store the hash string
 * @param hash_size Size of the hash output buffer
 */
static void calculate_content_hash(const char *content, char *hash_output,
                                   size_t hash_size) {
    if (!content || !hash_output || hash_size < 16) {
        if (hash_output && hash_size > 0) {
            strncpy(hash_output, "invalid", hash_size - 1);
            hash_output[hash_size - 1] = '\0';
        }
        return;
    }

    uint32_t hash = 5381; // djb2 hash algorithm
    const char *str = content;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }

    snprintf(hash_output, hash_size, "%08x", hash);
}

/**
 * @brief Count the number of lines in text content
 * @param text Text content to count lines in
 * @return Number of lines in the text
 */
static size_t count_lines(const char *text) {
    if (!text)
        return 0;

    size_t lines = 1;
    const char *ptr = text;

    while (*ptr) {
        if (*ptr == '\n') {
            lines++;
        }
        ptr++;
    }

    // If text ends with newline, don't count the empty line after it
    if (ptr > text && *(ptr - 1) == '\n') {
        lines--;
    }

    return lines;
}

/**
 * @brief Get the visual width of the last line in text
 * @param text Text content to analyze
 * @return Visual width of the last line (excluding ANSI sequences)
 */
static size_t get_last_line_width(const char *text) {
    if (!text)
        return 0;

    const char *last_newline = strrchr(text, '\n');
    const char *last_line = last_newline ? last_newline + 1 : text;

    size_t width = 0;
    while (*last_line) {
        if (*last_line == '\033') {
            // Skip ANSI escape sequences
            while (*last_line && *last_line != 'm') {
                last_line++;
            }
            if (*last_line)
                last_line++;
        } else {
            width++;
            last_line++;
        }
    }

    return width;
}

/**
 * @brief Check if text contains ANSI escape sequences
 * @param text Text content to check
 * @return true if text contains ANSI sequences, false otherwise
 */
static bool contains_ansi_sequences(const char *text) {
    if (!text)
        return false;
    return strchr(text, '\033') != NULL;
}

/**
 * @brief Detect if prompt is ASCII art style
 * @param text Text content to analyze for ASCII art characters
 * @return true if prompt appears to be ASCII art, false otherwise
 */
static bool is_ascii_art_prompt(const char *text) {
    if (!text)
        return false;

    size_t line_count = count_lines(text);
    if (line_count < 2)
        return false;

    // Look for common ASCII art characters
    const char *art_chars = "┌┐└┘├┤┬┴┼─│╭╮╰╯╠╣╦╩╬═║/\\()[]{}";

    size_t art_char_count = 0;
    size_t total_chars = 0;

    for (const char *ptr = text; *ptr; ptr++) {
        if (!isspace(*ptr) && *ptr != '\033') {
            total_chars++;
            if (strchr(art_chars, *ptr)) {
                art_char_count++;
            }
        }
    }

    // If more than 30% of non-whitespace chars are ASCII art chars, likely
    // ASCII art
    return total_chars > 0 && (art_char_count * 100 / total_chars) > 30;
}

/**
 * @brief Clear composition cache entry and free associated resources
 * @param entry Cache entry to clear
 */
static void clear_cache_entry(composition_cache_entry_t *entry) {
    if (!entry)
        return;

    if (entry->cached_output) {
        free(entry->cached_output);
        entry->cached_output = NULL;
    }

    memset(entry->prompt_hash, 0, sizeof(entry->prompt_hash));
    memset(entry->command_hash, 0, sizeof(entry->command_hash));
    memset(&entry->cached_analysis, 0, sizeof(entry->cached_analysis));
    memset(&entry->cached_positioning, 0, sizeof(entry->cached_positioning));
    memset(&entry->timestamp, 0, sizeof(entry->timestamp));
    entry->valid = false;
}

/**
 * @brief Find cache entry by prompt and command hash
 * @param engine Composition engine instance
 * @param prompt_hash Hash of prompt content to search for
 * @param command_hash Hash of command content to search for
 * @return Pointer to matching cache entry, or NULL if not found
 */
static composition_cache_entry_t *find_cache_entry(composition_engine_t *engine,
                                                   const char *prompt_hash,
                                                   const char *command_hash) {
    if (!engine || !prompt_hash || !command_hash)
        return NULL;

    for (size_t i = 0; i < COMPOSITION_ENGINE_CACHE_SIZE; i++) {
        composition_cache_entry_t *entry = &engine->cache[i];
        if (entry->valid && strcmp(entry->prompt_hash, prompt_hash) == 0 &&
            strcmp(entry->command_hash, command_hash) == 0) {
            return entry;
        }
    }

    return NULL;
}

/**
 * @brief Get least recently used cache entry for replacement
 * @param engine Composition engine instance
 * @return Pointer to LRU cache entry, or NULL if engine is invalid
 */
static composition_cache_entry_t *
get_lru_cache_entry(composition_engine_t *engine) {
    if (!engine)
        return NULL;

    composition_cache_entry_t *lru_entry = &engine->cache[0];
    struct timeval oldest_time = lru_entry->timestamp;

    for (size_t i = 1; i < COMPOSITION_ENGINE_CACHE_SIZE; i++) {
        composition_cache_entry_t *entry = &engine->cache[i];
        if (!entry->valid) {
            return entry; // Use invalid entry first
        }

        if (entry->timestamp.tv_sec < oldest_time.tv_sec ||
            (entry->timestamp.tv_sec == oldest_time.tv_sec &&
             entry->timestamp.tv_usec < oldest_time.tv_usec)) {
            lru_entry = entry;
            oldest_time = entry->timestamp;
        }
    }

    return lru_entry;
}

/**
 * @brief Check if cache entry is expired based on max cache age
 * @param engine Composition engine instance with cache age configuration
 * @param entry Cache entry to check for expiration
 * @return true if entry is expired or invalid, false if still valid
 */
static bool is_cache_entry_expired(const composition_engine_t *engine,
                                   const composition_cache_entry_t *entry) {
    if (!engine || !entry || !entry->valid)
        return true;

    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    uint64_t entry_age_ms =
        (current_time.tv_sec - entry->timestamp.tv_sec) * 1000 +
        (current_time.tv_usec - entry->timestamp.tv_usec) / 1000;

    return entry_age_ms > engine->max_cache_age_ms;
}

// ============================================================================
// PROMPT ANALYSIS FUNCTIONS
// ============================================================================

/**
 * @brief Analyze prompt structure non-invasively
 * @param engine Composition engine instance
 * @param prompt_content Prompt content to analyze
 * @param analysis Output structure to store analysis results
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code otherwise
 */
static composition_engine_error_t
analyze_prompt_structure(composition_engine_t *engine,
                         const char *prompt_content,
                         composition_analysis_t *analysis) {
    if (!engine || !prompt_content || !analysis) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    memset(analysis, 0, sizeof(composition_analysis_t));

    // Basic line and width analysis
    analysis->line_count = count_lines(prompt_content);
    analysis->last_line_length = get_last_line_width(prompt_content);
    analysis->has_ansi_sequences = contains_ansi_sequences(prompt_content);
    analysis->is_multiline = analysis->line_count > 1;
    analysis->is_ascii_art = is_ascii_art_prompt(prompt_content);

    // Find maximum line width
    analysis->max_line_width = 0;
    const char *line_start = prompt_content;
    const char *ptr = prompt_content;

    while (*ptr) {
        if (*ptr == '\n' || *(ptr + 1) == '\0') {
            size_t line_width = get_last_line_width(line_start);
            if (line_width > analysis->max_line_width) {
                analysis->max_line_width = line_width;
            }
            line_start = ptr + 1;
        }
        ptr++;
    }

    // Determine cursor position (end of last line)
    analysis->cursor_line = analysis->line_count - 1;
    analysis->cursor_column = analysis->last_line_length;

    // Check if prompt ends with space
    size_t content_len = strlen(prompt_content);
    analysis->has_trailing_space =
        content_len > 0 && (prompt_content[content_len - 1] == ' ' ||
                            prompt_content[content_len - 1] == '\t');

    // Store last line content for analysis
    const char *last_line_start = strrchr(prompt_content, '\n');
    if (last_line_start) {
        last_line_start++; // Skip the newline
    } else {
        last_line_start = prompt_content;
    }

    strncpy(analysis->last_line_content, last_line_start,
            sizeof(analysis->last_line_content) - 1);
    analysis->last_line_content[sizeof(analysis->last_line_content) - 1] = '\0';

    // Recommend composition strategy
    if (analysis->is_ascii_art) {
        analysis->recommended_strategy = COMPOSITION_STRATEGY_ASCII_ART;
    } else if (analysis->is_multiline) {
        analysis->recommended_strategy = COMPOSITION_STRATEGY_MULTILINE;
    } else if (analysis->has_ansi_sequences ||
               analysis->last_line_length > 50) {
        analysis->recommended_strategy = COMPOSITION_STRATEGY_COMPLEX;
    } else {
        analysis->recommended_strategy = COMPOSITION_STRATEGY_SIMPLE;
    }

    return COMPOSITION_ENGINE_SUCCESS;
}

// ============================================================================
// POSITIONING CALCULATION FUNCTIONS
// ============================================================================

/**
 * @brief Calculate optimal positioning for command layer
 * @param engine Composition engine instance
 * @param analysis Prompt analysis results to base positioning on
 * @param positioning Output structure to store calculated positioning
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code otherwise
 */
static composition_engine_error_t
calculate_optimal_positioning(composition_engine_t *engine,
                              const composition_analysis_t *analysis,
                              composition_positioning_t *positioning) {
    if (!engine || !analysis || !positioning) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    memset(positioning, 0, sizeof(composition_positioning_t));

    // Basic positioning setup
    positioning->prompt_start_line = 0;
    positioning->prompt_end_line = analysis->line_count - 1;

    // Determine command positioning based on prompt structure
    if (analysis->is_multiline || analysis->is_ascii_art) {
        // For multi-line or ASCII art prompts, place command on next line
        positioning->command_on_same_line = false;
        positioning->command_start_line = analysis->line_count;
        positioning->command_start_column = 0;
    } else {
        // For single-line prompts, place command on same line
        positioning->command_on_same_line = true;
        positioning->command_start_line = analysis->line_count - 1;
        positioning->command_start_column = analysis->last_line_length;

        // Add space if prompt doesn't end with one
        if (!analysis->has_trailing_space) {
            positioning->command_start_column++; // Account for space we'll add
        }
    }

    // Calculate total dimensions
    if (positioning->command_on_same_line) {
        positioning->total_lines = analysis->line_count;
        positioning->total_width = positioning->command_start_column;
    } else {
        positioning->total_lines = analysis->line_count + 1;
        positioning->total_width = analysis->max_line_width;
    }

    // Determine if cursor positioning is needed
    positioning->needs_cursor_positioning = analysis->is_multiline ||
                                            analysis->is_ascii_art ||
                                            analysis->has_ansi_sequences;

    return COMPOSITION_ENGINE_SUCCESS;
}

// ============================================================================
// COMPOSITION ALGORITHMS
// ============================================================================

/**
 * @brief Compose layers using simple strategy for single-line prompts
 * @param engine Composition engine instance
 * @param prompt_content Prompt layer content
 * @param command_content Command layer content
 * @param positioning Calculated positioning information
 * @param output Output buffer for composed result
 * @param output_size Size of output buffer
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code otherwise
 */
static composition_engine_error_t
compose_simple_strategy(composition_engine_t *engine,
                        const char *prompt_content, const char *command_content,
                        const composition_positioning_t *positioning,
                        char *output, size_t output_size) {
    if (!engine || !prompt_content || !command_content || !positioning ||
        !output) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    size_t written = 0;

    // Add prompt content
    size_t prompt_len = strlen(prompt_content);
    if (written + prompt_len >= output_size) {
        return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
    }

    strcpy(output + written, prompt_content);
    written += prompt_len;

    // Add space if needed
    if (!positioning->command_on_same_line) {
        if (written + 1 >= output_size) {
            return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
        }
        output[written++] = '\n';
    } else {
        // Check if we need to add a space
        if (prompt_len > 0 && prompt_content[prompt_len - 1] != ' ') {
            if (written + 1 >= output_size) {
                return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
            }
            output[written++] = ' ';
        }
    }

    // Add command content
    size_t command_len = strlen(command_content);
    if (written + command_len >= output_size) {
        return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
    }

    strcpy(output + written, command_content);
    written += command_len;

    // Null terminate
    if (written >= output_size) {
        return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
    }
    output[written] = '\0';

    return COMPOSITION_ENGINE_SUCCESS;
}

/**
 * @brief Compose layers using multiline strategy for complex prompts
 * @param engine Composition engine instance
 * @param prompt_content Prompt layer content
 * @param command_content Command layer content
 * @param positioning Calculated positioning information
 * @param output Output buffer for composed result
 * @param output_size Size of output buffer
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code otherwise
 */
static composition_engine_error_t compose_multiline_strategy(
    composition_engine_t *engine, const char *prompt_content,
    const char *command_content, const composition_positioning_t *positioning,
    char *output, size_t output_size) {
    if (!engine || !prompt_content || !command_content || !positioning ||
        !output) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    size_t written = 0;

    // Add prompt content exactly as-is
    size_t prompt_len = strlen(prompt_content);
    if (written + prompt_len >= output_size) {
        return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
    }

    strcpy(output + written, prompt_content);
    written += prompt_len;

    // For multiline prompts, command goes on the line after prompt
    if (positioning->command_on_same_line) {
        // Add space if prompt doesn't end with one
        if (prompt_len > 0 && prompt_content[prompt_len - 1] != ' ') {
            if (written + 1 >= output_size) {
                return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
            }
            output[written++] = ' ';
        }
    } else {
        // Add newline if prompt doesn't end with one
        if (prompt_len == 0 || prompt_content[prompt_len - 1] != '\n') {
            if (written + 1 >= output_size) {
                return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
            }
            output[written++] = '\n';
        }
    }

    // Add command content
    size_t command_len = strlen(command_content);
    if (written + command_len >= output_size) {
        return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
    }

    strcpy(output + written, command_content);
    written += command_len;

    // Null terminate
    if (written >= output_size) {
        return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
    }
    output[written] = '\0';

    return COMPOSITION_ENGINE_SUCCESS;
}

/**
 * @brief Compose layers using the appropriate strategy based on analysis
 * @param engine Composition engine instance
 * @param prompt_content Prompt layer content
 * @param command_content Command layer content
 * @param analysis Prompt analysis results for strategy selection
 * @param positioning Calculated positioning information
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code otherwise
 */
static composition_engine_error_t
compose_layers(composition_engine_t *engine, const char *prompt_content,
               const char *command_content,
               const composition_analysis_t *analysis,
               const composition_positioning_t *positioning) {
    if (!engine || !prompt_content || !command_content || !analysis ||
        !positioning) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    composition_engine_error_t result;

    // Determine strategy to use
    composition_strategy_t strategy = engine->current_strategy;
    if (strategy == COMPOSITION_STRATEGY_ADAPTIVE) {
        strategy = analysis->recommended_strategy;
    }

    // Allocate output buffer if needed
    if (!engine->composed_output) {
        engine->composed_output = malloc(COMPOSITION_ENGINE_MAX_OUTPUT_SIZE);
        if (!engine->composed_output) {
            return COMPOSITION_ENGINE_ERROR_MEMORY_ALLOCATION;
        }
        engine->composed_output_size = COMPOSITION_ENGINE_MAX_OUTPUT_SIZE;
    }

    // Apply composition strategy
    switch (strategy) {
    case COMPOSITION_STRATEGY_SIMPLE:
        result = compose_simple_strategy(
            engine, prompt_content, command_content, positioning,
            engine->composed_output, engine->composed_output_size);
        break;

    case COMPOSITION_STRATEGY_MULTILINE:
    case COMPOSITION_STRATEGY_COMPLEX:
    case COMPOSITION_STRATEGY_ASCII_ART:
        result = compose_multiline_strategy(
            engine, prompt_content, command_content, positioning,
            engine->composed_output, engine->composed_output_size);
        break;

    default:
        result = COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
        break;
    }

    return result;
}

// ============================================================================
// CACHE MANAGEMENT FUNCTIONS
// ============================================================================

/**
 * @brief Store composition result in cache for future reuse
 * @param engine Composition engine instance
 * @param prompt_hash Hash of prompt content for cache key
 * @param command_hash Hash of command content for cache key
 * @param analysis Analysis results to cache
 * @param positioning Positioning results to cache
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code otherwise
 */
static composition_engine_error_t
store_in_cache(composition_engine_t *engine, const char *prompt_hash,
               const char *command_hash, const composition_analysis_t *analysis,
               const composition_positioning_t *positioning) {
    if (!engine || !prompt_hash || !command_hash || !analysis || !positioning) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    composition_cache_entry_t *entry = get_lru_cache_entry(engine);
    if (!entry) {
        return COMPOSITION_ENGINE_ERROR_CACHE_INVALID;
    }

    // Clear existing entry
    clear_cache_entry(entry);

    // Store new entry
    snprintf(entry->prompt_hash, sizeof(entry->prompt_hash), "%s", prompt_hash);
    snprintf(entry->command_hash, sizeof(entry->command_hash), "%s",
             command_hash);
    entry->cached_analysis = *analysis;
    entry->cached_positioning = *positioning;

    // Store composed output
    if (engine->composed_output) {
        size_t output_len = strlen(engine->composed_output);
        entry->cached_output = malloc(output_len + 1);
        if (entry->cached_output) {
            strcpy(entry->cached_output, engine->composed_output);
        }
    }

    gettimeofday(&entry->timestamp, NULL);
    entry->valid = true;

    return COMPOSITION_ENGINE_SUCCESS;
}

// ============================================================================
// EVENT HANDLING FUNCTIONS
// ============================================================================

/**
 * Handle layer content changed event
 */
layer_events_error_t
composition_engine_handle_content_changed(const layer_event_t *event,
                                          void *user_data) {
    composition_engine_t *engine = (composition_engine_t *)user_data;
    if (!engine || !event)
        return LAYER_EVENTS_ERROR_INVALID_PARAM;

    // Invalidate cache when content changes
    engine->composition_cache_valid = false;

    // Update performance metrics
    if (engine->performance_monitoring) {
        engine->performance.cache_misses++;
    }

    // Enhanced Performance Monitoring: Record cache miss
    display_integration_record_layer_cache_operation("composition_engine",
                                                     false);

    return LAYER_EVENTS_SUCCESS;
}

/**
 * Handle theme changed event
 */
layer_events_error_t
composition_engine_handle_theme_changed(const layer_event_t *event,
                                        void *user_data) {
    composition_engine_t *engine = (composition_engine_t *)user_data;
    if (!engine || !event)
        return LAYER_EVENTS_ERROR_INVALID_PARAM;

    // Clear cache on theme changes
    composition_engine_clear_cache(engine);
    engine->composition_cache_valid = false;

    return LAYER_EVENTS_SUCCESS;
}

/**
 * Handle terminal resize event
 */
layer_events_error_t
composition_engine_handle_terminal_resize(const layer_event_t *event,
                                          void *user_data) {
    composition_engine_t *engine = (composition_engine_t *)user_data;
    if (!engine || !event)
        return LAYER_EVENTS_ERROR_INVALID_PARAM;

    // Clear cache on terminal resize
    composition_engine_clear_cache(engine);
    engine->composition_cache_valid = false;

    return LAYER_EVENTS_SUCCESS;
}

// ============================================================================
// CORE API IMPLEMENTATION
// ============================================================================

composition_engine_t *composition_engine_create(void) {
    composition_engine_t *engine = calloc(1, sizeof(composition_engine_t));
    if (!engine) {
        return NULL;
    }

    // Initialize default configuration
    engine->intelligent_positioning = true;
    engine->adaptive_strategy = true;
    engine->performance_monitoring = true;
    engine->max_cache_age_ms = COMPOSITION_ENGINE_CACHE_EXPIRY_MS;
    engine->current_strategy = COMPOSITION_STRATEGY_ADAPTIVE;

    // Initialize version string
    snprintf(engine->version_string, sizeof(engine->version_string), "%d.%d.%d",
             COMPOSITION_ENGINE_VERSION_MAJOR, COMPOSITION_ENGINE_VERSION_MINOR,
             COMPOSITION_ENGINE_VERSION_PATCH);

    return engine;
}

composition_engine_error_t composition_engine_init(
    composition_engine_t *engine, prompt_layer_t *prompt_layer,
    command_layer_t *command_layer, layer_event_system_t *event_system) {
    if (!engine || !prompt_layer || !command_layer || !event_system) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    // Store layer references
    engine->prompt_layer = prompt_layer;
    engine->command_layer = command_layer;
    engine->event_system = event_system;

    // Continuation prompts are handled via screen_buffer line prefixes
    // in display_controller.c using screen_buffer_render_with_continuation()
    engine->continuation_prompts_enabled = false;

    // Subscribe to relevant events
    if (engine->event_system) {
        layer_events_subscribe(
            engine->event_system, LAYER_EVENT_CONTENT_CHANGED,
            LAYER_ID_PROMPT_LAYER, composition_engine_handle_content_changed,
            engine, LAYER_EVENT_PRIORITY_HIGH);

        layer_events_subscribe(
            engine->event_system, LAYER_EVENT_CONTENT_CHANGED,
            LAYER_ID_COMMAND_LAYER, composition_engine_handle_content_changed,
            engine, LAYER_EVENT_PRIORITY_HIGH);

        layer_events_subscribe(engine->event_system, LAYER_EVENT_THEME_CHANGED,
                               LAYER_ID_PROMPT_LAYER,
                               composition_engine_handle_theme_changed, engine,
                               LAYER_EVENT_PRIORITY_NORMAL);

        layer_events_subscribe(
            engine->event_system, LAYER_EVENT_TERMINAL_RESIZE,
            LAYER_ID_PROMPT_LAYER, composition_engine_handle_terminal_resize,
            engine, LAYER_EVENT_PRIORITY_NORMAL);

        engine->event_subscription_active = true;
    }

    // Initialize performance metrics
    memset(&engine->performance, 0, sizeof(engine->performance));
    engine->performance.min_composition_time_ns = UINT64_MAX;

    engine->initialized = true;
    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_compose(composition_engine_t *engine) {
    if (!engine || !engine->initialized) {
        return COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED;
    }

    uint64_t start_time = get_timestamp_ns();
    composition_engine_error_t result = COMPOSITION_ENGINE_SUCCESS;

    // Get current prompt content
    char prompt_content[PROMPT_LAYER_MAX_CONTENT_SIZE];
    prompt_layer_error_t prompt_error = prompt_layer_get_rendered_content(
        engine->prompt_layer, prompt_content, sizeof(prompt_content));
    if (prompt_error != PROMPT_LAYER_SUCCESS) {
        return COMPOSITION_ENGINE_ERROR_LAYER_NOT_READY;
    }

    // Get current command content
    char command_content[COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE];
    command_layer_error_t command_error = command_layer_get_highlighted_text(
        engine->command_layer, command_content, sizeof(command_content));
    if (command_error != COMMAND_LAYER_SUCCESS) {
        return COMPOSITION_ENGINE_ERROR_LAYER_NOT_READY;
    }

    // Calculate content hashes for caching
    char prompt_hash[32], command_hash[32];
    calculate_content_hash(prompt_content, prompt_hash, sizeof(prompt_hash));
    calculate_content_hash(command_content, command_hash, sizeof(command_hash));

    // Check cache first
    composition_cache_entry_t *cached =
        find_cache_entry(engine, prompt_hash, command_hash);
    if (cached && !is_cache_entry_expired(engine, cached)) {
        // Use cached result
        if (cached->cached_output) {
            // Ensure we have a buffer (allocate if needed)
            if (!engine->composed_output) {
                engine->composed_output =
                    malloc(COMPOSITION_ENGINE_MAX_OUTPUT_SIZE);
                if (!engine->composed_output) {
                    return COMPOSITION_ENGINE_ERROR_MEMORY_ALLOCATION;
                }
                engine->composed_output_size =
                    COMPOSITION_ENGINE_MAX_OUTPUT_SIZE;
            }

            // Copy cached output into existing buffer (don't shrink buffer!)
            size_t output_len = strlen(cached->cached_output);
            if (output_len < engine->composed_output_size) {
                strcpy(engine->composed_output, cached->cached_output);
            } else {
                return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
            }
        }

        engine->current_analysis = cached->cached_analysis;
        engine->current_positioning = cached->cached_positioning;

        if (engine->performance_monitoring) {
            engine->performance.cache_hits++;
            engine->performance.composition_count++;
        }

        // Enhanced Performance Monitoring: Record cache hit
        display_integration_record_layer_cache_operation("composition_engine",
                                                         true);

        return COMPOSITION_ENGINE_SUCCESS;
    }

    // Cache miss - perform composition
    if (engine->performance_monitoring) {
        engine->performance.cache_misses++;
    }

    // Enhanced Performance Monitoring: Record cache miss
    display_integration_record_layer_cache_operation("composition_engine",
                                                     false);

    // ========================================================================
    // PHASE 4: MULTILINE DETECTION AND PATH SELECTION
    // ========================================================================

    // Detect if command contains newlines (multiline command)
    bool is_multiline = strchr(command_content, '\n') != NULL;

    // Check if we should use the multiline path
    bool use_multiline_path = is_multiline &&
                              engine->continuation_prompts_enabled &&
                              engine->screen_buffer != NULL;

    if (use_multiline_path) {
        // ====================================================================
        // MULTILINE PATH: Use continuation prompts with screen_buffer
        // ====================================================================

        uint64_t composition_start = get_timestamp_ns();

        // Allocate output buffer if needed
        if (!engine->composed_output) {
            engine->composed_output =
                malloc(COMPOSITION_ENGINE_MAX_OUTPUT_SIZE);
            if (!engine->composed_output) {
                return COMPOSITION_ENGINE_ERROR_MEMORY_ALLOCATION;
            }
            engine->composed_output_size = COMPOSITION_ENGINE_MAX_OUTPUT_SIZE;
        }

// Split command into lines
#define MAX_COMMAND_LINES 100
        command_line_info_t lines[MAX_COMMAND_LINES];
        size_t line_count =
            split_command_lines(command_content, lines, MAX_COMMAND_LINES);

        if (line_count == 0) {
            // Empty command - use regular path
            use_multiline_path = false;
            goto regular_composition_path;
        }

        // Build continuation prompts for all lines
        char prompts[MAX_COMMAND_LINES][256];
        result =
            build_continuation_prompts(engine, prompt_content, command_content,
                                       lines, line_count, prompts, 256);

        if (result != COMPOSITION_ENGINE_SUCCESS) {
            return result;
        }

        // Coordinate with screen_buffer to render
        result = coordinate_screen_buffer_rendering(
            engine, lines, line_count, prompts, engine->composed_output,
            engine->composed_output_size);

        if (result != COMPOSITION_ENGINE_SUCCESS) {
            return result;
        }

        uint64_t composition_end = get_timestamp_ns();
        if (engine->performance_monitoring) {
            engine->performance.combination_time_ns =
                composition_end - composition_start;
        }

        // Update analysis to reflect multiline composition
        memset(&engine->current_analysis, 0, sizeof(engine->current_analysis));
        engine->current_analysis.is_multiline = true;
        engine->current_analysis.line_count = line_count;

        // Update positioning to reflect multiline layout
        memset(&engine->current_positioning, 0,
               sizeof(engine->current_positioning));
        engine->current_positioning.total_lines = line_count;
        engine->current_positioning.command_on_same_line = false;

    } else {
        // ====================================================================
        // REGULAR PATH: Single-line or continuation prompts disabled
        // ====================================================================

    regular_composition_path:;

        // Analyze prompt structure
        uint64_t analysis_start = get_timestamp_ns();
        result = analyze_prompt_structure(engine, prompt_content,
                                          &engine->current_analysis);
        if (result != COMPOSITION_ENGINE_SUCCESS) {
            return result;
        }

        uint64_t analysis_end = get_timestamp_ns();
        if (engine->performance_monitoring) {
            engine->performance.analysis_time_ns =
                analysis_end - analysis_start;
        }

        // Calculate optimal positioning
        result = calculate_optimal_positioning(
            engine, &engine->current_analysis, &engine->current_positioning);
        if (result != COMPOSITION_ENGINE_SUCCESS) {
            return result;
        }

        // Perform layer composition
        uint64_t composition_start = get_timestamp_ns();
        result = compose_layers(engine, prompt_content, command_content,
                                &engine->current_analysis,
                                &engine->current_positioning);
        if (result != COMPOSITION_ENGINE_SUCCESS) {
            return result;
        }

        uint64_t composition_end = get_timestamp_ns();
        if (engine->performance_monitoring) {
            engine->performance.combination_time_ns =
                composition_end - composition_start;
        }
    }

    // Store in cache
    store_in_cache(engine, prompt_hash, command_hash, &engine->current_analysis,
                   &engine->current_positioning);

    // Update performance metrics
    if (engine->performance_monitoring) {
        uint64_t total_time = get_timestamp_ns() - start_time;

        engine->performance.composition_count++;

        if (engine->performance.composition_count == 1) {
            engine->performance.avg_composition_time_ns = total_time;
            engine->performance.min_composition_time_ns = total_time;
            engine->performance.max_composition_time_ns = total_time;
        } else {
            // Update average
            engine->performance.avg_composition_time_ns =
                (engine->performance.avg_composition_time_ns *
                     (engine->performance.composition_count - 1) +
                 total_time) /
                engine->performance.composition_count;

            // Update min/max
            if (total_time < engine->performance.min_composition_time_ns) {
                engine->performance.min_composition_time_ns = total_time;
            }
            if (total_time > engine->performance.max_composition_time_ns) {
                engine->performance.max_composition_time_ns = total_time;
            }
        }

        // Update cache hit rate
        if (engine->performance.composition_count > 0) {
            engine->performance.cache_hit_rate =
                (double)engine->performance.cache_hits * 100.0 /
                (double)engine->performance.composition_count;
        }
    }

    engine->composition_cache_valid = true;
    gettimeofday(&engine->last_composition_time, NULL);

    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_get_output(const composition_engine_t *engine, char *output,
                              size_t output_size) {
    if (!engine || !output || output_size == 0) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (!engine->initialized) {
        return COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED;
    }

    if (!engine->composed_output) {
        output[0] = '\0';
        return COMPOSITION_ENGINE_SUCCESS;
    }

    size_t content_len = strlen(engine->composed_output);
    if (content_len >= output_size) {
        return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
    }

    strcpy(output, engine->composed_output);
    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_get_analysis(const composition_engine_t *engine,
                                composition_analysis_t *analysis) {
    if (!engine || !analysis) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (!engine->initialized) {
        return COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED;
    }

    *analysis = engine->current_analysis;
    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_get_positioning(const composition_engine_t *engine,
                                   composition_positioning_t *positioning) {
    if (!engine || !positioning) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (!engine->initialized) {
        return COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED;
    }

    *positioning = engine->current_positioning;
    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_get_performance(const composition_engine_t *engine,
                                   composition_performance_t *performance) {
    if (!engine || !performance) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (!engine->initialized) {
        return COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED;
    }

    *performance = engine->performance;
    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_cleanup(composition_engine_t *engine) {
    if (!engine) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    // Unsubscribe from events
    if (engine->event_subscription_active && engine->event_system) {
        // Unsubscribe from all events - need to implement proper cleanup
        // layer_events_unsubscribe_all(engine->event_system,
        // LAYER_ID_COMPOSITION_ENGINE);
        engine->event_subscription_active = false;
    }

    // Clear cache
    composition_engine_clear_cache(engine);

    // Free composed output
    if (engine->composed_output) {
        free(engine->composed_output);
        engine->composed_output = NULL;
        engine->composed_output_size = 0;
    }

    // Free owned layers - composition engine takes ownership after init
    if (engine->command_layer) {
        command_layer_cleanup(engine->command_layer);
        command_layer_destroy(engine->command_layer);
        engine->command_layer = NULL;
    }

    if (engine->prompt_layer) {
        prompt_layer_cleanup(engine->prompt_layer);
        prompt_layer_destroy(engine->prompt_layer);
        engine->prompt_layer = NULL;
    }

    // Reset state
    memset(&engine->current_analysis, 0, sizeof(engine->current_analysis));
    memset(&engine->current_positioning, 0,
           sizeof(engine->current_positioning));
    memset(&engine->performance, 0, sizeof(engine->performance));

    engine->composition_cache_valid = false;
    engine->initialized = false;

    return COMPOSITION_ENGINE_SUCCESS;
}

void composition_engine_destroy(composition_engine_t *engine) {
    if (!engine) {
        return;
    }

    composition_engine_cleanup(engine);
    free(engine);
}

// ============================================================================
// CONFIGURATION AND CONTROL FUNCTIONS
// ============================================================================

composition_engine_error_t
composition_engine_set_strategy(composition_engine_t *engine,
                                composition_strategy_t strategy) {
    if (!engine) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (strategy >= COMPOSITION_STRATEGY_COUNT) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    engine->current_strategy = strategy;
    engine->composition_cache_valid = false; // Invalidate cache

    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_set_intelligent_positioning(composition_engine_t *engine,
                                               bool enable) {
    if (!engine) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    engine->intelligent_positioning = enable;
    engine->composition_cache_valid = false; // Invalidate cache

    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_set_performance_monitoring(composition_engine_t *engine,
                                              bool enable) {
    if (!engine) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    engine->performance_monitoring = enable;

    // Reset performance metrics if disabling
    if (!enable) {
        memset(&engine->performance, 0, sizeof(engine->performance));
        engine->performance.min_composition_time_ns = UINT64_MAX;
    }

    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_set_cache_max_age(composition_engine_t *engine,
                                     size_t max_age_ms) {
    if (!engine) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    engine->max_cache_age_ms = max_age_ms;

    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_clear_cache(composition_engine_t *engine) {
    if (!engine) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    for (size_t i = 0; i < COMPOSITION_ENGINE_CACHE_SIZE; i++) {
        clear_cache_entry(&engine->cache[i]);
    }

    engine->composition_cache_valid = false;

    return COMPOSITION_ENGINE_SUCCESS;
}

// ============================================================================
// ANALYSIS AND DEBUGGING FUNCTIONS
// ============================================================================

composition_engine_error_t
composition_engine_analyze_prompt(composition_engine_t *engine,
                                  composition_analysis_t *analysis) {
    if (!engine || !analysis) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (!engine->initialized) {
        return COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED;
    }

    // Get current prompt content
    char prompt_content[PROMPT_LAYER_MAX_CONTENT_SIZE];
    prompt_layer_error_t prompt_error = prompt_layer_get_rendered_content(
        engine->prompt_layer, prompt_content, sizeof(prompt_content));
    if (prompt_error != PROMPT_LAYER_SUCCESS) {
        return COMPOSITION_ENGINE_ERROR_LAYER_NOT_READY;
    }

    return analyze_prompt_structure(engine, prompt_content, analysis);
}

composition_engine_error_t composition_engine_calculate_positioning(
    composition_engine_t *engine, const composition_analysis_t *analysis,
    composition_positioning_t *positioning) {
    if (!engine || !analysis || !positioning) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (!engine->initialized) {
        return COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED;
    }

    return calculate_optimal_positioning(engine, analysis, positioning);
}

composition_engine_error_t
composition_engine_validate_cache(composition_engine_t *engine,
                                  size_t *valid_entries,
                                  size_t *expired_entries) {
    if (!engine) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    size_t valid_count = 0;
    size_t expired_count = 0;

    for (size_t i = 0; i < COMPOSITION_ENGINE_CACHE_SIZE; i++) {
        composition_cache_entry_t *entry = &engine->cache[i];

        if (!entry->valid) {
            continue;
        }

        if (is_cache_entry_expired(engine, entry)) {
            clear_cache_entry(entry);
            expired_count++;
        } else {
            valid_count++;
        }
    }

    if (valid_entries) {
        *valid_entries = valid_count;
    }
    if (expired_entries) {
        *expired_entries = expired_count;
    }

    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_get_version(const composition_engine_t *engine,
                               char *version_buffer, size_t buffer_size) {
    if (!engine || !version_buffer || buffer_size == 0) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    size_t version_len = strlen(engine->version_string);
    if (version_len >= buffer_size) {
        return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
    }

    strcpy(version_buffer, engine->version_string);
    return COMPOSITION_ENGINE_SUCCESS;
}

bool composition_engine_is_initialized(const composition_engine_t *engine) {
    return engine && engine->initialized;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

const char *composition_engine_error_string(composition_engine_error_t error) {
    switch (error) {
    case COMPOSITION_ENGINE_SUCCESS:
        return "Success";
    case COMPOSITION_ENGINE_ERROR_INVALID_PARAM:
        return "Invalid parameter";
    case COMPOSITION_ENGINE_ERROR_NULL_POINTER:
        return "NULL pointer";
    case COMPOSITION_ENGINE_ERROR_MEMORY_ALLOCATION:
        return "Memory allocation failed";
    case COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL:
        return "Buffer too small";
    case COMPOSITION_ENGINE_ERROR_CONTENT_TOO_LARGE:
        return "Content too large";
    case COMPOSITION_ENGINE_ERROR_LAYER_NOT_READY:
        return "Layer not ready";
    case COMPOSITION_ENGINE_ERROR_ANALYSIS_FAILED:
        return "Analysis failed";
    case COMPOSITION_ENGINE_ERROR_COMPOSITION_FAILED:
        return "Composition failed";
    case COMPOSITION_ENGINE_ERROR_CACHE_INVALID:
        return "Cache invalid";
    case COMPOSITION_ENGINE_ERROR_EVENT_FAILED:
        return "Event handling failed";
    case COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED:
        return "Engine not initialized";
    default:
        return "Unknown error";
    }
}

const char *
composition_engine_strategy_string(composition_strategy_t strategy) {
    switch (strategy) {
    case COMPOSITION_STRATEGY_SIMPLE:
        return "Simple";
    case COMPOSITION_STRATEGY_MULTILINE:
        return "Multiline";
    case COMPOSITION_STRATEGY_COMPLEX:
        return "Complex";
    case COMPOSITION_STRATEGY_ASCII_ART:
        return "ASCII Art";
    case COMPOSITION_STRATEGY_ADAPTIVE:
        return "Adaptive";
    default:
        return "Unknown";
    }
}

composition_engine_error_t
composition_engine_calculate_hash(const composition_engine_t *engine,
                                  char *hash_buffer, size_t buffer_size) {
    if (!engine || !hash_buffer || buffer_size < 16) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (!engine->initialized) {
        return COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED;
    }

    // Get current content
    char prompt_content[PROMPT_LAYER_MAX_CONTENT_SIZE];
    char command_content[COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE];

    prompt_layer_error_t prompt_error = prompt_layer_get_rendered_content(
        engine->prompt_layer, prompt_content, sizeof(prompt_content));
    if (prompt_error != PROMPT_LAYER_SUCCESS) {
        return COMPOSITION_ENGINE_ERROR_LAYER_NOT_READY;
    }

    command_layer_error_t command_error = command_layer_get_highlighted_text(
        engine->command_layer, command_content, sizeof(command_content));
    if (command_error != COMMAND_LAYER_SUCCESS) {
        return COMPOSITION_ENGINE_ERROR_LAYER_NOT_READY;
    }

    // Combine content and calculate hash
    char combined_content[PROMPT_LAYER_MAX_CONTENT_SIZE +
                          COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE + 16];
    snprintf(combined_content, sizeof(combined_content), "%s|%s",
             prompt_content, command_content);

    calculate_content_hash(combined_content, hash_buffer, buffer_size);

    return COMPOSITION_ENGINE_SUCCESS;
}

// ============================================================================
// CURSOR TRACKING IMPLEMENTATION (For LLE Terminal Control Wrapping)
// ============================================================================

/**
 * @brief Calculate visual width of a text string, stripping ANSI escape
 * sequences
 *
 * Strips ANSI escape sequences and GNU Readline markers (\001 and \002).
 * This is needed to calculate the prompt width correctly.
 *
 * @param text Text content to calculate visual width for
 * @return Visual width in columns (excluding escape sequences)
 */
static size_t calculate_visual_width(const char *text) {
    if (!text)
        return 0;

    size_t visual_len = 0;
    bool in_escape = false;

    for (const char *p = text; *p; p++) {
        // Skip GNU Readline prompt markers (irrelevant for LLE)
        if (*p == '\001' || *p == '\002') {
            continue;
        }

        if (*p == '\033' || *p == '\x1b') {
            in_escape = true;
            continue;
        }

        if (in_escape) {
            // Skip until we find the end of the escape sequence
            if (*p == 'm' || *p == 'K' || *p == 'J' || *p == 'H' || *p == 'A' ||
                *p == 'B' || *p == 'C' || *p == 'D' || *p == 'G' || *p == 'f' ||
                *p == 's' || *p == 'u') {
                in_escape = false;
            }
            continue;
        }

        // Count visible characters
        // TODO: Proper UTF-8 width calculation (for now assume 1 column per
        // char)
        visual_len++;
    }

    return visual_len;
}

/**
 * Compose layers with cursor position tracking using incremental tracking.
 *
 * This implements the proven approach from Replxx/Fish/ZLE: walk through
 * the buffer character-by-character, track (x, y) position, and record
 * the position when we reach the cursor byte offset.
 */
composition_engine_error_t composition_engine_compose_with_cursor(
    composition_engine_t *engine, size_t cursor_byte_offset, int terminal_width,
    composition_with_cursor_t *result) {
    if (!engine || !result) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (!engine->initialized) {
        return COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED;
    }

    if (terminal_width <= 0) {
        terminal_width = 80; // Fallback to standard width
    }

    // First, perform normal composition to get the composed output
    composition_engine_error_t comp_result = composition_engine_compose(engine);
    if (comp_result != COMPOSITION_ENGINE_SUCCESS) {
        return comp_result;
    }

    // Get the composed output
    comp_result = composition_engine_get_output(
        engine, result->composed_output, sizeof(result->composed_output));
    if (comp_result != COMPOSITION_ENGINE_SUCCESS) {
        return comp_result;
    }

    // Get prompt content to calculate its visual width
    char prompt_content[PROMPT_LAYER_MAX_CONTENT_SIZE];
    prompt_layer_error_t prompt_error = prompt_layer_get_rendered_content(
        engine->prompt_layer, prompt_content, sizeof(prompt_content));
    if (prompt_error != PROMPT_LAYER_SUCCESS) {
        return COMPOSITION_ENGINE_ERROR_LAYER_NOT_READY;
    }

    // Calculate prompt visual width (strip ANSI codes)
    size_t prompt_width = calculate_visual_width(prompt_content);

    // Get command text for cursor tracking
    const char *cmd = engine->command_layer->command_text;
    if (!cmd) {
        cmd = "";
    }
    size_t cmd_len = strlen(cmd);

    // Initialize cursor tracking
    int x = prompt_width; // Start after prompt
    int y = 0;
    size_t bytes_processed = 0;
    result->cursor_found = false;
    result->terminal_width = terminal_width;

    // Walk through command buffer character by character
    for (size_t i = 0; i < cmd_len;) {
        // Check if cursor is at this position BEFORE processing the character
        // cursor_byte_offset represents the position before the character at
        // that offset
        if (bytes_processed == cursor_byte_offset && !result->cursor_found) {
            result->cursor_screen_row = y;
            result->cursor_screen_column = x;
            result->cursor_found = true;
        }

        // Determine character type and visual width
        int char_width = 0;
        int bytes_consumed = 1;

        if (cmd[i] == '\t') {
            // Tab character - expand to next multiple of 8
            char_width = 8 - (x % 8);
        } else if ((cmd[i] & 0x80) == 0) {
            // ASCII character (single byte, width 1)
            char_width = 1;
        } else {
            // Multi-byte UTF-8 character
            // Detect byte count
            if ((cmd[i] & 0xE0) == 0xC0) {
                bytes_consumed = 2; // 2-byte UTF-8
            } else if ((cmd[i] & 0xF0) == 0xE0) {
                bytes_consumed = 3; // 3-byte UTF-8
            } else if ((cmd[i] & 0xF8) == 0xF0) {
                bytes_consumed = 4; // 4-byte UTF-8
            }

            // For now, assume width 1 (proper wide char detection would check
            // Unicode range)
            // TODO: Implement proper wcwidth() for CJK characters (width 2)
            char_width = 1;
        }

        // Advance cursor position
        x += char_width;

        // Handle line wrapping
        if (x >= terminal_width) {
            x = 0;
            y++;
        }

        // Advance through buffer
        i += bytes_consumed;
        bytes_processed += bytes_consumed;

        // Check AGAIN after advancing - cursor might be after this character
        if (bytes_processed == cursor_byte_offset && !result->cursor_found) {
            result->cursor_screen_row = y;
            result->cursor_screen_column = x;
            result->cursor_found = true;
        }
    }

    // If cursor is at end of buffer and not found yet (redundant with check
    // above, but kept for safety)
    if (bytes_processed == cursor_byte_offset && !result->cursor_found) {
        result->cursor_screen_row = y;
        result->cursor_screen_column = x;
        result->cursor_found = true;
    }

    // If cursor position exceeds buffer, place at end
    if (!result->cursor_found) {
        result->cursor_screen_row = y;
        result->cursor_screen_column = x;
        result->cursor_found = true;
    }

    return COMPOSITION_ENGINE_SUCCESS;
}

composition_engine_error_t
composition_engine_set_screen_buffer(composition_engine_t *engine,
                                     screen_buffer_t *buffer) {
    if (!engine) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    engine->screen_buffer = buffer;

    return COMPOSITION_ENGINE_SUCCESS;
}

// ============================================================================
// MULTILINE COMPOSITION HELPERS
// ============================================================================

/**
 * @brief Split command content by newlines into line info structures
 *
 * This implements the line splitting logic:
 * - Track byte offsets for each line
 * - Track length of each line
 * - Handle empty lines
 * - Handle last line without trailing \n
 *
 * @param command_text Full command text to split
 * @param lines Output array for line info structures
 * @param max_lines Maximum number of lines to store
 * @return Number of lines found (0 if empty or error)
 */
static size_t split_command_lines(const char *command_text,
                                  command_line_info_t *lines,
                                  size_t max_lines) {
    if (!command_text || !lines || max_lines == 0) {
        return 0;
    }

    size_t line_count = 0;
    const char *line_start = command_text;
    const char *ptr = command_text;
    size_t byte_offset = 0;

    while (*ptr && line_count < max_lines) {
        if (*ptr == '\n') {
            // Found end of line
            lines[line_count].content = line_start;
            lines[line_count].length = ptr - line_start;
            lines[line_count].byte_offset = byte_offset;
            line_count++;

            // Move to next line
            ptr++;
            line_start = ptr;
            byte_offset = ptr - command_text;
        } else {
            ptr++;
        }
    }

    // Handle last line (without trailing \n)
    if (line_count < max_lines && (line_start < ptr || *line_start != '\0')) {
        lines[line_count].content = line_start;
        lines[line_count].length = ptr - line_start;
        lines[line_count].byte_offset = byte_offset;
        line_count++;
    }

    return line_count;
}

/**
 * @brief Build continuation prompts for all lines in a multiline command
 *
 * Note: Context-aware continuation prompts are handled in display_controller.c
 * via screen_buffer_render_with_continuation() callback. This function is only
 * used as a fallback in the composition engine's multiline strategy.
 *
 * @param engine Composition engine instance
 * @param primary_prompt Primary prompt (for first line)
 * @param command_text Full command text (unused, for display_controller
 * callback)
 * @param lines Array of line info (unused, for display_controller callback)
 * @param line_count Number of lines to build prompts for
 * @param prompts Output array for prompts (must have line_count entries)
 * @param prompt_size Size of each prompt buffer
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code otherwise
 */
static composition_engine_error_t
build_continuation_prompts(composition_engine_t *engine,
                           const char *primary_prompt, const char *command_text,
                           const command_line_info_t *lines, size_t line_count,
                           char (*prompts)[256], size_t prompt_size) {
    (void)command_text; /* Used by display_controller callback instead */
    (void)lines;        /* Used by display_controller callback instead */

    if (!engine || !primary_prompt || !prompts || line_count == 0) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    /* Line 0: Use primary prompt */
    strncpy(prompts[0], primary_prompt, prompt_size - 1);
    prompts[0][prompt_size - 1] = '\0';

    /* Lines 1+: Simple fallback (context-aware prompts via display_controller)
     */
    for (size_t i = 1; i < line_count; i++) {
        strncpy(prompts[i], "> ", prompt_size - 1);
        prompts[i][prompt_size - 1] = '\0';
    }

    return COMPOSITION_ENGINE_SUCCESS;
}

/**
 * @brief Coordinate with screen_buffer to render multiline with prefixes
 *
 * This implements the screen buffer coordination:
 * - Call screen_buffer_set_line_prefix() for each line
 * - Call screen_buffer_render_multiline_with_prefixes()
 *
 * Note: This function does NOT render line content. It only sets prefixes
 * and renders the combined output. The screen_buffer is responsible for
 * managing the actual line content separately.
 *
 * @param engine Composition engine instance with screen buffer
 * @param lines Array of line info for each command line
 * @param line_count Number of lines to render
 * @param prompts Array of prompts/prefixes for each line
 * @param output Output buffer for rendered result
 * @param output_size Size of output buffer
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code otherwise
 */
static composition_engine_error_t coordinate_screen_buffer_rendering(
    composition_engine_t *engine, const command_line_info_t *lines,
    size_t line_count, char (*prompts)[256], char *output, size_t output_size) {
    if (!engine || !engine->screen_buffer || !lines || !prompts || !output) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    screen_buffer_t *sb = engine->screen_buffer;

    // Set prefix for each line
    for (size_t i = 0; i < line_count; i++) {
        bool result = screen_buffer_set_line_prefix(sb, (int)i, prompts[i]);
        if (!result) {
            return COMPOSITION_ENGINE_ERROR_COMPOSITION_FAILED;
        }
    }

    // Render all lines with prefixes
    bool render_result = screen_buffer_render_multiline_with_prefixes(
        sb,
        0,               // start_line
        (int)line_count, // num_lines
        output, output_size);

    if (!render_result) {
        return COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL;
    }

    return COMPOSITION_ENGINE_SUCCESS;
}

/**
 * @brief Translate cursor byte offset to screen position in multiline command
 *
 * This implements the cursor translation logic using
 * incremental character-by-character tracking (the proven approach from
 * Replxx/Fish/ZLE).
 *
 * Key principles:
 * - Walk through buffer character by character
 * - Track (line, column) position accounting for continuation prompts
 * - Handle UTF-8, wide characters, ANSI codes, tabs
 * - Record position when reaching cursor byte offset
 *
 * @param command_text Full command text to analyze
 * @param cursor_byte_offset Cursor position in command (byte offset)
 * @param lines Array of line info for each command line
 * @param line_count Number of lines in the command
 * @param prompts Array of prompts for each line
 * @param terminal_width Terminal width in columns
 * @param out_line Output: cursor line number (0-based)
 * @param out_column Output: cursor column number (0-based, includes prompt
 * width)
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code otherwise
 */
MAYBE_UNUSED
static composition_engine_error_t translate_cursor_to_screen_position(
    const char *command_text, size_t cursor_byte_offset,
    const command_line_info_t *lines, size_t line_count, char (*prompts)[256],
    int terminal_width, size_t *out_line, size_t *out_column) {
    if (!command_text || !lines || !prompts || !out_line || !out_column ||
        line_count == 0) {
        return COMPOSITION_ENGINE_ERROR_INVALID_PARAM;
    }

    if (terminal_width <= 0) {
        terminal_width = 80; // Fallback
    }

    // Find which line the cursor is on
    size_t cursor_line = 0;
    size_t cursor_offset_in_line = cursor_byte_offset;

    for (size_t i = 0; i < line_count; i++) {
        size_t line_end_offset = lines[i].byte_offset + lines[i].length;

        // Check if cursor is on this line
        if (cursor_byte_offset >= lines[i].byte_offset &&
            cursor_byte_offset <= line_end_offset) {
            cursor_line = i;
            cursor_offset_in_line = cursor_byte_offset - lines[i].byte_offset;
            break;
        }

        // If cursor is at the newline after this line
        if (i + 1 < line_count &&
            cursor_byte_offset == lines[i + 1].byte_offset - 1) {
            cursor_line = i;
            cursor_offset_in_line = lines[i].length; // At end of line
            break;
        }
    }

    // If cursor is beyond all lines, place at end
    if (cursor_line >= line_count) {
        cursor_line = line_count - 1;
        cursor_offset_in_line = lines[cursor_line].length;
    }

    // Calculate visual width of prompt for this line (strip ANSI codes)
    size_t prompt_width =
        screen_buffer_calculate_visual_width(prompts[cursor_line], 0);

    // Walk through the line content character by character
    size_t column = prompt_width; // Start after prompt
    size_t bytes_processed = 0;
    const char *line_text = lines[cursor_line].content;

    while (bytes_processed < cursor_offset_in_line &&
           bytes_processed < lines[cursor_line].length) {
        unsigned char ch = line_text[bytes_processed];

        // Determine character width
        int char_width = 0;
        int bytes_consumed = 1;

        if (ch == '\t') {
            // Tab character - expand to next multiple of 8
            char_width = 8 - (column % 8);
        } else if (ch == '\033') {
            // ANSI escape sequence - skip without adding width
            bytes_consumed = 1;
            while (bytes_processed + bytes_consumed <
                   lines[cursor_line].length) {
                unsigned char esc_ch =
                    line_text[bytes_processed + bytes_consumed];
                bytes_consumed++;
                if (esc_ch == 'm' || esc_ch == 'K' || esc_ch == 'J' ||
                    esc_ch == 'H' || esc_ch == 'A' || esc_ch == 'B' ||
                    esc_ch == 'C' || esc_ch == 'D' || esc_ch == 'G' ||
                    esc_ch == 'f' || esc_ch == 's' || esc_ch == 'u') {
                    break;
                }
            }
            char_width = 0; // ANSI codes have no visual width
        } else if ((ch & 0x80) == 0) {
            // ASCII character (single byte, width 1)
            char_width = 1;
        } else {
            // Multi-byte UTF-8 character
            if ((ch & 0xE0) == 0xC0) {
                bytes_consumed = 2; // 2-byte UTF-8
            } else if ((ch & 0xF0) == 0xE0) {
                bytes_consumed = 3; // 3-byte UTF-8
            } else if ((ch & 0xF8) == 0xF0) {
                bytes_consumed = 4; // 4-byte UTF-8
            }

            // For now, assume width 1 (proper wcwidth() would check Unicode
            // range)
            // TODO: Implement proper wide character detection for CJK (width 2)
            char_width = 1;
        }

        column += char_width;
        bytes_processed += bytes_consumed;
    }

    *out_line = cursor_line;
    *out_column = column;

    return COMPOSITION_ENGINE_SUCCESS;
}
