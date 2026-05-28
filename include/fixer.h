/**
 * @file fixer.h
 * @brief Auto-fix linter module for shell script portability fixes
 *
 * Provides functionality to automatically fix compatibility issues found
 * in shell scripts. Supports safe fixes (applied with --fix) and unsafe
 * fixes (require --unsafe-fixes flag).
 *
 * The fixer works with the compatibility database (compat.h) to identify
 * fixable issues and apply replacements.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef FIXER_H
#define FIXER_H

#include "compat.h"

#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Fixer Types
 * ============================================================================
 */

/**
 * @brief A single fix to be applied
 *
 * Represents a text replacement operation at a specific location in the file.
 * Fixes are applied from end to start to preserve line/column positions.
 */
typedef struct {
    int line;                    ///< Line number (1-based)
    int column;                  ///< Column number (1-based)
    size_t match_start;          ///< Byte offset of match start
    size_t match_length;         ///< Length of matched text
    const char *original;        ///< Original matched text (not owned)
    const char *replacement;     ///< Replacement text (not owned)
    fix_type_t type;             ///< Fix type (safe/unsafe/manual)
    const char *message;         ///< Description of the fix (not owned)
    const compat_entry_t *entry; ///< Source compat entry (not owned)
} fixer_fix_t;

/**
 * @brief Collection of fixes for a script
 */
typedef struct {
    fixer_fix_t *fixes; ///< Array of fixes
    size_t count;       ///< Number of fixes
    size_t capacity;    ///< Allocated capacity
    char *script_path;  ///< Path to script being fixed
    char *content;      ///< Original content (owned)
    size_t content_len; ///< Content length
} fixer_context_t;

/**
 * @brief Result of a fix operation
 */
typedef enum {
    FIXER_OK,         ///< Success
    FIXER_ERR_IO,     ///< I/O error reading/writing file
    FIXER_ERR_PARSE,  ///< Failed to parse fixed script
    FIXER_ERR_NOMEM,  ///< Memory allocation failed
    FIXER_ERR_NOFIX,  ///< No fixes to apply
    FIXER_ERR_VERIFY, ///< Fixed script failed verification
} fixer_result_t;

/**
 * @brief Options for fix application
 */
typedef struct {
    bool include_unsafe; ///< Include unsafe fixes
    bool dry_run;        ///< Preview only, don't modify files
    bool create_backup;  ///< Create .bak backup before modifying
    bool verify_syntax;  ///< Parse fixed script to verify correctness
    shell_mode_t target; ///< Target shell mode for verification
} fixer_options_t;

/* ============================================================================
 * Context Management
 * ============================================================================
 */

/**
 * @brief Initialize a fixer context
 *
 * @param ctx Context to initialize
 * @return FIXER_OK on success, error code on failure
 */
fixer_result_t fixer_init(fixer_context_t *ctx);

/**
 * @brief Clean up a fixer context
 *
 * Frees all resources associated with the context.
 *
 * @param ctx Context to clean up
 */
void fixer_cleanup(fixer_context_t *ctx);

/**
 * @brief Load a script file for fixing
 *
 * Reads the script content and prepares for fix collection.
 *
 * @param ctx Fixer context
 * @param path Path to script file
 * @return FIXER_OK on success, error code on failure
 */
fixer_result_t fixer_load_file(fixer_context_t *ctx, const char *path);

/**
 * @brief Load script content from string
 *
 * @param ctx Fixer context
 * @param content Script content (will be copied)
 * @param path Virtual path for error messages (can be NULL)
 * @return FIXER_OK on success, error code on failure
 */
fixer_result_t fixer_load_string(fixer_context_t *ctx, const char *content,
                                 const char *path);

/* ============================================================================
 * Fix Collection
 * ============================================================================
 */

/**
 * @brief Collect all fixable issues from the loaded script
 *
 * Analyzes the script using the compatibility database and collects
 * all fixes that can be applied.
 *
 * @param ctx Fixer context with loaded script
 * @param target Target shell mode
 * @return Number of fixes collected
 */
