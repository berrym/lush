/**
 * @file test_node_stubs.c
 * @brief Stub implementations for AST node unit tests
 */

#include <stdarg.h>
#include <stdio.h>

/// Error function stubs (from errors.c)
int error_return(int errcode, const char *fmt, ...) {
    (void)errcode;
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fprintf(stderr, "\n");
    }
    return errcode;
}

void error_syscall(const char *str) {
    if (str) {
        perror(str);
    }
}
