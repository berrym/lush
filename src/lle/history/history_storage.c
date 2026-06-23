/**
 * @file history_storage.c
 * @brief LLE History System - Persistence and File Storage
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 09 - History System
 * Persistence Layer
 *
 * Provides file-based persistence for history entries with:
 * - Save/load operations
 * - File locking for multi-process safety
 * - TSV format for simplicity and readability
 * - Corruption detection and recovery
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

#define LLE_HISTORY_FILE_VERSION_STR "1.0"
#define LLE_HISTORY_MAGIC_HEADER "# LLE History File v"
#define LLE_HISTORY_MAX_LINE_LENGTH 65536 /// 64KB per line

/* ============================================================================
 * FILE LOCKING
 * ============================================================================
 */

/**
 * @brief Acquire exclusive lock on history file
 *
 * Uses flock() for advisory locking to prevent concurrent writes
 * from multiple shell instances.
 *
 * @param fd File descriptor to lock
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if fd is invalid,
 *         LLE_ERROR_TIMEOUT if lock cannot be acquired, or
 * LLE_ERROR_SYSTEM_CALL on error
 */
static lle_result_t lle_history_file_lock(int fd) {
    if (fd < 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Try to acquire exclusive lock with timeout
    struct timespec timeout = {.tv_sec = 1, .tv_nsec = 0};
    int attempts = 5;

    while (attempts > 0) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            return LLE_SUCCESS;
        }

        if (errno != EWOULDBLOCK) {
            return LLE_FAULT(LLE_ERROR_SYSTEM_CALL, "history",
                             "history file lock failed");
        }

        /// Wait before retry
        nanosleep(&timeout, NULL);
        attempts--;
    }

    return LLE_ERROR_TIMEOUT;
}

/**
 * @brief Release lock on history file
 *
 * @param fd File descriptor to unlock
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if fd is invalid,
 *         or LLE_ERROR_SYSTEM_CALL on error
 */
