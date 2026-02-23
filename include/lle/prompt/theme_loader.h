/**
 * @file theme_loader.h
 * @brief LLE Theme File Loader - File I/O and Theme Loading API
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Issue #21 - Theme File Loading System
 * Version: 1.0.0
 *
 * Provides file loading, directory scanning, hot reload, and theme export
 * functionality for the LLE theme system.
 *
 * Theme File Locations (checked in order):
 * 1. $XDG_CONFIG_HOME/lush/themes/ (~/.config fallback)
 * 2. /etc/lush/themes/ (system-wide, optional)
 */

#ifndef LLE_PROMPT_THEME_LOADER_H
#define LLE_PROMPT_THEME_LOADER_H

#include "lle/error_handling.h"
#include "lle/prompt/theme.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

/** @brief Maximum path length for theme files */
#define LLE_THEME_PATH_MAX 4096

/** @brief Maximum file size for theme files (64KB) */
#define LLE_THEME_FILE_MAX_SIZE (64 * 1024)

/** @brief Theme file extension */
#define LLE_THEME_FILE_EXTENSION ".toml"

/** @brief User theme directory relative to XDG_CONFIG_HOME */
#define LLE_THEME_USER_DIR "lush/themes"

/** @brief System theme directory */
#define LLE_THEME_SYSTEM_DIR "/etc/lush/themes"

/* ============================================================================
 * LOADER RESULT STRUCTURE
 * ============================================================================
 */

/**
 * @brief Result structure for theme loading operations
 */
typedef struct lle_theme_load_result {
    lle_result_t status;                 /**< Overall status */
    char filepath[LLE_THEME_PATH_MAX];   /**< Path to loaded file */
    char theme_name[LLE_THEME_NAME_MAX]; /**< Name of loaded theme */
    char error_msg[256];                 /**< Error message if failed */
    size_t error_line;                   /**< Error line number */
    size_t error_column;                 /**< Error column number */
} lle_theme_load_result_t;

/**
 * @brief Batch load result for directory scanning
 */
typedef struct lle_theme_batch_result {
    size_t total_files;               /**< Total theme files found */
    size_t loaded_count;              /**< Successfully loaded count */
    size_t failed_count;              /**< Failed to load count */
    size_t skipped_count;             /**< Skipped (already exists) count */
    lle_theme_load_result_t *results; /**< Individual results (optional) */
    size_t results_capacity;          /**< Capacity of results array */
} lle_theme_batch_result_t;

/* ============================================================================
 * CORE LOADING API
 * ============================================================================
 */

/**
 * @brief Load a theme from a file path
 *
 * Reads and parses a theme file, populating the theme structure.
 * The theme is NOT registered with the registry - caller must do that.
 *
 * @param filepath  Path to the theme file
 * @param theme     Theme structure to populate (caller allocates)
 * @param result    Optional result structure for detailed error info
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_load_from_file(const char *filepath, lle_theme_t *theme,
                                      lle_theme_load_result_t *result);

/**
 * @brief Load a theme from a string
 *
 * Parses theme content from a string (useful for testing or embedded themes).
 *
 * @param content   Theme file content as a string
 * @param theme     Theme structure to populate (caller allocates)
 * @param result    Optional result structure for detailed error info
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_load_from_string(const char *content, lle_theme_t *theme,
                                        lle_theme_load_result_t *result);

/**
 * @brief Load all themes from a directory
 *
 * Scans a directory for *.toml files and loads each as a theme.
 * Successfully loaded themes are registered with the registry.
 *
 * @param dirpath   Directory path to scan
 * @param registry  Theme registry to register themes with
 * @param result    Optional batch result for statistics
 * @return Number of themes successfully loaded
 */
size_t lle_theme_load_directory(const char *dirpath,
                                lle_theme_registry_t *registry,
                                lle_theme_batch_result_t *result);

