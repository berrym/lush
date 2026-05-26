/**
 * @file history_multiline.c
 * @brief Multiline command support for LLE history system
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Part of Spec 09: History System (Phase 4 Day 13)
 *
 * Provides multiline command detection, formatting preservation, and
 * reconstruction for editing. Integrates with Lush's existing multiline
 * continuation system (input_continuation.h) and LLE buffer management.
 *
 * FEATURES:
 * - Automatic multiline detection using continuation_state_t
 * - Preserve original formatting (indentation, newlines, structure)
 * - Reconstruct for editing (restore original or flatten)
 * - Support for complex shell constructs (if/while/for/case/functions)
 * - Integration with buffer system for seamless editing
 */

#include "input_continuation.h" /// Lush multiline infrastructure
#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/**
 * @brief Count newlines in a string
 *
 * Counts the number of newline characters in the given string.
 *
 * @param str String to analyze (may be NULL)
 * @return Number of newlines found, or 0 if str is NULL
 */
static size_t count_newlines(const char *str) {
    if (!str) {
        return 0;
    }

    size_t count = 0;
    for (const char *p = str; *p; p++) {
        if (*p == '\n') {
            count++;
        }
    }
    return count;
}

/**
 * @brief Create a flattened version of multiline command
 *
 * Replaces newlines with spaces while preserving essential structure.
 * Collapses multiple consecutive whitespace characters into single spaces.
 *
 * @param original Original multiline command (must not be NULL)
 * @param flattened Output buffer for flattened command (must not be NULL)
 * @param flattened_size Size of output buffer in bytes
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if any parameter
 * is NULL, LLE_ERROR_BUFFER_OVERFLOW if command is too long for buffer
 */
