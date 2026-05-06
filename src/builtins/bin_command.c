/**
 * @file bin_command.c
 * @brief `command` builtin -- run command, suppressing function/alias lookup
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "alias.h"
#include "builtins.h"
#include "executor.h"
#include "lush_fork.h"
#include "shell_error.h"
#include "symtable.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Execute command bypassing shell functions and builtins
 *
 * POSIX command builtin. With no options, executes the specified command
 * bypassing any shell functions, aliases, or builtins of the same name.
 *
 * Options:
 *   -v  Print command path (like 'which')
 *   -V  Print verbose command description (like 'type')
 *   -p  Use default PATH for command search
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Command exit status, or 127 if command not found
 */
int bin_command(int argc, char **argv) {
    bool opt_v = false; /* -v: print path only */
    bool opt_V = false; /* -V: verbose description */
    bool opt_p = false; /* -p: use default PATH */
    int cmd_start = 1;

    /* Parse options */
    for (int i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            opt_v = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-V") == 0) {
            opt_V = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            opt_p = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-pv") == 0 || strcmp(argv[i], "-vp") == 0) {
            opt_p = true;
            opt_v = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-pV") == 0 || strcmp(argv[i], "-Vp") == 0) {
            opt_p = true;
            opt_V = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "--") == 0) {
            cmd_start = i + 1;
            break;
        } else {
            /* Unknown option - might be command name starting with dash */
            break;
        }
    }

    /* No command specified */
    if (cmd_start >= argc) {
        if (opt_v || opt_V) {
            /* -v or -V with no arguments is valid, just do nothing */
            return 0;
        }
        /* command with no arguments does nothing */
        return 0;
    }

    const char *cmd_name = argv[cmd_start];

    /* Handle -v option: print command path */
    if (opt_v) {
        int result = 0;
        for (int i = cmd_start; i < argc; i++) {
            const char *name = argv[i];

            /* Check for builtin (not bypassed by -v, just reported) */
            if (is_builtin(name)) {
                printf("%s\n", name);
                continue;
            }

            /* Check for alias - lookup_alias returns pointer into hash table */
            const char *alias_value = lookup_alias(name);
            if (alias_value) {
                printf("alias %s='%s'\n", name, alias_value);
                continue;
            }

            /* Search in PATH */
            char *path = find_command_in_path(name);
            if (path) {
                printf("%s\n", path);
                free(path);
            } else {
                /* Not found - silent for -v, but return failure */
                result = 1;
            }
        }
        return result;
    }

    /* Handle -V option: verbose description */
    if (opt_V) {
        int result = 0;
        for (int i = cmd_start; i < argc; i++) {
            const char *name = argv[i];

            /* Check for builtin */
            if (is_builtin(name)) {
                printf("%s is a shell builtin\n", name);
                continue;
            }

            /* Check for alias - lookup_alias returns pointer into hash table */
            const char *alias_value = lookup_alias(name);
            if (alias_value) {
                printf("%s is aliased to '%s'\n", name, alias_value);
                continue;
            }

            /* Search in PATH */
            char *path = find_command_in_path(name);
            if (path) {
                printf("%s is %s\n", name, path);
                free(path);
            } else {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "%s: not found", name);
                result = 1;
            }
        }
        return result;
    }

    /* Execute the command, bypassing builtins/aliases/functions */

    /* Find command in PATH */
    char *cmd_path = NULL;

    if (opt_p) {
        /* Use default POSIX PATH */
        const char *saved_path = getenv("PATH");
        char *saved_path_copy = saved_path ? strdup(saved_path) : NULL;

        /* Set default POSIX PATH */
        setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin", 1);

        cmd_path = find_command_in_path(cmd_name);

        /* Restore original PATH */
        if (saved_path_copy) {
            setenv("PATH", saved_path_copy, 1);
            free(saved_path_copy);
        } else {
            unsetenv("PATH");
        }
    } else {
        /* If command contains a slash, use it directly */
        if (strchr(cmd_name, '/')) {
            if (access(cmd_name, X_OK) == 0) {
                cmd_path = strdup(cmd_name);
            }
        } else {
            cmd_path = find_command_in_path(cmd_name);
        }
    }

    if (!cmd_path) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                              builtin_get_source_location(), "%s: not found",
                              cmd_name);
        return 127;
    }

    /* Fork and execute */
    pid_t pid = lush_fork();

    if (pid < 0) {
        /* Fork failed */
        int saved_errno = errno;
        executor_error_report(current_executor, SHELL_ERR_FORK_FAILED,
                              builtin_get_source_location(), "fork: %s",
                              strerror(saved_errno));
        free(cmd_path);
        return 1;
    }

    if (pid == 0) {
        /* Child process */
        /* Reset signal handlers to default */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        /* Build argv for execv - use original argv from cmd_start */
        execv(cmd_path, &argv[cmd_start]);

        /* execv failed */
        executor_error_report(current_executor, SHELL_ERR_EXEC_FAILED,
                              builtin_get_source_location(), "%s: %s", cmd_path,
                              strerror(errno));
        _exit(126);
    }

    /* Parent process - wait for child */
    free(cmd_path);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}
