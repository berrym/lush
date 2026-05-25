/**
 * @file alias.c
 * @brief Shell alias management and expansion
 *
 * Implements alias creation, lookup, expansion, and management for the shell.
 * Uses a hash table for efficient alias storage and retrieval.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "alias.h"

#include "builtins.h"
#include "errors.h"
#include "executor.h"
#include "ht.h"
#include "shell_error.h"
#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ht_strstr_t *aliases = NULL; // alias hash table
ht_enum_t *aliases_e = NULL; // alias enumeration object

/**
 * @brief Initialize the aliases hash table
 *
 * Creates the alias hash table if not already initialized and sets up
 * some default navigation and listing aliases for convenience.
 */
void init_aliases(void) {
    if (aliases == NULL) {
        aliases = ht_strstr_create(HT_STR_CASECMP | HT_SEED_RANDOM);
    }

    // set some example aliases
    set_alias("..", "cd ../");
    set_alias("...", "cd ../../");
    set_alias("l", "ls --color=auto");
    set_alias("la", "ls -a --color=force");
    set_alias("ll", "ls -alF --color=force");
    set_alias("ls", "ls --color=force");
}

/**
 * @brief Free the alias hash table
 *
 * Destroys the alias hash table and releases all associated memory.
 * Sets the global aliases pointer to NULL after destruction.
 */
void free_aliases(void) {
    if (aliases) {
        ht_strstr_destroy(aliases);
        aliases = NULL;
    }
}

/**
 * @brief Look up an alias by name
 *
 * Searches the alias hash table for the given key and returns
 * the associated value if found.
 *
 * @param key The alias name to look up
 * @return Pointer to the alias value string, or NULL if not found
 */
char *lookup_alias(const char *key) {
    if (!aliases || !key) {
        return NULL;
    }
    const char *val = ht_strstr_get(aliases, key);
    return (char *)val;
}

/**
 * @brief Print all defined aliases
 *
 * Iterates through the alias hash table and prints each alias
 * in POSIX format: alias name='value'
 */
void print_aliases(void) {
    const char *k = NULL, *v = NULL;
    aliases_e = ht_strstr_enum_create(aliases);

    while (ht_strstr_enum_next(aliases_e, &k, &v)) {
        // POSIX format: alias name='value'
        printf("alias %s='%s'\n", k, v);
    }
    ht_strstr_enum_destroy(aliases_e);
}

/**
 * @brief Set or update an alias
 *
 * Inserts a new alias or updates an existing one in the hash table.
 *
 * @param key The alias name
 * @param val The command string the alias expands to
 * @return true if the alias was successfully set, false on error
 */
bool set_alias(const char *key, const char *val) {
    if (!aliases || !key || !val) {
        return false;
    }

    ht_strstr_insert(aliases, key, val);
    char *alias = lookup_alias(key);
    return (alias != NULL);
}

/**
 * @brief Remove an alias
 *
 * Deletes the specified alias from the hash table if it exists.
 *
 * @param key The alias name to remove
 */
void unset_alias(const char *key) {
    if (aliases && key) {
        ht_strstr_remove(aliases, key);
    }
}

/**
 * @brief Check if a character is valid in an alias name
 *
 * Validates that a character is allowed in alias names per POSIX.
 * POSIX allows alphanumeric characters and underscore. This
 * implementation also permits common extensions like '.', '-', and '+'.
 *
 * @param c The character to validate
 * @return true if the character is valid, false otherwise
 */
bool valid_alias_name_char(char c) {
    // POSIX base: alphanumeric and underscore
    if (isalnum((unsigned char)c) || c == '_') {
        return true;
    }

    // Common extensions in practice
    switch (c) {
    case '.': // Often used for navigation aliases
    case '-': // Common in command names
    case '+': // Sometimes used
        return true;
    default:
        return false;
    }
}

/**
 * @brief Skip leading whitespace in a string
 *
 * Advances the pointer past any leading whitespace characters.
 *
 * @param str The string to process
 * @return Pointer to the first non-whitespace character, or NULL if str is NULL
 */
