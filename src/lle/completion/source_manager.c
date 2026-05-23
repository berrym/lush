/**
 * @file source_manager.c
 * @brief LLE Source Manager Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Manages completion sources and orchestrates querying via the
 * word_context analyzer's output. Each source receives the full word
 * context and emits literal-only candidates; the engine handles
 * splicing and rendering.
 */

#include "lle/completion/source_manager.h"
#include "lle/completion/builtin_completions.h"
#include "lle/completion/completion_sources.h"
#include "lle/completion/word_context.h"
#include "lle/lle_readline.h"
#include <string.h>

/* ============================================================================
 * Source applicability functions
 * ============================================================================
 */

static bool builtin_source_applicable(const lle_word_context_t *context) {
    if (lle_in_debug_prompt()) {
        return false;
    }
    return context->context_type == LLE_CONTEXT_COMMAND_POSITION;
}

static bool alias_source_applicable(const lle_word_context_t *context) {
    if (lle_in_debug_prompt()) {
        return false;
    }
    return context->context_type == LLE_CONTEXT_COMMAND_POSITION;
}

static bool
external_command_source_applicable(const lle_word_context_t *context) {
    if (lle_in_debug_prompt()) {
        return false;
    }
    return context->context_type == LLE_CONTEXT_COMMAND_POSITION;
}

/**
 * @brief Check if the dequoted prefix indicates a path-shaped local
 *        executable (./, ../, ~/, /, $VAR/).
 *
 * The analyzer has already dequoted the prefix; this check inspects
 * the typed shell-source surface via context->word_start..
 * context->filename_portion_start indirectly through the prefix's
 * leading bytes. Sources only see the dequoted form, so the path
 * markers ~, $, ., / are visible as raw bytes in the prefix.
 */
static bool prefix_indicates_path(const char *prefix) {
    if (!prefix || !prefix[0])
        return false;
    if (prefix[0] == '/')
        return true;
    if (prefix[0] == '~')
        return true;
    if (prefix[0] == '$' && strchr(prefix, '/'))
        return true;
    if (prefix[0] == '.') {
        if (prefix[1] == '/')
            return true;
        if (prefix[1] == '.' && prefix[2] == '/')
            return true;
    }
    return false;
}

static bool file_source_applicable(const lle_word_context_t *context) {
    if (lle_in_debug_prompt()) {
        return false;
    }
    if (context->context_type == LLE_CONTEXT_REDIRECT_TARGET)
        return true;

    if (context->context_type == LLE_CONTEXT_ARGUMENT ||
        context->context_type == LLE_CONTEXT_FOR_IN_LIST) {
        if (context->command_name) {
            const lle_builtin_completion_spec_t *spec =
                lle_builtin_get_spec(context->command_name);
            if (spec) {
                if (spec->default_arg_type == LLE_BUILTIN_ARG_FILE ||
                    spec->default_arg_type == LLE_BUILTIN_ARG_DIRECTORY) {
                    return true;
                }
                if (spec->subcommand_count > 0)
                    return false;
            }
        }
        return true;
    }

    // Path-shaped command-position completion (./local-script, etc.).
    if (context->context_type == LLE_CONTEXT_COMMAND_POSITION) {
        /* The user's typed bytes between word_start and
         * filename_portion_start carry any path markers. We use the
         * dequoted_filename_prefix as a stand-in: a leading ~/ or
         * /-prefix in the prefix string indicates a path. */
        if (context->expanded_directory != NULL)
            return true;
        if (prefix_indicates_path(context->dequoted_filename_prefix))
            return true;
    }
    return false;
}

static bool variable_source_applicable(const lle_word_context_t *context) {
    return context->context_type == LLE_CONTEXT_VARIABLE_NAME;
}

static bool history_source_applicable(const lle_word_context_t *context) {
    (void)context;
    if (lle_in_debug_prompt()) {
        return false;
    }
    return true;
}

/**
 * @brief Commands whose first non-option argument is an SSH-style host
 * target.
 *
 * When the user is completing the host position of one of these
 * commands, ssh_hosts_source fires. The list is open to extensions
 * (e.g. mosh-with-options) but kept conservative for now -- adding a
 * command here means stat-free SSH cache lookups happen on every TAB
 * for that command's argument, which is cheap but not free.
 *
 * scp / rsync / sftp accept `host:path` syntax; this wiring covers the
 * bare-host completion only (when the argument has no `:`). Path-side
 * remote completion is a separate, larger feature.
 */
static bool is_ssh_host_command(const char *command_name) {
    if (!command_name)
        return false;
    static const char *ssh_commands[] = {
        "ssh", "scp", "sftp", "ssh-copy-id", "mosh", "slogin", "rsync", NULL};
    for (const char **cmd = ssh_commands; *cmd != NULL; cmd++) {
        if (strcmp(command_name, *cmd) == 0)
            return true;
    }
    return false;
}

