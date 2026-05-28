/**
 * @file command_structure.c
 * @brief Command structure management for multiline parsing
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Part of Spec 22 Phase 2: Multiline Reconstruction Engine
 *
 * This module implements command structure management including creation,
 * destruction, and keyword tracking for shell command constructs.
 */

#include "lle/command_structure.h"
#include <string.h>

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

static void lle_free_nested_construct_tree(lle_nested_construct_t *construct);

/* ============================================================================
 * PUBLIC API - LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Create a new command structure instance
 * @param structure Pointer to store the created structure
 * @param memory_pool Memory pool for allocations (can be NULL for global pool)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_command_structure_create(lle_command_structure_t **structure,
                                          lle_memory_pool_t *memory_pool) {
    if (!structure) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // memory_pool can be NULL - will use global pool

    lle_command_structure_t *cmd_struct =
        lle_pool_alloc(sizeof(lle_command_structure_t));
    if (!cmd_struct) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(cmd_struct, 0, sizeof(lle_command_structure_t));
    cmd_struct->pool = memory_pool; // Can be NULL
    cmd_struct->primary_type = LLE_CONSTRUCT_SIMPLE;
    cmd_struct->is_complete = false;
    cmd_struct->has_syntax_error = false;

    *structure = cmd_struct;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a command structure and free all resources
 * @param structure The structure to destroy
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if structure is
 * NULL
 */
lle_result_t lle_command_structure_destroy(lle_command_structure_t *structure) {
    if (!structure) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Free keyword list
    lle_keyword_position_t *keyword = structure->first_keyword;
    while (keyword) {
        lle_keyword_position_t *next = keyword->next;
        lle_pool_free(keyword);
        keyword = next;
    }

    // Free indentation info
    if (structure->indentation) {
        if (structure->indentation->level_per_line) {
            lle_pool_free(structure->indentation->level_per_line);
        }
        lle_pool_free(structure->indentation);
    }

    // Free line mapping
    if (structure->line_mapping) {
        if (structure->line_mapping->line_offsets) {
            lle_pool_free(structure->line_mapping->line_offsets);
        }
        if (structure->line_mapping->line_lengths) {
            lle_pool_free(structure->line_mapping->line_lengths);
        }
        lle_pool_free(structure->line_mapping);
    }

    // Free nested construct tree (recursive)
    if (structure->root_construct) {
        lle_free_nested_construct_tree(structure->root_construct);
    }

    // Free structure itself
    lle_pool_free(structure);

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - KEYWORD MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Add a keyword position to the command structure
 * @param structure The command structure
 * @param type Type of the keyword
 * @param offset Byte offset of the keyword in the command
 * @param line_number Line number where keyword appears
 * @param indent_level Indentation level of the keyword
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_command_structure_add_keyword(lle_command_structure_t *structure,
                                  lle_keyword_type_t type, size_t offset,
                                  size_t line_number, size_t indent_level) {
    if (!structure) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate new keyword position
    lle_keyword_position_t *keyword =
        lle_pool_alloc(sizeof(lle_keyword_position_t));
    if (!keyword) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    keyword->type = type;
    keyword->offset = offset;
    keyword->line_number = line_number;
    keyword->indent_level = indent_level;
    keyword->next = NULL;

    // Add to end of list
    if (!structure->first_keyword) {
        structure->first_keyword = keyword;
    } else {
        lle_keyword_position_t *current = structure->first_keyword;
        while (current->next) {
            current = current->next;
        }
        current->next = keyword;
    }

    structure->keyword_count++;

    return LLE_SUCCESS;
}

/**
 * @brief Count keywords of a specific type in the structure
 * @param structure The command structure
 * @param type Type of keyword to count (LLE_KEYWORD_NONE counts all)
 * @param count Pointer to store the count
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_command_structure_count_keywords(lle_command_structure_t *structure,
                                     lle_keyword_type_t type, size_t *count) {
    if (!structure || !count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cnt = 0;
    lle_keyword_position_t *keyword = structure->first_keyword;

    while (keyword) {
        if (keyword->type == type) {
            cnt++;
        }
        keyword = keyword->next;
    }

    *count = cnt;
    return LLE_SUCCESS;
}

/**
 * @brief Find the matching closing keyword for an opening keyword
 * @param structure The command structure
 * @param keyword The opening keyword to find a match for
 * @param match Pointer to store the matching keyword (NULL if not found)
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if no match
 */
lle_result_t
lle_command_structure_find_matching_keyword(lle_command_structure_t *structure,
                                            lle_keyword_position_t *keyword,
                                            lle_keyword_position_t **match) {
    if (!structure || !keyword || !match) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *match = NULL;

    // Define matching pairs
    lle_keyword_type_t search_type = LLE_KEYWORD_NONE;

    switch (keyword->type) {
    case LLE_KEYWORD_DO:
        search_type = LLE_KEYWORD_DONE;
        break;
    case LLE_KEYWORD_IF:
        search_type = LLE_KEYWORD_FI;
        break;
    case LLE_KEYWORD_THEN:
        search_type = LLE_KEYWORD_FI;
        break;
    case LLE_KEYWORD_CASE:
        search_type = LLE_KEYWORD_ESAC;
        break;
    case LLE_KEYWORD_OPEN_BRACE:
        search_type = LLE_KEYWORD_CLOSE_BRACE;
        break;
    case LLE_KEYWORD_OPEN_PAREN:
        search_type = LLE_KEYWORD_CLOSE_PAREN;
        break;
    default:
        return LLE_ERROR_NOT_FOUND;
    }

    // Search forward for matching keyword at same or lower indent
    lle_keyword_position_t *current = keyword->next;
    int depth = 0;

    while (current) {
        if (current->type == keyword->type) {
            depth++;
        } else if (current->type == search_type) {
            if (depth == 0) {
                *match = current;
                return LLE_SUCCESS;
            }
            depth--;
        }
        current = current->next;
    }

    return LLE_ERROR_NOT_FOUND;
}

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Recursively free a nested construct tree
 * @param construct The root construct to free
 *
 * Frees all children before freeing the construct itself.
 */
static void lle_free_nested_construct_tree(lle_nested_construct_t *construct) {
    if (!construct) {
        return;
    }

    // Free children first
    lle_nested_construct_t *child = construct->first_child;
    while (child) {
        lle_nested_construct_t *next_sibling = child->next_sibling;
        lle_free_nested_construct_tree(child);
        child = next_sibling;
    }

    // Free this construct
    lle_pool_free(construct);
}
