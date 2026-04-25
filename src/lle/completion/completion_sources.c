/**
 * @file completion_sources.c
 * @brief LLE Completion Sources Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This module contains ONLY data retrieval logic - NO terminal I/O.
 * It bridges LLE completion system to Lush shell's existing data sources.
 */

#include "lle/completion/completion_sources.h"
#include "alias.h"
#include "builtins.h"
#include "ht.h"
#include <ctype.h>
#include <dirent.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* POSIX system variable */
extern char **environ;

/* ============================================================================
 * SHELL INTEGRATION FUNCTIONS (Strong Symbols)
 * ============================================================================
 */

/**
 * @brief Check if text is a shell builtin command
 *
 * Searches the builtins array to determine if the given text
 * matches a builtin command name.
 *
 * @param text Text to check
 * @return true if text is a builtin command, false otherwise
 */
bool lle_shell_is_builtin(const char *text) {
    if (!text) {
        return false;
    }

    for (size_t i = 0; i < builtins_count; i++) {
        if (strcmp(text, builtins[i].name) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Check if text is a shell alias
 *
 * Looks up the text in the alias table to determine if it
 * matches a defined alias name.
 *
 * @param text Text to check
 * @return true if text is an alias, false otherwise
 */
bool lle_shell_is_alias(const char *text) {
    if (!text) {
        return false;
    }

    /* lookup_alias returns pointer into hash table, do NOT free */
    const char *alias_value = lookup_alias(text);
    return (alias_value != NULL);
}

/* ============================================================================
 * BUILTIN COMMANDS SOURCE
 * ============================================================================
 */

/**
 * @brief Generate builtin command completions
 *
 * Scans the builtins array and adds matching entries to the result set.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Prefix string to match
 * @param result Completion result set to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_source_builtins(lle_memory_pool_t *memory_pool,
                                            const char *prefix,
                                            lle_completion_result_t *result) {
    if (!memory_pool || !prefix || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t prefix_len = strlen(prefix);

    for (size_t i = 0; i < builtins_count; i++) {
        if (strncmp(builtins[i].name, prefix, prefix_len) == 0) {
            lle_result_t res =
                lle_completion_result_add(result, builtins[i].name, " ",
                                          LLE_COMPLETION_TYPE_BUILTIN, 900);

            if (res != LLE_SUCCESS) {
                return res;
            }
        }
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * ALIASES SOURCE
 * ============================================================================
 */

/**
 * @brief Generate alias completions
 *
 * Enumerates the alias hash table and adds matching entries to the result set.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Prefix string to match
 * @param result Completion result set to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_source_aliases(lle_memory_pool_t *memory_pool,
                                           const char *prefix,
                                           lle_completion_result_t *result) {
    if (!memory_pool || !prefix || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!aliases) {
        return LLE_SUCCESS; // No aliases defined, not an error
    }

    size_t prefix_len = strlen(prefix);

    ht_enum_t *aliases_e = ht_strstr_enum_create(aliases);
    if (!aliases_e) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    const char *alias_name = NULL;
    const char *alias_value = NULL;

    while (ht_strstr_enum_next(aliases_e, &alias_name, &alias_value)) {
        if (alias_name && strncmp(alias_name, prefix, prefix_len) == 0) {
            lle_result_t res = lle_completion_result_add(
                result, alias_name, " ", LLE_COMPLETION_TYPE_ALIAS, 950);

            if (res != LLE_SUCCESS) {
                ht_strstr_enum_destroy(aliases_e);
                return res;
            }
        }
    }

    ht_strstr_enum_destroy(aliases_e);
    return LLE_SUCCESS;
}

/* ============================================================================
 * PATH COMMANDS SOURCE
 * ============================================================================
 */

/**
 * @brief Generate external command completions from PATH
 *
 * Scans directories in PATH environment variable for executable
 * files matching the prefix.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Prefix string to match
 * @param result Completion result set to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_source_commands(lle_memory_pool_t *memory_pool,
                                            const char *prefix,
                                            lle_completion_result_t *result) {
    if (!memory_pool || !prefix || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    char *path_env = getenv("PATH");
    if (!path_env) {
        return LLE_SUCCESS; // No PATH, not an error
    }

    char *path = strdup(path_env);
    if (!path) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    size_t prefix_len = strlen(prefix);
    char *dir = strtok(path, ":");
    lle_result_t final_result = LLE_SUCCESS;

    while (dir) {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                // Skip hidden files
                if (entry->d_name[0] == '.') {
                    continue;
                }

                // Check if name matches prefix
                if (strncmp(entry->d_name, prefix, prefix_len) != 0) {
                    continue;
                }

                // Build full path
                size_t path_size = strlen(dir) + strlen(entry->d_name) + 2;
                char *full_path = malloc(path_size);
                if (!full_path) {
                    continue; // Skip this entry on allocation failure
                }

                snprintf(full_path, path_size, "%s/%s", dir, entry->d_name);

                // Check if executable
                struct stat st;
                if (stat(full_path, &st) == 0 && (st.st_mode & S_IXUSR)) {
                    // Check if this command shadows a builtin or alias
                    // If so, store full path in description for smart insertion
                    const char *desc = NULL;
                    if (lle_shell_is_builtin(entry->d_name) ||
                        lle_shell_is_alias(entry->d_name)) {
                        desc = full_path;
                    }

                    lle_result_t res =
                        lle_completion_result_add_with_description(
                            result, entry->d_name, " ",
                            LLE_COMPLETION_TYPE_COMMAND, 800, desc);

                    if (res != LLE_SUCCESS && final_result == LLE_SUCCESS) {
                        final_result = res;
                    }
                }

                free(full_path);
            }
            closedir(d);
        }

        dir = strtok(NULL, ":");
    }

    free(path);
    return final_result;
}

/* ============================================================================
 * PATH EXPANSION HELPERS FOR COMPLETION
 * ============================================================================
 */

/**
 * @brief Expand tilde (~) to home directory for completion
 *
 * Handles:
 *   ~/path    -> /home/user/path
 *   ~user/path -> /home/user/path (if user exists)
 *
 * @param path The path to expand
 * @return Newly allocated expanded path, or NULL on error
 */
static char *lle_completion_expand_tilde(const char *path) {
    if (!path || path[0] != '~') {
        return path ? strdup(path) : NULL;
    }

    // Find the end of tilde expression (until '/' or end)
    const char *slash = strchr(path, '/');
    const char *rest = slash ? slash : "";
    size_t tilde_len = slash ? (size_t)(slash - path) : strlen(path);

    if (tilde_len == 1) {
        // Simple ~ expansion to $HOME
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : NULL;
        }
        if (!home) {
            return strdup(path); // Can't expand, return original
        }

        size_t result_len = strlen(home) + strlen(rest) + 1;
        char *result = malloc(result_len);
        if (result) {
            snprintf(result, result_len, "%s%s", home, rest);
        }
        return result;
    } else {
        // ~user expansion
        char *username = strndup(path + 1, tilde_len - 1);
        if (!username) {
            return strdup(path);
        }

        struct passwd *pw = getpwnam(username);
        free(username);

        if (!pw) {
            return strdup(path); // User not found, return original
        }

        size_t result_len = strlen(pw->pw_dir) + strlen(rest) + 1;
        char *result = malloc(result_len);
        if (result) {
            snprintf(result, result_len, "%s%s", pw->pw_dir, rest);
        }
        return result;
    }
}

/**
 * @brief Expand a single environment variable for completion
 *
 * Handles:
 *   $VAR/path -> /value/path
 *   ${VAR}/path -> /value/path
 *
 * @param path The path to expand
 * @return Newly allocated expanded path, or NULL on error
 */
static char *lle_completion_expand_variable(const char *path) {
    if (!path || path[0] != '$') {
        return path ? strdup(path) : NULL;
    }

    const char *var_start = path + 1;
    const char *var_end = NULL;
    const char *rest = NULL;

    if (var_start[0] == '{') {
        // ${VAR} format
        var_start++;
        var_end = strchr(var_start, '}');
        if (!var_end) {
            return strdup(path); // Unclosed brace, return original
        }
        rest = var_end + 1;
    } else {
        // $VAR format - variable name is alphanumeric + underscore
        var_end = var_start;
        while (*var_end &&
               (isalnum((unsigned char)*var_end) || *var_end == '_')) {
            var_end++;
        }
        rest = var_end;
    }

    if (var_end == var_start) {
        return strdup(path); // Empty variable name
    }

    // Extract variable name
    size_t var_len = var_end - var_start;
    char *var_name = strndup(var_start, var_len);
    if (!var_name) {
        return strdup(path);
    }

    // Look up value
    const char *value = getenv(var_name);
    free(var_name);

    if (!value) {
        return strdup(path); // Variable not set, return original
    }

    // Build result
    size_t result_len = strlen(value) + strlen(rest) + 1;
    char *result = malloc(result_len);
    if (result) {
        snprintf(result, result_len, "%s%s", value, rest);
    }
    return result;
}

/**
 * @brief Expand path for completion (tilde and variables)
 *
 * @param path The path to expand
 * @param original_prefix_len Output: length of the original unexpanded prefix
 * @return Newly allocated expanded path, or NULL on error
 */
static char *lle_completion_expand_path(const char *path,
                                        size_t *original_prefix_len) {
    if (!path) {
        if (original_prefix_len)
            *original_prefix_len = 0;
        return NULL;
    }

    if (original_prefix_len) {
        *original_prefix_len = strlen(path);
    }

    // Try tilde expansion first
    if (path[0] == '~') {
        return lle_completion_expand_tilde(path);
    }

    // Try variable expansion
    if (path[0] == '$') {
        return lle_completion_expand_variable(path);
    }

    // No expansion needed
    return strdup(path);
}

/* ============================================================================
 * FILES AND DIRECTORIES SOURCE
 * ============================================================================
 */

/**
 * @brief Internal file/directory completion implementation
 *
 * Scans directories for files and/or directories matching the prefix.
 * Supports tilde and variable expansion in paths.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Prefix string to match (may include path components)
 * @param result Completion result set to populate
 * @param directories_only If true, only return directories (for cd, rmdir)
 * @return LLE_SUCCESS or error code
 */
static lle_result_t lle_completion_source_files_internal(
    lle_memory_pool_t *memory_pool, const char *prefix,
    lle_completion_result_t *result, bool directories_only) {
    if (!memory_pool || !prefix || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Expand tilde and variables in the prefix for directory access
    size_t original_prefix_len = 0;
    char *expanded_prefix =
        lle_completion_expand_path(prefix, &original_prefix_len);
    if (!expanded_prefix) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Determine the original unexpanded directory prefix to preserve in results
    // e.g., "~/" or "$HOME/" should be preserved in completion output
    const char *original_dir_prefix = NULL;
    size_t original_dir_prefix_len = 0;
    const char *orig_last_slash = strrchr(prefix, '/');
    if (orig_last_slash) {
        original_dir_prefix_len =
            orig_last_slash - prefix + 1; // Include the slash
        original_dir_prefix = prefix;
    }

    // Extract directory and filename parts from expanded path
    const char *last_slash = strrchr(expanded_prefix, '/');
    const char *dir_path = ".";
    const char *file_prefix = expanded_prefix;

    char *dir_copy = NULL;
    if (last_slash) {
        size_t dir_len = last_slash - expanded_prefix;
        if (dir_len == 0) {
            dir_path = "/";
        } else {
            dir_copy = strndup(expanded_prefix, dir_len);
            if (dir_copy) {
                dir_path = dir_copy;
            }
        }
        file_prefix = last_slash + 1;
    }

    size_t file_prefix_len = strlen(file_prefix);
    bool show_hidden = (file_prefix[0] == '.');

    DIR *d = opendir(dir_path);
    if (!d) {
        free(dir_copy);
        free(expanded_prefix);
        return LLE_SUCCESS; // Directory doesn't exist, not an error
    }

    lle_result_t final_result = LLE_SUCCESS;
    struct dirent *entry;

    while ((entry = readdir(d)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Skip hidden files unless explicitly requested
        if (!show_hidden && entry->d_name[0] == '.') {
            continue;
        }

        // Check if name matches prefix
        if (strncmp(entry->d_name, file_prefix, file_prefix_len) != 0) {
            continue;
        }

        // Build full path for stat
        size_t path_size = strlen(dir_path) + strlen(entry->d_name) + 2;
        char *full_path = malloc(path_size);
        if (!full_path) {
            continue;
        }

        snprintf(full_path, path_size, "%s/%s", dir_path, entry->d_name);

        // Determine if directory or file
        struct stat st;
        bool is_dir = false;
        lle_completion_type_t type = LLE_COMPLETION_TYPE_FILE;
        const char *suffix = "";
        int32_t score = 600;

        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            is_dir = true;
            type = LLE_COMPLETION_TYPE_DIRECTORY;
            suffix = "/";
            score = 700;
        }

        free(full_path);

        // Skip files if directories_only is set
        if (directories_only && !is_dir) {
            continue;
        }

        // Build completion text preserving the original unexpanded prefix
        // e.g., ~/Documents instead of /home/user/Documents
        char *completion_text = NULL;
        if (original_dir_prefix && original_dir_prefix_len > 0) {
            // Use original unexpanded prefix (e.g., ~/, $HOME/)
            size_t text_size =
                original_dir_prefix_len + strlen(entry->d_name) + 1;
            completion_text = malloc(text_size);
            if (completion_text) {
                memcpy(completion_text, original_dir_prefix,
                       original_dir_prefix_len);
                strcpy(completion_text + original_dir_prefix_len,
                       entry->d_name);
            }
        } else if (last_slash && dir_copy) {
            // No expansion happened, use expanded dir_copy
            size_t text_size = strlen(dir_copy) + strlen(entry->d_name) + 2;
            completion_text = malloc(text_size);
            if (completion_text) {
                snprintf(completion_text, text_size, "%s/%s", dir_copy,
                         entry->d_name);
            }
        } else if (last_slash) { // Root directory
            size_t text_size = strlen(entry->d_name) + 2;
            completion_text = malloc(text_size);
            if (completion_text) {
                snprintf(completion_text, text_size, "/%s", entry->d_name);
            }
        }

        lle_result_t res = lle_completion_result_add(
            result, completion_text ? completion_text : entry->d_name, suffix,
            type, score);

        free(completion_text);

        if (res != LLE_SUCCESS && final_result == LLE_SUCCESS) {
            final_result = res;
        }
    }

    closedir(d);
    free(dir_copy);
    free(expanded_prefix);

    return final_result;
}

/**
 * @brief Generate file and directory completions
 *
 * Scans the filesystem for files and directories matching the prefix.
 * Supports tilde (~) and variable ($VAR) expansion in paths.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Prefix string to match
 * @param result Completion result set to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_source_files(lle_memory_pool_t *memory_pool,
                                         const char *prefix,
                                         lle_completion_result_t *result) {
    return lle_completion_source_files_internal(memory_pool, prefix, result,
                                                false);
}

/**
 * @brief Generate directory-only completions
 *
 * Scans the filesystem for directories matching the prefix.
 * Used for commands like cd and rmdir that only accept directories.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Prefix string to match
 * @param result Completion result set to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_completion_source_directories(lle_memory_pool_t *memory_pool,
                                  const char *prefix,
                                  lle_completion_result_t *result) {
    return lle_completion_source_files_internal(memory_pool, prefix, result,
                                                true);
}

/* ============================================================================
 * VARIABLES SOURCE
 * ============================================================================
 */

/**
 * @brief Generate environment variable completions
 *
 * Scans the environment and shell special variables for matches.
 * Includes both regular environment variables and special shell
 * variables like $?, $$, $!, etc.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Prefix string to match (without leading $)
 * @param result Completion result set to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_source_variables(lle_memory_pool_t *memory_pool,
                                             const char *prefix,
                                             lle_completion_result_t *result) {
    if (!memory_pool || !prefix || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t prefix_len = strlen(prefix);
    lle_result_t final_result = LLE_SUCCESS;

    // Complete environment variables
    for (char **env = environ; *env != NULL; env++) {
        const char *equals = strchr(*env, '=');
        if (!equals) {
            continue;
        }

        size_t var_len = equals - *env;
        if (var_len < prefix_len) {
            continue;
        }

        if (strncmp(*env, prefix, prefix_len) == 0) {
            char *var_name = strndup(*env, var_len);
            if (var_name) {
                lle_result_t res = lle_completion_result_add(
                    result, var_name, "", LLE_COMPLETION_TYPE_VARIABLE, 500);

                free(var_name);

                if (res != LLE_SUCCESS && final_result == LLE_SUCCESS) {
                    final_result = res;
                }
            }
        }
    }

    // Add common shell special variables
    const char *special_vars[] = {"?", "$", "!", "0", "#", "*", "@", "-", "_"};

    for (size_t i = 0; i < sizeof(special_vars) / sizeof(special_vars[0]);
         i++) {
        if (strncmp(special_vars[i], prefix, prefix_len) == 0) {
            lle_result_t res = lle_completion_result_add(
                result, special_vars[i], "", LLE_COMPLETION_TYPE_VARIABLE, 600);

            if (res != LLE_SUCCESS && final_result == LLE_SUCCESS) {
                final_result = res;
            }
        }
    }

    return final_result;
}

/* ============================================================================
 * HISTORY SOURCE
 * ============================================================================
 */

/**
 * @brief Generate history completions
 *
 * Provides completions from command history. Currently a placeholder
 * that returns success with no completions until history integration
 * is complete.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Prefix string to match
 * @param result Completion result set to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_source_history(lle_memory_pool_t *memory_pool,
                                           const char *prefix,
                                           lle_completion_result_t *result) {
    if (!memory_pool || !prefix || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // History completion integration will be added later
    // when history system is integrated with LLE
    // For now, return success with no completions

    return LLE_SUCCESS;
}

/* ============================================================================
 * SSH HOST SOURCE
 * ============================================================================
 */

#include "lle/completion/ssh_hosts.h"

/**
 * @brief Generate SSH host completions
 *
 * Provides completions from the SSH host cache, which includes
 * hosts from ~/.ssh/config and ~/.ssh/known_hosts.
 *
 * @param memory_pool Memory pool for allocations
 * @param prefix Prefix string to match
 * @param result Completion result set to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_source_ssh_hosts(lle_memory_pool_t *memory_pool,
                                             const char *prefix,
                                             lle_completion_result_t *result) {
    if (!memory_pool || !prefix || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    ssh_host_cache_t *cache = get_ssh_host_cache();
    if (!cache || cache->count == 0) {
        return LLE_SUCCESS;
    }

    size_t prefix_len = strlen(prefix);
    lle_result_t final_result = LLE_SUCCESS;

    for (size_t i = 0; i < cache->count; i++) {
        ssh_host_t *host = &cache->hosts[i];

        /* Try hostname match */
        if (strncmp(host->hostname, prefix, prefix_len) == 0) {
            /* Format: hostname or user@hostname */
            char completion[320];
            if (host->user[0]) {
                snprintf(completion, sizeof(completion), "%s@%s", host->user,
                         host->hostname);
            } else {
                snprintf(completion, sizeof(completion), "%s", host->hostname);
            }

            lle_result_t res = lle_completion_result_add(
                result, completion, " ", LLE_COMPLETION_TYPE_CUSTOM,
                800 + host->priority);

            if (res != LLE_SUCCESS && final_result == LLE_SUCCESS) {
                final_result = res;
            }
            continue;
        }

        /* Try alias match */
        if (host->alias[0] && strncmp(host->alias, prefix, prefix_len) == 0) {
            lle_result_t res = lle_completion_result_add(
                result, host->alias, " ", LLE_COMPLETION_TYPE_CUSTOM,
                850 + host->priority);

            if (res != LLE_SUCCESS && final_result == LLE_SUCCESS) {
                final_result = res;
            }
        }
    }

    return final_result;
}
