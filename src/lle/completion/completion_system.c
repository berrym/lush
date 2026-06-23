/**
 * @file completion_system.c
 * @brief LLE Completion System Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Enhanced completion system with proper architecture.
 * This FIXES the duplicate and categorization bugs.
 */

#include "lle/completion/completion_system.h"
#include <stdlib.h>
#include <string.h>

/// ============================================================================
/// LIFECYCLE FUNCTIONS
/// ============================================================================

/**
 * @brief Create a new completion system
 * @param pool Memory pool for allocations
 * @param out_system Output pointer for created system
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_completion_system_create(lle_memory_pool_t *pool,
                             lle_completion_system_t **out_system) {
    if (!pool || !out_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_completion_system_t *system = lle_pool_alloc(sizeof(*system));
    if (!system) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                         "completion system allocation failed");
    }

    /// Create source manager
    lle_result_t res = lle_source_manager_create(pool, &system->source_manager);
    if (res != LLE_SUCCESS) {
        return res;
    }

    system->current_state = NULL;
    system->menu = NULL;
    system->pool = pool;
    system->enable_history_source = true;
    system->enable_fuzzy_matching = false; /// Future feature
    system->max_completions = 100;

    *out_system = system;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a completion system and free resources
 * @param system System to destroy
 */
void lle_completion_system_destroy(lle_completion_system_t *system) {
    if (!system) {
        return;
    }

    /// Free source manager
    if (system->source_manager) {
        lle_source_manager_free(system->source_manager);
    }

    /// Free menu state (must be freed before current_state since menu
    /// references result owned by current_state)
    if (system->menu) {
        lle_completion_menu_state_free(system->menu);
        system->menu = NULL;
    }

    /// Free current state (this also frees the results and context)
    if (system->current_state) {
        lle_completion_state_free(system->current_state);
        system->current_state = NULL;
    }

    /// Note: system structure itself is pool-allocated
}

/**
 * @brief Clear current completion state
 * @param system System to clear
 */
void lle_completion_system_clear(lle_completion_system_t *system) {
    if (!system) {
        return;
    }

    /// Free menu first (must be freed before state since menu references
    /// result owned by state). The display_controller should have already
    /// cleared its reference before calling this function.
    if (system->menu) {
        lle_completion_menu_state_free(system->menu);
        system->menu = NULL;
    }

    if (system->current_state) {
        lle_completion_state_free(system->current_state);
        system->current_state = NULL;
    }
}

/// ============================================================================
/// HELPER FUNCTIONS
/// ============================================================================

/**
 * @brief Deduplicate completion results
 *
 * Type-aware deduplication: items with the same text but different types
 * are kept separate. This allows both builtin `echo` and external `echo`
 * to appear in completions, letting users choose which version to use.
 *
 * @param result Result set to deduplicate
 * @return LLE_SUCCESS or error code
 */
static lle_result_t deduplicate_results(lle_completion_result_t *result) {
    if (!result || result->count <= 1) {
        return LLE_SUCCESS;
    }

    size_t write_pos = 0;

    for (size_t read_pos = 0; read_pos < result->count; read_pos++) {
        const char *text = result->items[read_pos].text;
        lle_completion_type_t type = result->items[read_pos].type;

        /// Check if we've seen this text+type combination before
        bool duplicate = false;
        for (size_t check = 0; check < write_pos; check++) {
            /// Only dedupe if BOTH text AND type match
            if (strcmp(result->items[check].text, text) == 0 &&
                result->items[check].type == type) {
                duplicate = true;
                break;
            }
        }

        /// Keep items that are unique by text+type combination
        if (!duplicate) {
            if (write_pos != read_pos) {
                result->items[write_pos] = result->items[read_pos];
            }
            write_pos++;
        }
    }

    result->count = write_pos;
    return LLE_SUCCESS;
}

/**
 * @brief Compare two completion items for sorting
 * @param a First item
 * @param b Second item
 * @return Comparison result for qsort
 */
static int completion_compare(const void *a, const void *b) {
    const lle_completion_item_t *item_a = (const lle_completion_item_t *)a;
    const lle_completion_item_t *item_b = (const lle_completion_item_t *)b;

    /// Sort by relevance_score first (higher score = earlier in list)
    if (item_a->relevance_score != item_b->relevance_score) {
        return item_b->relevance_score -
               item_a->relevance_score; /// Descending order
    }

    /// Then by type for items with same score
    if (item_a->type != item_b->type) {
        return item_a->type - item_b->type;
    }

    /// Finally alphabetically within same type
    return strcmp(item_a->text, item_b->text);
}

/**
 * @brief Sort completion results by relevance and type
 * @param result Result set to sort
 * @return LLE_SUCCESS or error code
 */