static bool ssh_hosts_source_applicable(const lle_word_context_t *context) {
    if (lle_in_debug_prompt()) {
        return false;
    }
    if (context->context_type != LLE_CONTEXT_ARGUMENT)
        return false;
    if (!is_ssh_host_command(context->command_name))
        return false;
    /* For scp/sftp/rsync, `remote:path` switches the argument to a
     * remote-path completion (not host) once a `:` appears. The bare
     * host segment is everything before the first `:`. We let the
     * source itself decide what to emit -- it sees the dequoted prefix
     * and can detect `:` -- but we only fire it here, not for arguments
     * that have no command_name match. */
    return true;
}

static lle_result_t ssh_hosts_source_generate(lle_memory_pool_t *pool,
                                              const lle_word_context_t *context,
                                              lle_completion_result_t *result) {
    return lle_completion_source_ssh_hosts(pool, context, result);
}

/* ============================================================================
 * Source generation wrappers
 * ============================================================================
 */

static lle_result_t builtin_source_generate(lle_memory_pool_t *pool,
                                            const lle_word_context_t *context,
                                            lle_completion_result_t *result) {
    return lle_completion_source_builtins(pool, context, result);
}

static lle_result_t alias_source_generate(lle_memory_pool_t *pool,
                                          const lle_word_context_t *context,
                                          lle_completion_result_t *result) {
    return lle_completion_source_aliases(pool, context, result);
}

static lle_result_t
external_command_source_generate(lle_memory_pool_t *pool,
                                 const lle_word_context_t *context,
                                 lle_completion_result_t *result) {
    return lle_completion_source_commands(pool, context, result);
}

static bool is_directory_only_command(const char *command_name) {
    if (!command_name)
        return false;
    static const char *dir_commands[] = {"cd", "rmdir", NULL};
    for (const char **cmd = dir_commands; *cmd != NULL; cmd++) {
        if (strcmp(command_name, *cmd) == 0)
            return true;
    }
    return false;
}

static lle_result_t file_source_generate(lle_memory_pool_t *pool,
                                         const lle_word_context_t *context,
                                         lle_completion_result_t *result) {
    if (context && is_directory_only_command(context->command_name)) {
        return lle_completion_source_directories(pool, context, result);
    }
    return lle_completion_source_files(pool, context, result);
}

static lle_result_t variable_source_generate(lle_memory_pool_t *pool,
                                             const lle_word_context_t *context,
                                             lle_completion_result_t *result) {
    return lle_completion_source_variables(pool, context, result);
}

static lle_result_t history_source_generate(lle_memory_pool_t *pool,
                                            const lle_word_context_t *context,
                                            lle_completion_result_t *result) {
    return lle_completion_source_history(pool, context, result);
}

/* ============================================================================
 * Debug-prompt commands
 *
 * The (lush-debug) break-prompt has its own first-word vocabulary --
 * c / step / next / finish / vars / print / type / ... -- which has
 * nothing to do with shell commands. When lle_in_debug_prompt() is
 * true, the shell command / file / history / ssh sources sit out
 * (above) and this source supplies the command-position candidates
 * instead. Variable-name completion still uses the existing
 * variable_source -- `$x` completion is wanted in both prompts.
 * ============================================================================
 */

static bool debug_command_source_applicable(const lle_word_context_t *context) {
    return lle_in_debug_prompt() &&
           context->context_type == LLE_CONTEXT_COMMAND_POSITION;
}