static lle_result_t flatten_command(const char *original, char *flattened,
                                    size_t flattened_size) {
    if (!original || !flattened || flattened_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *src = original;
    char *dst = flattened;
    size_t remaining = flattened_size - 1; /// Reserve for null terminator
    bool prev_was_space = false;

    while (*src && remaining > 0) {
        char c = *src++;

        /// Replace newlines with spaces
        if (c == '\n' || c == '\r') {
            if (!prev_was_space && dst > flattened) {
                *dst++ = ' ';
                remaining--;
                prev_was_space = true;
            }
            continue;
        }

        /// Skip redundant spaces
        if (c == ' ' || c == '\t') {
            if (!prev_was_space) {
                *dst++ = ' ';
                remaining--;
                prev_was_space = true;
            }
            continue;
        }

        /// Copy normal character
        *dst++ = c;
        remaining--;
        prev_was_space = false;
    }

    /// Trim trailing space
    if (dst > flattened && dst[-1] == ' ') {
        dst--;
    }

    *dst = '\0';

    if (*src && remaining == 0) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Duplicate a string using memory pool
 *
 * Allocates memory from the LLE memory pool and copies the input string.
 *
 * @param str String to duplicate (may be NULL)
 * @return Pointer to duplicated string, or NULL if str is NULL or allocation
 * fails
 */
static char *pool_strdup(const char *str) {
    if (!str) {
        return NULL;
    }

    size_t len = strlen(str) + 1;
    char *dup = lle_pool_alloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================
 */

/**
 * @brief Detect if command is multiline
 *
 * Checks whether a command contains newline characters.
 *
 * @param command Command string to check (must not be NULL)
 * @param is_multiline Output for multiline detection result (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if any parameter
 * is NULL
 */
lle_result_t lle_history_detect_multiline(const char *command,
                                          bool *is_multiline) {
    if (!command || !is_multiline) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Simple check: does it contain newlines?
    *is_multiline = (strchr(command, '\n') != NULL);

    return LLE_SUCCESS;
}

/**
 * @brief Detect multiline structure using continuation analysis
 *
 * Analyzes a command using Lush's continuation system to detect
 * unclosed quotes, brackets, function definitions, control structures,
 * and here-documents.
 *
 * @param command Command string to analyze (must not be NULL)
 * @param info Output structure for multiline information (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if any parameter
 * is NULL
 */
lle_result_t
lle_history_detect_multiline_structure(const char *command,
                                       lle_history_multiline_info_t *info) {
    if (!command || !info) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Initialize info structure
    memset(info, 0, sizeof(lle_history_multiline_info_t));

    /// Count newlines
    info->line_count = count_newlines(command) + 1;
    info->is_multiline = (info->line_count > 1);

    if (!info->is_multiline) {
        return LLE_SUCCESS;
    }

    /// Use Lush continuation system to analyze structure
    continuation_state_t state;
    continuation_state_init(&state);

    /// Analyze the command
    continuation_analyze_line(command, &state);

    /// Extract structural information
    info->has_unclosed_quotes =
        (state.in_single_quote || state.in_double_quote || state.in_backtick);
    info->has_unclosed_brackets =
        (state.paren_count > 0 || state.brace_count > 0 ||
         state.bracket_count > 0);
    info->is_function_def = state.in_function_definition;
    info->is_control_structure =
        (state.in_if_statement || state.in_while_loop || state.in_for_loop ||
         state.in_until_loop || state.in_case_statement);
    info->is_here_doc = state.in_here_doc;

    /// Calculate total length
    info->total_length = strlen(command);

    /// Cleanup continuation state
    continuation_state_cleanup(&state);

    return LLE_SUCCESS;
}

/**
 * @brief Preserve multiline formatting in history entry
 *
 * Stores the original multiline format and creates a flattened version
 * for the main command field (used for searching).
 *
 * @param entry History entry to update (must not be NULL)
 * @param original_multiline Original multiline command (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if any parameter
 * is NULL, LLE_ERROR_OUT_OF_MEMORY on allocation failure,
 * LLE_ERROR_BUFFER_OVERFLOW if too long
 */
lle_result_t lle_history_preserve_multiline(lle_history_entry_t *entry,
                                            const char *original_multiline) {
    if (!entry || !original_multiline) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Free existing multiline storage if present
    if (entry->original_multiline) {
        lle_pool_free(entry->original_multiline);
        entry->original_multiline = NULL;
    }

    /// Detect if it's actually multiline
    bool is_multiline = false;
    lle_result_t result =
        lle_history_detect_multiline(original_multiline, &is_multiline);
    if (result != LLE_SUCCESS) {
        return result;
    }

    entry->is_multiline = is_multiline;

    if (!is_multiline) {
        /// Not multiline - no need to preserve separate original
        return LLE_SUCCESS;
    }

    /// Store original multiline format
    entry->original_multiline = pool_strdup(original_multiline);
    if (!entry->original_multiline) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Flatten the command into the main command field (for searching, etc.)
    char flattened[LLE_HISTORY_MAX_COMMAND_LENGTH];
    result = flatten_command(original_multiline, flattened, sizeof(flattened));
    if (result != LLE_SUCCESS) {
        lle_pool_free(entry->original_multiline);
        entry->original_multiline = NULL;
        return result;
    }

    /// Update the main command field with flattened version
    /// Note: entry->command should already be allocated
    if (entry->command) {
        lle_pool_free(entry->command);
    }

    entry->command = pool_strdup(flattened);
    if (!entry->command) {
        lle_pool_free(entry->original_multiline);
        entry->original_multiline = NULL;
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    entry->command_length = strlen(flattened);

    return LLE_SUCCESS;
}

/**
 * @brief Reconstruct multiline command for editing
 *
 * Retrieves the command in the specified format (original, flattened, or
 * compact).
 *
 * @param entry History entry to reconstruct from (must not be NULL)
 * @param buffer Output buffer for reconstructed command (must not be NULL)
 * @param buffer_size Size of output buffer in bytes
 * @param format Desired output format (ORIGINAL, FLATTENED, or COMPACT)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters
 * invalid, LLE_ERROR_INVALID_STATE if no source available,
 * LLE_ERROR_BUFFER_OVERFLOW if too long
 */
lle_result_t
lle_history_reconstruct_multiline(const lle_history_entry_t *entry,
                                  char *buffer, size_t buffer_size,
                                  lle_history_multiline_format_t format) {
    if (!entry || !buffer || buffer_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *source = NULL;

    switch (format) {
    case LLE_MULTILINE_FORMAT_ORIGINAL:
        /// Use original formatting if available
        if (entry->is_multiline && entry->original_multiline) {
            source = entry->original_multiline;
        } else {
            source = entry->command;
        }
        break;

    case LLE_MULTILINE_FORMAT_FLATTENED:
        /// Use flattened version
        source = entry->command;
        break;

    case LLE_MULTILINE_FORMAT_COMPACT:
        /// Use flattened version (same as FLATTENED for now)
        source = entry->command;
        break;

    default:
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!source) {
        return LLE_ERROR_INVALID_STATE;
    }

    /// Copy to buffer
    size_t source_len = strlen(source);
    if (source_len >= buffer_size) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    memcpy(buffer, source, source_len + 1);

    return LLE_SUCCESS;
}

/**
 * @brief Get multiline command for buffer loading
 *
 * Returns the appropriate command string (original multiline or flattened)
 * for loading into a buffer.
 *
 * @param entry History entry to get command from (must not be NULL)
 * @param command Output pointer for command string (must not be NULL)
 * @param command_length Output for command length (must not be NULL)
 * @param is_multiline Output for multiline flag (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if required
 * parameters are NULL
 */
lle_result_t
lle_history_get_multiline_for_buffer(const lle_history_entry_t *entry,
                                     char **command, size_t *command_length,
                                     bool *is_multiline) {
    if (!entry || !command || !command_length) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (is_multiline) {
        *is_multiline = entry->is_multiline;
    }

    /// Return original multiline format if available, otherwise flattened
    if (entry->is_multiline && entry->original_multiline) {
        *command = entry->original_multiline;
        *command_length = strlen(entry->original_multiline);
    } else {
        *command = entry->command;
        *command_length = entry->command_length;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Load multiline command into buffer system
 *
 * Retrieves the command from history and loads it into a buffer using
 * the provided load function.
 *
 * @param entry History entry to load (must not be NULL)
 * @param buffer_context Context to pass to load function (must not be NULL)
 * @param load_fn Function to call for loading (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if any parameter
 * is NULL, or error from load_fn
 */
lle_result_t
lle_history_load_multiline_into_buffer(const lle_history_entry_t *entry,
                                       void *buffer_context,
                                       lle_history_buffer_load_fn load_fn) {
    if (!entry || !buffer_context || !load_fn) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    char *command = NULL;
    size_t command_length = 0;
    bool is_multiline = false;

    lle_result_t result = lle_history_get_multiline_for_buffer(
        entry, &command, &command_length, &is_multiline);

    if (result != LLE_SUCCESS) {
        return result;
    }

    /// Call the buffer load function
    return load_fn(buffer_context, command, command_length, is_multiline);
}

/**
 * @brief Analyze multiline command lines
 *
 * Parses a multiline command and returns an array of line descriptors
 * with text pointers, lengths, line numbers, and indentation.
 *
 * @param command Command string to analyze (must not be NULL)
 * @param lines Output pointer for line array (must not be NULL, caller must
 * free)
 * @param line_count Output for number of lines (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if any parameter
 * is NULL, LLE_ERROR_OUT_OF_MEMORY on allocation failure
 */
lle_result_t
lle_history_analyze_multiline_lines(const char *command,
                                    lle_history_multiline_line_t **lines,
                                    size_t *line_count) {
    if (!command || !lines || !line_count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Count lines first
    size_t count = count_newlines(command) + 1;

    /// Allocate line array
    lle_history_multiline_line_t *line_array =
        lle_pool_alloc(sizeof(lle_history_multiline_line_t) * count);

    if (!line_array) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Parse lines
    const char *start = command;
    const char *end = NULL;
    size_t index = 0;

    while (*start && index < count) {
        /// Find end of line
        end = strchr(start, '\n');
        if (!end) {
            end = start + strlen(start);
        }

        /// Calculate line length
        size_t line_len = end - start;

        /// Store line information
        line_array[index].line_text =
            start; /// Note: Not duplicating, just pointing
        line_array[index].line_length = line_len;
        line_array[index].line_number = index + 1;

        /// Calculate indentation
        size_t indent = 0;
        for (const char *p = start; p < end && (*p == ' ' || *p == '\t'); p++) {
            indent++;
        }
        line_array[index].indentation = indent;

        /// Move to next line
        start = (*end == '\n') ? (end + 1) : end;
        index++;
    }

    *lines = line_array;
    *line_count = count;

    return LLE_SUCCESS;
}

/**
 * @brief Free multiline line analysis
 *
 * Frees the line array allocated by lle_history_analyze_multiline_lines.
 *
 * @param lines Line array to free (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if lines is NULL
 */
lle_result_t
lle_history_free_multiline_lines(lle_history_multiline_line_t *lines) {
    if (!lines) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_pool_free(lines);
    return LLE_SUCCESS;
}

/**
 * @brief Format multiline command with indentation
 *
 * Adds base indentation to each line of a multiline command.
 *
 * @param command Command string to format (must not be NULL)
 * @param formatted Output buffer for formatted command (must not be NULL)
 * @param formatted_size Size of output buffer in bytes
 * @param base_indent Number of spaces to add at the start of each line
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters
 * invalid, LLE_ERROR_BUFFER_OVERFLOW if formatted command is too long
 */
lle_result_t lle_history_format_multiline(const char *command, char *formatted,
                                          size_t formatted_size,
                                          size_t base_indent) {
    if (!command || !formatted || formatted_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *src = command;
    char *dst = formatted;
    size_t remaining = formatted_size - 1;
    bool at_line_start = true;
    size_t indent_added = 0;
    (void)indent_added; /// Reserved for indentation tracking

    while (*src && remaining > 0) {
        char c = *src++;

        /// Add indentation at start of each line
        if (at_line_start && c != '\n') {
            for (size_t i = 0; i < base_indent && remaining > 0; i++) {
                *dst++ = ' ';
                remaining--;
                indent_added++;
            }
            at_line_start = false;
        }

        /// Copy character
        *dst++ = c;
        remaining--;

        /// Track line starts
        if (c == '\n') {
            at_line_start = true;
        }
    }

    *dst = '\0';

    if (*src && remaining == 0) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Check if entry contains multiline command
 *
 * Returns whether the history entry contains a multiline command.
 *
 * @param entry History entry to check (may be NULL)
 * @return true if entry is multiline, false if NULL or single-line
 */
bool lle_history_is_multiline(const lle_history_entry_t *entry) {
    if (!entry) {
        return false;
    }

    return entry->is_multiline;
}

/**
 * @brief Get original multiline format
 *
 * Returns the original multiline format of a command with preserved formatting.
 *
 * @param entry History entry to get multiline from (may be NULL)
 * @return Pointer to original multiline string, or NULL if not multiline or
 * entry is NULL
 */
const char *
lle_history_get_original_multiline(const lle_history_entry_t *entry) {
    if (!entry || !entry->is_multiline) {
        return NULL;
    }

    return entry->original_multiline;
}

/**
 * @brief Get line count for multiline command
 *
 * Returns the number of lines in the original multiline command.
 *
 * @param entry History entry to get line count from (may be NULL)
 * @return Number of lines (1 if not multiline or entry is NULL)
 */
size_t lle_history_get_multiline_line_count(const lle_history_entry_t *entry) {
    if (!entry || !entry->is_multiline || !entry->original_multiline) {
        return 1; /// Single line
    }

    return count_newlines(entry->original_multiline) + 1;
}
