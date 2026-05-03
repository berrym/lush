/**
 * @file completion_sources.c
 * @brief LLE Completion Sources Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Shell data adapters. Sources read the dequoted filename prefix and,
 * for file/directory sources, the resolved expansion-prefix directory
 * from the supplied word context, and return candidates as filename
 * literals. Path-prefix preservation, escape rendering, and suffix
 * decisions are all the engine's job (handled by the splicer); this
 * module is intentionally narrow.
 */

#include "lle/completion/completion_sources.h"

#include "alias.h"
#include "builtins.h"
#include "ht.h"
#include "lle/completion/ssh_hosts.h"
#include "lle/unicode_compare.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

/* ============================================================================
 * Shell integration
 * ============================================================================ */

bool lle_shell_is_builtin(const char *text) {
    if (!text) return false;
    for (size_t i = 0; i < builtins_count; i++) {
        if (strcmp(text, builtins[i].name) == 0) return true;
    }
    return false;
}

bool lle_shell_is_alias(const char *text) {
    if (!text) return false;
    return lookup_alias(text) != NULL;
}

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/**
 * @brief Test whether `prefix` is a prefix of `candidate` using
 *        NFC-aware unicode comparison. Empty prefix matches any
 *        candidate.
 */
static bool nfc_prefix_match(const char *prefix, const char *candidate) {
    if (!prefix || !candidate) return false;
    size_t plen = strlen(prefix);
    if (plen == 0) return true;
    return lle_unicode_is_prefix(prefix, plen, candidate, strlen(candidate),
                                  &LLE_UNICODE_COMPARE_DEFAULT);
}

/**
 * @brief Resolve the directory a path-shaped source should scan.
 *
 * Returns context->expanded_directory if the engine has populated it
 * (path-shaped completion with a path prefix the engine resolved via
 * src/expand.c). Otherwise returns "." (cwd) as the fallback.
 */
static const char *source_scan_directory(const lle_word_context_t *context) {
    if (context->expanded_directory && context->expanded_directory[0])
        return context->expanded_directory;
    return ".";
}

/* ============================================================================
 * Source: builtin commands
 * ============================================================================ */

lle_result_t lle_completion_source_builtins(lle_memory_pool_t *pool,
                                            const lle_word_context_t *context,
                                            lle_completion_result_t *result) {
    if (!pool || !context || !result) return LLE_ERROR_INVALID_PARAMETER;
    const char *prefix = context->dequoted_filename_prefix;
    if (!prefix) return LLE_ERROR_INVALID_PARAMETER;

    for (size_t i = 0; i < builtins_count; i++) {
        if (nfc_prefix_match(prefix, builtins[i].name)) {
            lle_result_t r = lle_completion_result_add(
                result, builtins[i].name, NULL,
                LLE_COMPLETION_TYPE_BUILTIN, 900);
            if (r != LLE_SUCCESS) return r;
        }
    }
    return LLE_SUCCESS;
}

/* ============================================================================
 * Source: aliases
 * ============================================================================ */

lle_result_t lle_completion_source_aliases(lle_memory_pool_t *pool,
                                           const lle_word_context_t *context,
                                           lle_completion_result_t *result) {
    if (!pool || !context || !result) return LLE_ERROR_INVALID_PARAMETER;
    if (!aliases) return LLE_SUCCESS;

    const char *prefix = context->dequoted_filename_prefix;
    if (!prefix) return LLE_ERROR_INVALID_PARAMETER;

    ht_enum_t *iter = ht_strstr_enum_create(aliases);
    if (!iter) return LLE_ERROR_OUT_OF_MEMORY;

    const char *alias_name  = NULL;
    const char *alias_value = NULL;
    while (ht_strstr_enum_next(iter, &alias_name, &alias_value)) {
        if (alias_name && nfc_prefix_match(prefix, alias_name)) {
            lle_result_t r = lle_completion_result_add(
                result, alias_name, NULL, LLE_COMPLETION_TYPE_ALIAS, 950);
            if (r != LLE_SUCCESS) {
                ht_strstr_enum_destroy(iter);
                return r;
            }
        }
    }
    ht_strstr_enum_destroy(iter);
    return LLE_SUCCESS;
}

