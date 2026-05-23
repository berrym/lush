/**
 * @file compat.h
 * @brief Shell compatibility database system
 *
 * Provides a database of behavioral differences between POSIX sh, Bash, Zsh,
 * and Lush shells. This database powers the static analyzer's portability
 * checking, allowing scripts to be verified for cross-shell compatibility.
 *
 * The compatibility system integrates with the shell mode system (shell_mode.h)
 * and debug analysis system (debug/debug_analysis.h) to provide comprehensive
 * portability warnings.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef COMPAT_H
#define COMPAT_H

#include "shell_mode.h"

#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Compatibility Entry Types
 * ============================================================================
 */

/**
 * @brief Compatibility entry categories
 *
 * Categories for organizing compatibility entries by the type of shell
 * construct they describe.
 */
typedef enum {
    COMPAT_CATEGORY_BUILTIN,   /**< Built-in command differences (echo, test) */
    COMPAT_CATEGORY_EXPANSION, /**< Variable/parameter expansion differences */
    COMPAT_CATEGORY_QUOTING,   /**< Quoting and escaping differences */
    COMPAT_CATEGORY_SYNTAX,    /**< Syntax differences (arrays, functions) */
    COMPAT_CATEGORY_COUNT      /**< Number of categories (for array sizing) */
} compat_category_t;

/**
 * @brief Lint severity levels
 *
 * Severity levels for compatibility warnings, used by the analyzer to
 * determine how to report issues.
 */
typedef enum {
    COMPAT_SEVERITY_INFO,    /**< Informational - no action needed */
    COMPAT_SEVERITY_WARNING, /**< May cause portability issues */
    COMPAT_SEVERITY_ERROR,   /**< Will definitely fail in target shell */
    COMPAT_SEVERITY_COUNT    /**< Number of severity levels */
} compat_severity_t;

/**
 * @brief Fix type classification for auto-fix linter
 *
 * Classifies fixes by their safety level, controlling whether they are
 * applied automatically by the linter.
 */
typedef enum {
    FIX_TYPE_NONE,   /**< No automatic fix available */
    FIX_TYPE_SAFE,   /**< Safe to apply automatically (--fix) */
    FIX_TYPE_UNSAFE, /**< Risky fix, requires --unsafe-fixes */
    FIX_TYPE_MANUAL, /**< Cannot be auto-fixed, requires manual rewrite */
} fix_type_t;

/**
 * @brief Shell behavior description
 *
 * Describes how a specific shell handles a particular construct.
 */
typedef struct {
    const char *posix; /**< POSIX sh behavior */
    const char *bash;  /**< Bash behavior */
    const char *zsh;   /**< Zsh behavior */
    const char *lush;  /**< Lush behavior */
} compat_behavior_t;

/**
 * @brief Per-shell fix classification
 *
 * A fix may be safe for one target shell but unsafe for another.
 * For example, `echo -e` -> `printf` is safe when targeting POSIX
 * (since echo -e doesn't exist), but unsafe when targeting bash
 * (since the behavior differs slightly).
 */
typedef struct {
    fix_type_t posix; /**< Fix safety when targeting POSIX */
    fix_type_t bash;  /**< Fix safety when targeting Bash */
    fix_type_t zsh;   /**< Fix safety when targeting Zsh */
    fix_type_t lush;  /**< Fix safety when targeting Lush */
} compat_fix_class_t;

/**
 * @brief Lint configuration for a compatibility entry
 *
 * Defines how the analyzer should detect and report this compatibility
 * issue, including optional auto-fix configuration.
 */
typedef struct {
    compat_severity_t severity; /**< Severity level */
    const char *message;        /**< Message shown to user */
    const char *suggestion;     /**< Suggested fix (optional) */
    const char *pattern;        /**< Regex pattern to detect (optional) */
    compat_fix_class_t fix;     /**< Per-shell fix classification */
    const char *replacement;    /**< Replacement pattern for auto-fix */
} compat_lint_t;

/**
 * @brief Compatibility database entry
 *
 * A single entry in the compatibility database, describing a behavioral
 * difference between shells.
 */
typedef struct {
    const char *id;             /**< Unique identifier */
    compat_category_t category; /**< Category of this entry */
    const char *feature;        /**< Feature name (e.g., "echo", "arrays") */
    const char *description;    /**< Human-readable description */
    compat_behavior_t behavior; /**< Per-shell behavior descriptions */
    compat_lint_t lint;         /**< Lint configuration */
} compat_entry_t;

/**
 * @brief Compatibility check result
 *
 * Result of checking a construct for compatibility with a target shell.
 */
typedef struct {
    bool is_portable;            /**< True if construct is portable to target */
    const compat_entry_t *entry; /**< Entry that matched (if not portable) */
    shell_mode_t target;         /**< Target shell that was checked against */
    int line;                    /**< Line number where issue was found */
    int column;                  /**< Column number where issue was found */
} compat_result_t;

