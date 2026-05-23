/**
 * @file bin_env.c
 * @brief `env` builtin (also registered as `printenv`) -- run command with
 *        modified environment, or print current environment
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush_fork.h"

#include <errno.h>
#include <sys/wait.h>

extern char **environ;

/**
 * @brief Run command with modified environment
 *
 * Usage:
 *   env                    - Print all environment variables
 *   env NAME=VALUE... cmd  - Run cmd with additional env vars
 *   env -i [NAME=VALUE...] cmd - Run cmd with empty environment
 *   env -u NAME cmd        - Run cmd with NAME unset
 *   env -0                 - Use NUL instead of newline (for printing)
 *   printenv [NAME...]     - Print environment variable(s)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status of command, or 0 for printing
 */
int bin_env(int argc, char **argv) {
    bool ignore_env = false;      // -i: start with empty environment
    bool null_terminator = false; // -0: NUL instead of newline
    char **unset_vars = NULL;     // -u: variables to unset
    int unset_count = 0;
    int unset_capacity = 0;

    // Check if invoked as printenv
    bool is_printenv = (strcmp(argv[0], "printenv") == 0);

    int i = 1;

    // Parse options
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else if (strcmp(argv[i], "-i") == 0) {
            ignore_env = true;
            i++;
        } else if (strcmp(argv[i], "-0") == 0) {
            null_terminator = true;
            i++;
        } else if (strcmp(argv[i], "-u") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "env: -u requires an argument\n");
                free(unset_vars);
                return 1;
            }
            // Add to unset list
            if (unset_count >= unset_capacity) {
                unset_capacity = unset_capacity ? unset_capacity * 2 : 4;
                char **new_unset =
                    realloc(unset_vars, unset_capacity * sizeof(char *));
                if (!new_unset) {
                    free(unset_vars);
                    return 1;
                }
                unset_vars = new_unset;
            }
            unset_vars[unset_count++] = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf(
                "Usage: env [OPTION]... [NAME=VALUE]... [COMMAND [ARG]...]\n");
            printf("Run COMMAND with modified environment.\n\n");
            printf("Options:\n");
            printf("  -i          Start with empty environment\n");
            printf("  -u NAME     Remove NAME from environment\n");
            printf("  -0          Use NUL instead of newline for output\n");
            printf("  --help      Display this help\n\n");
            printf("If no COMMAND, print the environment.\n");
            free(unset_vars);
            return 0;
        } else {
            fprintf(stderr, "env: invalid option: %s\n", argv[i]);
            free(unset_vars);
            return 125;
        }
    }

    // Collect NAME=VALUE assignments
    char **env_assignments = NULL;
    int env_count = 0;
    int env_capacity = 0;

    while (i < argc && strchr(argv[i], '=') != NULL) {
        if (env_count >= env_capacity) {
            env_capacity = env_capacity ? env_capacity * 2 : 8;
            char **new_env =
                realloc(env_assignments, env_capacity * sizeof(char *));
            if (!new_env) {
                free(unset_vars);
                free(env_assignments);
                return 1;
            }
            env_assignments = new_env;
        }
        env_assignments[env_count++] = argv[i];
        i++;
    }

    // printenv: just print variable values
    if (is_printenv) {
        char delim = null_terminator ? '\0' : '\n';
        if (i >= argc) {
            // Print all environment variables
            extern char **environ;
            for (char **env = environ; *env; env++) {
                printf("%s%c", *env, delim);
            }
        } else {
            // Print specific variables
            int result = 0;
            for (; i < argc; i++) {
                const char *val = getenv(argv[i]);
                if (val) {
                    printf("%s%c", val, delim);
                } else {
                    result = 1; // Variable not found
                }
            }
            free(unset_vars);
            free(env_assignments);
            return result;
        }
        free(unset_vars);
        free(env_assignments);
        return 0;
    }

    // No command: print environment
    if (i >= argc) {
        char delim = null_terminator ? '\0' : '\n';
        if (ignore_env) {
            // Only print explicit assignments
            for (int j = 0; j < env_count; j++) {
                printf("%s%c", env_assignments[j], delim);
            }
        } else {
            extern char **environ;
            // Print current environment (minus unset vars, plus assignments)
            for (char **env = environ; *env; env++) {
                // Check if should be unset
                bool skip = false;
                for (int u = 0; u < unset_count; u++) {
                    size_t ulen = strlen(unset_vars[u]);
                    if (strncmp(*env, unset_vars[u], ulen) == 0 &&
                        (*env)[ulen] == '=') {
                        skip = true;
                        break;
                    }
                }
                // Check if overridden by assignment
                for (int j = 0; j < env_count; j++) {
                    char *eq = strchr(env_assignments[j], '=');
                    size_t nlen = eq - env_assignments[j];
                    if (strncmp(*env, env_assignments[j], nlen) == 0 &&
                        (*env)[nlen] == '=') {
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    printf("%s%c", *env, delim);
                }
            }
            // Print assignments
            for (int j = 0; j < env_count; j++) {
                printf("%s%c", env_assignments[j], delim);
            }
        }
        free(unset_vars);
        free(env_assignments);
        return 0;
    }

    // Run command with modified environment
    pid_t pid = lush_fork();
    if (pid < 0) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_FORK_FAILED, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "env: fork: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        free(unset_vars);
        free(env_assignments);
        return 126;
    }

    if (pid == 0) {
        // Child process

        if (ignore_env) {
            /* Clear entire environment - use portable approach
             * clearenv() is a GNU extension not available on macOS/BSD
             * Setting environ to NULL or empty array is POSIX portable
             */
            extern char **environ;
            static char *empty_env[] = {NULL};
            environ = empty_env;
        } else {
            // Unset specified variables
            for (int u = 0; u < unset_count; u++) {
                unsetenv(unset_vars[u]);
            }
        }

        // Apply assignments
        for (int j = 0; j < env_count; j++) {
            char *eq = strchr(env_assignments[j], '=');
            if (eq) {
                *eq = '\0';
                setenv(env_assignments[j], eq + 1, 1);
                *eq = '='; // Restore for potential re-use
            }
        }

        // Execute command
        execvp(argv[i], &argv[i]);

        // exec failed
        int exit_code = (errno == ENOENT) ? 127 : 126;
        fprintf(stderr, "env: %s: %s\n", argv[i], strerror(errno));
        _exit(exit_code);
    }

    // Parent: wait for child
    free(unset_vars);
    free(env_assignments);

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "env: waitpid: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 126;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 126;
}
