/**
 * @file expand.c
 * @brief Variable and parameter expansion
 *
 * Implements shell expansion including:
 * - Variable expansion ($VAR, ${VAR})
 * - Alias expansion
 * - Word list management
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "expand.h"

#include "alias.h"
#include "lush.h"

#include <ctype.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @brief Initialize expansion context with default values
 *
 * @param ctx Expansion context to initialize
 * @param mode Expansion mode flags
 */
void expand_ctx_init(expand_ctx_t *ctx, int mode) {
    if (ctx) {
        ctx->mode = mode;
        ctx->in_quotes = false;
        ctx->in_backticks = false;
    }
}

/**
 * @brief Check if a specific expansion mode is enabled
 *
 * @param ctx Expansion context to check
 * @param mode_flag Mode flag to test
 * @return true if mode is enabled, false otherwise
 */
bool expand_ctx_check(expand_ctx_t *ctx, int mode_flag) {
    if (!ctx) {
        return false;
    }
    return (ctx->mode & mode_flag) != 0;
}

/**
 * @brief Create a new word from a string
 *
 * Allocates and initializes a word structure containing a copy
 * of the provided string.
 *
 * @param str String to create word from
 * @return Pointer to new word, or NULL on failure
 */
word_t *word_create(const char *str) {
    if (!str) {
        return NULL;
    }

    /// Allocate word structure
    word_t *word = calloc(1, sizeof(word_t));
    if (!word) {
        return NULL;
    }

    /// Allocate and copy the word text
    size_t len = strlen(str);
    word->data = calloc(len + 1, sizeof(char));
    if (!word->data) {
        free(word);
        return NULL;
    }

    strcpy(word->data, str);
    word->len = len;
    word->next = NULL;

    return word;
}

/**
 * @brief Free a word list
 *
 * Frees all words in the linked list and their associated data.
 *
 * @param head Head of the word list to free
 */
void word_free_list(word_t *head) {
    while (head) {
        word_t *next = head->next;
        free(head->data);
        free(head);
        head = next;
    }
}

/**
 * @brief Convert a word list to a string
 *
 * Concatenates all words in the list into a single string,
 * separated by spaces.
 *
 * @param head Head of the word list
 * @return Newly allocated string, or NULL on failure (caller must free)
 */
char *word_list_to_string(word_t *head) {
    if (!head) {
        return NULL;
    }

    /// Calculate total length
    size_t total_len = 0;
    for (word_t *w = head; w; w = w->next) {
        total_len += w->len + 1; /// +1 for space or null
    }

    /// Allocate and build string
    char *result = calloc(total_len + 1, sizeof(char));
    if (!result) {
        return NULL;
    }

    char *p = result;
    for (word_t *w = head; w; w = w->next) {
        strcpy(p, w->data);
        p += w->len;
        if (w->next) {
            *p++ = ' ';
        }
    }

    return result;
}

/**
 * @brief Expand an alias recursively
 *
 * Looks up an alias and returns its expanded value. If the alias
 * value itself contains aliases, they are expanded recursively.
 *
 * @param alias_name Name of the alias to expand
 * @return Expanded alias value, or NULL if not found (caller must free)
 */
char *expand_alias_recursive(const char *alias_name) {
    if (!alias_name) {
        return NULL;
    }

    /// Look up the initial alias
    char *alias_value = lookup_alias(alias_name);
    if (!alias_value) {
        return NULL;
    }

    /// Make a copy we can modify
    char *result = strdup(alias_value);
    if (!result) {
        return NULL;
    }

    /// Recursively expand any further aliases
    /// This is a simplified version - real implementation would need
    /// to handle word tokenization and prevent infinite recursion

    return result;
}