/* ============================================================================
 * Database Management
 * ============================================================================
 */

/**
 * @brief Initialize the compatibility database
 *
 * Loads all compatibility data from TOML files in the data directory.
 * Should be called early in shell initialization.
 *
 * @param data_dir Path to data directory (NULL for default)
 * @return 0 on success, -1 on error
 */
int compat_init(const char *data_dir);

/**
 * @brief Clean up the compatibility database
 *
 * Frees all resources associated with the compatibility database.
 */
void compat_cleanup(void);

/**
 * @brief Reload the compatibility database
 *
 * Reloads all compatibility data from disk. Useful if TOML files have
 * been modified.
 *
 * @return 0 on success, -1 on error
 */
int compat_reload(void);

/* ============================================================================
 * Entry Query Functions
 * ============================================================================
 */

/**
 * @brief Get a compatibility entry by ID
 *
 * @param id Entry ID to look up
 * @return Pointer to entry, or NULL if not found
 */
const compat_entry_t *compat_get_entry(const char *id);

/**
 * @brief Get all entries for a category
 *
 * @param category Category to query
 * @param entries Output array (caller provides)
 * @param max_entries Maximum entries to return
 * @return Number of entries returned
 */
size_t compat_get_by_category(compat_category_t category,
                              const compat_entry_t **entries,
                              size_t max_entries);

/**
 * @brief Get all entries for a feature
 *
 * @param feature Feature name to query
 * @param entries Output array (caller provides)
 * @param max_entries Maximum entries to return
 * @return Number of entries returned
 */
size_t compat_get_by_feature(const char *feature,
                             const compat_entry_t **entries,
                             size_t max_entries);

/**
 * @brief Get the first entry matching a feature name
 *
 * Returns the first database entry that matches the given feature name.
 * Useful when you only need one representative entry for a feature.
 *
 * @param feature Feature name to query
 * @return Pointer to entry, or NULL if not found
 */
const compat_entry_t *compat_get_first_by_feature(const char *feature);

/**
 * @brief Get total number of entries in database
 *
 * @return Number of entries
 */
size_t compat_get_entry_count(void);

/**
 * @brief Iterate over all entries
 *
 * Calls the callback function for each entry in the database.
 *
 * @param callback Function to call for each entry
 * @param user_data User data passed to callback
 */
void compat_foreach_entry(void (*callback)(const compat_entry_t *entry,
                                           void *user_data),
                          void *user_data);

/* ============================================================================
 * Portability Checking
 * ============================================================================
 */

/**
 * @brief Check if a construct is portable to a target shell
 *
 * Checks if the given shell construct will work correctly in the target
 * shell mode.
 *
 * @param construct The construct to check (e.g., "echo -e", "${arr[0]}")
 * @param target Target shell mode to check against
 * @param result Output result (optional, may be NULL)
 * @return true if portable, false otherwise
 */
bool compat_is_portable(const char *construct, shell_mode_t target,
                        compat_result_t *result);

/**
 * @brief Check a line of script for compatibility issues
 *
 * Scans a line of shell script for constructs that may not be portable
 * to the target shell.
 *
 * @param line Line to check
 * @param target Target shell mode
 * @param results Output array for results
 * @param max_results Maximum results to return
 * @return Number of issues found
 */
size_t compat_check_line(const char *line, shell_mode_t target,
                         compat_result_t *results, size_t max_results);

/**
 * @brief Check a script for compatibility issues
 *
 * Scans an entire script for constructs that may not be portable to the
 * target shell.
 *
 * @param script Script content to check
 * @param target Target shell mode
 * @param results Output array for results
 * @param max_results Maximum results to return
 * @return Number of issues found
 */
size_t compat_check_script(const char *script, shell_mode_t target,
                           compat_result_t *results, size_t max_results);

// Forward declaration for AST node
struct node;

/**
 * @brief AST-based compatibility issue with copied data
 *
 * This structure owns copies of the strings, avoiding the static buffer
 * issues with compat_result_t.
 */
typedef struct {
    int line;
    int column;
    const char *severity;   /**< Static string, not owned */
    const char *message;    /**< Static string from TOML, not owned */
    const char *suggestion; /**< Static string from TOML, not owned */
    const char *feature;    /**< Static string, not owned */
} compat_ast_issue_t;

/**
 * @brief Check an AST for compatibility issues (accurate, no false positives)
 *
 * Walks the parsed AST looking for node types that represent non-portable
 * constructs. Unlike pattern-based checking, this won't match strings or
 * comments, providing accurate results.
 *
 * High-value detections:
 * - NODE_EXTENDED_TEST ([[ ]]) -> extended_test feature
 * - NODE_ARITH_CMD ((( ))) -> arithmetic_cmd feature
 * - NODE_PROC_SUB_IN/OUT (<() >()) -> process_substitution feature
 * - NODE_ARRAY_* (arrays) -> arrays feature
 *
 * @param ast Parsed AST root to check
 * @param target Target shell mode
 * @param results Output array for results
 * @param max_results Maximum results to return
 * @return Number of issues found
 */