static lle_result_t sort_results(lle_completion_result_t *result) {
    if (!result || result->count <= 1) {
        return LLE_SUCCESS;
    }

    qsort(result->items, result->count, sizeof(lle_completion_item_t),
          completion_compare);

    return LLE_SUCCESS;
}

/// ============================================================================
/// COMPLETION GENERATION (Spec 12 Core)
/// ============================================================================

/**
 * @brief Generate completions for buffer at cursor position
 * @param system Completion system to use
 * @param buffer Input buffer text
 * @param cursor_pos Cursor position in buffer
 * @param out_result Output pointer for results
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_completion_system_generate(lle_completion_system_t *system,
                               const char *buffer, size_t cursor_pos,
                               lle_completion_result_t **out_result) {
    if (!system || !buffer || !out_result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Step 1: Analyze the word context at the cursor.
    lle_word_context_t *context = NULL;
    lle_result_t res =
        lle_word_context_analyze(buffer, cursor_pos, system->pool, &context);
    if (res != LLE_SUCCESS) {
        return res;
    }

    /// Step 2: Create result structure
    lle_completion_result_t *result = NULL;
    res = lle_completion_result_create(system->pool, 64, &result);
    if (res != LLE_SUCCESS) {
        lle_word_context_free(context);
        return res;
    }

    /// Step 3: Query all applicable sources
    res = lle_source_manager_query(system->source_manager, context, result);
    if (res != LLE_SUCCESS) {
        lle_completion_result_free(result);
        lle_word_context_free(context);
        return res;
    }

    /// Step 4: Deduplicate results
    res = deduplicate_results(result);
    if (res != LLE_SUCCESS) {
        lle_completion_result_free(result);
        lle_word_context_free(context);
        return res;
    }

    /// Step 5: Sort results
    res = sort_results(result);
    if (res != LLE_SUCCESS) {
        lle_completion_result_free(result);
        lle_word_context_free(context);
        return res;
    }

    /// Step 6: Create and store completion state
    lle_completion_state_t *state = NULL;
    res = lle_completion_state_create(system->pool, buffer, cursor_pos, context,
                                      result, &state);
    if (res != LLE_SUCCESS) {
        lle_completion_result_free(result);
        lle_word_context_free(context);
        return res;
    }

    /// Step 7: Create menu if multiple completions (for display system)
    lle_completion_menu_state_t *menu = NULL;
    if (result->count > 1) {
        /// Create menu with default config
        lle_completion_menu_config_t menu_config = {
            .max_visible_items = 20,
            .show_category_headers = true,
            .show_type_indicators = false,
            .show_descriptions = false,
            .enable_scrolling = true,
            .min_items_for_menu = 2};

        /// Pass the dequoted word prefix so the menu's in-menu
        /// type-to-filter pass can construct the combined prefix
        /// (original_prefix + filter_string) when the user types
        /// additional characters while the menu is open.
        const char *original_prefix =
            (context && context->dequoted_filename_prefix)
                ? context->dequoted_filename_prefix
                : "";
        res = lle_completion_menu_state_create(
            system->pool, result, &menu_config, original_prefix, &menu);
        if (res != LLE_SUCCESS) {
            /// state owns result and context, so freeing state frees them too.
            /// Do NOT call result_free or context_free separately - that would
            /// be a double-free.
            lle_completion_state_free(state);
            return res;
        }
    }

    /// Clear old state and menu
    if (system->current_state) {
        lle_completion_state_free(system->current_state);
    }
    if (system->menu) {
        lle_completion_menu_state_free(system->menu);
    }

    system->current_state = state;
    system->menu = menu; /// NULL if single completion or no completions
    *out_result = result;

    return LLE_SUCCESS;
}

/// ============================================================================
/// STATE QUERIES
/// ============================================================================

/**
 * @brief Check if completion system has active state
 * @param system System to check
 * @return true if active, false otherwise
 */
bool lle_completion_system_is_active(const lle_completion_system_t *system) {
    return system && system->current_state && system->current_state->active;
}

/**
 * @brief Check if completion menu is currently visible
 * @param system System to check
 * @return true if menu visible, false otherwise
 */
bool lle_completion_system_is_menu_visible(
    const lle_completion_system_t *system) {
    return system && system->menu != NULL;
}

/**
 * @brief Get current completion state
 * @param system System to query
 * @return Current state or NULL
 */
lle_completion_state_t *
lle_completion_system_get_state(lle_completion_system_t *system) {
    return system ? system->current_state : NULL;
}

/**
 * @brief Get current completion menu state
 * @param system System to query
 * @return Menu state or NULL
 */
lle_completion_menu_state_t *
lle_completion_system_get_menu(lle_completion_system_t *system) {
    return system ? system->menu : NULL;
}
