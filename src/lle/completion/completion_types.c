/**
 * @file completion_types.c
 * @brief LLE Completion Type Classification System Implementation
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
 * This module contains ONLY pure logic and data structures - NO terminal I/O.
 * All allocations use LLE memory pool for efficient memory management.
 */

#include "lle/completion/completion_types.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/// Forward declarations for shell integration
/// These will be properly integrated via completion_sources module
extern bool lle_shell_is_builtin(const char *text);
extern bool lle_shell_is_alias(const char *text);
extern int lle_shell_feature_count(void);
extern const char *lle_shell_feature_name(int index);

/// ============================================================================
/// TYPE INFORMATION DATABASE
/// ============================================================================

static const lle_completion_type_info_t TYPE_INFO_DATABASE[] = {
    {  .type = LLE_COMPLETION_TYPE_BUILTIN,
     .type_name = "Built-in",
     .category_name = "BUILT-INS",
     .indicator = "", /// No symbols by default - user customizable via theme
 .default_priority = 900},
    {  .type = LLE_COMPLETION_TYPE_COMMAND,
     .type_name = "Command",
     .category_name = "COMMANDS",
     .indicator = "", /// No symbols by default - user customizable via theme
 .default_priority = 800},
    {     .type = LLE_COMPLETION_TYPE_FILE,
     .type_name = "File",
     .category_name = "FILES",
     .indicator = "", /// No symbols by default - user customizable via theme
 .default_priority = 600},
    {.type = LLE_COMPLETION_TYPE_DIRECTORY,
     .type_name = "Directory",
     .category_name = "DIRECTORIES",
     .indicator = "", /// No symbols by default - user customizable via theme
 .default_priority = 700},
    { .type = LLE_COMPLETION_TYPE_VARIABLE,
     .type_name = "Variable",
     .category_name = "VARIABLES",
     .indicator = "", /// No symbols by default - user customizable via theme
 .default_priority = 500},
    {    .type = LLE_COMPLETION_TYPE_ALIAS,
     .type_name = "Alias",
     .category_name = "ALIASES",
     .indicator = "", /// No symbols by default - user customizable via theme
 .default_priority = 950},
    {  .type = LLE_COMPLETION_TYPE_HISTORY,
     .type_name = "History",
     .category_name = "HISTORY",
     .indicator = "", /// No symbols by default - user customizable via theme
 .default_priority = 400},
    {   .type = LLE_COMPLETION_TYPE_CUSTOM,
     .type_name = "Custom",
     .category_name = "CUSTOM",
     .indicator = "", /// No symbols by default - user customizable via theme
 .default_priority = 600},
    {  .type = LLE_COMPLETION_TYPE_UNKNOWN,
     .type_name = "Other",
     .category_name = "OTHER",
     .indicator = "", /// No symbols by default - user customizable via theme
 .default_priority = 100}
};

/// Note: TYPE_INFO_COUNT not currently used but kept for future validation
#define TYPE_INFO_COUNT                                                        \
    (sizeof(TYPE_INFO_DATABASE) / sizeof(TYPE_INFO_DATABASE[0]))

/// ============================================================================
/// TYPE INFORMATION QUERIES
/// ============================================================================

/**
 * @brief Get type information for a completion type
 *
 * Returns a pointer to the static type information structure for the
 * given completion type. If the type is invalid, returns the UNKNOWN
 * type information.
 *
 * @param type Completion type to get info for
 * @return Pointer to static type information structure
 */
const lle_completion_type_info_t *
lle_completion_type_get_info(lle_completion_type_t type) {
    if (type < 0 || type >= LLE_COMPLETION_TYPE_COUNT) {
        return &TYPE_INFO_DATABASE[LLE_COMPLETION_TYPE_UNKNOWN];
    }
    return &TYPE_INFO_DATABASE[type];
}

