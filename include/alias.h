/**
 * @file alias.h
 * @brief Shell alias management declarations
 *
 * Provides alias creation, lookup, expansion, and validation functions
 * for shell command aliasing.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef ALIAS_H
#define ALIAS_H

#include "ht.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Global alias hash table
 */
extern ht_strstr_t *aliases;

/**
 * @brief Initialize the alias subsystem
 *
 * Creates the alias hash table and sets up initial state.
 * Must be called during shell initialization.
 */
void init_aliases(void);

/**
 * @brief Free all alias resources
 *
 * Destroys the alias hash table and frees all memory.
 * Should be called during shell cleanup.
 */
void free_aliases(void);

/**
 * @brief Look up an alias by name
 *
 * @param name Alias name to look up
 * @return Alias value string, or NULL if not found
 */
char *lookup_alias(const char *name);

/**
 * @brief Look up a zsh global alias by name
 *
 * Global aliases (defined via `alias -g NAME=value`) expand anywhere
 * on the command line, not just at command position. The expansion
 * happens at executor argv-build time -- each non-command argv slot
 * is checked against the global table and substituted in place. Per
 * the polyglot stance: lush's substitution is text-only (the
 * substituted value occupies one argv slot), matching the common
 * directory-shortcut use case (`alias -g ...='../..'`); zsh's
 * structural-operator case (`alias -g G='| grep'` introducing a
 * real pipeline) is not supported because lush would need to
 * re-tokenize the line after substitution. Documented in
 * docs/CONFIGURATION.md.
 *
 * @param name Alias name to look up
 * @return Alias value string, or NULL if not a global alias
 */
char *lookup_global_alias(const char *name);

/**
 * @brief Define or update a global alias (zsh -g semantics).
 *
 * Stored in a separate table from regular (command-position) aliases.
 * If a regular alias of the same name exists, it remains; lookup at
 * command position consults regular aliases first, at non-command
 * positions consults global aliases.
 *
 * @param name Alias name
 * @param value Alias expansion value
 * @return true on success
 */
bool set_global_alias(const char *name, const char *value);

/**
 * @brief Print all defined aliases
 *
 * Displays all aliases in "name='value'" format to stdout.
 */
void print_aliases(void);

/**
 * @brief Define or update an alias
 *
 * @param name Alias name to set
 * @param value Alias expansion value
 * @return true on success, false on failure
 */
bool set_alias(const char *name, const char *value);

/**
 * @brief Remove an alias definition
 *
 * @param name Alias name to unset
 */
void unset_alias(const char *name);

/**
 * @brief Check if a string is a valid alias name
 *
 * Valid alias names contain only alphanumeric characters,
 * underscores, and cannot start with a digit.
 *
 * @param name String to validate
 * @return true if valid alias name, false otherwise
 */
bool valid_alias_name(const char *name);

/**
 * @brief Print usage information for the alias builtin
 */
void alias_usage(void);

/**
 * @brief Print usage information for the unalias builtin
 */
void unalias_usage(void);

/**
 * @brief Recursively expand aliases up to a maximum depth
 *
 * Expands nested aliases, detecting and preventing infinite loops.
 *
 * @param name Alias name to expand
 * @param max_depth Maximum recursion depth
 * @return Fully expanded string (caller must free), or NULL on error
 */
char *expand_aliases_recursive(const char *name, int max_depth);

/**
 * @brief Expand alias for the first word of a command
 *
 * Only expands the first word if it is an alias, leaving
 * the rest of the command unchanged.
 *
 * @param command Command string to process
 * @return Expanded command (caller must free), or NULL on error
 */
char *expand_first_word_alias(const char *command);

/**
 * @brief Expand alias handling shell operators in the value
 *
 * Properly handles alias values containing shell operators
 * like pipes, redirections, and command separators.
 *
 * @param command Command string to process
 * @return Expanded command (caller must free), or NULL on error
 */
char *expand_alias_with_shell_operators(const char *command);

/**
 * @brief Check if a string contains shell operators
 *
 * Detects pipes (|), redirections (<, >), semicolons (;),
 * and other shell metacharacters.
 *
 * @param value String to check
 * @return true if contains shell operators, false otherwise
 */
bool contains_shell_operators(const char *value);

/**
 * @brief Check if a character is special in alias context
 *
 * @param c Character to check
 * @return true if character has special meaning in aliases
 */
bool is_special_alias_char(char c);

#endif
