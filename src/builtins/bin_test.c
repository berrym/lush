/**
 * @file bin_test.c
 * @brief `test` (and `[`) builtin -- evaluate conditional expression
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lle/unicode_compare.h"

#include <sys/stat.h>
#include <sys/types.h>

/// Forward declarations for mutual recursion.
static int evaluate_test_expression(char **argv, int start, int end);
static int evaluate_single_test(char **argv, int start, int end);

/**
 * @brief Evaluate conditional expressions
 *
 * Enhanced POSIX-compliant test builtin with logical operators.
 * Supports file tests (-f, -d, -e, etc.), string tests (-z, -n, =, !=),
 * numeric comparisons (-eq, -ne, -lt, etc.), and logical operators (!, -a, -o).
 *
 * @param argc Argument count
 * @param argv Argument vector with test expression
 * @return 0 if expression is true, 1 if false, 2 on error
 */
int bin_test(int argc, char **argv) {
    if (argc == 1) {
        return 1; /// False if no arguments
    }

    /// Handle closing bracket for '[' command
    if (strcmp(argv[0], "[") == 0) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            executor_error_report(current_executor,
                                  SHELL_ERR_MALFORMED_CONSTRUCT,
                                  builtin_get_source_location(),
                                  "'[' command missing closing ']'");
            return 2;
        }
        argc--; /// Remove the closing bracket
    }

    /// Use enhanced evaluation with logical operators
    return evaluate_test_expression(argv, 1, argc);
}

/**
 * @brief Recursively evaluate test expressions with logical operators
 *
 * Handles operator precedence: -o (OR) has lower precedence than -a (AND).
 * Also handles negation (!) operator.
 *
 * @param argv Argument vector
 * @param start Starting index in argv
 * @param end Ending index in argv (exclusive)
 * @return 0 if expression is true, 1 if false
 */
static int evaluate_test_expression(char **argv, int start, int end) {
    if (start >= end) {
        return 1; /// Empty expression is false
    }

    /// Handle negation operator
    if (start < end && strcmp(argv[start], "!") == 0) {
        int result = evaluate_test_expression(argv, start + 1, end);
        return (result == 0) ? 1 : 0; /// Flip the result
    }

    /// Find logical operators (-o has lower precedence than -a)
    /// First pass: look for -o (OR)
    for (int i = start + 1; i < end - 1; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            int left = evaluate_test_expression(argv, start, i);
            int right = evaluate_test_expression(argv, i + 1, end);
            return (left == 0 || right == 0) ? 0 : 1;
        }
    }

    /// Second pass: look for -a (AND)
    for (int i = start + 1; i < end - 1; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            int left = evaluate_test_expression(argv, start, i);
            int right = evaluate_test_expression(argv, i + 1, end);
            return (left == 0 && right == 0) ? 0 : 1;
        }
    }

    /// No logical operators found, evaluate as single test
    return evaluate_single_test(argv, start, end);
}

/**
 * @brief Evaluate a single test condition
 *
 * Handles unary operators (-z, -n, -f, -d, etc.) and binary operators
 * (=, !=, -eq, -ne, -lt, -le, -gt, -ge).
 *
 * @param argv Argument vector
 * @param start Starting index in argv
 * @param end Ending index in argv (exclusive)
 * @return 0 if condition is true, 1 if false, 2 on error
 */
static int evaluate_single_test(char **argv, int start, int end) {
    int argc = end - start;

    /// Simple test implementations
    if (argc == 1) {
        /// test STRING - true if string is non-empty
        return (strlen(argv[start]) > 0) ? 0 : 1;
    }

    if (argc == 2) {
        if (strcmp(argv[start], "-z") == 0) {
            /// test -z STRING - true if string is empty
            return (strlen(argv[start + 1]) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-n") == 0) {
            /// test -n STRING - true if string is non-empty
            return (strlen(argv[start + 1]) > 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-f") == 0) {
            /// test -f FILE - true if file exists and is regular
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISREG(st.st_mode)) ? 0
                                                                            : 1;
        } else if (strcmp(argv[start], "-d") == 0) {
            /// test -d DIR - true if directory exists
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISDIR(st.st_mode)) ? 0
                                                                            : 1;
        } else if (strcmp(argv[start], "-e") == 0) {
            /// test -e PATH - true if path exists
            struct stat st;
            return (stat(argv[start + 1], &st) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-c") == 0) {
            /// test -c FILE - true if file is character device
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISCHR(st.st_mode)) ? 0
                                                                            : 1;
        } else if (strcmp(argv[start], "-b") == 0) {
            /// test -b FILE - true if file is block device
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISBLK(st.st_mode)) ? 0
                                                                            : 1;
        } else if (strcmp(argv[start], "-L") == 0 ||
                   strcmp(argv[start], "-h") == 0) {
            /// test -L FILE or -h FILE - true if file is symbolic link
            struct stat st;
            return (lstat(argv[start + 1], &st) == 0 && S_ISLNK(st.st_mode))
                       ? 0
                       : 1;
        } else if (strcmp(argv[start], "-p") == 0) {
            /// test -p FILE - true if file is named pipe (FIFO)
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISFIFO(st.st_mode))
                       ? 0
                       : 1;
        } else if (strcmp(argv[start], "-S") == 0) {
            /// test -S FILE - true if file is socket
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISSOCK(st.st_mode))
                       ? 0
                       : 1;
        } else if (strcmp(argv[start], "-r") == 0) {
            /// test -r FILE - true if file is readable
            return (access(argv[start + 1], R_OK) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-w") == 0) {
            /// test -w FILE - true if file is writable
            return (access(argv[start + 1], W_OK) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-x") == 0) {
            /// test -x FILE - true if file is executable
            return (access(argv[start + 1], X_OK) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-s") == 0) {
            /// test -s FILE - true if file exists and is not empty
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && st.st_size > 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-t") == 0) {
            /// test -t FD - true if file descriptor is open and refers to a
            /// terminal
            int fd = atoi(argv[start + 1]);
            return isatty(fd) ? 0 : 1;
        }
    }

    if (argc == 3) {
        if (strcmp(argv[start + 1], "=") == 0) {
            /// test STRING1 = STRING2. Compare under NFC equivalence
            /// via the LLE Unicode primitive so canonically-equivalent
            /// inputs (precomposed é vs decomposed e + combining acute)
            /// satisfy the user's "are these the same string?" intent.
            /// Bash and zsh keep byte-level comparison; this is a
            /// deliberate lush-superset divergence justified by the
            /// project's NFC-everywhere policy.
            return lle_unicode_strings_equal(argv[start], argv[start + 2], NULL)
                       ? 0
                       : 1;
        } else if (strcmp(argv[start + 1], "!=") == 0) {
            /// test STRING1 != STRING2 -- same NFC equivalence,
            /// negated.
            return lle_unicode_strings_equal(argv[start], argv[start + 2], NULL)
                       ? 1
                       : 0;
        } else if (strcmp(argv[start + 1], "-eq") == 0) {
            /// test NUM1 -eq NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 == n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-ne") == 0) {
            /// test NUM1 -ne NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 != n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-lt") == 0) {
            /// test NUM1 -lt NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 < n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-le") == 0) {
            /// test NUM1 -le NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 <= n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-gt") == 0) {
            /// test NUM1 -gt NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 > n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-ge") == 0) {
            /// test NUM1 -ge NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 >= n2) ? 0 : 1;
        }
    }

    executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                          builtin_get_source_location(),
                          "unknown test condition or invalid arguments");
    return 2;
}