/* ============================================================================
 * Source: external commands from PATH
 * ============================================================================ */

lle_result_t lle_completion_source_commands(lle_memory_pool_t *pool,
                                            const lle_word_context_t *context,
                                            lle_completion_result_t *result) {
    if (!pool || !context || !result) return LLE_ERROR_INVALID_PARAMETER;
    const char *prefix = context->dequoted_filename_prefix;
    if (!prefix) return LLE_ERROR_INVALID_PARAMETER;

    char *path_env = getenv("PATH");
    if (!path_env) return LLE_SUCCESS;

    char *path = strdup(path_env);
    if (!path) return LLE_ERROR_OUT_OF_MEMORY;

    lle_result_t final_result = LLE_SUCCESS;
    char *dir = strtok(path, ":");
    while (dir) {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                if (!nfc_prefix_match(prefix, entry->d_name)) continue;

                size_t path_size = strlen(dir) + strlen(entry->d_name) + 2;
                char  *full_path = malloc(path_size);
                if (!full_path) continue;
                snprintf(full_path, path_size, "%s/%s", dir, entry->d_name);

                struct stat st;
                if (stat(full_path, &st) == 0 && (st.st_mode & S_IXUSR)) {
                    /* Shadowing detection: external commands that
                     * share a name with a builtin or alias get the
                     * full PATH-resolved path stored in description
                     * so the engine can splice the disambiguating
                     * absolute path on selection. */
                    const char *desc = NULL;
                    if (lle_shell_is_builtin(entry->d_name) ||
                        lle_shell_is_alias(entry->d_name)) {
                        desc = full_path;
                    }
                    lle_result_t r =
                        lle_completion_result_add_with_description(
                            result, entry->d_name, NULL,
                            LLE_COMPLETION_TYPE_COMMAND, 800, desc);
                    if (r != LLE_SUCCESS && final_result == LLE_SUCCESS)
                        final_result = r;
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
 * Source: files and directories
 * ============================================================================ */

static lle_result_t
files_internal(lle_memory_pool_t *pool, const lle_word_context_t *context,
               lle_completion_result_t *result, bool directories_only) {
    if (!pool || !context || !result) return LLE_ERROR_INVALID_PARAMETER;
    const char *prefix = context->dequoted_filename_prefix;
    if (!prefix) return LLE_ERROR_INVALID_PARAMETER;

    const char *dir_path  = source_scan_directory(context);
    bool        show_hidden = (prefix[0] == '.');

    DIR *d = opendir(dir_path);
    if (!d) return LLE_SUCCESS; /* unreadable directory is not an error */

    lle_result_t   final_result = LLE_SUCCESS;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (!show_hidden && entry->d_name[0] == '.') continue;
        if (!nfc_prefix_match(prefix, entry->d_name)) continue;

        size_t path_size = strlen(dir_path) + strlen(entry->d_name) + 2;
        char  *full_path = malloc(path_size);
        if (!full_path) continue;
        snprintf(full_path, path_size, "%s/%s", dir_path, entry->d_name);

        struct stat            st;
        bool                   is_dir = false;
        lle_completion_type_t  type   = LLE_COMPLETION_TYPE_FILE;
        int32_t                score  = 600;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            is_dir = true;
            type   = LLE_COMPLETION_TYPE_DIRECTORY;
            score  = 700;
        }
        free(full_path);

        if (directories_only && !is_dir) continue;

        /* Emit ONLY the filename literal. The splicer uses
         * filename_portion_start to splice this in at the right
         * byte offset, preserving the user's typed path-prefix
         * bytes (~/, $VAR/, etc.) verbatim. */
        lle_result_t r =
            lle_completion_result_add(result, entry->d_name, NULL, type, score);
        if (r != LLE_SUCCESS && final_result == LLE_SUCCESS) final_result = r;
    }
    closedir(d);
    return final_result;
}

lle_result_t lle_completion_source_files(lle_memory_pool_t *pool,
                                         const lle_word_context_t *context,
                                         lle_completion_result_t *result) {
    return files_internal(pool, context, result, false);
}

lle_result_t
lle_completion_source_directories(lle_memory_pool_t *pool,
                                  const lle_word_context_t *context,
                                  lle_completion_result_t *result) {
    return files_internal(pool, context, result, true);
}

/* ============================================================================
 * Source: variables
 * ============================================================================ */

lle_result_t lle_completion_source_variables(lle_memory_pool_t *pool,
                                             const lle_word_context_t *context,
                                             lle_completion_result_t *result) {
    if (!pool || !context || !result) return LLE_ERROR_INVALID_PARAMETER;
    const char *prefix = context->dequoted_filename_prefix;
    if (!prefix) return LLE_ERROR_INVALID_PARAMETER;

    /* Strip a leading '$' if the engine forwarded it; sources match
     * against the bare variable name. */
    if (prefix[0] == '$') prefix++;

    lle_result_t final_result = LLE_SUCCESS;

    for (char **env = environ; *env != NULL; env++) {
        const char *equals = strchr(*env, '=');
        if (!equals) continue;
        size_t var_len = (size_t)(equals - *env);
        char  *var_name = strndup(*env, var_len);
        if (!var_name) continue;
        if (nfc_prefix_match(prefix, var_name)) {
            lle_result_t r = lle_completion_result_add(
                result, var_name, NULL, LLE_COMPLETION_TYPE_VARIABLE, 500);
            if (r != LLE_SUCCESS && final_result == LLE_SUCCESS) final_result = r;
        }
        free(var_name);
    }

    /* Special variables ($?, $$, $!, etc.) are emitted as
     * single-character names. */
    static const char *special_vars[] = {"?", "$", "!", "0",
                                          "#", "*", "@", "-", "_"};
    for (size_t i = 0; i < sizeof(special_vars) / sizeof(special_vars[0]);
         i++) {
        if (nfc_prefix_match(prefix, special_vars[i])) {
            lle_result_t r = lle_completion_result_add(
                result, special_vars[i], NULL, LLE_COMPLETION_TYPE_VARIABLE, 600);
            if (r != LLE_SUCCESS && final_result == LLE_SUCCESS) final_result = r;
        }
    }

    return final_result;
}

/* ============================================================================
 * Source: history
 * ============================================================================ */

lle_result_t lle_completion_source_history(lle_memory_pool_t *pool,
                                           const lle_word_context_t *context,
                                           lle_completion_result_t *result) {
    if (!pool || !context || !result) return LLE_ERROR_INVALID_PARAMETER;
    /* History integration with LLE is a future concern; this source
     * currently produces no candidates. The architecture is in place
     * for it to be filled in without touching anything else. */
    return LLE_SUCCESS;
}

/* ============================================================================
 * Source: SSH hosts
 * ============================================================================ */

lle_result_t lle_completion_source_ssh_hosts(lle_memory_pool_t *pool,
                                             const lle_word_context_t *context,
                                             lle_completion_result_t *result) {
    if (!pool || !context || !result) return LLE_ERROR_INVALID_PARAMETER;
    const char *prefix = context->dequoted_filename_prefix;
    if (!prefix) return LLE_ERROR_INVALID_PARAMETER;

    ssh_host_cache_t *cache = get_ssh_host_cache();
    if (!cache || cache->count == 0) return LLE_SUCCESS;

    lle_result_t final_result = LLE_SUCCESS;
    for (size_t i = 0; i < cache->count; i++) {
        ssh_host_t *host = &cache->hosts[i];

        if (nfc_prefix_match(prefix, host->hostname)) {
            char completion[320];
            if (host->user[0]) {
                snprintf(completion, sizeof(completion), "%s@%s", host->user,
                         host->hostname);
            } else {
                snprintf(completion, sizeof(completion), "%s", host->hostname);
            }
            lle_result_t r = lle_completion_result_add(
                result, completion, NULL, LLE_COMPLETION_TYPE_CUSTOM,
                800 + host->priority);
            if (r != LLE_SUCCESS && final_result == LLE_SUCCESS) final_result = r;
            continue;
        }

        if (host->alias[0] && nfc_prefix_match(prefix, host->alias)) {
            lle_result_t r = lle_completion_result_add(
                result, host->alias, NULL, LLE_COMPLETION_TYPE_CUSTOM,
                850 + host->priority);
            if (r != LLE_SUCCESS && final_result == LLE_SUCCESS) final_result = r;
        }
    }
    return final_result;
}
