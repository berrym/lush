/**
 * @file fuzz_stubs.c
 * @brief Stub implementations for fuzz targets
 *
 * Provides minimal implementations of functions needed by parser/tokenizer
 * fuzz targets to avoid pulling in heavy dependencies like executor, LLE, etc.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Function Parameter Stubs (from executor.c)
 * ============================================================================
 */

typedef struct function_param {
    char *name;                  ///< Parameter name
    char *default_value;         ///< Default value (NULL if required)
    bool is_required;            ///< True if parameter is required
    struct function_param *next; ///< Next parameter in list
} function_param_t;

function_param_t *create_function_param(const char *name,
                                        const char *default_value) {
    function_param_t *param = malloc(sizeof(function_param_t));
    if (!param)
        return NULL;

    param->name = name ? strdup(name) : NULL;
    param->default_value = default_value ? strdup(default_value) : NULL;
    param->is_required = (default_value == NULL);
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
 * AST-copy / debug-trace stubs (from executor.c and debug.c)
 * ============================================================================
 *
 * parser.c gained references to these when the zsh multi-name function
 * definition (`function NAME1 NAME2 { body }`) landed: the multi-name
 * path deep-copies the body via copy_ast_node, and the parse-trace
 * instrumentation calls debug_trace_printf guarded on g_debug_context.
 * The parser fuzz target exercises parsing only, so a NULL-returning
 * copy and a no-op trace are the correct minimal shapes -- they keep
 * the heavy executor/debug translation units out of the fuzz link
 * while leaving the parse path fully fuzzable. Opaque pointer types
 * suffice; the fuzzer never dereferences the copy result.
 */

struct node;

struct node *copy_ast_node(struct node *node) {
    (void)node;
    return NULL;
}

struct debug_context;
struct debug_context *g_debug_context = NULL;

void debug_trace_printf(struct debug_context *ctx, const char *format, ...) {
    (void)ctx;
    (void)format;
    /// Silent for fuzzing
}

/* ============================================================================
 * UTF-8 Support
 * ============================================================================
 *
 * Real implementations now come from src/lle/unicode/utf8_support.c and
 * src/lle/unicode/unicode_grapheme.c, which are linked into the fuzz
 * binaries directly (issue #50). The local stub of
 * lle_utf8_decode_codepoint that lived here was removed to avoid a
 * duplicate-symbol link error.
 */

/* ============================================================================
 * Error Function Stubs (from errors.c)
 * ============================================================================
 */

int error_return(int errcode, const char *fmt, ...) {
    (void)errcode;
    (void)fmt;
    /// Silent for fuzzing
    return errcode;
}

void error_syscall(const char *str) {
    (void)str;
    /// Silent for fuzzing
}

/* ============================================================================
 * Global Variables
 * ============================================================================
 */

/// last_exit_status is defined in src/globals.c