static lle_result_t lle_history_file_unlock(int fd) {
    if (fd < 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (flock(fd, LOCK_UN) != 0) {
        return LLE_FAULT(LLE_ERROR_SYSTEM_CALL, "history",
                         "history file unlock failed");
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * LINE PARSING AND FORMATTING
 * ============================================================================
 */

/**
 * @brief Escape special characters in string for TSV format
 *
 * Escapes: \\t -> \\\\t, \\n -> \\\\n, \\\\ -> \\\\\\\\
 *
 * @param str Input string to escape
 * @param output Output buffer for escaped string
 * @param output_size Size of output buffer in bytes
 * @return LLE_SUCCESS on success, or LLE_ERROR_INVALID_PARAMETER if parameters
 * are invalid
 */
static lle_result_t lle_escape_string(const char *str, char *output,
                                      size_t output_size) {
    if (!str || !output || output_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t out_idx = 0;
    size_t in_idx = 0;

    while (str[in_idx] != '\0' && out_idx < output_size - 1) {
        if (str[in_idx] == '\t') {
            if (out_idx + 2 >= output_size)
                break;
            output[out_idx++] = '\\';
            output[out_idx++] = 't';
        } else if (str[in_idx] == '\n') {
            if (out_idx + 2 >= output_size)
                break;
            output[out_idx++] = '\\';
            output[out_idx++] = 'n';
        } else if (str[in_idx] == '\\') {
            if (out_idx + 2 >= output_size)
                break;
            output[out_idx++] = '\\';
            output[out_idx++] = '\\';
        } else {
            output[out_idx++] = str[in_idx];
        }
        in_idx++;
    }

    output[out_idx] = '\0';
    return LLE_SUCCESS;
}

/**
 * @brief Unescape special characters from TSV format
 *
 * Unescapes: \\\\t -> \\t, \\\\n -> \\n, \\\\\\\\ -> \\\\
 *
 * @param str Input escaped string
 * @param output Output buffer for unescaped string
 * @param output_size Size of output buffer in bytes
 * @return LLE_SUCCESS on success, or LLE_ERROR_INVALID_PARAMETER if parameters
 * are invalid
 */
static lle_result_t lle_unescape_string(const char *str, char *output,
                                        size_t output_size) {
    if (!str || !output || output_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t out_idx = 0;
    size_t in_idx = 0;

    while (str[in_idx] != '\0' && out_idx < output_size - 1) {
        if (str[in_idx] == '\\' && str[in_idx + 1] != '\0') {
            if (str[in_idx + 1] == 't') {
                output[out_idx++] = '\t';
                in_idx += 2;
            } else if (str[in_idx + 1] == 'n') {
                output[out_idx++] = '\n';
                in_idx += 2;
            } else if (str[in_idx + 1] == '\\') {
                output[out_idx++] = '\\';
                in_idx += 2;
            } else {
                output[out_idx++] = str[in_idx++];
            }
        } else {
            output[out_idx++] = str[in_idx++];
        }
    }

    output[out_idx] = '\0';
    return LLE_SUCCESS;
}

/**
 * @brief Format history entry as TSV line
 *
 * Format: TIMESTAMP\\tCOMMAND\\tEXIT_CODE\\tWORKING_DIR\\n
 *
 * @param entry History entry to format
 * @param line Output buffer for formatted line
 * @param line_size Size of output buffer in bytes
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters are
 * invalid, LLE_ERROR_OUT_OF_MEMORY on allocation failure, or
 * LLE_ERROR_BUFFER_OVERFLOW if line too long
 */
static lle_result_t lle_history_format_entry(const lle_history_entry_t *entry,
                                             char *line, size_t line_size) {
    if (!entry || !line || line_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate temporary buffers for escaped strings
    char *escaped_cmd = lle_pool_alloc(LLE_HISTORY_MAX_COMMAND_LENGTH * 2);
    char *escaped_wd = lle_pool_alloc(LLE_HISTORY_MAX_PATH_LENGTH * 2);

    if (!escaped_cmd || !escaped_wd) {
        if (escaped_cmd)
            lle_pool_free(escaped_cmd);
        if (escaped_wd)
            lle_pool_free(escaped_wd);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history storage allocation failed");
    }

    /// Escape command and working directory
    lle_escape_string(entry->command, escaped_cmd,
                      LLE_HISTORY_MAX_COMMAND_LENGTH * 2);

    const char *wd = entry->working_directory ? entry->working_directory : "";
    lle_escape_string(wd, escaped_wd, LLE_HISTORY_MAX_PATH_LENGTH * 2);

    /// Format line
    int written = snprintf(line, line_size, "%lu\t%s\t%d\t%s\n",
                           (unsigned long)entry->timestamp, escaped_cmd,
                           entry->exit_code, escaped_wd);

    lle_pool_free(escaped_cmd);
    lle_pool_free(escaped_wd);

    if (written < 0 || (size_t)written >= line_size) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Parse TSV line into history entry
 *
 * Parses a tab-separated line and creates a history entry.
 * Comments (lines starting with #) and empty lines are skipped.
 *
 * @param line Input TSV line to parse
 * @param entry Output pointer for created entry (NULL if line is comment/empty)
 * @param memory_pool Memory pool for allocation (currently uses global pool)
 * @return LLE_SUCCESS on success, or LLE_ERROR_INVALID_PARAMETER if parameters
 * are invalid
 */
static lle_result_t lle_history_parse_line(const char *line,
                                           lle_history_entry_t **entry,
                                           lle_memory_pool_t *memory_pool) {
    if (!line || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') {
        *entry = NULL;
        return LLE_SUCCESS;
    }

    /// Parse fields
    uint64_t timestamp = 0;
    char cmd_buffer[LLE_HISTORY_MAX_COMMAND_LENGTH * 2];
    int exit_code = 0;
    char wd_buffer[LLE_HISTORY_MAX_PATH_LENGTH * 2];

    int parsed =
        sscanf(line, "%lu\t%[^\t]\t%d\t%[^\n]", (unsigned long *)&timestamp,
               cmd_buffer, &exit_code, wd_buffer);

    if (parsed < 2) {
        /// Malformed line - skip it
        *entry = NULL;
        return LLE_SUCCESS;
    }

    /// Unescape command
    char unescaped_cmd[LLE_HISTORY_MAX_COMMAND_LENGTH];
    lle_unescape_string(cmd_buffer, unescaped_cmd, sizeof(unescaped_cmd));

    /// Create entry
    lle_result_t result =
        lle_history_entry_create(entry, unescaped_cmd, memory_pool);
    if (result != LLE_SUCCESS) {
        return result;
    }

    (*entry)->timestamp = timestamp;
    (*entry)->exit_code = exit_code;

    /// Set working directory if present
    if (parsed >= 4 && wd_buffer[0] != '\0') {
        char unescaped_wd[LLE_HISTORY_MAX_PATH_LENGTH];
        lle_unescape_string(wd_buffer, unescaped_wd, sizeof(unescaped_wd));

        /// Free the working_directory allocated by lle_history_entry_create
        if ((*entry)->working_directory) {
            lle_pool_free((*entry)->working_directory);
        }

        size_t wd_len = strlen(unescaped_wd) + 1;
        (*entry)->working_directory = lle_pool_alloc(wd_len);
        if ((*entry)->working_directory) {
            memcpy((*entry)->working_directory, unescaped_wd, wd_len);
        }
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * SAVE OPERATIONS
 * ============================================================================
 */

/**
 * @brief Save all history entries to file
 *
 * Writes all history entries to a TSV file with locking for multi-process
 * safety.
 *
 * @param core History core engine containing entries to save
 * @param file_path Path to history file to write
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters are
 * invalid, LLE_ERROR_IO_ERROR on file operations failure, or other error codes
 */
lle_result_t lle_history_save_to_file(lle_history_core_t *core,
                                      const char *file_path) {
    if (!core || !file_path) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_rwlock_rdlock(&core->lock);

    /// Open file for writing
    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        pthread_rwlock_unlock(&core->lock);
        return LLE_ERROR_IO_ERROR;
    }

    /// Acquire lock
    lle_result_t result = lle_history_file_lock(fd);
    if (result != LLE_SUCCESS) {
        close(fd);
        pthread_rwlock_unlock(&core->lock);
        return result;
    }

    /// Write header
    char header[256];
    snprintf(header, sizeof(header), "%s%s\n# Generated: %lu\n# Entries: %zu\n",
             LLE_HISTORY_MAGIC_HEADER, LLE_HISTORY_FILE_VERSION_STR,
             (unsigned long)time(NULL), core->entry_count);

    if (write(fd, header, strlen(header)) < 0) {
        lle_history_file_unlock(fd);
        close(fd);
        pthread_rwlock_unlock(&core->lock);
        return LLE_ERROR_IO_ERROR;
    }

    /// Write entries
    char *line_buffer = lle_pool_alloc(LLE_HISTORY_MAX_LINE_LENGTH);
    if (!line_buffer) {
        lle_history_file_unlock(fd);
        close(fd);
        pthread_rwlock_unlock(&core->lock);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history storage allocation failed");
    }

    for (size_t i = 0; i < core->entry_count; i++) {
        lle_history_entry_t *entry = core->entries[i];
        if (!entry)
            continue;

        /// Skip deleted entries - they were removed by deduplication
        if (entry->state == LLE_HISTORY_STATE_DELETED)
            continue;

        result = lle_history_format_entry(entry, line_buffer,
                                          LLE_HISTORY_MAX_LINE_LENGTH);
        if (result != LLE_SUCCESS) {
            continue; /// Skip malformed entries
        }

        if (write(fd, line_buffer, strlen(line_buffer)) < 0) {
            lle_pool_free(line_buffer);
            lle_history_file_unlock(fd);
            close(fd);
            pthread_rwlock_unlock(&core->lock);
            return LLE_ERROR_IO_ERROR;
        }
    }

    lle_pool_free(line_buffer);

    /// Update statistics
    core->stats.save_count++;

    /// Release lock and close
    lle_history_file_unlock(fd);
    close(fd);

    pthread_rwlock_unlock(&core->lock);

    return LLE_SUCCESS;
}

/**
 * @brief Append single entry to history file
 *
 * For incremental saves without rewriting entire file.
 * Uses file locking for multi-process safety.
 *
 * @param entry History entry to append
 * @param file_path Path to history file to append to
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters are
 * invalid, LLE_ERROR_IO_ERROR on file operations failure, or other error codes
 */
lle_result_t lle_history_append_entry(const lle_history_entry_t *entry,
                                      const char *file_path) {
    if (!entry || !file_path) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Open file for appending
    int fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        return LLE_ERROR_IO_ERROR;
    }

    /// Acquire lock
    lle_result_t result = lle_history_file_lock(fd);
    if (result != LLE_SUCCESS) {
        close(fd);
        return result;
    }

    /// Format and write entry
    char *line_buffer = lle_pool_alloc(LLE_HISTORY_MAX_LINE_LENGTH);
    if (!line_buffer) {
        lle_history_file_unlock(fd);
        close(fd);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history storage allocation failed");
    }

    result = lle_history_format_entry(entry, line_buffer,
                                      LLE_HISTORY_MAX_LINE_LENGTH);
    if (result != LLE_SUCCESS) {
        lle_pool_free(line_buffer);
        lle_history_file_unlock(fd);
        close(fd);
        return result;
    }

    if (write(fd, line_buffer, strlen(line_buffer)) < 0) {
        lle_pool_free(line_buffer);
        lle_history_file_unlock(fd);
        close(fd);
        return LLE_ERROR_IO_ERROR;
    }

    lle_pool_free(line_buffer);

    /// Release lock and close
    lle_history_file_unlock(fd);
    close(fd);

    return LLE_SUCCESS;
}

/* ============================================================================
 * LOAD OPERATIONS
 * ============================================================================
 */

/**
 * @brief Load history entries from file
 *
 * Reads history entries from a TSV file and populates the history core.
 * If the file does not exist, returns success with empty history.
 *
 * @param core History core engine to populate with loaded entries
 * @param file_path Path to history file to read
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters are
 * invalid, LLE_ERROR_IO_ERROR on file read failure, or other error codes
 */
lle_result_t lle_history_load_from_file(lle_history_core_t *core,
                                        const char *file_path) {
    if (!core || !file_path) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Check if file exists
    struct stat st;
    if (stat(file_path, &st) != 0) {
        /// File doesn't exist - not an error, just empty history
        return LLE_SUCCESS;
    }

    /// Open file for reading
    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        return LLE_ERROR_IO_ERROR;
    }

    pthread_rwlock_wrlock(&core->lock);

    /// Read and parse lines
    char *line_buffer = lle_pool_alloc(LLE_HISTORY_MAX_LINE_LENGTH);
    if (!line_buffer) {
        fclose(fp);
        pthread_rwlock_unlock(&core->lock);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history storage allocation failed");
    }

    size_t loaded_count = 0;
    (void)loaded_count; /// Reserved for load statistics
    size_t skipped_count = 0;
    (void)skipped_count; /// Reserved for skip statistics

    while (fgets(line_buffer, LLE_HISTORY_MAX_LINE_LENGTH, fp) != NULL) {
        lle_history_entry_t *entry = NULL;
        lle_result_t result =
            lle_history_parse_line(line_buffer, &entry, core->memory_pool);

        if (result != LLE_SUCCESS) {
            skipped_count++;
            continue;
        }

        if (!entry) {
            /// Comment or empty line
            continue;
        }

        /// Manually add to history without calling lle_history_add_entry
        /// (to avoid deadlock - we already have write lock)

        /// Check capacity
        if (core->entry_count >= core->entry_capacity) {
            result = lle_history_expand_capacity(core);
            if (result != LLE_SUCCESS) {
                lle_history_entry_destroy(entry, core->memory_pool);
                break;
            }
        }

        /// Assign entry ID
        entry->entry_id = core->next_entry_id++;

        /// Add to array
        core->entries[core->entry_count] = entry;

        /// Update linked list
        if (core->last_entry) {
            core->last_entry->next = entry;
            entry->prev = core->last_entry;
        } else {
            core->first_entry = entry;
        }
        core->last_entry = entry;

        core->entry_count++;

        /// Add to index if enabled
        if (core->entry_lookup) {
            lle_history_index_insert(core->entry_lookup, entry->entry_id,
                                     entry);
        }

        /// Update statistics
        core->stats.total_entries++;
        core->stats.active_entries++;

        loaded_count++;
    }

    lle_pool_free(line_buffer);
    fclose(fp);

    /// Update statistics
    core->stats.load_count++;

    pthread_rwlock_unlock(&core->lock);

    return LLE_SUCCESS;
}
