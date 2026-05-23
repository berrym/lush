/**
 * @file posix_history.h
 * @brief Enhanced POSIX-compliant history management
 *
 * Provides POSIX-compliant history management including fc command
 * implementation, enhanced history builtin with bash/zsh compatibility,
 * editor integration, and file operations.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef POSIX_HISTORY_H
#define POSIX_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Limits
 * ============================================================================
 */

/**
 * @brief Maximum history entries (POSIX minimum is 128, we support more)
 */
#define POSIX_HISTORY_MAX_ENTRIES 10000

/**
 * @brief Minimum history entries as per POSIX
 */
#define POSIX_HISTORY_MIN_ENTRIES 128

/**
 * @brief Maximum length of a single history command
 */
#define POSIX_HISTORY_MAX_COMMAND_LENGTH 4096

/**
 * @brief Maximum length of editor command line
 */
#define POSIX_HISTORY_MAX_EDITOR_COMMAND 1024

/**
 * @brief Default history file name
 */
#define POSIX_HISTORY_DEFAULT_FILE ".lush_history"

/**
 * @brief History number wraparound limit (POSIX allows implementation-defined)
 */
#define POSIX_HISTORY_WRAPAROUND_LIMIT 32767

/* ============================================================================
 * Data Structures
 * ============================================================================
 */

/**
 * @brief History entry with POSIX-compliant numbering and metadata
 */
typedef struct posix_history_entry {
    int number;       /**< POSIX history number (1-based) */
    char *command;    /**< Command text */
    time_t timestamp; /**< When command was executed */
    size_t length;    /**< Length of command text */
    bool modified;    /**< Whether entry was modified during editing */
} posix_history_entry_t;

/**
 * @brief History range specification for fc and history commands
 */
typedef struct posix_history_range {
    int first;            /**< First entry number */
    int last;             /**< Last entry number */
    bool first_specified; /**< Whether first was explicitly specified */
    bool last_specified;  /**< Whether last was explicitly specified */
    bool valid;           /**< Whether range is valid */
} posix_history_range_t;

/**
 * @brief fc command options
 */
typedef struct posix_fc_options {
    bool list_mode;              /**< -l: List mode instead of edit */
    bool reverse_order;          /**< -r: Reverse chronological order */
    bool suppress_numbers;       /**< -n: Suppress line numbers in list */
    bool substitute_mode;        /**< -s: Substitute and re-execute */
    char *editor;                /**< -e: Editor to use */
    char *old_pattern;           /**< Pattern to replace in substitute mode */
    char *new_pattern;           /**< Replacement pattern in substitute mode */
    posix_history_range_t range; /**< History range to operate on */
} posix_fc_options_t;

/**
 * @brief Enhanced history command options (bash/zsh compatible)
 */
typedef struct posix_history_options {
    bool clear_history;   /**< -c: Clear history */
    bool delete_entry;    /**< -d: Delete specific entry */
    bool read_file;       /**< -r: Read history from file */
    bool write_file;      /**< -w: Write history to file */
    bool append_file;     /**< -a: Append new entries to file */
    bool read_new;        /**< -n: Read new entries from file */
    bool show_timestamps; /**< -t: Show timestamps */
    int delete_offset;    /**< Offset for -d option */
    int count;            /**< Number of entries to show */
    char *filename;       /**< Filename for file operations */
} posix_history_options_t;

/**
 * @brief History manager state
 */
typedef struct posix_history_manager {
    posix_history_entry_t *entries; /**< Array of history entries */
    size_t count;                   /**< Current number of entries */
    size_t capacity;                /**< Maximum capacity */
    int next_number;                /**< Next history number to assign */
    int base_number;                /**< Base number for current session */
    bool wraparound_occurred;       /**< Whether number wraparound occurred */
    char *filename;                 /**< Current history file */
    time_t last_save;               /**< Last save timestamp */
    bool no_duplicates;             /**< Whether to avoid duplicate entries */
} posix_history_manager_t;

/* ============================================================================
 * Core History Management Functions
 * ============================================================================
 */

/**
 * @brief Create a new POSIX history manager
 *
 * Initializes a new history manager with POSIX-compliant numbering and
 * configurable capacity. The manager handles number wraparound according
 * to POSIX specifications.
 *
 * @param capacity Maximum number of history entries (0 for default)
 * @return Pointer to new history manager, or NULL on failure
 *
 * @note The returned manager must be freed with posix_history_destroy()
 * @note If capacity is 0, uses POSIX_HISTORY_MAX_ENTRIES
 */
posix_history_manager_t *posix_history_create(size_t capacity);