size_t compat_check_ast(struct node *ast, shell_mode_t target,
                        compat_result_t *results, size_t max_results);

/**
 * @brief Check an AST and return issues with copied data
 *
 * Similar to compat_check_ast but returns compat_ast_issue_t which has
 * stable pointers to strings (not affected by static buffer reuse).
 *
 * @param ast Parsed AST root to check
 * @param target Target shell mode
 * @param issues Output array for issues
 * @param max_issues Maximum issues to return
 * @return Number of issues found
 */
size_t compat_check_ast_issues(struct node *ast, shell_mode_t target,
                               compat_ast_issue_t *issues, size_t max_issues);

/* ============================================================================
 * Strict Mode Support
 * ============================================================================
 */

/**
 * @brief Enable strict compatibility mode
 *
 * When strict mode is enabled, compatibility warnings are treated as errors.
 *
 * @param strict true to enable strict mode
 */
void compat_set_strict(bool strict);

/**
 * @brief Check if strict mode is enabled
 *
 * @return true if strict mode is enabled
 */
bool compat_is_strict(void);

/**
 * @brief Get effective severity for an entry
 *
 * Returns the effective severity, accounting for strict mode.
 * In strict mode, warnings are elevated to errors.
 *
 * @param entry Entry to check
 * @return Effective severity
 */
compat_severity_t compat_effective_severity(const compat_entry_t *entry);

/* ============================================================================
 * Target Shell Support
 * ============================================================================
 */

/**
 * @brief Set the target shell for compatibility checking
 *
 * Sets the default target shell used when checking for compatibility.
 * The target is stored as a string for flexibility (e.g., "posix", "bash",
 * "zsh", "lush", or future variants like "bash5").
 *
 * @param target Target shell name (NULL resets to "posix")
 */
void compat_set_target(const char *target);

/**
 * @brief Get the current target shell
 *
 * @return Current target shell name (never NULL, defaults to "posix")
 */
const char *compat_get_target(void);

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

/**
 * @brief Get category name as string
 *
 * @param category Category to get name for
 * @return Category name string (static, do not free)
 */
const char *compat_category_name(compat_category_t category);

/**
 * @brief Get severity name as string
 *
 * @param severity Severity to get name for
 * @return Severity name string (static, do not free)
 */
const char *compat_severity_name(compat_severity_t severity);

/**
 * @brief Parse a category name string
 *
 * @param name Category name to parse
 * @param category Output parameter for parsed category
 * @return true on success, false if name is not recognized
 */
bool compat_category_parse(const char *name, compat_category_t *category);

/**
 * @brief Parse a severity name string
 *
 * @param name Severity name to parse
 * @param severity Output parameter for parsed severity
 * @return true on success, false if name is not recognized
 */
bool compat_severity_parse(const char *name, compat_severity_t *severity);

/**
 * @brief Get fix type name as string
 *
 * @param type Fix type to get name for
 * @return Fix type name string (static, do not free)
 */
const char *compat_fix_type_name(fix_type_t type);

/**
 * @brief Parse a fix type name string
 *
 * @param name Fix type name to parse ("safe", "unsafe", "manual")
 * @param type Output parameter for parsed fix type
 * @return true on success, false if name is not recognized
 */
bool compat_fix_type_parse(const char *name, fix_type_t *type);

/**
 * @brief Get fix type for a specific target shell
 *
 * Returns the appropriate fix classification based on the target shell.
 * This allows the same fix to be safe for one shell but unsafe for another.
 *
 * @param fix_class Per-shell fix classification
 * @param target Target shell name (e.g., "posix", "bash", "zsh", "lush")
 * @return Fix type for the specified target
 */
fix_type_t compat_get_fix_type_for_target(const compat_fix_class_t *fix_class,
                                          const char *target);

/**
 * @brief Format a compatibility result as a message
 *
 * Formats a compatibility result into a human-readable message suitable
 * for display to the user.
 *
 * @param result Result to format
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of characters written
 */
int compat_format_result(const compat_result_t *result, char *buffer,
                         size_t size);

/* ============================================================================
 * Debugging
 * ============================================================================
 */

/**
 * @brief Print database statistics
 *
 * Outputs statistics about the loaded compatibility database to stderr.
 */
void compat_debug_print_stats(void);

/**
 * @brief Print an entry for debugging
 *
 * Outputs the full contents of an entry to stderr.
 *
 * @param entry Entry to print
 */
void compat_debug_print_entry(const compat_entry_t *entry);

#endif // COMPAT_H
