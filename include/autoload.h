/**
 * @file autoload.h
 * @brief Zsh-style lazy function loading
 *
 * Tracks function names that have been declared autoloadable via the
 * `autoload` builtin. On first call to a declared name, the dispatcher
 * asks autoload_try_resolve() to locate the function file in `$FPATH`
 * (or the platform-default zsh function directories), wrap its contents
 * as `function NAME { ... }`, source the result, and remove the name
 * from the registry. Subsequent calls find the function already defined
 * and skip the resolver entirely.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_AUTOLOAD_H
#define LUSH_AUTOLOAD_H

#include <stdbool.h>

struct executor;

/**
 * @brief Register `name` as autoloadable
 *
 * Idempotent. Subsequent calls with the same name are no-ops.
 *
 * @param name Function name (must be non-empty)
 * @return true on success (registered or already present), false on
 *         invalid input / OOM.
 */
bool autoload_register(const char *name);

/**
 * @brief Try to resolve `name` to a defined function via fpath lookup
 *
 * Looks up `name` in the autoload registry. If declared, searches the
 * fpath chain for a file matching `name`, reads it, wraps it in a
 * `function name { body }` definition, and routes through the executor
 * so the function joins the live function table. The entry is removed
 * from the registry on success.
 *
 * @param executor Executor context used to source the function file
 * @param name     Command name being dispatched
 * @return true if the function is now defined (registry hit and source
 *         succeeded), false if the name wasn't declared, the file
 *         wasn't located on fpath, or sourcing failed.
 */
bool autoload_try_resolve(struct executor *executor, const char *name);

/**
 * @brief Test whether `name` is currently declared autoloadable
 *
 * Read-only probe; does not resolve. Useful for diagnostics and tests.
 *
 * @param name Function name
 * @return true if `name` is in the registry awaiting resolution.
 */
bool autoload_is_declared(const char *name);

/**
 * @brief Drop all autoload registrations
 *
 * Called from executor teardown / test harnesses. Idempotent.
 */
void autoload_clear(void);

#endif /* LUSH_AUTOLOAD_H */