/**
 * @brief Load user themes from standard locations
 *
 * Loads themes from:
 * 1. $XDG_CONFIG_HOME/lush/themes/ (~/.config/lush/themes/ fallback)
 * 2. /etc/lush/themes/ (if exists)
 *
 * @param registry  Theme registry to register themes with
 * @return Number of themes successfully loaded
 */
size_t lle_theme_load_user_themes(lle_theme_registry_t *registry);

/* ============================================================================
 * HOT RELOAD API
 * ============================================================================
 */

/**
 * @brief Reload all user themes
 *
 * Reloads themes from user directories, updating existing themes
 * and adding new ones. Does not affect built-in themes.
 *
 * @param registry  Theme registry to update
 * @return Number of themes reloaded
 */
size_t lle_theme_reload_user_themes(lle_theme_registry_t *registry);

/**
 * @brief Reload a specific theme from its file
 *
 * Reloads a single theme file, updating the theme in the registry.
 * The theme must have been loaded from a file originally.
 *
 * @param registry  Theme registry
 * @param name      Theme name to reload
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_reload_by_name(lle_theme_registry_t *registry,
                                      const char *name);

/**
 * @brief Check if the active theme's file has been modified
 *
 * Compares the active theme's source file mtime against a cached value.
 * If the file has changed, reloads the theme from disk automatically.
 * Returns true if the theme was reloaded, false otherwise.
 *
 * Safe to call frequently (uses stat() only). Ignores builtin themes
 * and themes without a source filepath.
 *
 * @param registry  Theme registry to check
 * @return true if theme was reloaded, false otherwise
 */
bool lle_theme_check_hot_reload(lle_theme_registry_t *registry);

/* ============================================================================
 * THEME EXPORT API
 * ============================================================================
 */

/**
 * @brief Export a theme to TOML format
 *
 * Generates a TOML string representation of the theme that can be
 * written to a file or printed to stdout.
 *
 * @param theme     Theme to export
 * @param output    Output buffer for TOML content
 * @param output_size  Size of output buffer
 * @return Length of generated content, or 0 on error
 */
size_t lle_theme_export_to_toml(const lle_theme_t *theme, char *output,
                                size_t output_size);

/**
 * @brief Export a theme to a file
 *
 * Writes the theme in TOML format to the specified file.
 *
 * @param theme     Theme to export
 * @param filepath  Path to output file
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_export_to_file(const lle_theme_t *theme,
                                      const char *filepath);

/* ============================================================================
 * PATH UTILITIES
 * ============================================================================
 */

/**
 * @brief Get the user theme directory path
 *
 * Returns the path to the user's theme directory:
 * $XDG_CONFIG_HOME/lush/themes/ or ~/.config/lush/themes/
 *
 * @param buffer    Output buffer for path
 * @param size      Size of buffer
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_get_user_dir(char *buffer, size_t size);

/**
 * @brief Get the system theme directory path
 *
 * Returns /etc/lush/themes/
 *
 * @param buffer    Output buffer for path
 * @param size      Size of buffer
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_get_system_dir(char *buffer, size_t size);

/**
 * @brief Check if a theme file exists
 *
 * @param filepath  Path to check
 * @return true if file exists and is readable
 */
bool lle_theme_file_exists(const char *filepath);

/**
 * @brief Create user theme directory if it doesn't exist
 *
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_ensure_user_dir(void);

/* ============================================================================
 * BATCH RESULT HELPERS
 * ============================================================================
 */

/**
 * @brief Initialize a batch result structure
 *
 * @param result     Result structure to initialize
 * @param capacity   Capacity for individual results (0 to skip)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_batch_result_init(lle_theme_batch_result_t *result,
                                         size_t capacity);

/**
 * @brief Free resources in a batch result structure
 *
 * @param result  Result structure to cleanup
 */
void lle_theme_batch_result_cleanup(lle_theme_batch_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* LLE_PROMPT_THEME_LOADER_H */
