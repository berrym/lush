/**
 * @file adaptive_minimal_controller.c
 * @brief Minimal controller for basic line editing environments
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 26 Phase 2 - Minimal Controller
 *
 * Implements the minimal controller for basic line editing environments.
 * Provides simple text input/output without terminal control sequences.
 *
 * Key Features:
 * - Basic line-by-line editing
 * - Simple history support
 * - Basic tab completion
 * - No terminal control sequences
 * - Minimal resource usage
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * SIMPLE STRUCTURES
 * ============================================================================
 */

/**
 * Simple text buffer for minimal editing.
 */
typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    size_t cursor_position;
} lle_text_buffer_t;

/**
 * Basic history implementation (ring buffer).
 */
typedef struct {
    char **entries;
    size_t capacity;
    size_t count;
    size_t write_index;
    size_t read_index;
} lle_basic_history_t;

/**
 * Simple completion system.
 */
typedef struct {
    char **completions;
    size_t count;
    size_t capacity;
    size_t current_index;
} lle_simple_completion_t;

/**
 * Simple input processor for minimal mode.
 */
typedef struct {
    char *input_buffer;
    size_t buffer_size;
    bool echo_enabled;
} lle_simple_input_processor_t;

/**
 * Minimal controller structure.
 */
struct lle_minimal_controller_t {
    // Basic text processing
    lle_text_buffer_t *text_buffer;
    lle_basic_history_t *history;
    lle_simple_completion_t *completion;
    lle_simple_input_processor_t *input_processor;

    // Output configuration
    FILE *output_stream;
    bool echo_enabled;

    // Memory management
    lush_memory_pool_t *memory_pool;

    // Statistics
    uint64_t lines_read;
    uint64_t history_entries_added;
    uint64_t completions_performed;
};

/* ============================================================================
 * TEXT BUFFER IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Create a basic text buffer for minimal editing.
 *
 * Allocates and initializes a simple text buffer with default capacity.
 *
 * @return Pointer to the created buffer, or NULL on allocation failure.
 */
static lle_text_buffer_t *lle_text_buffer_create_basic(void) {
    lle_text_buffer_t *buffer = calloc(1, sizeof(lle_text_buffer_t));
    if (!buffer) {
        return NULL;
    }

    buffer->capacity = 4096;
    buffer->data = malloc(buffer->capacity);
    if (!buffer->data) {
        free(buffer);
        return NULL;
    }

    buffer->data[0] = '\0';
    buffer->length = 0;
    buffer->cursor_position = 0;

    return buffer;
}

/**
 * @brief Destroy a text buffer and free its resources.
 *
 * @param buffer The text buffer to destroy, or NULL for no-op.
 */
static void lle_text_buffer_destroy(lle_text_buffer_t *buffer) {
    if (!buffer) {
        return;
    }

    free(buffer->data);
    free(buffer);
}

/**
 * @brief Clear all content from a text buffer.
 *
 * Resets the buffer length and cursor position to zero.
 *
 * @param buffer The text buffer to clear, or NULL for no-op.
 */
LLE_MAYBE_UNUSED
static void lle_text_buffer_clear(lle_text_buffer_t *buffer) {
    if (!buffer) {
        return;
    }

    buffer->length = 0;
    buffer->cursor_position = 0;
    buffer->data[0] = '\0';
}

/**
 * @brief Set the content of a text buffer.
 *
 * Replaces the buffer content with the given text, growing
 * the buffer if necessary.
 *
 * @param buffer The text buffer.
 * @param text The text to set.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t lle_text_buffer_set(lle_text_buffer_t *buffer,
                                        const char *text) {
    if (!buffer || !text) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t text_len = strlen(text);
    if (text_len >= buffer->capacity) {
        size_t new_capacity = text_len + 1;
        char *new_data = realloc(buffer->data, new_capacity);
        if (!new_data) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }

    strcpy(buffer->data, text);
    buffer->length = text_len;
    buffer->cursor_position = text_len;

    return LLE_SUCCESS;
}

/* ============================================================================
 * BASIC HISTORY IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Create a basic history ring buffer.
 *
 * Allocates and initializes a history structure with default capacity.
 *
 * @return Pointer to the created history, or NULL on allocation failure.
 */
