/**
 * @file dirstack.h
 * @brief Directory stack for pushd/popd functionality
 *
 * Implements a stack of directories that can be pushed to and popped from,
 * supporting the pushd, popd, and dirs builtins.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef DIRSTACK_H
#define DIRSTACK_H

#include <stdbool.h>
#include <stddef.h>

/** Maximum size of directory stack */
#define DIRSTACK_MAX 64

/**
 * @brief Initialize the directory stack
 *
 * Must be called before any other dirstack functions.
 * Initializes the stack to empty state.
 */
void dirstack_init(void);

/**
 * @brief Clean up the directory stack
 *
 * Frees all memory used by the stack.
 */
void dirstack_cleanup(void);

/**
 * @brief Push a directory onto the stack
 *
 * @param dir Directory path to push (will be copied)
 * @return 0 on success, -1 if stack is full or allocation fails
 */
int dirstack_push(const char *dir);

/**
 * @brief Pop the top directory from the stack
 *
 * @return Popped directory path (caller must free), or NULL if empty
 */
char *dirstack_pop(void);

/**
 * @brief Peek at a stack entry without removing it
 *
 * @param n Index from top (0 = top of stack)
 * @return Directory path at index, or NULL if out of range
 *         Caller must NOT free the returned pointer
 */
const char *dirstack_peek(int n);

/**
 * @brief Remove an entry from the stack
 *
 * @param n Index from top (0 = top of stack)
 * @return 0 on success, -1 if index out of range
 */
int dirstack_remove(int n);

/**
 * @brief Clear all entries from the stack
 */
void dirstack_clear(void);

/**
 * @brief Get the current size of the stack
 *
 * @return Number of entries in the stack
 */
int dirstack_size(void);

/**
 * @brief Rotate the stack
 *
 * Moves the Nth entry to the top of the stack.
 *
 * @param n Index to rotate to top (positive = from top, negative = from bottom)
 * @return 0 on success, -1 if index out of range
 */
int dirstack_rotate(int n);

/**
 * @brief Print the directory stack
 *
 * @param one_per_line If true, print each entry on its own line
 * @param show_index If true, show stack index numbers
 */
void dirstack_print(bool one_per_line, bool show_index);

/**
 * @brief Sync the DIRSTACK shell array variable
 *
 * Updates the DIRSTACK array variable to match the current stack contents.
 */
void dirstack_sync_variable(void);

#endif /// DIRSTACK_H
