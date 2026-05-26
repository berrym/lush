/**
 * @file bin_read.c
 * @brief `read` builtin -- read a line from stdin into shell variables
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "symtable.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>

/**
 * @brief Read one line from a raw file descriptor, byte at a time
 *
 * Stops at '\n' (consumed but not included in result) or EOF. Returns
 * a malloc'd NUL-terminated string on success, NULL on immediate EOF
 * or allocation failure.
 *
 * Bypasses stdio because the read builtin must work after the
 * redirection layer dup2()s a new fd into STDIN_FILENO. Stdio's stdin
 * FILE* may carry a stale feof flag set during the shell's own script
 * reading from the original stdin — getline()/fgets() would then
 * return -1 immediately without ever consulting the new fd. Matches
 * what bash and zsh do for the read builtin (issue #55).
 *
 * @param fd File descriptor to read from
 * @return Newly allocated line (caller frees), or NULL on EOF / OOM
 */
static char *read_line_from_fd(int fd) {
    size_t cap = 64;
    size_t len = 0;
    char *line = malloc(cap);
    if (!line) {
        return NULL;
    }
    bool got_any_byte = false;
    while (1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            break;
        }
        got_any_byte = true;
        if (c == '\n') {
            break;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *grown = realloc(line, cap);
            if (!grown) {
                free(line);
                return NULL;
            }
            line = grown;
        }
        line[len++] = c;
    }
    if (!got_any_byte) {
        free(line);
        return NULL;
    }
    line[len] = '\0';
    return line;
}

