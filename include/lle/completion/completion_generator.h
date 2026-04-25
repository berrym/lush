/**
 * @file completion_generator.h
 * @brief LLE Completion Generator - Main Orchestration Layer
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Main completion orchestration layer that:
 * - Analyzes input context (command position, variable, etc.)
 * - Determines which completion sources to use
 * - Calls appropriate sources to generate candidates
 * - Combines and sorts results
 * - Returns final completion result
 *
 * This module contains ONLY logic - NO terminal I/O.
 * All rendering is handled by the display layer.
 */

#ifndef LLE_COMPLETION_GENERATOR_H
#define LLE_COMPLETION_GENERATOR_H

#include "lle/completion/completion_sources.h"
#include "lle/completion/completion_types.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// COMPLETION CONTEXT
// ============================================================================

/**
 * @brief Completion context type
 */
typedef enum {
    LLE_COMPLETION_CONTEXT_COMMAND,  /**< Command position (first word or after
                                        |;&) */
    LLE_COMPLETION_CONTEXT_ARGUMENT, /**< Command argument position */
    LLE_COMPLETION_CONTEXT_VARIABLE, /**< Variable completion (starts with $) */
    LLE_COMPLETION_CONTEXT_UNKNOWN   /**< Unknown context */
} lle_completion_context_t;

/**
 * @brief Completion context information
 */
typedef struct {
    lle_completion_context_t type; /**< Context type */
    const char *word;              /**< Word being completed */
    size_t word_start;             /**< Start position of word in buffer */
    size_t word_length;            /**< Length of word */
    bool at_command_position;      /**< True if at command position */
} lle_completion_context_info_t;

// ============================================================================
// CONTEXT ANALYSIS
// ============================================================================

/**
 * @brief Analyze buffer to determine completion context
 *
 * @param buffer Input buffer containing command text
 * @param cursor_pos Cursor position in buffer (byte offset)
 * @param context Output parameter for context info structure
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_completion_analyze_context(const char *buffer, size_t cursor_pos,
                               lle_completion_context_info_t *context);

/**
 * @brief Extract the word being completed from buffer
 *
 * @param buffer Input buffer containing command text
 * @param cursor_pos Cursor position in buffer (byte offset)
 * @param word_start Output parameter for word start position in buffer
 * @param word Output parameter for extracted word (caller must free)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_extract_word(const char *buffer, size_t cursor_pos,
                                         size_t *word_start, char **word);

/**
 * @brief Check if position is at command position
 *
 * @param buffer Input buffer containing command text
 * @param position Position to check (byte offset)
 * @return true if at command position, false otherwise
 */
bool lle_completion_is_command_position(const char *buffer, size_t position);

// ============================================================================
// COMPLETION GENERATION
// ============================================================================

/**
 * @brief Generate completions for given buffer and cursor position
 *
 * Main entry point for completion generation. Analyzes context and
 * calls appropriate sources to generate completion candidates.
 *
 * @param memory_pool Memory pool for allocations
 * @param buffer Input buffer containing command text
 * @param cursor_pos Cursor position in buffer (byte offset)
 * @param result Output parameter for completion result structure
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_generate(lle_memory_pool_t *memory_pool,
                                     const char *buffer, size_t cursor_pos,
                                     lle_completion_result_t **result);

/**
 * @brief Generate completions for command context
 *
 * Combines builtins, aliases, and PATH commands to generate
 * command completion candidates.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Command prefix to complete
 * @param result Completion result structure to populate
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_generate_commands(lle_memory_pool_t *memory_pool,
                                              const char *prefix,
                                              lle_completion_result_t *result);

/**
 * @brief Generate completions for argument context
 *
 * Primarily generates file/directory completions for command arguments.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Argument prefix to complete
 * @param result Completion result structure to populate
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_generate_arguments(lle_memory_pool_t *memory_pool,
                                               const char *prefix,
                                               lle_completion_result_t *result);

/**
 * @brief Generate completions for variable context
 *
 * Generates environment and shell variable completions.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Variable prefix to complete (without leading $)
 * @param result Completion result structure to populate
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_generate_variables(lle_memory_pool_t *memory_pool,
                                               const char *prefix,
                                               lle_completion_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* LLE_COMPLETION_GENERATOR_H */