static const char *skip_whitespace(const char *str) {
    if (!str) {
        return NULL;
    }
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

/**
 * @brief Validate an alias name
 *
 * Checks that an alias name consists entirely of valid characters
 * and follows POSIX naming rules (first character cannot be a digit).
 *
 * @param key The alias name to validate
 * @return true if the name is valid, false otherwise
 */
bool valid_alias_name(const char *key) {
    if (!key || !*key) {
        return false;
    }

    // Check for empty string after trimming whitespace
    const char *trimmed = skip_whitespace(key);
    if (!trimmed || !*trimmed) {
        return false;
    }

    // POSIX bans digit-first alias names but both bash and zsh permit them
    // (e.g. zsh's directory aliases `alias 1='cd +1' ... 9='cd +9'`). Match
    // the consensus: accept digit-first names too.

    const char *p = trimmed;
    while (*p && !isspace((unsigned char)*p)) {
        if (!valid_alias_name_char(*p)) {
            return false;
        }
        p++;
    }

    // Make sure we processed at least one character
    return (p > trimmed);
}

/**
 * @brief Print alias command usage information
 *
 * Displays usage instructions for the alias builtin command.
 */
void alias_usage(void) {
    fprintf(stderr, "usage: alias [name[=value] ...]\n"
                    "       alias [name ...]\n"
                    "\n"
                    "Define or display aliases.\n"
                    "\n"
                    "Without arguments, print all aliases.\n"
                    "With name arguments, print the aliases for those names.\n"
                    "With name=value arguments, define aliases.\n");
}

/**
 * @brief Print unalias command usage information
 *
 * Displays usage instructions for the unalias builtin command.
 */
void unalias_usage(void) {
    fprintf(stderr, "usage: unalias [-a] name [name ...]\n"
                    "\n"
                    "Remove alias definitions.\n"
                    "\n"
                    "  -a    remove all alias definitions\n");
}

/**
 * @brief Find the first unquoted equals sign in a string
 *
 * Searches for an equals sign that is not inside single or double quotes,
 * properly handling escape sequences.
 *
 * @param str The string to search
 * @return Pointer to the equals sign, or NULL if not found
 */
static const char *find_equals(const char *str) {
    if (!str) {
        return NULL;
    }

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for (const char *p = str; *p; p++) {
        if (escaped) {
            escaped = false;
            continue;
        }

        if (*p == '\\' && !in_single_quote) {
            escaped = true;
            continue;
        }

        if (*p == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }

        if (*p == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }

        if (*p == '=' && !in_single_quote && !in_double_quote) {
            return p;
        }
    }

    return NULL;
}

/**
 * @brief Parse an alias assignment string
 *
 * Parses an assignment in the form name=value, handling quoted values
 * and escape sequences in double-quoted strings.
 *
 * @param assignment The assignment string to parse (e.g., "ll='ls -l'")
 * @param name Output pointer for the alias name (newly allocated, caller must
 * free)
 * @param value Output pointer for the alias value (newly allocated, caller must
 * free)
 * @return true on success, false on failure (no equals sign or allocation
 * error)
 */
static bool parse_alias_assignment(const char *assignment, char **name,
                                   char **value) {
    if (!assignment || !name || !value) {
        return false;
    }

    *name = NULL;
    *value = NULL;

    const char *equals = find_equals(assignment);
    if (!equals) {
        return false;
    }

    // Extract name part
    size_t name_len = equals - assignment;
    const char *name_start = skip_whitespace(assignment);
    const char *name_end = equals;

    // Trim trailing whitespace from name
    while (name_end > name_start && isspace((unsigned char)*(name_end - 1))) {
        name_end--;
    }

    name_len = name_end - name_start;
    if (name_len == 0) {
        return false;
    }

    *name = malloc(name_len + 1);
    if (!*name) {
        return false;
    }
    strncpy(*name, name_start, name_len);
    (*name)[name_len] = '\0';

    // Extract value part
    const char *value_start = skip_whitespace(equals + 1);
    const char *value_end = value_start + strlen(value_start);

    // Trim trailing whitespace from value
    while (value_end > value_start &&
           isspace((unsigned char)*(value_end - 1))) {
        value_end--;
    }

    // Handle quoted values
    if (value_end > value_start) {
        char quote_char = 0;
        if ((*value_start == '\'' || *value_start == '"') &&
            *(value_end - 1) == *value_start) {
            quote_char = *value_start;
            value_start++;
            value_end--;
        }

        size_t value_len = value_end - value_start;
        *value = malloc(value_len + 1);
        if (!*value) {
            free(*name);
            *name = NULL;
            return false;
        }

        // Copy value, handling escape sequences if in double quotes
        size_t j = 0;
        for (const char *p = value_start; p < value_end; p++) {
            if (quote_char == '"' && *p == '\\' && p + 1 < value_end) {
                // Handle common escape sequences in double quotes
                switch (*(p + 1)) {
                case 'n':
                    (*value)[j++] = '\n';
                    p++;
                    break;
                case 't':
                    (*value)[j++] = '\t';
                    p++;
                    break;
                case 'r':
                    (*value)[j++] = '\r';
                    p++;
                    break;
                case '\\':
                    (*value)[j++] = '\\';
                    p++;
                    break;
                case '"':
                    (*value)[j++] = '"';
                    p++;
                    break;
                case '$':
                    (*value)[j++] = '$';
                    p++;
                    break;
                default:
                    (*value)[j++] = *p;
                    break;
                }
            } else {
                (*value)[j++] = *p;
            }
        }
        (*value)[j] = '\0';
    } else {
        // Empty value
        *value = malloc(1);
        if (!*value) {
            free(*name);
            *name = NULL;
            return false;
        }
        (*value)[0] = '\0';
    }

    return true;
}

/**
 * @brief Expand an alias recursively with cycle detection
 *
 * Expands an alias and recursively expands the first word of the
 * result if it is also an alias. Uses depth limiting to prevent
 * infinite loops from circular alias definitions.
 *
 * @param name The alias name to expand
 * @param max_depth Maximum recursion depth to prevent infinite loops
 * @return Newly allocated string with expanded value (caller must free),
 *         or NULL if alias doesn't exist or max_depth reached
 */
char *expand_aliases_recursive(const char *name, int max_depth) {
    if (!name || max_depth <= 0) {
        return NULL;
    }

    // Look up the initial alias
    char *value = lookup_alias(name);
    if (!value) {
        return NULL;
    }

    // Make a copy we can work with
    char *result = strdup(value);
    if (!result) {
        return NULL;
    }

    // Use tokenizer to properly parse the alias value
    tokenizer_t *tokenizer = tokenizer_new(result);
    if (!tokenizer) {
        free(result);
        return NULL;
    }

    // Get the first token to check for recursive expansion
    token_t *first_token = tokenizer_current(tokenizer);
    if (!first_token || first_token->type != TOK_WORD) {
        tokenizer_free(tokenizer);
        return result; // Return as-is if no valid word token
    }

    // Check if the first word is also an alias
    char *recursive =
        expand_aliases_recursive(first_token->text, max_depth - 1);
    if (recursive) {
        // Build new command with expanded first word
        size_t recursive_len = strlen(recursive);
        size_t remaining_len = strlen(result) - strlen(first_token->text);
        char *new_result = malloc(recursive_len + remaining_len + 2);

        if (new_result) {
            strcpy(new_result, recursive);

            // Add the rest of the original command after the first word
            const char *rest = result + strlen(first_token->text);
            if (*rest) {
                strcat(new_result, rest);
            }

            free(result);
            free(recursive);
            tokenizer_free(tokenizer);
            return new_result;
        }
        free(recursive);
    }

    tokenizer_free(tokenizer);
    return result;
}

/**
 * @brief Expand only the first word of a command as an alias
 *
 * Matches POSIX behavior where only the first word of a simple command
 * can be expanded as an alias. Uses the tokenizer for proper word
 * boundary detection.
 *
 * @param command The command line to process
 * @return Newly allocated string with the first word expanded (caller must
 * free), or a copy of the original command if no alias found
 */
char *expand_first_word_alias(const char *command) {
    if (!command) {
        return NULL;
    }

    // Use tokenizer to properly identify the first word
    tokenizer_t *tokenizer = tokenizer_new(command);
    if (!tokenizer) {
        return strdup(command); // Return original on tokenizer failure
    }

    token_t *first_token = tokenizer_current(tokenizer);
    if (!first_token || first_token->type != TOK_WORD) {
        tokenizer_free(tokenizer);
        return strdup(command); // Return original if no word token
    }

    // Try to expand the first word as an alias
    char *alias_value = lookup_alias(first_token->text);
    if (!alias_value) {
        tokenizer_free(tokenizer);
        return strdup(command); // No alias found, return original
    }

    // Construct the new command with the alias expansion
    const char *rest_of_command = command + strlen(first_token->text);
    size_t alias_len = strlen(alias_value);
    size_t rest_len = strlen(rest_of_command);
    char *result = malloc(alias_len + rest_len + 1);

    if (result) {
        strcpy(result, alias_value);
        strcat(result, rest_of_command);
    }

    tokenizer_free(tokenizer);
    return result ? result : strdup(command);
}

/**
 * @brief Check if a character is a special alias character
 *
 * Determines if a character is allowed in alias names but not in
 * variable names. POSIX permits some special characters in alias
 * names that are not valid in shell variable identifiers.
 *
 * @param c The character to check
 * @return true if the character is valid for alias names, false otherwise
 */
bool is_special_alias_char(char c) { return valid_alias_name_char(c); }

/**
 * @brief Check if an alias value contains shell operators
 *
 * Detects if an alias expansion contains shell operators that require
 * re-parsing, such as pipes, redirections, logical operators, command
 * separators, subshells, and command substitutions.
 *
 * @param value The alias value string to check
 * @return true if shell operators are present, false otherwise
 */
bool contains_shell_operators(const char *value) {
    if (!value) {
        return false;
    }

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for (const char *p = value; *p; p++) {
        if (escaped) {
            escaped = false;
            continue;
        }

        if (*p == '\\' && !in_single_quote) {
            escaped = true;
            continue;
        }

        if (*p == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }

        if (*p == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }

        // Check for shell operators outside of quotes
        if (!in_single_quote && !in_double_quote) {
            switch (*p) {
            case '|': // Pipe
                if (*(p + 1) == '|') {
                    return true; // Logical OR ||
                }
                return true; // Pipe |
            case '&':        // Background or logical AND
                if (*(p + 1) == '&') {
                    return true; // Logical AND &&
                }
                return true; // Background &
            case '>':        // Redirect out
            case '<':        // Redirect in
                return true;
            case ';': // Command separator
                return true;
            case '(': // Subshell
            case ')':
                return true;
            case '{': // Command group
            case '}':
                return true;
            case '`': // Command substitution
                return true;
            case '$': // Variable expansion or command substitution
                if (*(p + 1) == '(') {
                    return true; // Command substitution $(...)
                }
                break;
            }
        }
    }

    return false;
}

/**
 * @brief Expand an alias that may contain shell operators
 *
 * Enhanced alias expansion that handles aliases containing shell operators
 * (pipes, redirections, etc.) which require the result to be re-parsed.
 *
 * @param command The command line to process
 * @return Newly allocated string with expanded command (caller must free),
 *         or NULL if no alias expansion is needed
 */
char *expand_alias_with_shell_operators(const char *command) {
    if (!command) {
        return NULL;
    }

    // Use tokenizer to get the first word
    tokenizer_t *tokenizer = tokenizer_new(command);
    if (!tokenizer) {
        return NULL;
    }

    token_t *first_token = tokenizer_current(tokenizer);
    if (!first_token || first_token->type != TOK_WORD) {
        tokenizer_free(tokenizer);
        return NULL;
    }

    // Check if first word is an alias
    char *alias_value = lookup_alias(first_token->text);
    if (!alias_value) {
        tokenizer_free(tokenizer);
        return NULL;
    }

    // Get the rest of the command after the first word
    const char *rest_of_command = command + strlen(first_token->text);

    // Skip whitespace after first word
    while (*rest_of_command && isspace((unsigned char)*rest_of_command)) {
        rest_of_command++;
    }

    // Build the expanded command
    size_t alias_len = strlen(alias_value);
    size_t rest_len = strlen(rest_of_command);
    size_t total_len = alias_len + (rest_len > 0 ? rest_len + 1 : 0) + 1;

    char *expanded = malloc(total_len);
    if (!expanded) {
        tokenizer_free(tokenizer);
        return NULL;
    }

    strcpy(expanded, alias_value);
    if (rest_len > 0) {
        strcat(expanded, " ");
        strcat(expanded, rest_of_command);
    }

    tokenizer_free(tokenizer);
    return expanded;
}

/**
 * @brief Builtin alias command implementation
 *
 * Creates or displays aliases. With no arguments, prints all defined aliases.
 * With name arguments, prints those specific aliases. With name=value
 * arguments, defines new aliases. POSIX-compliant implementation.
 *
 * @param argc Argument count
 * @param argv Argument vector with alias names or assignments
 * @return 0 on success, 1 if any alias operation failed
 */
int bin_alias(int argc, char **argv) {
    // No arguments: print all aliases
    if (argc == 1) {
        print_aliases();
        return 0;
    }

    int exit_status = 0;

    // `--` ends option parsing; subsequent tokens are alias names or
    // assignments even if they begin with `-`. Both bash and zsh accept
    // this. Lush has no `-` flags for `alias` today, but the convention
    // is still required so scripts like `alias -- -='cd -'` parse — the
    // bare `-` would otherwise be flagged as a not-found lookup target.
    int start = 1;
    if (argc >= 2 && strcmp(argv[1], "--") == 0) {
        start = 2;
        if (argc == 2) {
            // `alias --` alone is equivalent to `alias` with no args.
            print_aliases();
            return 0;
        }
    }

    // Process each argument
    for (int i = start; i < argc; i++) {
        char *name = NULL;
        char *value = NULL;

        // Try to parse as assignment (name=value)
        if (parse_alias_assignment(argv[i], &name, &value)) {
            // Check if name is valid
            if (!valid_alias_name(name)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "invalid alias name: %s", name);
                free(name);
                free(value);
                exit_status = 1;
                continue;
            }

            // Can't alias builtin commands or keywords
            if (is_builtin(name)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "cannot alias shell keyword: %s", name);
                free(name);
                free(value);
                exit_status = 1;
                continue;
            }

            // Set the alias
            if (!set_alias(name, value)) {
                executor_error_report(current_executor, SHELL_ERR_ALIAS_ERROR,
                                      builtin_get_source_location(),
                                      "failed to create alias: %s", name);
                exit_status = 1;
            }

            free(name);
            free(value);
        } else {
            // Not an assignment, treat as name lookup
            char *alias_value = lookup_alias(argv[i]);
            if (alias_value) {
                printf("alias %s='%s'\n", argv[i], alias_value);
            } else {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "%s: not found", argv[i]);
                exit_status = 1;
            }
        }
    }

    return exit_status;
}