/**
 * @brief Get category name for a completion type
 *
 * Returns the human-readable category name string for grouping
 * completions in menu display (e.g., "COMMANDS", "FILES").
 *
 * @param type Completion type to get category for
 * @return Category name string
 */
const char *lle_completion_type_get_category(lle_completion_type_t type) {
    const lle_completion_type_info_t *info = lle_completion_type_get_info(type);
    return info->category_name;
}

/**
 * @brief Get visual indicator for a completion type
 *
 * Returns the visual indicator string (icon/symbol) for the
 * given completion type, used in menu rendering.
 *
 * @param type Completion type to get indicator for
 * @return Indicator string (may be empty if theme uses no indicators)
 */
const char *lle_completion_type_get_indicator(lle_completion_type_t type) {
    const lle_completion_type_info_t *info = lle_completion_type_get_info(type);
    return info->indicator;
}

/// ============================================================================
/// HELPER: STRING DUPLICATION WITH MEMORY POOL
/// ============================================================================

/**
 * @brief Duplicate a string using the memory pool
 *
 * Allocates memory from the pool and copies the string contents.
 * The caller does not need to free the returned memory as it will
 * be freed when the pool is destroyed.
 *
 * @param str String to duplicate (may be NULL)
 * @return Duplicated string or NULL on failure or if str is NULL
 */
static char *lle_strdup_pool(const char *str) {
    if (!str) {
        return NULL;
    }

    size_t len = strlen(str) + 1;
    char *copy = (char *)lle_pool_alloc(len);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, str, len);
    return copy;
}

/// ============================================================================
/// COMPLETION ITEM MANAGEMENT
/// ============================================================================

