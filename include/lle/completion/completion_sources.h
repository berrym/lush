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
 * ============================================================================ */

bool lle_shell_is_builtin(const char *text);
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

lle_result_t lle_completion_source_builtins(lle_memory_pool_t *pool,
                                            const lle_word_context_t *context,
                                            lle_completion_result_t *result);

lle_result_t lle_completion_source_aliases(lle_memory_pool_t *pool,
                                           const lle_word_context_t *context,
                                           lle_completion_result_t *result);

lle_result_t lle_completion_source_commands(lle_memory_pool_t *pool,
                                            const lle_word_context_t *context,
                                            lle_completion_result_t *result);

lle_result_t lle_completion_source_files(lle_memory_pool_t *pool,
                                         const lle_word_context_t *context,
                                         lle_completion_result_t *result);

lle_result_t
lle_completion_source_directories(lle_memory_pool_t *pool,
                                  const lle_word_context_t *context,
                                  lle_completion_result_t *result);

lle_result_t lle_completion_source_variables(lle_memory_pool_t *pool,
                                             const lle_word_context_t *context,
                                             lle_completion_result_t *result);

lle_result_t lle_completion_source_history(lle_memory_pool_t *pool,
                                           const lle_word_context_t *context,
                                           lle_completion_result_t *result);

lle_result_t lle_completion_source_ssh_hosts(lle_memory_pool_t *pool,
                                             const lle_word_context_t *context,
                                             lle_completion_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* LLE_COMPLETION_SOURCES_H */