size_t fixer_collect_fixes(fixer_context_t *ctx, shell_mode_t target);

/**
 * @brief Add a fix manually
 *
 * @param ctx Fixer context
 * @param fix Fix to add (will be copied)
 * @return FIXER_OK on success, error code on failure
 */
fixer_result_t fixer_add_fix(fixer_context_t *ctx, const fixer_fix_t *fix);

/**
 * @brief Get count of safe fixes
 *
 * @param ctx Fixer context
 * @return Number of safe fixes
 */
size_t fixer_count_safe(const fixer_context_t *ctx);

/**
 * @brief Get count of unsafe fixes
 *
 * @param ctx Fixer context
 * @return Number of unsafe fixes
 */
size_t fixer_count_unsafe(const fixer_context_t *ctx);

/**
 * @brief Get count of manual fixes (cannot be auto-fixed)
 *
 * @param ctx Fixer context
 * @return Number of manual fixes
 */
size_t fixer_count_manual(const fixer_context_t *ctx);

/* ============================================================================
 * Fix Application
 * ============================================================================
 */

/**
 * @brief Apply collected fixes to the script content
 *
 * Applies fixes in reverse order (end to start) to preserve positions.
 * Returns the fixed content without modifying the original file.
 *
 * @param ctx Fixer context
 * @param options Fix options
 * @param output Output buffer for fixed content (caller allocates)
 * @param output_size Size of output buffer
 * @param fixes_applied Output: number of fixes actually applied
 * @return FIXER_OK on success, error code on failure
 */
fixer_result_t fixer_apply_fixes(fixer_context_t *ctx,
                                 const fixer_options_t *options, char *output,
                                 size_t output_size, size_t *fixes_applied);

/**
 * @brief Apply fixes and return newly allocated string
 *
 * @param ctx Fixer context
 * @param options Fix options
 * @param output Output pointer (caller must free)
 * @param fixes_applied Output: number of fixes actually applied
 * @return FIXER_OK on success, error code on failure
 */
fixer_result_t fixer_apply_fixes_alloc(fixer_context_t *ctx,
                                       const fixer_options_t *options,
                                       char **output, size_t *fixes_applied);

/**
 * @brief Verify that fixed content parses correctly
 *
 * Parses the fixed script to ensure no syntax errors were introduced.
 *
 * @param content Fixed script content
 * @param target Target shell mode for parsing
 * @return true if script parses correctly, false otherwise
 */
bool fixer_verify_syntax(const char *content, shell_mode_t target);

/**
 * @brief Write fixed content to file
 *
 * Writes the fixed content to the file, optionally creating a backup.
 *
 * @param path Path to write to
 * @param content Fixed content
 * @param create_backup If true, create .bak backup first
 * @return FIXER_OK on success, error code on failure
 */
fixer_result_t fixer_write_file(const char *path, const char *content,
                                bool create_backup);

/* ============================================================================
 * Diff Generation
 * ============================================================================
 */

/**
 * @brief Generate unified diff of changes
 *
 * Creates a unified diff showing the changes that would be made.
 *
 * @param ctx Fixer context
 * @param options Fix options
 * @param output Output buffer for diff (caller allocates)
 * @param output_size Size of output buffer
 * @return Number of characters written, or -1 on error
 */
int fixer_generate_diff(fixer_context_t *ctx, const fixer_options_t *options,
                        char *output, size_t output_size);

/**
 * @brief Print diff to stdout
 *
 * @param ctx Fixer context
 * @param options Fix options
 */
void fixer_print_diff(fixer_context_t *ctx, const fixer_options_t *options);

/* ============================================================================
 * Reporting
 * ============================================================================
 */

/**
 * @brief Print summary of collected fixes
 *
 * Outputs a summary of what fixes are available.
 *
 * @param ctx Fixer context
 * @param options Fix options (to show what would be applied)
 */
void fixer_print_summary(const fixer_context_t *ctx,
                         const fixer_options_t *options);