/**
 * @brief Create a new completion item
 *
 * Creates a completion item with the given text and properties.
 * All strings are duplicated into the memory pool.
 *
 * @param memory_pool Memory pool for allocations
 * @param text Completion text (required)
 * @param suffix Suffix to append when completing (e.g., "/" for directories)
 * @param type Completion type for categorization
 * @param relevance_score Score for sorting (0-1000, higher is more relevant)
 * @param item Output pointer for created item
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_item_create(lle_memory_pool_t *memory_pool,
                                        const char *text, const char *suffix,
                                        lle_completion_type_t type,
                                        int32_t relevance_score,
                                        lle_completion_item_t **item) {
    return lle_completion_item_create_with_description(
        memory_pool, text, suffix, type, relevance_score, NULL, item);
}

/**
 * @brief Create a completion item with description
 *
 * Creates a completion item with the given text, properties, and
 * an optional description for display in the completion menu.
 *
 * @param memory_pool Memory pool for allocations
 * @param text Completion text (required)
 * @param suffix Suffix to append when completing
 * @param type Completion type for categorization
 * @param relevance_score Score for sorting (0-1000)
 * @param description Optional description text (may be NULL)
 * @param item Output pointer for created item
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_item_create_with_description(
    lle_memory_pool_t *memory_pool, const char *text, const char *suffix,
    lle_completion_type_t type, int32_t relevance_score,
    const char *description, lle_completion_item_t **item) {
    if (!memory_pool || !text || !item) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate item structure
    lle_completion_item_t *new_item =
        (lle_completion_item_t *)lle_pool_alloc(sizeof(lle_completion_item_t));
    if (!new_item) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                         "completion type allocation failed");
    }

    /// Duplicate text
    new_item->text = lle_strdup_pool(text);
    if (!new_item->text) {
        lle_pool_free(new_item);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                         "completion type allocation failed");
    }
    new_item->owns_text = true;

    /// Duplicate suffix if provided
    if (suffix) {
        new_item->suffix = lle_strdup_pool(suffix);
        if (!new_item->suffix) {
            lle_pool_free(new_item->text);
            lle_pool_free(new_item);
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                             "completion type allocation failed");
        }
        new_item->owns_suffix = true;
    } else {
        new_item->suffix = NULL;
        new_item->owns_suffix = false;
    }

    /// Set type and get indicator
    new_item->type = type;
    new_item->type_indicator = lle_completion_type_get_indicator(type);

    /// Set relevance score (clamp to valid range)
    if (relevance_score < 0)
        relevance_score = 0;
    if (relevance_score > 1000)
        relevance_score = 1000;
    new_item->relevance_score = relevance_score;

    /// Duplicate description if provided
    if (description) {
        new_item->description = lle_strdup_pool(description);
        new_item->owns_description = (new_item->description != NULL);
    } else {
        new_item->description = NULL;
        new_item->owns_description = false;
    }

    *item = new_item;
    return LLE_SUCCESS;
}

/**
 * @brief Free a completion item
 *
 * Frees the completion item and all owned strings.
 * Memory is returned to the pool.
 *
 * @param memory_pool Memory pool the item was allocated from
 * @param item Item to free
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_item_free(lle_memory_pool_t *memory_pool,
                                      lle_completion_item_t *item) {
    if (!memory_pool || !item) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (item->owns_text && item->text) {
        lle_pool_free(item->text);
    }

    if (item->owns_suffix && item->suffix) {
        lle_pool_free(item->suffix);
    }

    if (item->owns_description && item->description) {
        lle_pool_free(item->description);
    }

    lle_pool_free(item);
    return LLE_SUCCESS;
}

/// ============================================================================
/// COMPLETION RESULT MANAGEMENT
/// ============================================================================

/**
 * @brief Create a new completion result set
 *
 * Creates a result container for storing completion items.
 * The container will grow automatically as items are added.
 *
 * @param memory_pool Memory pool for allocations
 * @param initial_capacity Initial array capacity (0 for default)
 * @param result Output pointer for created result set
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_result_create(lle_memory_pool_t *memory_pool,
                                          size_t initial_capacity,
                                          lle_completion_result_t **result) {
    if (!memory_pool || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (initial_capacity == 0) {
        initial_capacity = 16; /// Default capacity
    }

    /// Allocate result structure
    lle_completion_result_t *new_result =
        (lle_completion_result_t *)lle_pool_alloc(
            sizeof(lle_completion_result_t));
    if (!new_result) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                         "completion type allocation failed");
    }

    /// Allocate items array
    new_result->items = (lle_completion_item_t *)lle_pool_alloc(
        sizeof(lle_completion_item_t) * initial_capacity);
    if (!new_result->items) {
        lle_pool_free(new_result);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                         "completion type allocation failed");
    }

    new_result->count = 0;
    new_result->capacity = initial_capacity;
    new_result->memory_pool = memory_pool;

    /// Initialize category counts
    new_result->builtin_count = 0;
    new_result->command_count = 0;
    new_result->file_count = 0;
    new_result->directory_count = 0;
    new_result->variable_count = 0;
    new_result->alias_count = 0;
    new_result->history_count = 0;

    *result = new_result;
    return LLE_SUCCESS;
}

/**
 * @brief Add a completion item to a result set
 *
 * Adds the item to the result set and transfers ownership.
 * The item structure itself is freed after copying its contents.
 *
 * @param result Result set to add to
 * @param item Item to add (ownership transferred)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_result_add_item(lle_completion_result_t *result,
                                            lle_completion_item_t *item) {
    if (!result || !item) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Grow array if needed
    if (result->count >= result->capacity) {
        size_t new_capacity = result->capacity * 2;
        lle_completion_item_t *new_items =
            (lle_completion_item_t *)lle_pool_alloc(
                sizeof(lle_completion_item_t) * new_capacity);
        if (!new_items) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                             "completion type allocation failed");
        }

        /// Copy existing items
        memcpy(new_items, result->items,
               sizeof(lle_completion_item_t) * result->count);

        /// Free old array
        lle_pool_free(result->items);

        result->items = new_items;
        result->capacity = new_capacity;
    }

    /// Add item (transfer ownership)
    result->items[result->count] = *item;
    result->count++;

    /// Update category count
    switch (item->type) {
    case LLE_COMPLETION_TYPE_BUILTIN:
        result->builtin_count++;
        break;
    case LLE_COMPLETION_TYPE_COMMAND:
        result->command_count++;
        break;
    case LLE_COMPLETION_TYPE_FILE:
        result->file_count++;
        break;
    case LLE_COMPLETION_TYPE_DIRECTORY:
        result->directory_count++;
        break;
    case LLE_COMPLETION_TYPE_VARIABLE:
        result->variable_count++;
        break;
    case LLE_COMPLETION_TYPE_ALIAS:
        result->alias_count++;
        break;
    case LLE_COMPLETION_TYPE_HISTORY:
        result->history_count++;
        break;
    case LLE_COMPLETION_TYPE_CUSTOM:
        result->custom_count++;
        break;
    default:
        break;
    }

    /// Free the item wrapper (contents now owned by result)
    lle_pool_free(item);

    return LLE_SUCCESS;
}

/**
 * @brief Add a completion to a result set by text
 *
 * Convenience function that creates an item and adds it to the result set.
 *
 * @param result Result set to add to
 * @param text Completion text
 * @param suffix Suffix to append when completing
 * @param type Completion type
 * @param relevance_score Relevance score (0-1000)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_result_add(lle_completion_result_t *result,
                                       const char *text, const char *suffix,
                                       lle_completion_type_t type,
                                       int32_t relevance_score) {
    if (!result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_completion_item_t *item = NULL;
    lle_result_t res = lle_completion_item_create(
        result->memory_pool, text, suffix, type, relevance_score, &item);

    if (res != LLE_SUCCESS) {
        return res;
    }

    return lle_completion_result_add_item(result, item);
}

/**
 * @brief Add a completion with description to the result
 *
 * Creates completion item internally. The description field can be used
 * to store additional metadata (e.g., full path for external commands
 * that shadow builtins).
 *
 * @param result completion result
 * @param text completion text
 * @param suffix suffix to append (may be NULL)
 * @param type completion type
 * @param relevance_score relevance score (0-1000)
 * @param description optional description (may be NULL)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_result_add_with_description(
    lle_completion_result_t *result, const char *text, const char *suffix,
    lle_completion_type_t type, int32_t relevance_score,
    const char *description) {
    if (!result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_completion_item_t *item = NULL;
    lle_result_t res = lle_completion_item_create_with_description(
        result->memory_pool, text, suffix, type, relevance_score, description,
        &item);

    if (res != LLE_SUCCESS) {
        return res;
    }

    return lle_completion_result_add_item(result, item);
}

/**
 * @brief Compare two completion items for sorting
 *
 * Compares by type first, then by relevance score (descending),
 * then alphabetically by text.
 *
 * @param a First item pointer
 * @param b Second item pointer
 * @return Comparison result for qsort (-1, 0, or 1)
 */