static lle_basic_history_t *lle_basic_history_create(void) {
    lle_basic_history_t *history = calloc(1, sizeof(lle_basic_history_t));
    if (!history) {
        return NULL;
    }

    history->capacity = 1000;
    history->entries = calloc(history->capacity, sizeof(char *));
    if (!history->entries) {
        free(history);
        return NULL;
    }

    return history;
}

/**
 * @brief Destroy a basic history and free all entries.
 *
 * @param history The history to destroy, or NULL for no-op.
 */
static void lle_basic_history_destroy(lle_basic_history_t *history) {
    if (!history) {
        return;
    }

    for (size_t i = 0; i < history->count; i++) {
        free(history->entries[i]);
    }
    free(history->entries);
    free(history);
}

/**
 * @brief Add an entry to the history ring buffer.
 *
 * Skips adding if the entry duplicates the most recent entry.
 * Overwrites oldest entries when capacity is exceeded.
 *
 * @param history The history to add to.
 * @param entry The command string to add.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t lle_basic_history_add(lle_basic_history_t *history,
                                          const char *entry) {
    if (!history || !entry || strlen(entry) == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Don't add duplicate of last entry
    if (history->count > 0) {
        size_t last_index =
            (history->write_index + history->capacity - 1) % history->capacity;
        if (strcmp(history->entries[last_index], entry) == 0) {
            return LLE_SUCCESS;
        }
    }

    // Free old entry if overwriting
    if (history->entries[history->write_index]) {
        free(history->entries[history->write_index]);
    }

    // Add new entry
    history->entries[history->write_index] = strdup(entry);
    if (!history->entries[history->write_index]) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    history->write_index = (history->write_index + 1) % history->capacity;
    if (history->count < history->capacity) {
        history->count++;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Get a history entry by index.
 *
 * Index 0 returns the most recent entry, index 1 the second most recent, etc.
 *
 * @param history The history to query.
 * @param index The index (0 = most recent).
 * @return The history entry string, or NULL if index is out of range.
 */
static const char *lle_basic_history_get(lle_basic_history_t *history,
                                         size_t index) {
    if (!history || index >= history->count) {
        return NULL;
    }

    // Calculate actual index (newest first)
    size_t actual_index =
        (history->write_index + history->capacity - 1 - index) %
        history->capacity;
    return history->entries[actual_index];
}

/* ============================================================================
 * SIMPLE COMPLETION IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Create a simple completion system.
 *
 * Allocates and initializes a completion structure with default capacity.
 *
 * @return Pointer to the created completion system, or NULL on allocation
 * failure.
 */
static lle_simple_completion_t *lle_simple_completion_create(void) {
    lle_simple_completion_t *completion =
        calloc(1, sizeof(lle_simple_completion_t));
    if (!completion) {
        return NULL;
    }

    completion->capacity = 100;
    completion->completions = calloc(completion->capacity, sizeof(char *));
    if (!completion->completions) {
        free(completion);
        return NULL;
    }

    return completion;
}

/**
 * @brief Destroy a simple completion system and free all entries.
 *
 * @param completion The completion system to destroy, or NULL for no-op.
 */
static void lle_simple_completion_destroy(lle_simple_completion_t *completion) {
    if (!completion) {
        return;
    }

    for (size_t i = 0; i < completion->count; i++) {
        free(completion->completions[i]);
    }
    free(completion->completions);
    free(completion);
}

/**
 * @brief Clear all completion suggestions.
 *
 * Frees all stored completion strings and resets the count.
 *
 * @param completion The completion system to clear, or NULL for no-op.
 */
