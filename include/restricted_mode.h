/**
 * @file restricted_mode.h
 * @brief Restricted-shell mode (bash rbash / zsh RESTRICTED / POSIX 2024 -r).
 *
 * This is a usability boundary, NOT a security boundary -- matching
 * bash's own manual:
 *
 *     "It is not intended to be a completely secure environment for
 *      running shell scripts where absolute security is required."
 *
 * Trivially escapable via `vi :shell`, `man !sh`, `find -exec sh`,
 * `awk 'BEGIN{system("sh")}'`, etc. Use it for admin-controlled menu
 * shells, not for sandboxing untrusted users.
 *
 * Restriction set (matches bash's rbash; the POSIX 2024 `set -r` text
 * delegates the exact list to implementations, so we adopt the de-facto
 * standard):
 *
 *   1. `cd` is disabled
 *   2. `SHELL`, `PATH`, `HISTFILE`, `ENV`, `BASH_ENV` are made readonly
 *   3. Command names containing `/` are rejected
 *   4. `/`-paths to `.` (source) and `hash -p` are rejected
 *   5. Output redirections `>`, `>|`, `<>`, `>&`, `&>`, `>>` are forbidden
 *   6. `exec` to replace the shell is disabled
 *   7. `set +r` / `set +o restricted` cannot clear the mode
 *
 * Restrictions engage AFTER rc-file processing so admin startup scripts
 * (`/etc/profile`, `~/.profile`, `~/.lush_login`, `~/.lushrc`) can
 * pre-configure the environment. Once engaged the flag is one-way.
 *
 * The gate `shell_opts.restricted_mode_engaged` distinguishes "user
 * requested -r" (shell_opts.restricted_mode) from "restrictions are
 * actually enforcing now" (restricted_mode_engaged). Enforcement
 * sites consult restricted_mode_engaged, not restricted_mode.
 */

#ifndef RESTRICTED_MODE_H
#define RESTRICTED_MODE_H

#include <stdbool.h>

#include "shell_error.h"

/**
 * @brief Engage restricted mode.
 *
 * Idempotent: safe to call repeatedly; only the first invocation
 * performs the readonly lockdown. Called from init.c AFTER rc-file
 * processing. Marks `shell_opts.restricted_mode_engaged = true` so
 * the per-site enforcement gates (cd, exec, redirection, command
 * dispatch, etc.) fire.
 */
void restricted_mode_engage(void);

/**
 * @brief Test whether restricted mode is currently enforcing.
 *
 * Single-line convenience wrapper around the flag check. Call sites
 * use this rather than reaching into shell_opts directly so the
 * gate location is searchable / rename-safe.
 */
bool restricted_mode_is_engaged(void);

/**
 * @brief Report a restricted-mode-blocked operation via the
 *        structured-error system, and return 1 (the conventional
 *        shell error status). Designed to be returned directly
 *        from builtin/dispatch sites:
 *
 *            if (restricted_mode_is_engaged()) {
 *                return restricted_mode_reject(loc, "cd: restricted");
 *            }
 *
 * The `loc` is the source location of the offending command.
 *
 * @param loc Source location of the rejected command
 * @param what Verb/operation phrase for the error message
 *             (e.g. "cd", "exec", "redirection")
 * @return Always 1 (failure exit status)
 */
int restricted_mode_reject(source_location_t loc, const char *what);

#endif /// RESTRICTED_MODE_H