static int compare_completion_items(const void *a, const void *b) {
    const lle_completion_item_t *item_a = (const lle_completion_item_t *)a;
    const lle_completion_item_t *item_b = (const lle_completion_item_t *)b;

    /// First sort by type (category)
    if (item_a->type != item_b->type) {
        return item_a->type - item_b->type;
    }

    /// Within same type, sort by relevance score (descending)
    if (item_a->relevance_score != item_b->relevance_score) {
        return item_b->relevance_score - item_a->relevance_score;
    }

    /// Finally, alphabetical order
    return strcmp(item_a->text, item_b->text);
}

/**
 * @brief Sort completion results
 *
 * Sorts the result set by type, relevance score, and alphabetically.
 *
 * @param result Result set to sort
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_result_sort(lle_completion_result_t *result) {
    if (!result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (result->count <= 1) {
        return LLE_SUCCESS;
    }

    qsort(result->items, result->count, sizeof(lle_completion_item_t),
          compare_completion_items);

    return LLE_SUCCESS;
}

/**
 * @brief Free a completion result set
 *
 * Frees all items in the result set and the result structure itself.
 *
 * @param result Result set to free
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_result_free(lle_completion_result_t *result) {
    if (!result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Free all items
    for (size_t i = 0; i < result->count; i++) {
        lle_completion_item_t *item = &result->items[i];
        if (item->owns_text && item->text) {
            lle_pool_free(item->text);
        }
        if (item->owns_suffix && item->suffix) {
            lle_pool_free(item->suffix);
        }
        if (item->owns_description && item->description) {
            lle_pool_free(item->description);
        }
    }

    lle_pool_free(result->items);
    lle_pool_free(result);

    return LLE_SUCCESS;
}

/// ============================================================================
/// CLASSIFICATION HELPERS
/// ============================================================================

/**
 * @brief Check if text is a shell builtin command
 *
 * Delegates to the shell integration function to determine
 * if the given text matches a builtin command name.
 *
 * @param text Text to check
 * @return true if text is a builtin command, false otherwise
 */