/**
 * @brief Print detailed list of fixes
 *
 * Outputs details about each fix including line numbers and descriptions.
 *
 * @param ctx Fixer context
 * @param include_unsafe Include unsafe fixes in output
 */
void fixer_print_fixes(const fixer_context_t *ctx, bool include_unsafe);

/**
 * @brief Get result description as string
 *
 * @param result Result code
 * @return Description string (static, do not free)
 */
const char *fixer_result_string(fixer_result_t result);

/* ============================================================================
 * Interactive Fix Mode
 * ============================================================================
 */

/**
 * @brief User response for interactive fix mode
 */
typedef enum {
    FIXER_RESPONSE_YES,  ///< Apply this fix
    FIXER_RESPONSE_NO,   ///< Skip this fix
    FIXER_RESPONSE_ALL,  ///< Apply all remaining fixes
    FIXER_RESPONSE_QUIT, ///< Stop and apply accepted fixes
    FIXER_RESPONSE_DIFF, ///< Show diff for this fix
    FIXER_RESPONSE_HELP, ///< Show help
} fixer_response_t;

/**
 * @brief Interactive fix session state
 */
typedef struct {
    fixer_context_t *ctx;    ///< Fixer context
    fixer_options_t options; ///< Fix options
    bool *accepted;          ///< Array of accepted fix flags
    size_t current;          ///< Current fix index
    bool apply_all;          ///< Apply all remaining without prompting
    bool aborted;            ///< Session was aborted
} fixer_interactive_t;

/**
 * @brief Initialize interactive fix session
 *
 * @param session Session state to initialize
 * @param ctx Fixer context with collected fixes
 * @param options Fix options
 * @return FIXER_OK on success, error code on failure
 */
fixer_result_t fixer_interactive_init(fixer_interactive_t *session,
                                      fixer_context_t *ctx,
                                      const fixer_options_t *options);

/**
 * @brief Clean up interactive session
 *
 * @param session Session to clean up
 */
void fixer_interactive_cleanup(fixer_interactive_t *session);

/**
 * @brief Get next fix to present to user
 *
 * @param session Interactive session
 * @param fix Output: next fix (not owned)
 * @return true if there's a fix to present, false if done
 */
bool fixer_interactive_next(fixer_interactive_t *session,
                            const fixer_fix_t **fix);

/**
 * @brief Process user response for current fix
 *
 * @param session Interactive session
 * @param response User's response
 */
void fixer_interactive_respond(fixer_interactive_t *session,
                               fixer_response_t response);

/**
 * @brief Apply accepted fixes from interactive session
 *
 * @param session Interactive session
 * @param output Output pointer for fixed content (caller must free)
 * @param fixes_applied Output: number of fixes applied
 * @return FIXER_OK on success, error code on failure
 */
fixer_result_t fixer_interactive_apply(fixer_interactive_t *session,
                                       char **output, size_t *fixes_applied);

/**
 * @brief Print a single fix with context for interactive review
 *
 * Shows the fix location, original code, proposed change, and
 * the fix description in a user-friendly format.
 *
 * @param ctx Fixer context
 * @param fix Fix to display
 * @param index Fix number (1-based, for display)
 * @param total Total number of fixes
 */
void fixer_print_fix_interactive(const fixer_context_t *ctx,
                                 const fixer_fix_t *fix, size_t index,
                                 size_t total);

/**
 * @brief Print interactive mode help
 */
void fixer_print_interactive_help(void);

/**
 * @brief Read user response in interactive mode
 *
 * Reads a single character response from the user.
 *
 * @return User response
 */
fixer_response_t fixer_read_response(void);

/**
 * @brief Run interactive fix session
 *
 * High-level function that runs the full interactive session,
 * prompting the user for each fix and applying accepted changes.
 *
 * @param ctx Fixer context with collected fixes
 * @param options Fix options
 * @param script_path Path to script (for writing)
 * @return Number of fixes applied, or -1 on error
 */
int fixer_run_interactive(fixer_context_t *ctx, const fixer_options_t *options,
                          const char *script_path);

#endif /// FIXER_H
