/**
 * @file node.c
 * @brief AST node management
 *
 * Implements Abstract Syntax Tree (AST) node creation, manipulation,
 * and destruction for the shell parser. Nodes represent commands,
 * pipelines, redirections, and other shell constructs.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "node.h"

#include "errors.h"
#include "shell_error.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Create a new AST node
 *
 * Allocates and initializes a new node of the specified type.
 * All fields are zero-initialized.
 *
 * @param type Type of node to create
 * @return Pointer to new node (never NULL, aborts on allocation failure)
 */
node_t *new_node(node_type_t type) {
    node_t *node = NULL;

    if ((node = calloc(1, sizeof(node_t))) == NULL) {
        error_syscall("new_node");
    }

    node->type = type;
    node->loc = SOURCE_LOC_UNKNOWN;

    return node;
}

/**
 * @brief Create a new AST node with source location
 *
 * Allocates and initializes a new node of the specified type,
 * capturing the source location for error reporting.
 *
 * @param type Type of node to create
 * @param loc Source location from token
 * @return Pointer to new node (never NULL, aborts on allocation failure)
 */
node_t *new_node_at(node_type_t type, source_location_t loc) {
    node_t *node = new_node(type);
    if (node) {
        node->loc = loc;
    }
    return node;
}

/**
 * @brief Add a child node to a parent
 *
 * Appends the child to the parent's list of children. If the parent
 * already has children, the new child is added as the last sibling.
 *
 * @param parent Parent node to add child to
 * @param child Child node to add
 */
void add_child_node(node_t *parent, node_t *child) {
    if (parent == NULL || child == NULL) {
        return;
    }

    if (parent->first_child == NULL) {
        parent->first_child = child;
    } else {
        node_t *sibling = parent->first_child;

        while (sibling->next_sibling != NULL) {
            sibling = sibling->next_sibling;
        }

        sibling->next_sibling = child;
        child->prev_sibling = sibling;
    }

    parent->children++;
}

/**
 * @brief Set a node's value to a string
 *
 * Sets the node's value type to VAL_STR and stores a copy of
 * the provided string. If val is NULL, stores NULL.
 *
 * @param node Node to set value for
 * @param val String value to set (will be duplicated)
 */
void set_node_val_str(node_t *node, char *val) {
    node->val_type = VAL_STR;

    if (!val) {
        node->val.str = NULL;
    } else {
        char *val2 = strdup(val);
        if (!val2) {
            error_return("set_node_val_str");
            return;
        }
        node->val.str = val2;
    }
}

/**
 * @brief Free an AST node tree
 *
 * Recursively frees the node and all its children/siblings.
 * Also frees any string values stored in nodes.
 *
 * @param node Root of the tree to free (may be NULL)
 */
void free_node_tree(node_t *node) {
    if (!node) {
        return;
    }

    /// Free siblings of this node first (iteratively to avoid deep recursion)
    node_t *sibling = node->next_sibling;
    while (sibling) {
        node_t *next_sib = sibling->next_sibling;
        sibling->next_sibling = NULL; /// Prevent double-free
        free_node_tree(sibling);
        sibling = next_sib;
    }

    /// Free all children
    node_t *child = node->first_child;
    while (child) {
        node_t *next_child = child->next_sibling;
        child->next_sibling = NULL; /// Prevent double-free from sibling loop
        free_node_tree(child);
        child = next_child;
    }

    /// Free string value if present
    if (node->val_type == VAL_STR && node->val.str) {
        free(node->val.str);
    }

    free(node);
}