bool lle_completion_is_builtin(const char *text) {
    if (!text) {
        return false;
    }

    /// This will be properly implemented in completion_sources module
    /// For now, use weak symbol that can be overridden
    return lle_shell_is_builtin(text);
}

/**
 * @brief Check if text is a shell alias
 *
 * Delegates to the shell integration function to determine
 * if the given text matches a defined alias name.
 *
 * @param text Text to check
 * @return true if text is an alias, false otherwise
 */
bool lle_completion_is_alias(const char *text) {
    if (!text) {
        return false;
    }

    /// This will be properly implemented in completion_sources module
    /// For now, use weak symbol that can be overridden
    return lle_shell_is_alias(text);
}

/**
 * @brief Check if text is a directory path
 *
 * Uses stat() to determine if the given path refers to
 * a directory.
 *
 * @param text Path to check
 * @return true if path is a directory, false otherwise
 */
bool lle_completion_is_directory(const char *text) {
    if (!text) {
        return false;
    }

    struct stat st;
    if (stat(text, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    return false;
}

/**
 * @brief Classify text into a completion type
 *
 * Determines the appropriate completion type for the given text
 * based on its content and position in the command line.
 *
 * @param text Text to classify
 * @param is_command_position true if at command position (first word)
 * @return Appropriate completion type
 */
lle_completion_type_t lle_completion_classify_text(const char *text,
                                                   bool is_command_position) {
    if (!text) {
        return LLE_COMPLETION_TYPE_UNKNOWN;
    }

    /// Check for variable
    if (text[0] == '$') {
        return LLE_COMPLETION_TYPE_VARIABLE;
    }

    /// If not at command position, likely a file/directory
    if (!is_command_position) {
        if (lle_completion_is_directory(text)) {
            return LLE_COMPLETION_TYPE_DIRECTORY;
        }
        return LLE_COMPLETION_TYPE_FILE;
    }

    /// At command position - classify command type
    if (lle_completion_is_alias(text)) {
        return LLE_COMPLETION_TYPE_ALIAS;
    }

    if (lle_completion_is_builtin(text)) {
        return LLE_COMPLETION_TYPE_BUILTIN;
    }

    /// Check if it's a path
    if (strchr(text, '/') != NULL) {
        if (lle_completion_is_directory(text)) {
            return LLE_COMPLETION_TYPE_DIRECTORY;
        }
        return LLE_COMPLETION_TYPE_FILE;
    }

    /// Assume it's a command from PATH
    return LLE_COMPLETION_TYPE_COMMAND;
}

/// ============================================================================
/// STATISTICS AND QUERIES
/// ============================================================================

/**
 * @brief Count items of a specific type in result set
 *
 * Returns the cached count of items matching the given type.
 *
 * @param result Result set to query
 * @param type Type to count
 * @return Number of items of the given type
 */
size_t
lle_completion_result_count_by_type(const lle_completion_result_t *result,
                                    lle_completion_type_t type) {
    if (!result) {
        return 0;
    }

    switch (type) {
    case LLE_COMPLETION_TYPE_BUILTIN:
        return result->builtin_count;
    case LLE_COMPLETION_TYPE_COMMAND:
        return result->command_count;
    case LLE_COMPLETION_TYPE_FILE:
        return result->file_count;
    case LLE_COMPLETION_TYPE_DIRECTORY:
        return result->directory_count;
    case LLE_COMPLETION_TYPE_VARIABLE:
        return result->variable_count;
    case LLE_COMPLETION_TYPE_ALIAS:
        return result->alias_count;
    case LLE_COMPLETION_TYPE_HISTORY:
        return result->history_count;
    case LLE_COMPLETION_TYPE_CUSTOM:
        return result->custom_count;
    default:
        return 0;
    }
}

/**
 * @brief Get an item from a result set by index
 *
 * Returns a pointer to the item at the given index, or NULL
 * if the index is out of bounds.
 *
 * @param result Result set to query
 * @param index Index of item to retrieve
 * @return Pointer to item or NULL if index is invalid
 */
const lle_completion_item_t *
lle_completion_result_get_item(const lle_completion_result_t *result,
                               size_t index) {
    if (!result || index >= result->count) {
        return NULL;
    }

    return &result->items[index];
}

/// ============================================================================
/// WEAK SYMBOLS FOR SHELL INTEGRATION
/// ============================================================================

/// These weak symbols can be overridden by the actual shell integration
/// in the completion_sources module

__attribute__((weak)) bool lle_shell_is_builtin(const char *text) {
    (void)text;
    return false;
}

__attribute__((weak)) bool lle_shell_is_alias(const char *text) {
    (void)text;
    return false;
}

/// Weak fallbacks for the setopt/unsetopt feature-name bridge. The
/// strong versions in completion_sources.c enumerate the shell's feature
/// matrix (shell_mode.c); a standalone liblle.a has no shell_mode, so
/// completion simply offers no feature names.
__attribute__((weak)) int lle_shell_feature_count(void) { return 0; }

__attribute__((weak)) const char *lle_shell_feature_name(int index) {
    (void)index;
    return NULL;
}

/// Weak ASCII-only fallback for the shell identifier predicate used by
/// the syntax highlighter (display/syntax_highlighting.c) and variable
/// completion (completion/word_context.c). The strong, feature-flag-aware
/// implementation lives in src/identifier.c and consults shell_mode_allows
/// (FEATURE_UNICODE_IDENTIFIERS); it overrides these at the full-shell
/// link. A standalone liblle.a build has no shell_mode, so the fallback
/// matches POSIX ASCII identifier syntax: Start = [A-Za-z_], Continue =
/// [A-Za-z0-9_]. Signatures must match include/identifier.h exactly so
/// the strong definitions take precedence.
__attribute__((weak)) size_t lush_ident_match_start(const char *p,
                                                    size_t remaining) {
    if (!p || remaining == 0) {
        return 0;
    }
    unsigned char b = (unsigned char)p[0];
    return (b == '_' || isalpha(b)) ? 1u : 0u;
}

__attribute__((weak)) size_t lush_ident_match_continue(const char *p,
                                                       size_t remaining) {
    if (!p || remaining == 0) {
        return 0;
    }
    unsigned char b = (unsigned char)p[0];
    return (b == '_' || isalnum(b)) ? 1u : 0u;
}

/// Codepoint-level counterparts, used by the completion word-context
/// analyzer. Same weak-fallback contract: ASCII-only here, overridden
/// by the feature-aware src/identifier.c at the full-shell link.
__attribute__((weak)) bool lush_ident_is_start_cp(uint32_t cp) {
    return cp == '_' || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

__attribute__((weak)) bool lush_ident_is_continue_cp(uint32_t cp) {
    return lush_ident_is_start_cp(cp) || (cp >= '0' && cp <= '9');
}
