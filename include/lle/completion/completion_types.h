/**
 * @file completion_types.h
 * @brief LLE Completion Type Classification System
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
 * Provides type metadata for completions to enable:
 * - Visual categorization in interactive menus
 * - Type-specific indicators (symbols/colors)
 * - Intelligent ranking and grouping
 * - Enhanced display integration
 *
 * This module is part of the LLE completion system (Spec 12).
 * It contains ONLY pure logic and data structures - NO terminal I/O.
 * All rendering is handled by the display layer.
 */

#ifndef LLE_COMPLETION_TYPES_H
#define LLE_COMPLETION_TYPES_H

#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// COMPLETION TYPE ENUMERATION
// ============================================================================

/**
 * @brief Completion type classification for categorization and display
 */
typedef enum {
    LLE_COMPLETION_TYPE_BUILTIN,   /**< Shell built-in commands (cd, echo, etc.)
                                    */
    LLE_COMPLETION_TYPE_COMMAND,   /**< External commands from PATH */
    LLE_COMPLETION_TYPE_FILE,      /**< Regular files */
    LLE_COMPLETION_TYPE_DIRECTORY, /**< Directories */
    LLE_COMPLETION_TYPE_VARIABLE,  /**< Shell/environment variables */
    LLE_COMPLETION_TYPE_ALIAS,     /**< Command aliases */
    LLE_COMPLETION_TYPE_HISTORY,   /**< History entries */
    LLE_COMPLETION_TYPE_CUSTOM,    /**< User-defined custom completions */
    LLE_COMPLETION_TYPE_UNKNOWN,   /**< Unclassified/fallback */
    LLE_COMPLETION_TYPE_COUNT      /**< Number of types (for iteration) */
} lle_completion_type_t;

// ============================================================================
// COMPLETION ITEM WITH METADATA
// ============================================================================

/**
 * @brief Completion item with type metadata for categorization and display
 */
typedef struct lle_completion_item {
    char *text;                 /**< Completion text (owned by this struct) */
    char *suffix;               /**< Suffix to append (space, /, etc.) */
    lle_completion_type_t type; /**< Completion type */
    const char *type_indicator; /**< Visual indicator (symbol/emoji) */
    int32_t relevance_score;    /**< Relevance ranking (0-1000) */
    char *description;          /**< Optional description (may be NULL) */

    // Memory management flags
    bool owns_text;        /**< Whether this struct owns text memory */
    bool owns_suffix;      /**< Whether this struct owns suffix memory */
    bool owns_description; /**< Whether this struct owns description memory */
} lle_completion_item_t;

/**
 * @brief Completion result with classified items
 */
typedef struct lle_completion_result {
    lle_completion_item_t *items; /**< Array of completion items */
    size_t count;                 /**< Number of items in array */
    size_t capacity;              /**< Allocated capacity */

    // Category statistics for display
    size_t builtin_count;   /**< Number of builtin completions */
    size_t command_count;   /**< Number of command completions */
    size_t file_count;      /**< Number of file completions */
    size_t directory_count; /**< Number of directory completions */
    size_t variable_count;  /**< Number of variable completions */
    size_t alias_count;     /**< Number of alias completions */
    size_t history_count;   /**< Number of history completions */
    size_t custom_count;    /**< Number of custom completions */

    // Memory pool for allocations
    lle_memory_pool_t *memory_pool; /**< Memory pool for allocations */
} lle_completion_result_t;

// ============================================================================
// TYPE INFORMATION AND METADATA
// ============================================================================

/**
 * @brief Type metadata for classification and display
 */
typedef struct {
    lle_completion_type_t type; /**< Completion type */
    const char *type_name; /**< Human-readable name ("Command", "File", etc.) */
    const char
        *category_name; /**< Category name for grouping ("COMMANDS", "FILES") */
    const char *indicator;    /**< Visual indicator (symbol/emoji) */
    int32_t default_priority; /**< Default priority for ranking (0-1000) */
} lle_completion_type_info_t;

/**
 * @brief Get type information for a completion type
 *
 * @param type completion type
 * @return type information structure (static, do not free)
 */
const lle_completion_type_info_t *
lle_completion_type_get_info(lle_completion_type_t type);

/**
 * @brief Get category name for a completion type
 *
 * @param type completion type
 * @return category name (e.g., "COMMANDS", "FILES", "VARIABLES")
 */
const char *lle_completion_type_get_category(lle_completion_type_t type);

/**
 * @brief Get visual indicator for a completion type
 *
 * @param type completion type
 * @return indicator string (symbol or emoji)
 */
const char *lle_completion_type_get_indicator(lle_completion_type_t type);

// ============================================================================
// COMPLETION ITEM MANAGEMENT
// ============================================================================

