#include "posix_history.h"
#include "lush.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @file posix_history.c
 * @brief Enhanced POSIX-Compliant History Management Implementation
 *
 * This module implements the complete POSIX-compliant history management
 * system for Lush shell, including the fc command and enhanced history
 * builtin with bash/zsh compatibility.
 *
 * The implementation follows POSIX.1-2017 specifications for history
 * numbering, range handling, and command execution while providing
 * enhanced features for modern shell usage.
 *
 * @author Michael Berry
 * @version 1.0
 */

/// ============================================================================
/// Global State and Error Handling
/// ============================================================================

/**
 * @brief Global error message storage
 */
static char posix_history_error_message[512] = {0};

/**
 * @brief Debug mode flag
 */
static bool posix_history_debug_enabled = false;

/**
 * @brief Debug logging macro
 */
#define POSIX_HISTORY_DEBUG(fmt, ...)                                          \
    do {                                                                       \
        if (posix_history_debug_enabled) {                                     \
            fprintf(stderr, "[POSIX_HISTORY] " fmt "\n", ##__VA_ARGS__);       \
        }                                                                      \
    } while (0)

/**
 * @brief Set error message for debugging
 *
 * @param error Error message to store
 */
static void posix_history_set_error(const char *error) {
    if (error && strlen(error) < sizeof(posix_history_error_message)) {
        strncpy(posix_history_error_message, error,
                sizeof(posix_history_error_message) - 1);
        posix_history_error_message[sizeof(posix_history_error_message) - 1] =
            '\0';
    }
}

/**
 * @brief Clear error message
 */
static void posix_history_clear_error(void) {
    posix_history_error_message[0] = '\0';
}

/// ============================================================================
/// History Entry Management
/// ============================================================================

/**
 * @brief Create a new history entry
 *
 * @param command Command text
 * @param number History number
 * @return Pointer to new entry, or NULL on failure
 */
static posix_history_entry_t *posix_history_entry_create(const char *command,
                                                         int number) {
    if (!command) {
        return NULL;
    }

    posix_history_entry_t *entry = malloc(sizeof(posix_history_entry_t));
    if (!entry) {
        posix_history_set_error("Failed to allocate memory for history entry");
        return NULL;
    }

    entry->command = strdup(command);
    if (!entry->command) {
        free(entry);
        posix_history_set_error("Failed to allocate memory for command text");
        return NULL;
    }

    entry->number = number;
    entry->timestamp = time(NULL);
    entry->length = strlen(command);
    entry->modified = false;

    return entry;
}

/**
 * @brief Destroy a history entry
 *
 * @param entry Entry to destroy
 */
MAYBE_UNUSED
static void posix_history_entry_destroy(posix_history_entry_t *entry) {
    if (entry) {
        free(entry->command);
        free(entry);
    }
}

/**
 * Clean up a history entry that is part of an array (don't free the entry
 * itself)
 *
 * @param entry Entry to clean up (part of array, don't free the struct)
 */
static void posix_history_entry_cleanup(posix_history_entry_t *entry) {
    if (entry) {
        free(entry->command);
        entry->command = NULL;
        entry->length = 0;
    }
}

/// ============================================================================
/// Core History Management Functions
/// ============================================================================

posix_history_manager_t *posix_history_create(size_t capacity) {
    posix_history_clear_error();

    if (capacity == 0) {
        capacity = POSIX_HISTORY_MAX_ENTRIES;
    }

    if (capacity > POSIX_HISTORY_MAX_ENTRIES) {
        capacity = POSIX_HISTORY_MAX_ENTRIES;
    }

    if (capacity < POSIX_HISTORY_MIN_ENTRIES) {
        capacity = POSIX_HISTORY_MIN_ENTRIES;
    }

    posix_history_manager_t *manager = malloc(sizeof(posix_history_manager_t));
    if (!manager) {
        posix_history_set_error(
            "Failed to allocate memory for history manager");
        return NULL;
    }

    manager->entries = malloc(capacity * sizeof(posix_history_entry_t));
    if (!manager->entries) {
        free(manager);
        posix_history_set_error(
            "Failed to allocate memory for history entries");
        return NULL;
    }

    manager->count = 0;
    manager->capacity = capacity;
    manager->next_number = 1;
    manager->base_number = 1;
    manager->wraparound_occurred = false;
    manager->filename = NULL;
    manager->last_save = 0;
    manager->no_duplicates = false;

    POSIX_HISTORY_DEBUG("Created history manager with capacity %zu", capacity);
    return manager;
}

void posix_history_destroy(posix_history_manager_t *manager) {
    if (!manager) {
        return;
    }

    POSIX_HISTORY_DEBUG("Destroying history manager with %zu entries",
                        manager->count);

    /// Save history if filename is set
    if (manager->filename) {
        posix_history_save(manager, NULL, false);
    }

    /// Free all entries
    for (size_t i = 0; i < manager->count; i++) {
        posix_history_entry_cleanup(&manager->entries[i]);
    }

    free(manager->entries);
    free(manager->filename);
    free(manager);
}

int posix_history_add(posix_history_manager_t *manager, const char *command) {
    if (!manager || !command) {
        posix_history_set_error("Invalid parameters for history add");
        return -1;
    }

    /// Skip empty commands and whitespace-only commands
    const char *trimmed = command;
    while (*trimmed && isspace(*trimmed)) {
        trimmed++;
    }
    if (!*trimmed) {
        return -1;
    }

    /// Check for duplicates if enabled
    if (manager->no_duplicates && manager->count > 0) {
        posix_history_entry_t *last = &manager->entries[manager->count - 1];
        if (strcmp(last->command, command) == 0) {
            /// Update timestamp of existing entry instead of adding duplicate
            last->timestamp = time(NULL);
            POSIX_HISTORY_DEBUG("Updated timestamp of duplicate entry: %s",
                                command);
            return last->number;
        }
    }

    /// Handle capacity overflow
    if (manager->count >= manager->capacity) {
        /// Remove oldest entry
        posix_history_entry_cleanup(&manager->entries[0]);

        /// Shift all entries down
        memmove(&manager->entries[0], &manager->entries[1],
                (manager->count - 1) * sizeof(posix_history_entry_t));
        manager->count--;
    }

    /// Assign next number with wraparound handling
    int number = manager->next_number;
    manager->next_number++;

    if (manager->next_number > POSIX_HISTORY_WRAPAROUND_LIMIT) {
        manager->next_number = 1;
        manager->wraparound_occurred = true;
        POSIX_HISTORY_DEBUG("History number wraparound occurred");
    }

    /// Create and add new entry
    posix_history_entry_t *entry = posix_history_entry_create(command, number);
    if (!entry) {
        return -1;
    }

    manager->entries[manager->count] = *entry;
    free(entry); /// We copied the struct, so free the container
    manager->count++;

    POSIX_HISTORY_DEBUG("Added history entry %d: %s", number, command);
    return number;
}

posix_history_entry_t *posix_history_get(posix_history_manager_t *manager,
                                         int number) {
    if (!manager || number <= 0) {
        return NULL;
    }

    /// Linear search for the number (could be optimized with indexing)
    for (size_t i = 0; i < manager->count; i++) {
        if (manager->entries[i].number == number) {
            return &manager->entries[i];
        }
    }

    return NULL;
}

posix_history_entry_t *
posix_history_get_by_index(posix_history_manager_t *manager, size_t index) {
    if (!manager || index >= manager->count) {
        return NULL;
    }

    return &manager->entries[index];
}

bool posix_history_delete(posix_history_manager_t *manager, int number) {
    if (!manager || number <= 0) {
        posix_history_set_error("Invalid parameters for history delete");
        return false;
    }

    /// Find the entry to delete
    for (size_t i = 0; i < manager->count; i++) {
        if (manager->entries[i].number == number) {
            /// Clean up the entry
            posix_history_entry_cleanup(&manager->entries[i]);

            /// Shift remaining entries down
            if (i < manager->count - 1) {
                memmove(&manager->entries[i], &manager->entries[i + 1],
                        (manager->count - i - 1) *
                            sizeof(posix_history_entry_t));
            }

            manager->count--;
            POSIX_HISTORY_DEBUG("Deleted history entry %d", number);
            return true;
        }
    }

    posix_history_set_error("History entry not found");
    return false;
}

bool posix_history_clear(posix_history_manager_t *manager) {
    if (!manager) {
        posix_history_set_error("Invalid manager for history clear");
        return false;
    }

    /// Destroy all entries
    for (size_t i = 0; i < manager->count; i++) {
        posix_history_entry_cleanup(&manager->entries[i]);
    }

    manager->count = 0;
    manager->next_number = 1;
    manager->base_number = 1;
    manager->wraparound_occurred = false;

    POSIX_HISTORY_DEBUG("Cleared all history entries");
    return true;
}

/// ============================================================================
/// Range and Number Management
/// ============================================================================

bool posix_history_parse_range(posix_history_manager_t *manager,
                               const char *first_str, const char *last_str,
                               posix_history_range_t *range) {
    if (!manager || !range) {
        posix_history_set_error("Invalid parameters for range parsing");
        return false;
    }

    posix_history_clear_error();
    memset(range, 0, sizeof(*range));

    /// If no range specified, default to most recent entry
    if (!first_str && !last_str) {
        if (manager->count == 0) {
            posix_history_set_error("No history available");
            return false;
        }
        range->first = manager->entries[manager->count - 1].number;
        range->last = range->first;
        range->first_specified = false;
        range->last_specified = false;
        range->valid = true;
        return true;
    }

    /// Parse first specifier
    if (first_str) {
        range->first = posix_history_resolve_number(manager, first_str);
        range->first_specified = true;
        if (range->first == -1) {
            posix_history_set_error("Invalid first range specifier");
            return false;
        }
    } else {
        /// Default first to most recent
        if (manager->count > 0) {
            range->first = manager->entries[manager->count - 1].number;
        }
        range->first_specified = false;
    }

    /// Parse last specifier
    if (last_str) {
        range->last = posix_history_resolve_number(manager, last_str);
        range->last_specified = true;
        if (range->last == -1) {
            posix_history_set_error("Invalid last range specifier");
            return false;
        }
    } else {
        /// Default last to first
        range->last = range->first;
        range->last_specified = false;
    }

    /// Validate range
    if (range->first > range->last) {
        posix_history_set_error("Invalid range: first > last");
        return false;
    }

    range->valid = true;
    POSIX_HISTORY_DEBUG("Parsed range: %d to %d", range->first, range->last);
    return true;
}

int posix_history_resolve_number(posix_history_manager_t *manager,
                                 const char *spec) {
    if (!manager || !spec) {
        return -1;
    }

    /// Handle negative offsets (-1 = last, -2 = second-to-last, etc.)
    if (spec[0] == '-' && isdigit(spec[1])) {
        int offset = atoi(spec + 1);
        if (offset <= 0 || (size_t)offset > manager->count) {
            return -1;
        }
        return manager->entries[manager->count - offset].number;
    }

    /// Handle direct numeric specifications
    if (isdigit(spec[0])) {
        int number = atoi(spec);
        /// Verify this number exists
        if (posix_history_get(manager, number)) {
            return number;
        }
        return -1;
    }

    /// Handle string prefixes - find most recent command starting with spec
    for (int i = (int)manager->count - 1; i >= 0; i--) {
        if (strncmp(manager->entries[i].command, spec, strlen(spec)) == 0) {
            return manager->entries[i].number;
        }
    }

    return -1;
}

bool posix_history_get_valid_range(posix_history_manager_t *manager,
                                   int *min_number, int *max_number) {
    if (!manager || !min_number || !max_number) {
        return false;
    }

    if (manager->count == 0) {
        return false;
    }

    *min_number = manager->entries[0].number;
    *max_number = manager->entries[manager->count - 1].number;

    return true;
}

/// ============================================================================
/// File Operations
/// ============================================================================

int posix_history_load(posix_history_manager_t *manager, const char *filename,
                       bool append) {
    if (!manager) {
        posix_history_set_error("Invalid manager for history load");
        return -1;
    }

    const char *file_to_use = filename ? filename : manager->filename;
    if (!file_to_use) {
        posix_history_set_error("No filename specified for history load");
        return -1;
    }

    FILE *fp = fopen(file_to_use, "r");
    if (!fp) {
        if (errno == ENOENT) {
            POSIX_HISTORY_DEBUG("History file does not exist: %s", file_to_use);
            return 0; /// Not an error, just no history yet
        }
        posix_history_set_error("Failed to open history file for reading");
        return -1;
    }

    if (!append) {
        posix_history_clear(manager);
    }

    int loaded = 0;
    char line[POSIX_HISTORY_MAX_COMMAND_LENGTH];

    while (fgets(line, sizeof(line), fp)) {
        /// Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        /// Skip empty lines
        if (line[0] == '\0') {
            continue;
        }

        /// Add to history
        if (posix_history_add(manager, line) != -1) {
            loaded++;
        }
    }

    fclose(fp);
    POSIX_HISTORY_DEBUG("Loaded %d history entries from %s", loaded,
                        file_to_use);
    return loaded;
}

int posix_history_save(posix_history_manager_t *manager, const char *filename,
                       bool include_timestamps) {
    if (!manager) {
        posix_history_set_error("Invalid manager for history save");
        return -1;
    }

    const char *file_to_use = filename ? filename : manager->filename;
    if (!file_to_use) {
        posix_history_set_error("No filename specified for history save");
        return -1;
    }

    /// Create backup if file exists
    struct stat st;
    if (stat(file_to_use, &st) == 0) {
        char backup_name[1024];
        snprintf(backup_name, sizeof(backup_name), "%s.backup", file_to_use);
        if (rename(file_to_use, backup_name) != 0) {
            POSIX_HISTORY_DEBUG("Warning: Could not create backup of %s",
                                file_to_use);
        }
    }

    FILE *fp = fopen(file_to_use, "w");
    if (!fp) {
        posix_history_set_error("Failed to open history file for writing");
        return -1;
    }

    int saved = 0;
    for (size_t i = 0; i < manager->count; i++) {
        if (include_timestamps) {
            fprintf(fp, "#%ld\n", (long)manager->entries[i].timestamp);
        }
        fprintf(fp, "%s\n", manager->entries[i].command);
        saved++;
    }

    fclose(fp);
    manager->last_save = time(NULL);

    POSIX_HISTORY_DEBUG("Saved %d history entries to %s", saved, file_to_use);
    return saved;
}

int posix_history_append_new(posix_history_manager_t *manager,
                             const char *filename) {
    if (!manager) {
        posix_history_set_error("Invalid manager for history append");
        return -1;
    }

    const char *file_to_use = filename ? filename : manager->filename;
    if (!file_to_use) {
        posix_history_set_error("No filename specified for history append");
        return -1;
    }

    FILE *fp = fopen(file_to_use, "a");
    if (!fp) {
        posix_history_set_error("Failed to open history file for appending");
        return -1;
    }

    int appended = 0;
    time_t current_time = time(NULL);

    for (size_t i = 0; i < manager->count; i++) {
        /// Only append entries newer than last save
        if (manager->entries[i].timestamp > manager->last_save) {
            fprintf(fp, "%s\n", manager->entries[i].command);
            appended++;
        }
    }

    fclose(fp);
    manager->last_save = current_time;

    POSIX_HISTORY_DEBUG("Appended %d new entries to %s", appended, file_to_use);
    return appended;
}

int posix_history_read_new(posix_history_manager_t *manager,
                           const char *filename) {
    /// For now, just reload all entries
    /// A more sophisticated implementation would track file position
    return posix_history_load(manager, filename, true);
}

/// ============================================================================
/// Utility and Configuration Functions
/// ============================================================================

bool posix_history_set_filename(posix_history_manager_t *manager,
                                const char *filename) {
    if (!manager) {
        return false;
    }

    free(manager->filename);
    manager->filename = filename ? strdup(filename) : NULL;

    return true;
}

void posix_history_set_no_duplicates(posix_history_manager_t *manager,
                                     bool no_duplicates) {
    if (manager) {
        manager->no_duplicates = no_duplicates;
        POSIX_HISTORY_DEBUG("Set no_duplicates to %s",
                            no_duplicates ? "true" : "false");
    }
}

bool posix_history_get_stats(posix_history_manager_t *manager,
                             size_t *total_entries, int *current_number,
                             int *wraparound_count) {
    if (!manager || !total_entries || !current_number || !wraparound_count) {
        return false;
    }

    *total_entries = manager->count;
    *current_number = manager->next_number - 1;
    *wraparound_count = manager->wraparound_occurred ? 1 : 0;

    return true;
}

bool posix_history_validate(posix_history_manager_t *manager) {
    if (!manager) {
        return false;
    }

    /// Check basic constraints
    if (manager->count > manager->capacity) {
        return false;
    }

    /// Check entry consistency
    for (size_t i = 0; i < manager->count; i++) {
        if (!manager->entries[i].command) {
            return false;
        }
        if (manager->entries[i].length != strlen(manager->entries[i].command)) {
            return false;
        }
    }

    return true;
}

/// ============================================================================
/// Editor Integration
/// ============================================================================

char *posix_history_get_default_editor(void) {
    const char *editor = getenv("FCEDIT");
    if (editor && *editor) {
        return strdup(editor);
    }

    editor = getenv("EDITOR");
    if (editor && *editor) {
        return strdup(editor);
    }

    /// Default to vi as per POSIX
    return strdup("vi");
}

bool posix_history_create_temp_file(const char *content, char **filename) {
    if (!content || !filename) {
        return false;
    }

    /// Create temporary file
    char template[] = "/tmp/lush_fc_XXXXXX";
    int fd = mkstemp(template);
    if (fd == -1) {
        posix_history_set_error("Failed to create temporary file");
        return false;
    }

    /// Write content
    size_t content_len = strlen(content);
    ssize_t written = write(fd, content, content_len);
    close(fd);

    if (written != (ssize_t)content_len) {
        unlink(template);
        posix_history_set_error("Failed to write to temporary file");
        return false;
    }

    *filename = strdup(template);
    return true;
}

char *posix_history_read_file_content(const char *filename) {
    if (!filename) {
        return NULL;
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return NULL;
    }

    /// Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    /// Allocate buffer
    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    /// Read content
    size_t read_size = fread(content, 1, size, fp);
    fclose(fp);

    content[read_size] = '\0';
    return content;
}

/// ============================================================================
/// Error Handling and Debugging
/// ============================================================================

const char *posix_history_get_last_error(void) {
    if (posix_history_error_message[0] != '\0') {
        return posix_history_error_message;
    }
    return NULL;
}

void posix_history_set_debug(bool enable) {
    posix_history_debug_enabled = enable;
    POSIX_HISTORY_DEBUG("Debug mode %s", enable ? "enabled" : "disabled");
}