LLE_MAYBE_UNUSED
static void lle_simple_completion_clear(lle_simple_completion_t *completion) {
    if (!completion) {
        return;
    }

    for (size_t i = 0; i < completion->count; i++) {
        free(completion->completions[i]);
        completion->completions[i] = NULL;
    }
    completion->count = 0;
    completion->current_index = 0;
}

/* ============================================================================
 * SIMPLE INPUT PROCESSOR IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Create a simple input processor for minimal mode.
 *
 * Allocates and initializes a basic input processor with default settings.
 *
 * @param processor Output pointer to receive the created processor.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t
lle_simple_input_processor_create(lle_simple_input_processor_t **processor) {

    lle_simple_input_processor_t *proc =
        calloc(1, sizeof(lle_simple_input_processor_t));
    if (!proc) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    proc->buffer_size = 4096;
    proc->input_buffer = malloc(proc->buffer_size);
    if (!proc->input_buffer) {
        free(proc);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    proc->echo_enabled = true;

    *processor = proc;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a simple input processor and free its resources.
 *
 * @param processor The processor to destroy, or NULL for no-op.
 */
static void
lle_simple_input_processor_destroy(lle_simple_input_processor_t *processor) {

    if (!processor) {
        return;
    }

    free(processor->input_buffer);
    free(processor);
}

/**
 * @brief Read a line of input using the simple input processor.
 *
 * Reads from stdin using fgets and removes trailing newlines.
 *
 * @param processor The input processor.
 * @param line Output pointer to receive the input buffer (not duplicated).
 * @return LLE_SUCCESS on success, or an error code on failure/EOF.
 */
static lle_result_t
lle_simple_input_processor_read_line(lle_simple_input_processor_t *processor,
                                     char **line) {

    if (!fgets(processor->input_buffer, processor->buffer_size, stdin)) {
        if (feof(stdin)) {
            return LLE_ERROR_OUT_OF_MEMORY; // EOF
        }
        return LLE_ERROR_INPUT_PARSING;
    }

    // Remove trailing newline
    size_t len = strlen(processor->input_buffer);
    if (len > 0 && processor->input_buffer[len - 1] == '\n') {
        processor->input_buffer[len - 1] = '\0';
    }

    *line = processor->input_buffer;
    return LLE_SUCCESS;
}

/* ============================================================================
 * MINIMAL CONTROLLER API
 * ============================================================================
 */

/**
 * @brief Initialize the minimal controller for basic line editing.
 *
 * Creates and configures a minimal controller with basic text buffer,
 * history, completion, and input processing capabilities.
 *
 * @param context The adaptive context to initialize the controller in.
 * @param memory_pool Memory pool for allocations.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t
lle_initialize_minimal_controller(lle_adaptive_context_t *context,
                                  lush_memory_pool_t *memory_pool) {

    lle_minimal_controller_t *minimal =
        calloc(1, sizeof(lle_minimal_controller_t));
    if (!minimal) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Initialize text buffer
    minimal->text_buffer = lle_text_buffer_create_basic();
    if (!minimal->text_buffer) {
        free(minimal);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Initialize history
    minimal->history = lle_basic_history_create();
    if (!minimal->history) {
        lle_text_buffer_destroy(minimal->text_buffer);
        free(minimal);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Initialize completion
    minimal->completion = lle_simple_completion_create();
    if (!minimal->completion) {
        lle_basic_history_destroy(minimal->history);
        lle_text_buffer_destroy(minimal->text_buffer);
        free(minimal);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Initialize input processor
    lle_result_t result =
        lle_simple_input_processor_create(&minimal->input_processor);
    if (result != LLE_SUCCESS) {
        lle_simple_completion_destroy(minimal->completion);
        lle_basic_history_destroy(minimal->history);
        lle_text_buffer_destroy(minimal->text_buffer);
        free(minimal);
        return result;
    }

    minimal->output_stream = stdout;
    minimal->echo_enabled = context->detection_result->stdout_is_tty;
    minimal->memory_pool = memory_pool;

    context->controller.minimal = minimal;
    return LLE_SUCCESS;
}

/**
 * @brief Clean up and destroy a minimal controller.
 *
 * Releases all resources including text buffer, history, completion,
 * and input processor.
 *
 * @param minimal The minimal controller to destroy, or NULL for no-op.
 */
