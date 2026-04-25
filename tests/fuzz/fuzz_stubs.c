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
    char *name;                  /* Parameter name */
    char *default_value;         /* Default value (NULL if required) */
    bool is_required;            /* True if parameter is required */
    struct function_param *next; /* Next parameter in list */
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
 * UTF-8 Support Stubs (from LLE)
 * ============================================================================
 */

/**
 * @brief Decode a UTF-8 codepoint from a byte sequence
 *
 * Minimal implementation for fuzzing - just handles basic decoding.
 */
int lle_utf8_decode_codepoint(const char *str, size_t len,
                              uint32_t *codepoint) {
    if (!str || len == 0 || !codepoint) {
        return 0;
    }

    unsigned char c = (unsigned char)str[0];

    /* ASCII (single byte) */
    if (c < 0x80) {
        *codepoint = c;
        return 1;
    }

    /* 2-byte sequence */
    if ((c & 0xE0) == 0xC0 && len >= 2) {
        *codepoint = ((c & 0x1F) << 6) | (str[1] & 0x3F);
        return 2;
    }

    /* 3-byte sequence */
    if ((c & 0xF0) == 0xE0 && len >= 3) {
        *codepoint =
            ((c & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
        return 3;
    }

    /* 4-byte sequence */
    if ((c & 0xF8) == 0xF0 && len >= 4) {
        *codepoint = ((c & 0x07) << 18) | ((str[1] & 0x3F) << 12) |
                     ((str[2] & 0x3F) << 6) | (str[3] & 0x3F);
        return 4;
    }

    /* Invalid - treat as single byte */
    *codepoint = c;
    return 1;
}

/* ============================================================================
 * Error Function Stubs (from errors.c)
 * ============================================================================
 */

int error_return(int errcode, const char *fmt, ...) {
    (void)errcode;
    (void)fmt;
    /* Silent for fuzzing */
    return errcode;
}

void error_syscall(const char *str) {
    (void)str;
    /* Silent for fuzzing */
}

/* ============================================================================
 * Global Variables
 * ============================================================================
 */

/* last_exit_status is defined in src/globals.c */