/**
 * @brief Destroy history manager and free all resources
 *
 * Properly destroys the history manager, saving any unsaved changes and
 * freeing all allocated memory.
 *
 * @param manager History manager to destroy (can be NULL)
 *
 * @note Automatically saves history before destruction if filename is set
 * @note Safe to call with NULL pointer
 */
void posix_history_destroy(posix_history_manager_t *manager);

/**
 * @brief Add a command to history with POSIX numbering
 *
 * Adds a new command to the history with proper POSIX-compliant numbering.
 * Handles number wraparound and duplicate detection according to configuration.
 *
 * @param manager History manager
 * @param command Command text to add
 * @return History number assigned, or -1 on failure
 *
 * @note Empty commands and whitespace-only commands are ignored
 * @note Duplicate handling depends on no_duplicates setting
 * @note Numbers wrap around at POSIX_HISTORY_WRAPAROUND_LIMIT
 */
int posix_history_add(posix_history_manager_t *manager, const char *command);

/**
 * @brief Get history entry by number
 *
 * Retrieves a history entry by its POSIX history number. Handles number
 * wraparound correctly.
 *
 * @param manager History manager
 * @param number History number to retrieve
 * @return Pointer to history entry, or NULL if not found
 *
 * @note The returned pointer is valid until the next history operation
 * @note Handles wraparound numbers correctly
 */
posix_history_entry_t *posix_history_get(posix_history_manager_t *manager,
                                         int number);

/**
 * @brief Get history entry by index (0-based)
 *
 * Retrieves a history entry by its array index. This is different from
 * history number and is used for internal operations.
 *
 * @param manager History manager
 * @param index Array index (0-based)
 * @return Pointer to history entry, or NULL if index invalid
 */
posix_history_entry_t *
posix_history_get_by_index(posix_history_manager_t *manager, size_t index);

/**
 * @brief Delete history entry by number
 *
 * Removes a history entry by its POSIX history number. This is used by
 * the enhanced history builtin's -d option.
 *
 * @param manager History manager
 * @param number History number to delete
 * @return true on success, false if entry not found
 *
 * @note Subsequent entries retain their original numbers
 * @note This operation cannot be undone
 */
bool posix_history_delete(posix_history_manager_t *manager, int number);

/**
 * @brief Clear all history entries
 *
 * Removes all history entries from the manager. This is used by the
 * enhanced history builtin's -c option.
 *
 * @param manager History manager
 * @return true on success, false on failure
 *
 * @note This operation cannot be undone
 * @note Resets history numbering to 1
 */
bool posix_history_clear(posix_history_manager_t *manager);

/* ============================================================================
 * Range and Number Management
 * ============================================================================
 */

/**
 * @brief Parse history range specification
 *
 * Parses POSIX-compliant history range specifications like "1", "1 5",
 * "string", "-2", etc. Handles both numeric and string pattern matching.
 *
 * @param manager History manager for context
 * @param first_str First range specifier (can be NULL)
 * @param last_str Last range specifier (can be NULL)
 * @param range Pointer to range structure to populate
 * @return true on success, false on parse error
 *
 * @note Supports POSIX patterns: number, negative offset, string prefix
 * @note If only first is specified, last defaults to first
 * @note Empty specifications default to most recent entries
 */
bool posix_history_parse_range(posix_history_manager_t *manager,
                               const char *first_str, const char *last_str,
                               posix_history_range_t *range);

/**
 * @brief Resolve history number from string specification
 *
 * Resolves a history specification (number, negative offset, or string pattern)
 * to an actual history number.
 *
 * @param manager History manager
 * @param spec String specification
 * @return Resolved history number, or -1 if not found
 *
 * @note Handles negative offsets (-1 = last, -2 = second-to-last)
 * @note Handles string prefixes (finds most recent matching command)
 * @note Handles direct numeric specifications
 */
int posix_history_resolve_number(posix_history_manager_t *manager,
                                 const char *spec);

/**
 * @brief Get valid range of history numbers
 *
 * Returns the valid range of history numbers currently available,
 * accounting for wraparound and capacity limits.
 *
 * @param manager History manager
 * @param min_number Pointer to receive minimum valid number
 * @param max_number Pointer to receive maximum valid number
 * @return true on success, false if no history available
 */
bool posix_history_get_valid_range(posix_history_manager_t *manager,
                                   int *min_number, int *max_number);

/* ============================================================================
 * File Operations
 * ============================================================================
 */

/**
 * @brief Load history from file
 *
 * Loads history entries from a file, preserving POSIX numbering and
 * handling various file formats.
 *
 * @param manager History manager
 * @param filename File to load from (NULL for default)
 * @param append Whether to append to existing history or replace
 * @return Number of entries loaded, or -1 on error
 *
 * @note Supports standard history file formats
 * @note Handles timestamp preservation if available
 * @note Automatically assigns POSIX numbers if not in file
 */