static lle_result_t
debug_command_source_generate(lle_memory_pool_t *pool,
                              const lle_word_context_t *context,
                              lle_completion_result_t *result) {
    (void)pool;
    (void)context;
    /* Mirrors the dispatcher in src/debug/debug_breakpoints.c::
     * debug_handle_user_input. Short aliases (c / s / n / f / l / p
     * / h / q / t) are listed as their own entries so prefix matching
     * surfaces both forms naturally. */
    static const char *const debug_commands[] = {
        "backtrace", "bt",       "c",      "continue", "down",  "eval", "f",
        "feature",   "features", "finish", "h",        "help",  "l",    "list",
        "mode",      "n",        "next",   "p",        "print", "q",    "quit",
        "s",         "set",      "step",   "t",        "type",  "up",   "vars",
        "watch",     "where",    NULL,
    };

    for (size_t i = 0; debug_commands[i] != NULL; i++) {
        (void)lle_completion_result_add(result, debug_commands[i], NULL,
                                        LLE_COMPLETION_TYPE_CUSTOM, 1000);
    }
    return LLE_SUCCESS;
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

lle_result_t lle_source_manager_create(lle_memory_pool_t *pool,
                                       lle_source_manager_t **out_manager) {
    if (!pool || !out_manager)
        return LLE_ERROR_INVALID_PARAMETER;

    lle_source_manager_t *manager = lle_pool_alloc(sizeof(*manager));
    if (!manager)
        return LLE_ERROR_OUT_OF_MEMORY;

    manager->num_sources = 0;
    manager->pool = pool;

    lle_result_t res;

    res = lle_source_manager_register(manager, LLE_SOURCE_BUILTINS, "builtins",
                                      builtin_source_generate,
                                      builtin_source_applicable);
    if (res != LLE_SUCCESS)
        return res;

    res = lle_source_manager_register(manager, LLE_SOURCE_ALIASES, "aliases",
                                      alias_source_generate,
                                      alias_source_applicable);
    if (res != LLE_SUCCESS)
        return res;

    res = lle_source_manager_register(
        manager, LLE_SOURCE_EXTERNAL_COMMANDS, "external_commands",
        external_command_source_generate, external_command_source_applicable);
    if (res != LLE_SUCCESS)
        return res;

    res = lle_source_manager_register(manager, LLE_SOURCE_FILES, "files",
                                      file_source_generate,
                                      file_source_applicable);
    if (res != LLE_SUCCESS)
        return res;

    res = lle_source_manager_register(manager, LLE_SOURCE_VARIABLES,
                                      "variables", variable_source_generate,
                                      variable_source_applicable);
    if (res != LLE_SUCCESS)
        return res;

    res = lle_source_manager_register(manager, LLE_SOURCE_HISTORY, "history",
                                      history_source_generate,
                                      history_source_applicable);
    if (res != LLE_SUCCESS)
        return res;

    // Builtin argument completions (options, subcommands, dynamic args).
    res = lle_source_manager_register(
        manager, LLE_SOURCE_CUSTOM, "builtin_args",
        lle_builtin_completions_generate, lle_builtin_completions_applicable);
    if (res != LLE_SUCCESS)
        return res;

    /* SSH host completion for ssh/scp/sftp/etc. Fires on arguments to
     * those commands; the source consults the ssh_hosts cache (lazy
     * init from ~/.ssh/config + ~/.ssh/known_hosts on first call). */
    res = lle_source_manager_register(manager, LLE_SOURCE_SSH_HOSTS,
                                      "ssh_hosts", ssh_hosts_source_generate,
                                      ssh_hosts_source_applicable);
    if (res != LLE_SUCCESS)
        return res;

    /* Debug-prompt command completion -- only fires when
     * lle_in_debug_prompt() is true; supplies the (lush-debug)
     * vocabulary at the command position. */
    res = lle_source_manager_register(
        manager, LLE_SOURCE_CUSTOM, "debug_commands",
        debug_command_source_generate, debug_command_source_applicable);
    if (res != LLE_SUCCESS)
        return res;

    *out_manager = manager;
    return LLE_SUCCESS;
}

void lle_source_manager_free(lle_source_manager_t *manager) {
    // Pool-backed; nothing to free here.
    (void)manager;
}

lle_result_t
lle_source_manager_register(lle_source_manager_t *manager,
                            lle_source_type_t type, const char *name,
                            lle_source_generate_fn generate_fn,
                            lle_source_applicable_fn applicable_fn) {
    if (!manager || !name || !generate_fn)
        return LLE_ERROR_INVALID_PARAMETER;
    if (manager->num_sources >= MAX_COMPLETION_SOURCES)
        return LLE_ERROR_BUFFER_OVERFLOW;

    lle_completion_source_t *source = lle_pool_alloc(sizeof(*source));
    if (!source)
        return LLE_ERROR_OUT_OF_MEMORY;

    source->type = type;
    source->name = name;
    source->generate = generate_fn;
    source->is_applicable = applicable_fn;
    source->user_data = NULL;

    manager->sources[manager->num_sources++] = source;
    return LLE_SUCCESS;
}

lle_result_t lle_source_manager_query(lle_source_manager_t *manager,
                                      const lle_word_context_t *context,
                                      lle_completion_result_t *result) {
    if (!manager || !context || !result)
        return LLE_ERROR_INVALID_PARAMETER;

    /* Brace-expansion fan-out: when the analyzer reports
     * branch_count > 0, query each branch with a synthetic single-
     * directory context and accumulate. Mode-specific intersection /
     * union logic is the engine's concern; this loop produces every
     * candidate from every branch and lets the engine deduplicate or
     * filter as configured. */
    if (context->branch_count > 0 && context->branches != NULL) {
        for (size_t b = 0; b < context->branch_count; b++) {
            lle_word_context_t branch_ctx = *context;
            branch_ctx.expanded_directory =
                context->branches[b].expanded_directory;
            branch_ctx.dequoted_filename_prefix =
                context->branches[b].dequoted_filename_prefix;
            branch_ctx.branches = NULL;
            branch_ctx.branch_count = 0;

            for (size_t i = 0; i < manager->num_sources; i++) {
                lle_completion_source_t *source = manager->sources[i];
                if (source->is_applicable &&
                    !source->is_applicable(&branch_ctx)) {
                    continue;
                }
                (void)source->generate(manager->pool, &branch_ctx, result);
            }
        }
        return LLE_SUCCESS;
    }

    // Single-directory dispatch: each applicable source is called once.
    for (size_t i = 0; i < manager->num_sources; i++) {
        lle_completion_source_t *source = manager->sources[i];
        if (source->is_applicable && !source->is_applicable(context))
            continue;
        (void)source->generate(manager->pool, context, result);
    }

    return LLE_SUCCESS;
}
