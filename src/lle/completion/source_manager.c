/**
 * @file source_manager.c
 * @brief LLE Source Manager Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Manages completion sources and orchestrates querying.
 */

#include "lle/completion/source_manager.h"
#include "lle/completion/builtin_completions.h" /* For builtin arg completions */
#include "lle/completion/completion_generator.h" /* For existing source functions */
#include "lle/completion/completion_sources.h" /* For lle_completion_source_aliases */
#include <string.h>

// ============================================================================
// SOURCE APPLICABILITY FUNCTIONS
// ============================================================================

/**
 * @brief Check if builtin source is applicable
 * @param context Context to check
 * @return true if at command position
 */
static bool builtin_source_applicable(const lle_context_analyzer_t *context) {
    return context->type == LLE_CONTEXT_COMMAND;
}

/**
 * @brief Check if alias source is applicable
 * @param context Context to check
 * @return true if at command position
 */
static bool alias_source_applicable(const lle_context_analyzer_t *context) {
    return context->type == LLE_CONTEXT_COMMAND;
}

/**
 * @brief Check if external command source is applicable
 * @param context Context to check
 * @return true if at command position
 */
static bool
external_command_source_applicable(const lle_context_analyzer_t *context) {
    return context->type == LLE_CONTEXT_COMMAND;
}

/**
 * @brief Check if prefix indicates a path for local executable
 *
 * Detects prefixes that indicate the user wants to run a local file:
 *   ./        - current directory
 *   ../       - parent directory
 *   ~/        - home directory
 *   /         - absolute path
 *   $VAR/     - variable expansion (e.g., $HOME/)
 *
 * @param prefix Prefix string to check
 * @return true if path prefix detected
 */
