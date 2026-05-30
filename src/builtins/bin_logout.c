/**
 * @file bin_logout.c
 * @brief `logout` builtin -- terminate a login shell with optional exit code
 *
 * Mirrors the bash/zsh `logout` builtin: only valid inside a login
 * shell. When run from a non-login shell it errors out instead of
 * exiting (so a misplaced `logout` in a script doesn't surprise the
 * user by closing their interactive shell two layers up). The
 * actual termination + logout-script cascade is the same path
 * `exit` uses, so traps and ~/.lush_logout fire normally.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "init.h"
#include "lush.h"
#include "shell_error.h"

#include <stdio.h>
#include <unistd.h>

/**
 * @brief Terminate a login shell.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional exit code)
 * @return The exit code, or 1 if invoked outside a login shell
 */
int bin_logout(int argc, char **argv) {
    if (!is_login_shell()) {
        shell_error_t *err = shell_error_create(
            SHELL_ERR_LOOP_CONTROL, SHELL_SEVERITY_ERROR,
            builtin_get_source_location(), "not login shell: use `exit'");
        if (err) {
            shell_error_set_suggestion(
                err, "logout is only valid inside a login shell; "
                     "use `exit' to leave a non-login shell");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: logout: not login shell: use `exit'\n");
        }
        return 1;
    }

    int exit_code = last_exit_status;
    if (argc > 1) {
        exit_code = atoi(argv[1]);
    }

    /// Same termination path as `exit`: set the global flag, store
    /// the exit code, let the main loop run its cleanup including
    /// config_execute_logout_scripts() and SIGHUP cascade to jobs.
    exit_flag = true;
    last_exit_status = exit_code;

    return exit_code;
}
