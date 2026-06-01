/**
 * @file compdef.h
 * @brief compdef bindings: command-name -> completion-function-name.
 *
 * compdef records a mapping from a command name (the word at argv[0]
 * when the user TABs) to the name of a shell function that emits
 * candidates via compadd. The LLE completion source consults this
 * table on every completion: when ctx->command_name has a binding,
 * the source sets up an active completion-result accumulator, invokes
 * the function in-process via the executor, and drains the
 * accumulated candidates.
 *
 * Function-defined-later is fine -- compdef stores the function NAME,
 * not a pointer. Resolution happens at completion time. An unresolved
 * name yields zero candidates from this source; other sources (files,
 * builtins, etc.) still fire.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_COMPDEF_H
#define LUSH_COMPDEF_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize the compdef bindings table.
 *
 * Idempotent. Safe to call multiple times.
 */
void init_compdef_bindings(void);

/**
 * @brief Free the compdef bindings table and all entries.
 *
 * Idempotent. Safe to call before init.
 */
void free_compdef_bindings(void);

/**
 * @brief Look up the completion function bound to a command name.
 *
 * @param cmd Command name (e.g. "git").
 * @return Function name (e.g. "_git") or NULL if no binding exists.
 *         The returned pointer is owned by the bindings table; do not
 *         free. Valid until the next compdef_set / compdef_unset /
 *         free_compdef_bindings on the same key.
 */
const char *compdef_lookup(const char *cmd);

/**
 * @brief Bind a completion function to a command name.
 *
 * Re-binding replaces. Does not validate that `fn` is currently
 * defined -- functions may be loaded later.
 *
 * @param cmd Command name.
 * @param fn Function name.
 * @return true on success, false on invalid input or allocation failure.
 */
bool compdef_set(const char *cmd, const char *fn);

/**
 * @brief Remove a compdef binding.
 *
 * @param cmd Command name.
 * @return true if a binding was removed; false if no binding existed
 *         for `cmd`.
 */
bool compdef_unset(const char *cmd);

/**
 * @brief Callback signature for compdef_enum.
 *
 * @param cmd Command name.
 * @param fn Bound function name.
 * @param user_data Caller-supplied opaque pointer.
 * @return true to continue iteration, false to stop.
 */
typedef bool (*compdef_enum_fn)(const char *cmd, const char *fn,
                                void *user_data);

/**
 * @brief Iterate all compdef bindings.
 *
 * @param cb Callback invoked once per binding.
 * @param user_data Forwarded to the callback.
 */
void compdef_enum(compdef_enum_fn cb, void *user_data);

/**
 * @brief Number of registered bindings (for testing / listing).
 */
size_t compdef_count(void);

#endif /* LUSH_COMPDEF_H */
