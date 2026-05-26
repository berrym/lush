/**
 * @file node_to_source.h
 * @brief AST to source code regeneration API
 *
 * Provides functionality to convert an AST back to shell source code
 * for round-trip testing and debugging.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef NODE_TO_SOURCE_H
#define NODE_TO_SOURCE_H

#include "node.h"

/**
 * @brief Convert an AST node back to shell source code
 *
 * Regenerates shell source code from an AST. The output is normalized
 * (canonical form) - original whitespace and formatting may differ.
 *
 * @param node Root node of AST to convert
 * @return Newly allocated string with shell source, or NULL on error.
 *         Caller must free the returned string.
 */
char *node_to_source(node_t *node);

/**
 * @brief Compare two AST nodes for structural equality
 *
 * Recursively compares two AST nodes to verify they have the same
 * structure, types, and values. Used for round-trip testing.
 *
 * @param a First node
 * @param b Second node
 * @return 1 if structurally equal, 0 if different
 */
int node_equals(node_t *a, node_t *b);

#endif /// NODE_TO_SOURCE_H