int bin_read(int argc, char **argv) {
    /// Option flags
    char *prompt = NULL;
    bool raw_mode = false;
    int timeout_secs = -1;
    int nchars = -1;
    bool silent_mode = false;

    int opt_index = 1;

    /// Parse options
    while (opt_index < argc && argv[opt_index][0] == '-') {
        char *arg = argv[opt_index];

        if (strcmp(arg, "-p") == 0) {
            /// -p prompt: Display prompt before reading
            if (opt_index + 1 >= argc) {
                executor_error_report(current_executor,
                                      SHELL_ERR_MISSING_ARGUMENT,
                                      builtin_get_source_location(),
                                      "-p requires a prompt string");
                return 1;
            }
            prompt = argv[++opt_index];
        } else if (strcmp(arg, "-r") == 0) {
            /// -r: Raw mode (don't interpret backslashes)
            raw_mode = true;
        } else if (strcmp(arg, "-t") == 0) {
            /// -t timeout: Timeout after specified seconds
            if (opt_index + 1 >= argc) {
                executor_error_report(current_executor,
                                      SHELL_ERR_MISSING_ARGUMENT,
                                      builtin_get_source_location(),
                                      "-t requires a timeout value");
                return 1;
            }
            timeout_secs = atoi(argv[++opt_index]);
            if (timeout_secs < 0) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "invalid timeout value");
                return 1;
            }
        } else if (strcmp(arg, "-n") == 0) {
            /// -n nchars: Read only specified number of characters
            if (opt_index + 1 >= argc) {
                executor_error_report(current_executor,
                                      SHELL_ERR_MISSING_ARGUMENT,
                                      builtin_get_source_location(),
                                      "-n requires a character count");
                return 1;
            }
            nchars = atoi(argv[++opt_index]);
            if (nchars <= 0) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "invalid character count");
                return 1;
            }
        } else if (strcmp(arg, "-s") == 0) {
            /// -s: Silent mode (don't echo input)
            silent_mode = true;
        } else if (strcmp(arg, "--") == 0) {
            /// End of options
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

    /// Must have at least one variable name
    if (opt_index >= argc) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "missing variable name");
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
                for (size_t k = 0; k < current_executor->context_depth &&
                                   k < SHELL_ERROR_CONTEXT_MAX;
                     k++) {
                    if (current_executor->context_stack[k]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[k]);
                    }
                }
            }
            shell_error_set_suggestion(
                err, "usage: read [-p prompt] [-r] variable_name");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: read: usage: read [-p prompt] [-r] variable_name\n");
        }
        return 1;
    }

    /* POSIX read accepts one or more variable names. With N names,
     * the input line is split on IFS into the first N-1 fields
     * (whitespace coalesced for default IFS) and the remainder
     * (including any internal IFS chars) is assigned to the Nth
     * variable. If fewer than N words are present, trailing
     * variables get the empty string. Issue #101. */
    int n_varnames = argc - opt_index;
    if (n_varnames <= 0) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                              builtin_get_source_location(),
                              "at least one variable name required");
        return 1;
    }
    char *varname = argv[opt_index]; /* first name; kept for the
                                      * single-var fast paths below */
    if (!is_valid_identifier(varname)) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                              builtin_get_source_location(),
                              "'%s' not a valid identifier", varname);
        return 1;
    }
    for (int i = 1; i < n_varnames; i++) {
        if (!is_valid_identifier(argv[opt_index + i])) {
            executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                                  builtin_get_source_location(),
                                  "'%s' not a valid identifier",
                                  argv[opt_index + i]);
            return 1;
        }
    }

    /// Display prompt if specified
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }

    char *line = NULL;
    int result = 0;
    int fd = fileno(stdin);
    bool is_tty = isatty(fd);

    /// Save original terminal settings if we need to modify them
    struct termios orig_termios, new_termios;
    bool termios_modified = false;

    if (is_tty && (silent_mode || nchars > 0)) {
        if (tcgetattr(fd, &orig_termios) == 0) {
            new_termios = orig_termios;

            if (silent_mode) {
                /// Disable echo
                new_termios.c_lflag &= ~ECHO;
            }

            if (nchars > 0) {
                /// Non-canonical mode for character-by-character reading
                new_termios.c_lflag &= ~ICANON;
                new_termios.c_cc[VMIN] = 1;
                new_termios.c_cc[VTIME] = 0;
            }

            if (tcsetattr(fd, TCSANOW, &new_termios) == 0) {
                termios_modified = true;
            }
        }
    }

    /// Handle timeout with select()
    if (timeout_secs >= 0) {
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        tv.tv_sec = timeout_secs;
        tv.tv_usec = 0;

        int select_result = select(fd + 1, &readfds, NULL, NULL, &tv);

        if (select_result <= 0) {
            /// Timeout (0) or error (-1)
            if (termios_modified) {
                tcsetattr(fd, TCSANOW, &orig_termios);
            }
            symtable_set_global(varname, "");
            return (select_result == 0) ? 142 : 1; /// 142 = timeout exit code
        }
    }

    /// Read input based on options
    if (nchars > 0) {
        /// Read exactly nchars characters
        line = malloc(nchars + 1);
        if (!line) {
            if (termios_modified) {
                tcsetattr(fd, TCSANOW, &orig_termios);
            }
            return 1;
        }

        int chars_read = 0;
        while (chars_read < nchars) {
            /// Check timeout for each character if specified
            if (timeout_secs >= 0) {
                fd_set readfds;
                struct timeval tv;

                FD_ZERO(&readfds);
                FD_SET(fd, &readfds);
                tv.tv_sec = timeout_secs;
                tv.tv_usec = 0;

                int select_result = select(fd + 1, &readfds, NULL, NULL, &tv);
                if (select_result <= 0) {
                    line[chars_read] = '\0';
                    result = (select_result == 0) ? 142 : 1;
                    break;
                }
            }

            ssize_t n = read(fd, &line[chars_read], 1);
            if (n <= 0) {
                /// EOF or error
                if (chars_read == 0) {
                    free(line);
                    line = NULL;
                    result = 1;
                } else {
                    line[chars_read] = '\0';
                }
                break;
            }

            /// Stop at newline even in nchars mode
            if (line[chars_read] == '\n') {
                line[chars_read] = '\0';
                break;
            }

            chars_read++;
        }

        if (line && chars_read == nchars) {
            line[nchars] = '\0';
        }

        /// Print newline if silent mode (since echo was disabled)
        if (silent_mode && is_tty) {
            printf("\n");
        }
    } else {
        /// Normal line reading via raw read() syscall — see
        /// read_line_from_fd above for why stdio is unsafe here.
        line = read_line_from_fd(fd);

        /// Print newline if silent mode (since echo was disabled)
        if (silent_mode && is_tty && line) {
            printf("\n");
        }
    }

    /// Restore terminal settings
    if (termios_modified) {
        tcsetattr(fd, TCSANOW, &orig_termios);
    }

    if (!line) {
        /// EOF or input error
        symtable_set_global(varname, "");
        return result ? result : 1;
    }

    /// Process backslashes unless in raw mode
    if (!raw_mode && line) {
        char *processed = malloc(strlen(line) + 1);
        if (processed) {
            int j = 0;
            for (int i = 0; line[i]; i++) {
                if (line[i] == '\\' && line[i + 1]) {
                    /// Process escape sequences
                    i++; /// Skip the backslash
                    switch (line[i]) {
                    case 'n':
                        processed[j++] = '\n';
                        break;
                    case 't':
                        processed[j++] = '\t';
                        break;
                    case 'r':
                        processed[j++] = '\r';
                        break;
                    case '\\':
                        processed[j++] = '\\';
                        break;
                    default:
                        processed[j++] = '\\';
                        processed[j++] = line[i];
                        break;
                    }
                } else {
                    processed[j++] = line[i];
                }
            }
            processed[j] = '\0';
            free(line);
            line = processed;
        }
    }

    /* Assign the line to one or more variables. Single-name fast
     * path matches POSIX read's default behavior (entire line ->
     * the one variable). For N>1 names, split on IFS into N-1
     * leading fields and assign the remainder (preserving internal
     * IFS chars) to the last variable. */
    if (n_varnames == 1) {
        symtable_set_global(varname, line ? line : "");
    } else {
        const char *src = line ? line : "";
        /* POSIX IFS default is space, tab, newline. Honor a user-set
         * IFS if present in the symbol table. Read IFS as the SET
         * of delimiter chars; consecutive whitespace IFS chars are
         * collapsed by POSIX field-splitting semantics, but
         * non-whitespace IFS chars produce empty fields. For the
         * canonical read-line case (default IFS), the whitespace-
         * coalescing behavior is what real scripts depend on. */
        char *ifs_val = symtable_get_var(current_executor->symtable, "IFS");
        const char *ifs = ifs_val ? ifs_val : " \t\n";

        for (int i = 0; i < n_varnames - 1; i++) {
            /// Skip leading IFS whitespace before each field.
            while (*src && strchr(ifs, *src)) {
                src++;
            }
            const char *field_start = src;
            while (*src && !strchr(ifs, *src)) {
                src++;
            }
            size_t flen = (size_t)(src - field_start);
            char *field = malloc(flen + 1);
            if (!field) {
                free(ifs_val);
                free(line);
                return 1;
            }
            memcpy(field, field_start, flen);
            field[flen] = '\0';
            symtable_set_global(argv[opt_index + i], field);
            free(field);
        }
        /* Last variable: skip one leading IFS-whitespace run then
         * take everything else verbatim (including internal IFS
         * chars). */
        while (*src && strchr(ifs, *src)) {
            src++;
        }
        symtable_set_global(argv[opt_index + n_varnames - 1], src);
        free(ifs_val);
    }

    if (line)
        free(line);
    return result;
}
