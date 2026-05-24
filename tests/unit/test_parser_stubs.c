/**
 * @file test_parser_stubs.c
 * @brief Stub implementations for parser unit tests
 *
 * Provides minimal implementations of functions needed by parser tests
 * to avoid pulling in the entire executor and other heavy dependencies.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Function Parameter Stubs (from executor.c)
 * ============================================================================
 */

typedef struct function_param {
    char *name;
    char *default_value;
    struct function_param *next;
} function_param_t;

function_param_t *create_function_param(const char *name,
                                        const char *default_value) {
    function_param_t *param = malloc(sizeof(function_param_t));
    if (!param)
        return NULL;

    param->name = name ? strdup(name) : NULL;
    param->default_value = default_value ? strdup(default_value) : NULL;
    param->next = NULL;

    return param;
}

void free_function_params(function_param_t *params) {
    while (params) {
        function_param_t *next = params->next;
        free(params->name);
        free(params->default_value);
        free(params);
        params = next;
    }
}

/* ============================================================================
 * POSIX Mode Stub
 * ============================================================================
 */

bool is_posix_mode_enabled(void) { return false; }

/* ============================================================================
 * Error Function Stubs (from errors.c)
 * ============================================================================
 */

#include <stdarg.h>
#include <stdio.h>

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

/* ============================================================================
 * Global Variables
 * ============================================================================
 */

#include "lush.h"

int last_exit_status = 0;
shell_options_t shell_opts = {0};

// Interactive shell stub
bool is_interactive_shell(void) { return false; }

// Debug subsystem stubs -- parser.c uses debug_trace_printf when
// instrumenting recognizer decisions. Tests don't link the debug
// subsystem so stub it as a no-op here.
struct debug_context;
struct debug_context *g_debug_context = NULL;
void debug_trace_printf(struct debug_context *ctx, const char *format, ...) {
    (void)ctx;
    (void)format;
}