void lle_cleanup_minimal_controller(lle_minimal_controller_t *minimal) {
    if (!minimal) {
        return;
    }

    lle_simple_input_processor_destroy(minimal->input_processor);
    lle_simple_completion_destroy(minimal->completion);
    lle_basic_history_destroy(minimal->history);
    lle_text_buffer_destroy(minimal->text_buffer);
    free(minimal);
}

/**
 * @brief Read a line of input using the minimal controller.
 *
 * Displays the prompt, reads input from stdin, stores it in the
 * text buffer, adds non-empty lines to history, and returns a copy.
 *
 * @param minimal The minimal controller.
 * @param prompt The prompt string to display.
 * @param line Output pointer to receive the allocated input line.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_minimal_read_line(lle_minimal_controller_t *minimal,
                                   const char *prompt, char **line) {

    if (!minimal || !prompt || !line) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Display prompt
    if (minimal->echo_enabled) {
        fputs(prompt, minimal->output_stream);
        fflush(minimal->output_stream);
    }

    // Read line
    char *input_line = NULL;
    lle_result_t result = lle_simple_input_processor_read_line(
        minimal->input_processor, &input_line);
    if (result != LLE_SUCCESS) {
        return result;
    }

    // Set text buffer
    result = lle_text_buffer_set(minimal->text_buffer, input_line);
    if (result != LLE_SUCCESS) {
        return result;
    }

    // Add to history if non-empty
    if (strlen(input_line) > 0) {
        lle_basic_history_add(minimal->history, input_line);
        minimal->history_entries_added++;
    }

    // Duplicate line for caller
    *line = strdup(input_line);
    if (!*line) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    minimal->lines_read++;
    return LLE_SUCCESS;
}

/**
 * @brief Get a history entry from the minimal controller.
 *
 * @param minimal The minimal controller.
 * @param index The history index (0 = most recent).
 * @param entry Output pointer to receive the history entry.
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if index is out of range.
 */
lle_result_t lle_minimal_get_history(lle_minimal_controller_t *minimal,
                                     size_t index, const char **entry) {

    if (!minimal || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *hist_entry = lle_basic_history_get(minimal->history, index);
    if (!hist_entry) {
        return LLE_ERROR_NOT_FOUND;
    }

    *entry = hist_entry;
    return LLE_SUCCESS;
}

/**
 * @brief Get the number of entries in the minimal controller's history.
 *
 * @param minimal The minimal controller.
 * @return The number of history entries, or 0 if minimal is NULL.
 */
size_t lle_minimal_get_history_count(lle_minimal_controller_t *minimal) {
    if (!minimal || !minimal->history) {
        return 0;
    }

    return minimal->history->count;
}

/**
 * @brief Get minimal controller statistics.
 *
 * Retrieves usage statistics from the minimal controller.
 *
 * @param minimal The minimal controller.
 * @param lines_read Output pointer for lines read count, or NULL to skip.
 * @param history_entries Output pointer for history entries added, or NULL to
 * skip.
 * @param completions Output pointer for completions performed, or NULL to skip.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_minimal_get_stats(const lle_minimal_controller_t *minimal,
                                   uint64_t *lines_read,
                                   uint64_t *history_entries,
                                   uint64_t *completions) {

    if (!minimal) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (lines_read) {
        *lines_read = minimal->lines_read;
    }
    if (history_entries) {
        *history_entries = minimal->history_entries_added;
    }
    if (completions) {
        *completions = minimal->completions_performed;
    }

    return LLE_SUCCESS;
}