/**
 * @brief Builtin unalias command implementation
 *
 * Removes alias definitions. With -a option, removes all aliases.
 * Otherwise removes the specified alias names. POSIX-compliant implementation.
 *
 * @param argc Argument count
 * @param argv Argument vector with -a option or alias names to remove
 * @return 0 on success, 1 if any alias was not found or on usage error
 */
int bin_unalias(int argc, char **argv) {
    if (argc < 2) {
        unalias_usage();
        return 1;
    }

    int exit_status = 0;

    // Handle -a option (remove all aliases)
    if (argc == 2 && strcmp(argv[1], "-a") == 0) {
        if (aliases) {
            ht_strstr_destroy(aliases);
            aliases = ht_strstr_create(HT_STR_CASECMP | HT_SEED_RANDOM);
        }
        return 0;
    }

    // Process each name argument
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(SHELL_ERR_INVALID_OPTION,
                                                    SHELL_SEVERITY_ERROR, loc,
                                                    "-a must be used alone");
            if (err) {
                if (current_executor && SOURCE_LOC_VALID(loc)) {
                    char *src_line =
                        executor_get_source_line(current_executor, loc.line);
                    if (src_line) {
                        shell_error_set_source_line(err, src_line, loc.column,
                                                    loc.column + loc.length);
                        free(src_line);
                    }
                }
                if (current_executor) {
                    for (size_t k = 0; k < current_executor->context_depth &&
                                       k < SHELL_ERROR_CONTEXT_MAX;
                         k++) {
                        if (current_executor->context_stack[k]) {
                            shell_error_push_context(
                                err, "%s", current_executor->context_stack[k]);
                        }
                    }
                }
                shell_error_set_suggestion(
                    err,
                    "use 'unalias -a' to remove all aliases, or list names "
                    "without -a");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr, "lush: unalias: -a must be used alone\n");
            }
            exit_status = 1;
            continue;
        }

        if (lookup_alias(argv[i])) {
            unset_alias(argv[i]);
        } else {
            executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                                  builtin_get_source_location(),
                                  "%s: not found", argv[i]);
            exit_status = 1;
        }
    }

    return exit_status;
}
