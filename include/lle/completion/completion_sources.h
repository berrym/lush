/**
 * @file completion_sources.h
 * @brief LLE Completion Sources - Shell Data Adapters
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Shell data adapters that provide completion candidates from various
 * sources: builtin commands, aliases, PATH executables, files and
 * directories, environment and shell variables, command history, SSH
 * hosts.
 *
 * Sources receive the full word_context produced by the analyzer.
 * They read context->dequoted_filename_prefix for the prefix to
 * match (NFC-normalized, dequoted) and, for path-shaped sources,
 * context->expanded_directory for the absolute directory path the
 * engine has already resolved. Sources MUST emit candidates as
 * literals -- no path-prefix preservation, no quote machinery, no
 * escaping. The engine handles splicing and rendering via the
 * splicer.
 */

#ifndef LLE_COMPLETION_SOURCES_H
#define LLE_COMPLETION_SOURCES_H

#include "lle/completion/completion_types.h"
#include "lle/completion/word_context.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Shell integration helpers
 * ============================================================================
 */

/**
 * @brief Check whether a name resolves to a shell builtin
 * @param text Candidate command name (NUL-terminated)
 * @return true if text names a registered builtin, false otherwise
 */
bool lle_shell_is_builtin(const char *text);

/**
 * @brief Check whether a name resolves to a shell alias
 * @param text Candidate command name (NUL-terminated)
 * @return true if text names a registered alias, false otherwise
 */
bool lle_shell_is_alias(const char *text);

/* ============================================================================
 * Source functions
 * ============================================================================
 *
 * Common signature: (pool, context, result) -> lle_result_t. The
 * prefix the source matches against is context->dequoted_filename_prefix.
 * For file/directory sources, context->expanded_directory holds the
 * absolute path to scan; if NULL, the source uses the current
 * working directory.
 */

/**
 * @brief Emit builtin-command completion candidates
 * @param pool Memory pool for candidate allocations
 * @param context Word context produced by the analyzer
 * @param result Result set to append matching candidates to
 * @return LLE_SUCCESS or an error code
 */
lle_result_t lle_completion_source_builtins(lle_memory_pool_t *pool,
                                            const lle_word_context_t *context,
                                            lle_completion_result_t *result);

/**
 * @brief Emit alias-name completion candidates
 * @param pool Memory pool for candidate allocations
 * @param context Word context produced by the analyzer
 * @param result Result set to append matching candidates to
 * @return LLE_SUCCESS or an error code
 */
lle_result_t lle_completion_source_aliases(lle_memory_pool_t *pool,
                                           const lle_word_context_t *context,
                                           lle_completion_result_t *result);

/**
 * @brief Emit PATH-executable command completion candidates
 * @param pool Memory pool for candidate allocations
 * @param context Word context produced by the analyzer
 * @param result Result set to append matching candidates to
 * @return LLE_SUCCESS or an error code
 */
lle_result_t lle_completion_source_commands(lle_memory_pool_t *pool,
                                            const lle_word_context_t *context,
                                            lle_completion_result_t *result);

/**
 * @brief Emit file-path completion candidates from the resolved directory
 * @param pool Memory pool for candidate allocations
 * @param context Word context (expanded_directory selects the scan path)
 * @param result Result set to append matching candidates to
 * @return LLE_SUCCESS or an error code
 */
lle_result_t lle_completion_source_files(lle_memory_pool_t *pool,
                                         const lle_word_context_t *context,
                                         lle_completion_result_t *result);

/**
 * @brief Emit directory-only completion candidates from the resolved directory
 * @param pool Memory pool for candidate allocations
 * @param context Word context (expanded_directory selects the scan path)
 * @param result Result set to append matching candidates to
 * @return LLE_SUCCESS or an error code
 */
lle_result_t
lle_completion_source_directories(lle_memory_pool_t *pool,
                                  const lle_word_context_t *context,
                                  lle_completion_result_t *result);

/**
 * @brief Emit environment and shell variable completion candidates
 * @param pool Memory pool for candidate allocations
 * @param context Word context produced by the analyzer
 * @param result Result set to append matching candidates to
 * @return LLE_SUCCESS or an error code
 */
lle_result_t lle_completion_source_variables(lle_memory_pool_t *pool,
                                             const lle_word_context_t *context,
                                             lle_completion_result_t *result);

/**
 * @brief Emit command-history completion candidates
 * @param pool Memory pool for candidate allocations
 * @param context Word context produced by the analyzer
 * @param result Result set to append matching candidates to
 * @return LLE_SUCCESS or an error code
 */
lle_result_t lle_completion_source_history(lle_memory_pool_t *pool,
                                           const lle_word_context_t *context,
                                           lle_completion_result_t *result);

/**
 * @brief Emit SSH host completion candidates from known_hosts/config
 * @param pool Memory pool for candidate allocations
 * @param context Word context produced by the analyzer
 * @param result Result set to append matching candidates to
 * @return LLE_SUCCESS or an error code
 */
lle_result_t lle_completion_source_ssh_hosts(lle_memory_pool_t *pool,
                                             const lle_word_context_t *context,
                                             lle_completion_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* LLE_COMPLETION_SOURCES_H */
