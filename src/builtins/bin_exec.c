/**
 * @file bin_exec.c
 * @brief `exec` builtin -- replace shell with command, or set redirections
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"
#include "signals.h"
#include "symtable.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>

/**
 * @brief Replace the shell process with a command
 *
 * Replaces the current shell with the specified command using execvp.
 * Can also be used for file descriptor manipulation (redirections only).
 * Executes EXIT traps before replacing the process.
 *
 * @param argc Argument count
 * @param argv Argument vector with command and arguments
 * @return Does not return on success; 1 on error or restricted mode
 */
int bin_exec(int argc, char **argv) {
    /// Privileged mode security check
    if (shell_opts.privileged_mode) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err = shell_error_create(
            SHELL_ERR_PERMISSION_DENIED, SHELL_SEVERITY_ERROR, loc,
            "restricted command in privileged mode");
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_set_suggestion(
                err, "drop privileges or run from an unrestricted shell");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: exec: restricted command in privileged mode\n");
        }
        return 1;
    }

    /// If no arguments, exec does nothing and returns success
    if (argc == 1) {
        return 0;
    }

#ifdef LUSH_FUZZ_SANDBOX
    /// Under fuzzing sandbox, exec must not call execvp() (which would
    /// replace the fuzzer process) or exit() on execvp failure (which
    /// would terminate it). Short-circuit any exec-with-args invocation
    /// to a no-op returning 127 — the same status the fall-through
    /// exit() would convey to a parent.
    (void)argv;
    return 127;
#endif

    /// Check for redirection-only exec (exec < file, exec > file, etc.)
    bool has_redirections = false;
    bool has_command = false;

    /// Scan arguments to determine if this is redirection-only or command exec
    for (int i = 1; i < argc; i++) {
        if (strchr(argv[i], '<') || strchr(argv[i], '>')) {
            has_redirections = true;
        } else if (argv[i][0] != '<' && argv[i][0] != '>' &&
                   !isdigit(argv[i][0])) {
            has_command = true;
            break;
        }
    }

    /// If only redirections, handle file descriptor manipulation
    if (has_redirections && !has_command) {
        /// TODO: Implement redirection-only exec
        /// For now, we'll focus on command replacement exec
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err = shell_error_create(
            SHELL_ERR_NOT_IMPLEMENTED, SHELL_SEVERITY_ERROR, loc,
            "redirection-only exec not yet implemented");
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_set_suggestion(
                err,
                "use 'command < file' or wrap in a subshell as a workaround");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: exec: redirection-only exec not yet "
                            "implemented\n");
        }
        return 1;
    }

    /// Command replacement exec - find the command to execute
    char *command = NULL;
    char **exec_argv = NULL;
    int exec_argc = 0;
    (void)exec_argc; /// Reserved for argument count validation

    /// Find the first non-redirection argument as the command
    int cmd_start = 1;
    while (cmd_start < argc &&
           (argv[cmd_start][0] == '<' || argv[cmd_start][0] == '>' ||
            isdigit(argv[cmd_start][0]))) {
        cmd_start++;
    }

    if (cmd_start >= argc) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "no command specified");
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_set_suggestion(
                err, "usage: exec [redirections] command [args ...]");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: exec: no command specified\n");
        }
        return 1;
    }

    command = argv[cmd_start];
    exec_argc = argc - cmd_start;
    exec_argv = &argv[cmd_start];

    /// Execute EXIT traps before replacing the process
    execute_exit_traps();

    /// Flush all output streams before exec
    fflush(stdout);
    fflush(stderr);
    fflush(stdin);

    /// Try to execute the command using execvp
    /// This replaces the current process entirely
    execvp(command, exec_argv);

    /// If we get here, exec failed
    int saved_errno = errno;
    shell_error_t *error = shell_error_create(
        SHELL_ERR_COMMAND_NOT_FOUND, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
        "exec: %s: %s", command, strerror(saved_errno));
    shell_error_display(error, stderr, isatty(STDERR_FILENO));
    shell_error_free(error);

    /// exec failure should exit the shell with error status
    exit(127);
}
