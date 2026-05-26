/**
 * @file bin_mapfile.c
 * @brief `mapfile` (and `readarray`) builtin -- read lines from stdin into
 * array
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "symtable.h"

#include <ctype.h>
#include <errno.h>

/**
 * @brief Read lines from stdin into an array variable
 *
 * mapfile/readarray builtin - reads lines from standard input into
 * an indexed array variable. Supports various options for controlling
 * the reading behavior.
 *
 * Options:
 *   -d delim   Use delim as line delimiter instead of newline
 *   -n count   Read at most count lines (0 means all)
 *   -O origin  Start assigning at index origin (default 0)
 *   -s count   Skip the first count lines
 *   -t         Remove trailing delimiter from each line
 *   -u fd      Read from file descriptor fd instead of stdin
 *   -C callback  Execute callback for each line read
 *   -c quantum   Call callback every quantum lines
 *
 * @param argc Argument count
 * @param argv Argument vector with options and array name
 * @return 0 on success, 1 on error
 */
int bin_mapfile(int argc, char **argv) {
    /// Default options
    char delim = '\n';
    int max_count = 0; /// 0 means read all
    int origin = 0;
    int skip_count = 0;
    bool trim_delim = false;
    int fd = STDIN_FILENO;
    char *callback = NULL;
    int callback_quantum = 5000;

    /// Default array name
    char *array_name = "MAPFILE";

    int opt_index = 1;

    /// Parse options
    while (opt_index < argc && argv[opt_index][0] == '-' &&
           argv[opt_index][1] != '\0') {
        char *arg = argv[opt_index];

        if (strcmp(arg, "-d") == 0) {
            /// -d delim: Use delim as delimiter
            if (opt_index + 1 >= argc) {
                executor_error_report(
                    current_executor, SHELL_ERR_MISSING_ARGUMENT,
                    builtin_get_source_location(), "-d requires a delimiter");
                return 1;
            }
            opt_index++;
            if (argv[opt_index][0] != '\0') {
                delim = argv[opt_index][0];
            } else {
                delim = '\0'; /// NUL delimiter
            }
        } else if (strcmp(arg, "-n") == 0) {
            /// -n count: Read at most count lines
            if (opt_index + 1 >= argc) {
                executor_error_report(
                    current_executor, SHELL_ERR_MISSING_ARGUMENT,
                    builtin_get_source_location(), "-n requires a count");
                return 1;
            }
            max_count = atoi(argv[++opt_index]);
            if (max_count < 0) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "invalid count");
                return 1;
            }
        } else if (strcmp(arg, "-O") == 0) {
            /// -O origin: Start index
            if (opt_index + 1 >= argc) {
                executor_error_report(current_executor,
                                      SHELL_ERR_MISSING_ARGUMENT,
                                      builtin_get_source_location(),
                                      "-O requires an origin index");
                return 1;
            }
            origin = atoi(argv[++opt_index]);
            if (origin < 0) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "invalid origin");
                return 1;
            }
        } else if (strcmp(arg, "-s") == 0) {
            /// -s count: Skip lines
            if (opt_index + 1 >= argc) {
                executor_error_report(
                    current_executor, SHELL_ERR_MISSING_ARGUMENT,
                    builtin_get_source_location(), "-s requires a count");
                return 1;
            }
            skip_count = atoi(argv[++opt_index]);
            if (skip_count < 0) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "invalid skip count");
                return 1;
            }
        } else if (strcmp(arg, "-t") == 0) {
            /// -t: Trim delimiter
            trim_delim = true;
        } else if (strcmp(arg, "-u") == 0) {
            /// -u fd: Read from file descriptor
            if (opt_index + 1 >= argc) {
                executor_error_report(current_executor,
                                      SHELL_ERR_MISSING_ARGUMENT,
                                      builtin_get_source_location(),
                                      "-u requires a file descriptor");
                return 1;
            }
            fd = atoi(argv[++opt_index]);
            if (fd < 0) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "invalid file descriptor");
                return 1;
            }
        } else if (strcmp(arg, "-C") == 0) {
            /// -C callback: Callback command
            if (opt_index + 1 >= argc) {
                executor_error_report(current_executor,
                                      SHELL_ERR_MISSING_ARGUMENT,
                                      builtin_get_source_location(),
                                      "-C requires a callback command");
                return 1;
            }
            callback = argv[++opt_index];
        } else if (strcmp(arg, "-c") == 0) {
            /// -c quantum: Callback frequency
            if (opt_index + 1 >= argc) {
                executor_error_report(
                    current_executor, SHELL_ERR_MISSING_ARGUMENT,
                    builtin_get_source_location(), "-c requires a quantum");
                return 1;
            }
            callback_quantum = atoi(argv[++opt_index]);
            if (callback_quantum <= 0) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "invalid quantum");
                return 1;
            }
        } else if (strcmp(arg, "--") == 0) {
            opt_index++;
            break;
        } else {
            executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                                  builtin_get_source_location(),
                                  "invalid option: %s", arg);
            return 1;
        }
        opt_index++;
    }

    /// Get array name if provided
    if (opt_index < argc) {
        array_name = argv[opt_index];
        if (!is_valid_identifier(array_name)) {
            executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                                  builtin_get_source_location(),
                                  "'%s' not a valid identifier", array_name);
            return 1;
        }
    }

    /// Clear existing array (unless using -O with non-zero origin)
    if (origin == 0) {
        symtable_unset_global(array_name);
    }

    /// Read lines via a freshly-duped FILE*. We deliberately do NOT
    /// reuse the global `stdin` even when fd == STDIN_FILENO: stdio's
    /// stdin FILE* carries a stale `feof` flag from the shell's own
    /// script reading, which makes getdelim() return -1 immediately
    /// without ever touching the redirected fd. bash and zsh hit the
    /// same issue and solve it by bypassing stdio entirely for read /
    /// mapfile (see bin_read.c comment for the same fix on the read
    /// builtin). dup+fdopen gives a fresh FILE* whose state reflects
    /// the actual fd, including any `<<<` here-string redirection
    /// setup_redirections placed at fd 0. Issue #101.
    int dup_fd = dup(fd);
    if (dup_fd < 0) {
        int saved_errno = errno;
        executor_error_report(
            current_executor, SHELL_ERR_BAD_FD, builtin_get_source_location(),
            "cannot dup file descriptor %d: %s", fd, strerror(saved_errno));
        return 1;
    }
    FILE *input = fdopen(dup_fd, "r");
    if (!input) {
        int saved_errno = errno;
        close(dup_fd);
        executor_error_report(
            current_executor, SHELL_ERR_BAD_FD, builtin_get_source_location(),
            "cannot open file descriptor %d: %s", fd, strerror(saved_errno));
        return 1;
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    int lines_read = 0;
    int lines_skipped = 0;
    int array_index = origin;

    while ((line_len = getdelim(&line, &line_cap, delim, input)) != -1) {
        /// Skip lines if requested
        if (lines_skipped < skip_count) {
            lines_skipped++;
            continue;
        }

        /// Check max count - but continue reading to consume input
        if (max_count > 0 && lines_read >= max_count) {
            /// Consume remaining input to prevent it from being executed
            while (getdelim(&line, &line_cap, delim, input) != -1) {
                /// Just discard
            }
            break;
        }

        /// Trim delimiter if requested
        char *value = line;
        if (trim_delim && line_len > 0 && line[line_len - 1] == delim) {
            line[line_len - 1] = '\0';
        }

        /// Set array element - convert index to string
        char index_str[32];
        snprintf(index_str, sizeof(index_str), "%d", array_index);
        symtable_set_array_element(array_name, index_str, value);

        array_index++;
        lines_read++;

        /// Execute callback if specified
        if (callback && (lines_read % callback_quantum) == 0) {
            /// Execute callback with index and line
            char cmd[4096];
            snprintf(cmd, sizeof(cmd), "%s %d", callback, array_index - 1);
            /// Would need executor access here - skip for now
            /// executor_execute_command_line(executor, cmd);
        }
    }

    free(line);

    /// Close the fdopen'd FILE*. fclose() closes the underlying dup_fd
    /// as well. The original fd remains intact for the rest of the
    /// shell to use.
    fclose(input);

    /// Suppress unused variable warning
    (void)callback;
    (void)callback_quantum;

    return 0;
}