int posix_history_load(posix_history_manager_t *manager, const char *filename,
                       bool append);

/**
 * @brief Save history to file
 *
 * Saves current history to a file with POSIX-compliant format.
 *
 * @param manager History manager
 * @param filename File to save to (NULL for default)
 * @param include_timestamps Whether to include timestamps in output
 * @return Number of entries saved, or -1 on error
 *
 * @note Creates backup of existing file before overwriting
 * @note Uses atomic write operation for safety
 * @note Includes metadata for proper restoration
 */
int posix_history_save(posix_history_manager_t *manager, const char *filename,
                       bool include_timestamps);

/**
 * @brief Append new entries to file
 *
 * Appends only new (unsaved) history entries to the history file.
 * This is used by the enhanced history builtin's -a option.
 *
 * @param manager History manager
 * @param filename File to append to (NULL for default)
 * @return Number of entries appended, or -1 on error
 *
 * @note Only appends entries added since last save
 * @note Updates internal save timestamp
 */
int posix_history_append_new(posix_history_manager_t *manager,
                             const char *filename);

/**
 * @brief Read new entries from file
 *
 * Reads entries from the history file that are newer than the last
 * read operation. This is used by the enhanced history builtin's -n option.
 *
 * @param manager History manager
 * @param filename File to read from (NULL for default)
 * @return Number of new entries read, or -1 on error
 *
 * @note Only reads entries newer than last read timestamp
 * @note Useful for sharing history between shell sessions
 */
int posix_history_read_new(posix_history_manager_t *manager,
                           const char *filename);

/* ============================================================================
 * POSIX fc Command Support
 * ============================================================================
 */

/**
 * @brief Execute fc command with given options
 *
 * Main entry point for the POSIX fc command. Handles all fc modes:
 * list, edit, and substitute.
 *
 * @param manager History manager
 * @param options fc command options
 * @return Exit status (0 for success, non-zero for error)
 *
 * @note Handles editor invocation for edit mode
 * @note Executes commands for edit and substitute modes
 * @note Formats output appropriately for list mode
 */
int posix_fc_execute(posix_history_manager_t *manager,
                     const posix_fc_options_t *options);

/**
 * @brief List history entries with fc formatting
 *
 * Lists history entries in the format required by fc -l command.
 *
 * @param manager History manager
 * @param range Range of entries to list
 * @param reverse_order Whether to list in reverse order
 * @param suppress_numbers Whether to suppress line numbers
 * @return 0 on success, non-zero on error
 */
int posix_fc_list(posix_history_manager_t *manager,
                  const posix_history_range_t *range, bool reverse_order,
                  bool suppress_numbers);

/**
 * @brief Edit and re-execute history entries
 *
 * Invokes an editor on the specified history range and re-executes
 * the resulting commands.
 *
 * @param manager History manager
 * @param range Range of entries to edit
 * @param editor Editor command to use (NULL for default)
 * @return 0 on success, non-zero on error
 *
 * @note Uses FCEDIT or EDITOR environment variables if editor is NULL
 * @note Creates temporary file for editing
 * @note Executes resulting commands line by line
 */
int posix_fc_edit(posix_history_manager_t *manager,
                  const posix_history_range_t *range, const char *editor);

/**
 * @brief Substitute and re-execute history command
 *
 * Performs string substitution on a history command and re-executes it.
 *
 * @param manager History manager
 * @param number History number to operate on
 * @param old_pattern Pattern to replace
 * @param new_pattern Replacement pattern
 * @return 0 on success, non-zero on error
 *
 * @note Performs global substitution if pattern appears multiple times
 * @note Re-executes the modified command
 * @note Adds the modified command to history
 */
int posix_fc_substitute(posix_history_manager_t *manager, int number,
                        const char *old_pattern, const char *new_pattern);

/* ============================================================================
 * Enhanced History Builtin Support
 * ============================================================================
 */

/**
 * @brief Execute enhanced history command
 *
 * Main entry point for the enhanced history builtin with bash/zsh
 * compatibility options.
 *
 * @param manager History manager
 * @param options History command options
 * @return Exit status (0 for success, non-zero for error)
 */
int posix_history_execute(posix_history_manager_t *manager,
                          const posix_history_options_t *options);

/**
 * @brief List history entries with optional count limit
 *
 * Lists history entries with formatting similar to bash history command.
 *
 * @param manager History manager
 * @param count Number of entries to show (0 for all)
 * @param show_timestamps Whether to include timestamps
 * @return 0 on success, non-zero on error
 */
int posix_history_list(posix_history_manager_t *manager, int count,
                       bool show_timestamps);