/**
 * @brief Create a new completion item
 *
 * @param memory_pool memory pool for allocation
 * @param text completion text (will be duplicated)
 * @param suffix suffix to append (may be NULL, will be duplicated if not NULL)
 * @param type completion type
 * @param relevance_score relevance score (0-1000)
 * @param item output parameter for created item
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_item_create(lle_memory_pool_t *memory_pool,
                                        const char *text, const char *suffix,
                                        lle_completion_type_t type,
                                        int32_t relevance_score,
                                        lle_completion_item_t **item);

/**
 * @brief Create completion item with description
 *
 * @param memory_pool memory pool for allocation
 * @param text completion text (will be duplicated)
 * @param suffix suffix to append (may be NULL)
 * @param type completion type
 * @param relevance_score relevance score (0-1000)
 * @param description optional description (will be duplicated if not NULL)
 * @param item output parameter for created item
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_item_create_with_description(
    lle_memory_pool_t *memory_pool, const char *text, const char *suffix,
    lle_completion_type_t type, int32_t relevance_score,
    const char *description, lle_completion_item_t **item);

/**
 * @brief Free a completion item
 *
 * @param memory_pool memory pool used for allocation
 * @param item item to free
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_item_free(lle_memory_pool_t *memory_pool,
                                      lle_completion_item_t *item);

// ============================================================================
// COMPLETION RESULT MANAGEMENT
// ============================================================================

/**
 * @brief Create a new completion result structure
 *
 * @param memory_pool memory pool for allocations
 * @param initial_capacity initial capacity for items array
 * @param result output parameter for created result
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_result_create(lle_memory_pool_t *memory_pool,
                                          size_t initial_capacity,
                                          lle_completion_result_t **result);

/**
 * @brief Add a completion item to the result
 *
 * Takes ownership of the item.
 *
 * @param result completion result
 * @param item completion item to add (ownership transferred)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_result_add_item(lle_completion_result_t *result,
                                            lle_completion_item_t *item);

/**
 * @brief Add a simple completion to the result
 *
 * Creates completion item internally.
 *
 * @param result completion result
 * @param text completion text
 * @param suffix suffix to append (may be NULL)
 * @param type completion type
 * @param relevance_score relevance score (0-1000)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_result_add(lle_completion_result_t *result,
                                       const char *text, const char *suffix,
                                       lle_completion_type_t type,
                                       int32_t relevance_score);

/**
 * @brief Add a completion with description to the result
 *
 * Creates completion item internally. The description field can be used
 * to store additional metadata (e.g., full path for external commands).
 *
 * @param result completion result
 * @param text completion text
 * @param suffix suffix to append (may be NULL)
 * @param type completion type
 * @param relevance_score relevance score (0-1000)
 * @param description optional description (may be NULL)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_result_add_with_description(
    lle_completion_result_t *result, const char *text, const char *suffix,
    lle_completion_type_t type, int32_t relevance_score,
    const char *description);

/**
 * @brief Sort completion result by type and relevance
 *
 * Groups items by category, then sorts by relevance within each category.
 *
 * @param result completion result to sort
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_result_sort(lle_completion_result_t *result);

/**
 * @brief Free a completion result and all its items
 *
 * @param result completion result to free
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_result_free(lle_completion_result_t *result);

// ============================================================================
// CLASSIFICATION HELPERS
// ============================================================================

/**
 * @brief Classify a completion text into a type
 *
 * Uses heuristics based on text content and context.
 *
 * @param text completion text
 * @param is_command_position whether completing at command position
 * @return classified type
 */
lle_completion_type_t lle_completion_classify_text(const char *text,
                                                   bool is_command_position);

/**
 * @brief Check if text is a builtin command
 *
 * @param text text to check
 * @return true if builtin, false otherwise
 */
bool lle_completion_is_builtin(const char *text);

/**
 * @brief Check if text is an alias
 *
 * @param text text to check
 * @return true if alias, false otherwise
 */
bool lle_completion_is_alias(const char *text);

/**
 * @brief Check if text refers to a directory
 *
 * @param text text to check
 * @return true if directory, false otherwise
 */
bool lle_completion_is_directory(const char *text);

// ============================================================================
// STATISTICS AND QUERIES
// ============================================================================

/**
 * @brief Get count of items for a specific type
 *
 * @param result completion result
 * @param type completion type
 * @return count of items of that type
 */
size_t
lle_completion_result_count_by_type(const lle_completion_result_t *result,
                                    lle_completion_type_t type);

/**
 * @brief Get item at specific index
 *
 * @param result completion result
 * @param index item index
 * @return item at index, or NULL if index out of bounds
 */
const lle_completion_item_t *
lle_completion_result_get_item(const lle_completion_result_t *result,
                               size_t index);

#ifdef __cplusplus
}
#endif

#endif // LLE_COMPLETION_TYPES_H