static bool is_path_prefix(const char *prefix) {
    if (!prefix || !prefix[0]) {
        return false;
    }
    /* Absolute path */
    if (prefix[0] == '/') {
        return true;
    }
    /* Home directory */
    if (prefix[0] == '~') {
        return true;
    }
    /* Variable expansion containing slash (e.g., $HOME/) */
    if (prefix[0] == '$' && strchr(prefix, '/')) {
        return true;
    }
    /* Relative paths: ./ or ../ */
    if (prefix[0] == '.') {
        if (prefix[1] == '/') {
            return true;
        }
        if (prefix[1] == '.' && prefix[2] == '/') {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if file source is applicable
 * @param context Context to check
 * @return true if argument, redirect, or command with path prefix
 */
static bool file_source_applicable(const lle_context_analyzer_t *context) {
    /* Always applicable for redirects */
    if (context->type == LLE_CONTEXT_REDIRECT) {
        return true;
    }
    /* For arguments, check builtin context */
    if (context->type == LLE_CONTEXT_ARGUMENT) {
        if (context->command_name) {
            const lle_builtin_completion_spec_t *spec =
                lle_builtin_get_spec(context->command_name);
            if (spec) {
                /* Check if builtin explicitly expects file/directory args */
                if (spec->default_arg_type == LLE_BUILTIN_ARG_FILE ||
                    spec->default_arg_type == LLE_BUILTIN_ARG_DIRECTORY) {
                    return true;
                }
                /* If builtin has subcommands, suppress generic file completion
                 */
                if (spec->subcommand_count > 0) {
                    return false;
                }
            }
        }
        return true;
    }
    /* Also applicable at command position if prefix indicates a path */
    if (context->type == LLE_CONTEXT_COMMAND &&
        is_path_prefix(context->partial_word)) {
        return true;
    }
    return false;
}

/**
 * @brief Check if variable source is applicable
 * @param context Context to check
 * @return true if variable context
 */
static bool variable_source_applicable(const lle_context_analyzer_t *context) {
    return context->type == LLE_CONTEXT_VARIABLE;
}

/**
 * @brief Check if history source is applicable
 * @param context Context to check (unused)
 * @return Always true (fallback source)
 */
static bool history_source_applicable(const lle_context_analyzer_t *context) {
    (void)context; /* Unused */
    return true;   /* Always available */
}

// ============================================================================
// SOURCE GENERATION FUNCTIONS
// ============================================================================

/**
 * @brief Generate builtin command completions
 * @param pool Memory pool
 * @param context Completion context (unused)
 * @param prefix Prefix to match
 * @param result Result set to populate
 * @return LLE_SUCCESS or error code
 */
static lle_result_t
builtin_source_generate(lle_memory_pool_t *pool,
                        const lle_context_analyzer_t *context,
                        const char *prefix, lle_completion_result_t *result) {
    (void)context; /* Not needed for builtins */

    /* Call ONLY the builtin source function to avoid duplicates */
    return lle_completion_source_builtins(pool, prefix, result);
}

/**
 * @brief Generate alias completions
 * @param pool Memory pool
 * @param context Completion context (unused)
 * @param prefix Prefix to match
 * @param result Result set to populate
 * @return LLE_SUCCESS or error code
 */
static lle_result_t alias_source_generate(lle_memory_pool_t *pool,
                                          const lle_context_analyzer_t *context,
                                          const char *prefix,
                                          lle_completion_result_t *result) {
    (void)context; /* Not needed for aliases */

    /* Call the alias source function */
    return lle_completion_source_aliases(pool, prefix, result);
}

/**
 * @brief Generate external command completions from PATH
 * @param pool Memory pool
 * @param context Completion context (unused)
 * @param prefix Prefix to match
 * @param result Result set to populate
 * @return LLE_SUCCESS or error code
 */
static lle_result_t external_command_source_generate(
    lle_memory_pool_t *pool, const lle_context_analyzer_t *context,
    const char *prefix, lle_completion_result_t *result) {
    (void)context; /* Not needed for commands */

    /* Call ONLY the external command source to avoid duplicates with builtins
     */
    return lle_completion_source_commands(pool, prefix, result);
}

/**
 * @brief Check if command expects only directory arguments
 * @param command_name Command name to check
 * @return true if command only accepts directories (e.g., cd, rmdir)
 */
static bool is_directory_only_command(const char *command_name) {
    if (!command_name) {
        return false;
    }

    /*
     * POSIX commands that only accept directory arguments.
     * Note: pushd/popd are bash extensions, not implemented in lush.
     */
    static const char *dir_commands[] = {"cd", "rmdir", NULL};

    for (const char **cmd = dir_commands; *cmd != NULL; cmd++) {
        if (strcmp(command_name, *cmd) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Generate file/directory completions
 *
 * Command-aware: for cd/rmdir shows only directories,
 * for other commands shows both files and directories.
 *
 * @param pool Memory pool
 * @param context Completion context for command detection
 * @param prefix Prefix to match
 * @param result Result set to populate
 * @return LLE_SUCCESS or error code
 */
static lle_result_t file_source_generate(lle_memory_pool_t *pool,
                                         const lle_context_analyzer_t *context,
                                         const char *prefix,
                                         lle_completion_result_t *result) {
    /* Check if command expects directories only */
    if (context && is_directory_only_command(context->command_name)) {
        return lle_completion_source_directories(pool, prefix, result);
    }

    /* Default: show both files and directories */
    return lle_completion_source_files(pool, prefix, result);
}

/**
 * @brief Generate variable completions
 * @param pool Memory pool
 * @param context Completion context (unused)
 * @param prefix Prefix to match
 * @param result Result set to populate
 * @return LLE_SUCCESS or error code
 */
static lle_result_t
variable_source_generate(lle_memory_pool_t *pool,
                         const lle_context_analyzer_t *context,
                         const char *prefix, lle_completion_result_t *result) {
    (void)context; /* Not needed for variables */

    /* Use existing completion function */
    return lle_completion_generate_variables(pool, prefix, result);
}

/**
 * @brief Generate history completions
 * @param pool Memory pool
 * @param context Completion context (unused)
 * @param prefix Prefix to match
 * @param result Result set to populate
 * @return LLE_SUCCESS or error code
 */
static lle_result_t
history_source_generate(lle_memory_pool_t *pool,
                        const lle_context_analyzer_t *context,
                        const char *prefix, lle_completion_result_t *result) {
    (void)context; /* Not needed for history */

    /* Use existing completion function */
    return lle_completion_source_history(pool, prefix, result);
}

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * @brief Create a new source manager with default sources
 * @param pool Memory pool for allocations
 * @param out_manager Output for created manager
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_source_manager_create(lle_memory_pool_t *pool,
                                       lle_source_manager_t **out_manager) {
    if (!pool || !out_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_source_manager_t *manager = lle_pool_alloc(sizeof(*manager));
    if (!manager) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    manager->num_sources = 0;
    manager->pool = pool;

    /* Register default sources */
    lle_result_t res;

    res = lle_source_manager_register(manager, LLE_SOURCE_BUILTINS, "builtins",
                                      builtin_source_generate,
                                      builtin_source_applicable);
    if (res != LLE_SUCCESS) {
        return res;
    }

    res = lle_source_manager_register(manager, LLE_SOURCE_ALIASES, "aliases",
                                      alias_source_generate,
                                      alias_source_applicable);
    if (res != LLE_SUCCESS) {
        return res;
    }

    res = lle_source_manager_register(
        manager, LLE_SOURCE_EXTERNAL_COMMANDS, "external_commands",
        external_command_source_generate, external_command_source_applicable);
    if (res != LLE_SUCCESS) {
        return res;
    }

    res = lle_source_manager_register(manager, LLE_SOURCE_FILES, "files",
                                      file_source_generate,
                                      file_source_applicable);
    if (res != LLE_SUCCESS) {
        return res;
    }

    res = lle_source_manager_register(manager, LLE_SOURCE_VARIABLES,
                                      "variables", variable_source_generate,
                                      variable_source_applicable);
    if (res != LLE_SUCCESS) {
        return res;
    }

    res = lle_source_manager_register(manager, LLE_SOURCE_HISTORY, "history",
                                      history_source_generate,
                                      history_source_applicable);
    if (res != LLE_SUCCESS) {
        return res;
    }

    /* Register builtin argument completions (options, subcommands, dynamic
     * args) */
    res = lle_source_manager_register(
        manager, LLE_SOURCE_CUSTOM, "builtin_args",
        lle_builtin_completions_generate, lle_builtin_completions_applicable);
    if (res != LLE_SUCCESS) {
        return res;
    }

    *out_manager = manager;
    return LLE_SUCCESS;
}

/**
 * @brief Free source manager resources
 * @param manager Manager to free
 */
void lle_source_manager_free(lle_source_manager_t *manager) {
    if (!manager) {
        return;
    }

    /* Memory is pool-allocated, will be freed with pool */
    (void)manager;
}

/**
 * @brief Register a completion source with the manager
 * @param manager Manager to register with
 * @param type Source type
 * @param name Source name
 * @param generate_fn Function to generate completions
 * @param applicable_fn Function to check if source applies
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_source_manager_register(lle_source_manager_t *manager,
                            lle_source_type_t type, const char *name,
                            lle_source_generate_fn generate_fn,
                            lle_source_applicable_fn applicable_fn) {
    if (!manager || !name || !generate_fn) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (manager->num_sources >= MAX_COMPLETION_SOURCES) {
        return LLE_ERROR_BUFFER_OVERFLOW; /* Array full */
    }

    lle_completion_source_t *source = lle_pool_alloc(sizeof(*source));
    if (!source) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    source->type = type;
    source->name = name;
    source->generate = generate_fn;
    source->is_applicable = applicable_fn;
    source->user_data = NULL;

    manager->sources[manager->num_sources++] = source;
    return LLE_SUCCESS;
}

/**
 * @brief Query all applicable sources for completions
 * @param manager Source manager
 * @param context Completion context
 * @param prefix Prefix to match
 * @param result Result set to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_source_manager_query(lle_source_manager_t *manager,
                                      const lle_context_analyzer_t *context,
                                      const char *prefix,
                                      lle_completion_result_t *result) {
    if (!manager || !context || !prefix || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Query all applicable sources */
    for (size_t i = 0; i < manager->num_sources; i++) {
        lle_completion_source_t *source = manager->sources[i];

        /* Check if source is applicable for this context */
        if (source->is_applicable && !source->is_applicable(context)) {
            continue; /* Skip this source */
        }

        /* Query source for completions */
        lle_result_t res =
            source->generate(manager->pool, context, prefix, result);

        /* Continue even if source fails - other sources may succeed */
        (void)res;
    }

    return LLE_SUCCESS;
}