/* ============================================================================
 * Utility and Configuration Functions
 * ============================================================================
 */

/**
 * @brief Set default history filename
 *
 * Sets the default filename for history file operations.
 *
 * @param manager History manager
 * @param filename Default filename (copied internally)
 * @return true on success, false on failure
 */
bool posix_history_set_filename(posix_history_manager_t *manager,
                                const char *filename);

/**
 * @brief Enable or disable duplicate detection
 *
 * Controls whether duplicate consecutive entries are avoided.
 *
 * @param manager History manager
 * @param no_duplicates true to avoid duplicates, false to allow
 */
void posix_history_set_no_duplicates(posix_history_manager_t *manager,
                                     bool no_duplicates);

/**
 * @brief Get history statistics
 *
 * Retrieves statistics about the history manager for debugging and
 * monitoring purposes.
 *
 * @param manager History manager
 * @param total_entries Pointer to receive total entry count
 * @param current_number Pointer to receive current number
 * @param wraparound_count Pointer to receive wraparound count
 * @return true on success, false on failure
 */
bool posix_history_get_stats(posix_history_manager_t *manager,
                             size_t *total_entries, int *current_number,
                             int *wraparound_count);

/**
 * @brief Validate history manager state
 *
 * Performs internal consistency checks on the history manager.
 * Used for debugging and testing.
 *
 * @param manager History manager
 * @return true if state is consistent, false if corruption detected
 */
bool posix_history_validate(posix_history_manager_t *manager);

/* ============================================================================
 * Editor Integration
 * ============================================================================
 */

/**
 * @brief Get default editor for fc command
 *
 * Determines the editor to use for fc command based on FCEDIT, EDITOR
 * environment variables and system defaults.
 *
 * @return Editor command string (must be freed by caller)
 *
 * @note Checks FCEDIT first, then EDITOR, then system defaults
 * @note Returns "vi" as final fallback
 */
char *posix_history_get_default_editor(void);

/**
 * @brief Create temporary file for editing
 *
 * Creates a temporary file with the specified content for editor invocation.
 *
 * @param content Content to write to temporary file
 * @param filename Pointer to receive temporary filename (must be freed)
 * @return true on success, false on failure
 *
 * @note Caller must delete the temporary file when done
 * @note Uses secure temporary file creation
 */
bool posix_history_create_temp_file(const char *content, char **filename);

/**
 * @brief Read content from file
 *
 * Reads the entire content of a file into a string.
 *
 * @param filename File to read
 * @return File content (must be freed by caller), or NULL on failure
 */
char *posix_history_read_file_content(const char *filename);

/* ============================================================================
 * Error Handling and Debugging
 * ============================================================================
 */

/**
 * @brief History error codes
 */
typedef enum {
    POSIX_HISTORY_SUCCESS = 0,          /**< Operation successful */
    POSIX_HISTORY_ERROR_INVALID_PARAM,  /**< Invalid parameter */
    POSIX_HISTORY_ERROR_OUT_OF_MEMORY,  /**< Memory allocation failed */
    POSIX_HISTORY_ERROR_FILE_IO,        /**< File I/O error */
    POSIX_HISTORY_ERROR_INVALID_NUMBER, /**< Invalid history number */
    POSIX_HISTORY_ERROR_INVALID_RANGE,  /**< Invalid range specification */
    POSIX_HISTORY_ERROR_EDITOR_FAILED,  /**< Editor invocation failed */
    POSIX_HISTORY_ERROR_COMMAND_FAILED  /**< Command execution failed */
} posix_history_error_t;

/**
 * @brief Get last error message
 *
 * Returns a human-readable description of the last error that occurred.
 *
 * @return Error message string, or NULL if no error
 */
const char *posix_history_get_last_error(void);

/**
 * @brief Set debug mode
 *
 * Enables or disables debug output for history operations.
 *
 * @param enable true to enable debug mode, false to disable
 */
void posix_history_set_debug(bool enable);

// ============================================================================
// Enhanced History Integration Functions
// ============================================================================

/**
 * @brief Initialize enhanced history system
 *
 * This function sets up the enhanced history system and loads existing
 * history from the default file.
 */
void enhanced_history_init(void);

/**
 * @brief Add command to enhanced history
 *
 * @param command Command to add
 */
void enhanced_history_add(const char *command);

/**
 * @brief Save enhanced history to file
 */
void enhanced_history_save(void);

/**
 * @brief Cleanup enhanced history system
 */
void enhanced_history_cleanup(void);

// ============================================================================
// Global Instance
// ============================================================================

/**
 * @brief Global POSIX history manager instance
 */
extern posix_history_manager_t *global_posix_history;

#ifdef __cplusplus
}
#endif

#endif // POSIX_HISTORY_H
