/**
 * @file arithmetic.c
 * @brief POSIX-Compliant Arithmetic Expansion Module for Lush Shell
 *
 * This module provides arithmetic expansion using the shunting yard algorithm.
 * It handles $((...)) expressions with full support for:
 * - Integer arithmetic (+, -, *, /, %)
 * - Comparison operators (<, <=, >, >=, ==, !=)
 * - Logical operators (&&, ||, !)
 * - Bitwise operators (&, |, ^, ~, <<, >>)
 * - Assignment operators (=, +=, -=, *=, /=, %=)
 * - Increment/decrement (++, --)
 * - Exponentiation (**)
 * - Variable references and command substitution
 * - Hexadecimal and octal number formats
 *
 * Based on the shunting yard algorithm implementation from:
 * - Original: http://en.literateprograms.org/Shunting_yard_algorithm_(C)
 * - Modified by Mohammed Isam for Layla shell
 * - Further modernized for Lush shell
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "arithmetic.h"

#include "executor.h"
#include "identifier.h"
#include "lush.h"
#include "symtable.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define MAXOPSTACK 64
#define MAXNUMSTACK 64
#define MAXBASE 36

/// Stack item for arithmetic evaluation
typedef struct {
    enum { ITEM_LONG_INT = 1, ITEM_VAR_PTR = 2 } type;

    union {
        ssize_t val;
        char *var_name; /// Store variable name instead of pointer
    };
    void *executor_context; /// Store executor context for scoped variable
                            /// resolution
} stack_item_t;

/// Operator associativity
enum { ASSOC_NONE = 0, ASSOC_LEFT = 1, ASSOC_RIGHT = 2 };

/// Operator structure
typedef struct {
    char op;
    int prec;
    int assoc;
    char unary;
    char chars;
    ssize_t (*eval)(stack_item_t *a1, stack_item_t *a2);
} op_t;

/// Arithmetic evaluation context
typedef struct {
    op_t *opstack[MAXOPSTACK];
    int nopstack;
    stack_item_t numstack[MAXNUMSTACK];
    int nnumstack;
    bool errflag;
    char *error_message;
    void *executor; /// Store executor context for scoped variable resolution
} arithm_context_t;

/**
 * @brief Clean up an arithmetic evaluation context
 *
 * Frees any dynamically allocated memory in the context, including
 * variable names stored in the number stack and error messages.
 *
 * @param ctx The arithmetic context to clean up
 */
static void arithm_context_cleanup(arithm_context_t *ctx) {
    if (!ctx) {
        return;
    }

    /// Free any variable names stored in the number stack
    /// IMPORTANT: Only free var_name if type is ITEM_VAR_PTR, since
    /// val and var_name share the same memory (union) and we must not
    /// free numeric values as pointers
    for (int i = 0; i < ctx->nnumstack; i++) {
        if (ctx->numstack[i].type == ITEM_VAR_PTR &&
            ctx->numstack[i].var_name) {
            free(ctx->numstack[i].var_name);
            ctx->numstack[i].var_name = NULL;
        }
    }

    if (ctx->error_message) {
        free(ctx->error_message);
        ctx->error_message = NULL;
    }
}

/**
 * @brief Free any allocated memory in a stack item
 *
 * This should be called after a popped stack item has been consumed
 * to prevent memory leaks from strdup'd var_name fields.
 *
 * @param item The stack item to clean up
 */
static void stack_item_cleanup(stack_item_t *item) {
    if (item && item->type == ITEM_VAR_PTR && item->var_name) {
        free(item->var_name);
        item->var_name = NULL;
    }
}

/* Typed error state for the structured shell error system. The pre-2026-04
 * bare globals (arithm_error_flag / arithm_error_message) were deleted as
 * part of the error-system migration; callers now read this state through
 * the accessor functions declared in arithmetic.h.
 *
 * Why file-scope state at all (rather than passing a context through
 * eval_*): the eval_* operator functions take only the two stack-item
 * arguments shunting yard requires. Threading a fifth parameter through
 * every operator is a deeper API rework that overlaps with #64's source-
 * location threading; that lands together when #64 is addressed. Until
 * then, file-scope state is the right architectural shape — the
 * arithmetic module owns its evaluation; the executor owns reporting.
 */
typedef struct {
    bool flagged;
    shell_error_code_t code;
    char message[512];
    char while_context[128];
    char help[256];
} arithm_error_state_t;

static arithm_error_state_t s_arithm_error;

/// Forward declarations for internal functions
static ssize_t long_value(stack_item_t *item);
static void push_opstack(arithm_context_t *ctx, op_t *op);
static op_t *pop_opstack(arithm_context_t *ctx);
static void push_numstackl(arithm_context_t *ctx, ssize_t val);
static void push_numstackv(arithm_context_t *ctx, const char *var_name);
static stack_item_t pop_numstack(arithm_context_t *ctx);
static void shunt_op(arithm_context_t *ctx, op_t *op);
static op_t *get_op(const char *expr);
static ssize_t get_num(const char *expr, int *nchars);
static char *get_var_name(const char *expr, int *nchars);

/// ============================================================================
/// OPERATOR EVALUATION FUNCTIONS
/// ============================================================================

/* Signed-overflow-safe primitives. Bash and zsh both rely on signed
 * wraparound for +, -, *, unary -, <<; in standard C that is undefined
 * behaviour, but performing the operation in unsigned space and casting
 * back is well-defined and produces the same two's-complement bit
 * pattern on every platform lush targets. Right shift is left as a
 * signed shift (impl-defined, arithmetic on lush platforms — matches
 * bash) but with the count masked to a defined range. Division and
 * modulo are signed but explicitly handle the LONG_MIN/-1 UB corner
 * to match bash's observed result. */
_Static_assert(sizeof(ssize_t) == sizeof(long),
               "arithm helpers assume ssize_t is long");
#define ARITHM_BITS ((size_t)(sizeof(ssize_t) * CHAR_BIT))

static inline ssize_t arithm_wrap_add(ssize_t a, ssize_t b) {
    return (ssize_t)((size_t)a + (size_t)b);
}
static inline ssize_t arithm_wrap_sub(ssize_t a, ssize_t b) {
    return (ssize_t)((size_t)a - (size_t)b);
}
static inline ssize_t arithm_wrap_mul(ssize_t a, ssize_t b) {
    return (ssize_t)((size_t)a * (size_t)b);
}
static inline ssize_t arithm_wrap_neg(ssize_t a) {
    return (ssize_t)(0 - (size_t)a);
}
static inline ssize_t arithm_wrap_lsh(ssize_t a, ssize_t count) {
    size_t c = (size_t)count & (ARITHM_BITS - 1);
    return (ssize_t)((size_t)a << c);
}
static inline ssize_t arithm_wrap_rsh(ssize_t a, ssize_t count) {
    size_t c = (size_t)count & (ARITHM_BITS - 1);
    return a >> c;
}
static inline ssize_t arithm_safe_div(ssize_t a, ssize_t b) {
    if (a == LONG_MIN && b == -1) {
        return LONG_MIN;
    }
    return a / b;
}
static inline ssize_t arithm_safe_mod(ssize_t a, ssize_t b) {
    if (a == LONG_MIN && b == -1) {
        return 0;
    }
    return a % b;
}

/**
 * @brief Unary minus operator evaluation
 * @param a1 Operand
 * @param a2 Unused
 * @return Negated value
 */
static ssize_t eval_uminus(stack_item_t *a1, stack_item_t *a2) {
    (void)a2;
    return arithm_wrap_neg(long_value(a1));
}

/// @brief Unary plus operator (identity)
static ssize_t eval_uplus(stack_item_t *a1, stack_item_t *a2) {
    (void)a2;
    return long_value(a1);
}

/// @brief Logical NOT operator
static ssize_t eval_lognot(stack_item_t *a1, stack_item_t *a2) {
    (void)a2;
    return !long_value(a1);
}

/// @brief Bitwise NOT (complement) operator
static ssize_t eval_bitnot(stack_item_t *a1, stack_item_t *a2) {
    (void)a2;
    return ~long_value(a1);
}

/// @brief Multiplication operator
static ssize_t eval_mul(stack_item_t *a1, stack_item_t *a2) {
    return arithm_wrap_mul(long_value(a1), long_value(a2));
}

/// @brief Addition operator
static ssize_t eval_add(stack_item_t *a1, stack_item_t *a2) {
    return arithm_wrap_add(long_value(a1), long_value(a2));
}

/// @brief Subtraction operator
static ssize_t eval_sub(stack_item_t *a1, stack_item_t *a2) {
    return arithm_wrap_sub(long_value(a1), long_value(a2));
}

/// @brief Division operator with zero-check
static ssize_t eval_div(stack_item_t *a1, stack_item_t *a2) {
    ssize_t divisor = long_value(a2);
    if (divisor == 0) {
        arithm_set_error(SHELL_ERR_DIVISION_BY_ZERO, "evaluating / operator",
                         "divisor must be non-zero", "division by zero");
        return 0;
    }
    return arithm_safe_div(long_value(a1), divisor);
}

/// @brief Modulo operator with zero-check
static ssize_t eval_mod(stack_item_t *a1, stack_item_t *a2) {
    ssize_t divisor = long_value(a2);
    if (divisor == 0) {
        arithm_set_error(SHELL_ERR_MODULO_BY_ZERO, "evaluating %% operator",
                         "divisor must be non-zero",
                         "division by zero in modulo operation");
        return 0;
    }
    return arithm_safe_mod(long_value(a1), divisor);
}

/// @brief Left shift operator
static ssize_t eval_lsh(stack_item_t *a1, stack_item_t *a2) {
    return arithm_wrap_lsh(long_value(a1), long_value(a2));
}

/// @brief Right shift operator
static ssize_t eval_rsh(stack_item_t *a1, stack_item_t *a2) {
    return arithm_wrap_rsh(long_value(a1), long_value(a2));
}

/// @brief Less than comparison
static ssize_t eval_lt(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) < long_value(a2);
}

/// @brief Less than or equal comparison
static ssize_t eval_le(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) <= long_value(a2);
}

/// @brief Greater than comparison
static ssize_t eval_gt(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) > long_value(a2);
}

/// @brief Greater than or equal comparison
static ssize_t eval_ge(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) >= long_value(a2);
}

/// @brief Equality comparison
static ssize_t eval_eq(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) == long_value(a2);
}

/// @brief Inequality comparison
static ssize_t eval_ne(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) != long_value(a2);
}

/// @brief Bitwise AND operator
static ssize_t eval_bitand(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) & long_value(a2);
}

/// @brief Bitwise XOR operator
static ssize_t eval_bitxor(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) ^ long_value(a2);
}

/// @brief Bitwise OR operator
static ssize_t eval_bitor(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) | long_value(a2);
}

/// @brief Logical AND operator (short-circuit)
static ssize_t eval_logand(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) && long_value(a2);
}

/// @brief Logical OR operator (short-circuit)
static ssize_t eval_logor(stack_item_t *a1, stack_item_t *a2) {
    return long_value(a1) || long_value(a2);
}

/// @brief Comma operator - evaluates both, returns second
static ssize_t eval_comma(stack_item_t *a1, stack_item_t *a2) {
    (void)a1; /// First expression is evaluated but result discarded
    return long_value(a2);
}

/// @brief Ternary colon - placeholder, actual logic in shunt_op
static ssize_t eval_ternary_colon(stack_item_t *a1, stack_item_t *a2) {
    (void)a1;
    (void)a2;
    /// This should never be called directly - handled in shunt_op
    return 0;
}

/// @brief Ternary question - placeholder, actual logic in shunt_op
static ssize_t eval_ternary_question(stack_item_t *a1, stack_item_t *a2) {
    (void)a1;
    (void)a2;
    /// This should never be called directly - handled in shunt_op
    return 0;
}

/// ============================================================================
/// ASSIGNMENT OPERATORS
/// ============================================================================

/**
 * @brief Get variable value using executor context if available
 *
 * Uses the executor's symbol table for scoped variable resolution,
 * falling back to global symbol table if no executor context.
 *
 * @param item Stack item containing variable name and executor context
 * @return Current value of the variable, or 0 if not found
 */
static ssize_t get_var_value_scoped(stack_item_t *item) {
    if (!item || !item->var_name) {
        return 0;
    }

    /// Use executor context if available for scoped variable resolution
    if (item->executor_context) {
        executor_t *exec = (executor_t *)item->executor_context;
        if (exec && exec->symtable) {
            char *value = symtable_get_var(exec->symtable, item->var_name);
            if (value) {
                ssize_t result = strtol(value, NULL, 10);
                free(value);
                return result;
            }
        }
    }

    /// Fallback to global
    char *value = symtable_get_global(item->var_name);
    if (value) {
        return strtol(value, NULL, 10);
    }
    return 0;
}

/**
 * @brief Set variable value using executor context if available
 *
 * Uses the executor's symbol table for scoped variable assignment,
 * falling back to global symbol table if no executor context.
 *
 * @param item Stack item containing variable name and executor context
 * @param value Value to set
 */
static void set_var_value_scoped(stack_item_t *item, ssize_t value) {
    if (!item || !item->var_name) {
        return;
    }

    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%zd", value);

    /// Use executor context if available for scoped variable assignment
    if (item->executor_context) {
        executor_t *exec = (executor_t *)item->executor_context;
        if (exec && exec->symtable) {
            symtable_set_var(exec->symtable, item->var_name, value_str,
                             SYMVAR_NONE);
            return;
        }
    }

    /// Fallback to global
    symtable_set_global(item->var_name, value_str);
}

/* Helper for the six assignment operators that share the same lvalue
 * check. The `op` argument is the operator token used in the while:
 * line ("=", "+=", etc.). */
#define ARITHM_REQUIRE_LVALUE(item, op)                                        \
    do {                                                                       \
        if ((item)->type != ITEM_VAR_PTR || !(item)->var_name) {               \
            arithm_set_error(SHELL_ERR_ARITH_INVALID_ASSIGN_TARGET,            \
                             "evaluating " op " operator",                     \
                             "left-hand side must be a variable name",         \
                             "invalid assignment target");                     \
            return 0;                                                          \
        }                                                                      \
    } while (0)

/// @brief Simple assignment operator
static ssize_t eval_assign(stack_item_t *a1, stack_item_t *a2) {
    ARITHM_REQUIRE_LVALUE(a1, "=");

    ssize_t value = long_value(a2);
    set_var_value_scoped(a1, value);
    return value;
}

/// @brief Addition assignment operator (+=)
static ssize_t eval_addeq(stack_item_t *a1, stack_item_t *a2) {
    ARITHM_REQUIRE_LVALUE(a1, "+=");

    ssize_t current_value = get_var_value_scoped(a1);
    ssize_t add_value = long_value(a2);
    ssize_t result = arithm_wrap_add(current_value, add_value);

    set_var_value_scoped(a1, result);
    return result;
}

/// @brief Subtraction assignment operator (-=)
static ssize_t eval_subeq(stack_item_t *a1, stack_item_t *a2) {
    ARITHM_REQUIRE_LVALUE(a1, "-=");

    ssize_t current_value = get_var_value_scoped(a1);
    ssize_t sub_value = long_value(a2);
    ssize_t result = arithm_wrap_sub(current_value, sub_value);

    set_var_value_scoped(a1, result);
    return result;
}

/// @brief Multiplication assignment operator (*=)
static ssize_t eval_muleq(stack_item_t *a1, stack_item_t *a2) {
    ARITHM_REQUIRE_LVALUE(a1, "*=");

    ssize_t current_value = get_var_value_scoped(a1);
    ssize_t mul_value = long_value(a2);
    ssize_t result = arithm_wrap_mul(current_value, mul_value);

    set_var_value_scoped(a1, result);
    return result;
}

/// @brief Division assignment operator (/=) with zero-check
static ssize_t eval_diveq(stack_item_t *a1, stack_item_t *a2) {
    ARITHM_REQUIRE_LVALUE(a1, "/=");

    ssize_t div_value = long_value(a2);
    if (div_value == 0) {
        arithm_set_error(SHELL_ERR_DIVISION_BY_ZERO, "evaluating /= operator",
                         "divisor must be non-zero", "division by zero");
        return 0;
    }

    ssize_t current_value = get_var_value_scoped(a1);
    ssize_t result = arithm_safe_div(current_value, div_value);

    set_var_value_scoped(a1, result);
    return result;
}

/// @brief Modulo assignment operator (%=) with zero-check
static ssize_t eval_modeq(stack_item_t *a1, stack_item_t *a2) {
    ARITHM_REQUIRE_LVALUE(a1, "%=");

    ssize_t mod_value = long_value(a2);
    if (mod_value == 0) {
        arithm_set_error(SHELL_ERR_MODULO_BY_ZERO, "evaluating %%= operator",
                         "divisor must be non-zero", "modulo by zero");
        return 0;
    }

    ssize_t current_value = get_var_value_scoped(a1);
    ssize_t result = arithm_safe_mod(current_value, mod_value);

    set_var_value_scoped(a1, result);
    return result;
}

/// ============================================================================
/// INCREMENT/DECREMENT OPERATORS
/// ============================================================================

#define ARITHM_REQUIRE_INC_TARGET(item, op)                                    \
    do {                                                                       \
        if ((item)->type != ITEM_VAR_PTR || !(item)->var_name) {               \
            arithm_set_error(SHELL_ERR_ARITH_INVALID_INCREMENT_TARGET,         \
                             "evaluating " op " operator",                     \
                             "operand must be a variable name",                \
                             "invalid increment target");                      \
            return 0;                                                          \
        }                                                                      \
    } while (0)

#define ARITHM_REQUIRE_DEC_TARGET(item, op)                                    \
    do {                                                                       \
        if ((item)->type != ITEM_VAR_PTR || !(item)->var_name) {               \
            arithm_set_error(SHELL_ERR_ARITH_INVALID_DECREMENT_TARGET,         \
                             "evaluating " op " operator",                     \
                             "operand must be a variable name",                \
                             "invalid decrement target");                      \
            return 0;                                                          \
        }                                                                      \
    } while (0)

/// @brief Pre-increment operator (++x)
static ssize_t eval_preinc(stack_item_t *a1, stack_item_t *a2) {
    (void)a2;
    ARITHM_REQUIRE_INC_TARGET(a1, "pre-increment ++");

    ssize_t value = arithm_wrap_add(long_value(a1), 1);
    set_var_value_scoped(a1, value);
    return value;
}

/// @brief Pre-decrement operator (--x)
static ssize_t eval_predec(stack_item_t *a1, stack_item_t *a2) {
    (void)a2;
    ARITHM_REQUIRE_DEC_TARGET(a1, "pre-decrement --");

    ssize_t value = arithm_wrap_sub(long_value(a1), 1);
    set_var_value_scoped(a1, value);
    return value;
}

/// @brief Post-increment operator (x++) - returns old value
static ssize_t eval_postinc(stack_item_t *a1, stack_item_t *a2) {
    (void)a2;
    ARITHM_REQUIRE_INC_TARGET(a1, "post-increment ++");

    ssize_t old_value = long_value(a1);
    set_var_value_scoped(a1, arithm_wrap_add(old_value, 1));
    return old_value;
}

/// @brief Post-decrement operator (x--) - returns old value
static ssize_t eval_postdec(stack_item_t *a1, stack_item_t *a2) {
    (void)a2;
    ARITHM_REQUIRE_DEC_TARGET(a1, "post-decrement --");

    ssize_t old_value = long_value(a1);
    set_var_value_scoped(a1, arithm_wrap_sub(old_value, 1));
    return old_value;
}

/**
 * @brief Exponentiation operator (**)
 *
 * Calculates base raised to the power of exponent.
 * Only non-negative integer exponents are supported.
 *
 * @param a1 Base value
 * @param a2 Exponent value (must be non-negative)
 * @return Result of base^exponent, or 0 on error
 */
static ssize_t eval_exp(stack_item_t *a1, stack_item_t *a2) {
    ssize_t base = long_value(a1);
    ssize_t exp = long_value(a2);
    if (exp < 0) {
        arithm_set_error(
            SHELL_ERR_ARITH_NEGATIVE_EXPONENT, "evaluating ** operator",
            "exponent must be non-negative", "negative exponent not supported");
        return 0;
    }

    ssize_t result = 1;
    for (ssize_t i = 0; i < exp; i++) {
        result = arithm_wrap_mul(result, base);
    }
    return result;
}

/// Operator character constants
#define CH_GT '>'
#define CH_LT '<'
#define CH_GE 0x01
#define CH_LE 0x02
#define CH_RSH 0x03
#define CH_LSH 0x04
#define CH_NE 0x05
#define CH_EQ 0x06
#define CH_AND 0x07
#define CH_OR 0x08
#define CH_EXP 0x09
#define CH_ASSIGN 0x0A
#define CH_PREINC 0x0B
#define CH_PREDEC 0x0C
#define CH_POSTINC 0x0D
#define CH_POSTDEC 0x0E
#define CH_ADDEQ 0x0F
#define CH_SUBEQ 0x10
#define CH_MULEQ 0x11
#define CH_DIVEQ 0x12
#define CH_MODEQ 0x13
#define CH_TERNARY_Q 0x14 /// ? part of ternary
#define CH_TERNARY_C 0x15 /// : part of ternary
#define CH_COMMA 0x16     /// comma operator

/// Operator definitions (only binary operators in main table)
static op_t operators[] = {
    {         '(',  0,  ASSOC_NONE, 0, 1,                  NULL},
    {         ')',  0,  ASSOC_NONE, 0, 1,                  NULL},
    {         '!',  2, ASSOC_RIGHT, 1, 1,           eval_lognot},
    {         '~',  2, ASSOC_RIGHT, 1, 1,           eval_bitnot},
    {      CH_EXP,  3, ASSOC_RIGHT, 0, 2,              eval_exp},
    {         '*',  4,  ASSOC_LEFT, 0, 1,              eval_mul},
    {         '/',  4,  ASSOC_LEFT, 0, 1,              eval_div},
    {         '%',  4,  ASSOC_LEFT, 0, 1,              eval_mod},
    {         '+',  5,  ASSOC_LEFT, 0, 1,              eval_add},
    {         '-',  5,  ASSOC_LEFT, 0, 1,              eval_sub},
    {      CH_LSH,  6,  ASSOC_LEFT, 0, 2,              eval_lsh},
    {      CH_RSH,  6,  ASSOC_LEFT, 0, 2,              eval_rsh},
    {       CH_LT,  7,  ASSOC_LEFT, 0, 1,               eval_lt},
    {       CH_LE,  7,  ASSOC_LEFT, 0, 2,               eval_le},
    {       CH_GT,  7,  ASSOC_LEFT, 0, 1,               eval_gt},
    {       CH_GE,  7,  ASSOC_LEFT, 0, 2,               eval_ge},
    {       CH_EQ,  8,  ASSOC_LEFT, 0, 2,               eval_eq},
    {       CH_NE,  8,  ASSOC_LEFT, 0, 2,               eval_ne},
    {         '&',  9,  ASSOC_LEFT, 0, 1,           eval_bitand},
    {         '^', 10,  ASSOC_LEFT, 0, 1,           eval_bitxor},
    {         '|', 11,  ASSOC_LEFT, 0, 1,            eval_bitor},
    {      CH_AND, 12,  ASSOC_LEFT, 0, 2,           eval_logand},
    {       CH_OR, 13,  ASSOC_LEFT, 0, 2,            eval_logor},
    {CH_TERNARY_Q, 14, ASSOC_RIGHT, 0, 1, eval_ternary_question},
    {CH_TERNARY_C, 14, ASSOC_RIGHT, 0, 1,    eval_ternary_colon},
    {         '=', 15, ASSOC_RIGHT, 0, 1,           eval_assign},
    {    CH_ADDEQ, 15, ASSOC_RIGHT, 0, 2,            eval_addeq},
    {    CH_SUBEQ, 15, ASSOC_RIGHT, 0, 2,            eval_subeq},
    {    CH_MULEQ, 15, ASSOC_RIGHT, 0, 2,            eval_muleq},
    {    CH_DIVEQ, 15, ASSOC_RIGHT, 0, 2,            eval_diveq},
    {    CH_MODEQ, 15, ASSOC_RIGHT, 0, 2,            eval_modeq},
    {    CH_COMMA, 16,  ASSOC_LEFT, 0, 1,            eval_comma},
    {           0,  0,           0, 0, 0,                  NULL}
};

/// Unary operator definitions (separate from main table)
static op_t op_uminus = {'-', 2, ASSOC_RIGHT, 1, 1, eval_uminus};
static op_t op_uplus = {'+', 2, ASSOC_RIGHT, 1, 1, eval_uplus};
static op_t op_preinc = {CH_PREINC, 2, ASSOC_RIGHT, 1, 2, eval_preinc};
static op_t op_predec = {CH_PREDEC, 2, ASSOC_RIGHT, 1, 2, eval_predec};
static op_t op_postinc = {CH_POSTINC, 1, ASSOC_LEFT, 1, 2, eval_postinc};
static op_t op_postdec = {CH_POSTDEC, 1, ASSOC_LEFT, 1, 2, eval_postdec};

/// Operator shortcuts
#define OP_UMINUS (&op_uminus)
#define OP_UPLUS (&op_uplus)
#define OP_PREINC (&op_preinc)
#define OP_PREDEC (&op_predec)
#define OP_POSTINC (&op_postinc)
#define OP_POSTDEC (&op_postdec)

/// ============================================================================
/// STACK MANAGEMENT AND VALUE CONVERSION
/// ============================================================================

/**
 * @brief Get the numeric value from a stack item
 *
 * Converts a stack item to its numeric value. For literal integers,
 * returns the stored value. For variable references, looks up the
 * variable value in the symbol table using executor context if available.
 *
 * @param item The stack item to evaluate
 * @return The numeric value, or 0 for undefined variables
 */
static ssize_t long_value(stack_item_t *item) {
    switch (item->type) {
    case ITEM_LONG_INT:
        return item->val;
    case ITEM_VAR_PTR: {
        if (item->var_name) {
            /// Use executor context if available for scoped variable resolution
            if (item->executor_context) {
                /// Use proper executor structure from executor.h
                executor_t *exec = (executor_t *)item->executor_context;

                if (exec && exec->symtable) {
                    char *value =
                        symtable_get_var(exec->symtable, item->var_name);
                    if (value) {
                        long result = atol(value);
                        free(value);
                        return result;
                    }
                }
            }

            /// Fallback to global manager
            symtable_manager_t *manager = symtable_get_global_manager();
            if (manager) {
                char *value = symtable_get_var(manager, item->var_name);
                if (value) {
                    long result = atol(value);
                    free(value);
                    return result;
                }
            }
        }
        return 0;
    }
    default:
        return 0;
    }
}

/**
 * @brief Push an operator onto the operator stack
 * @param ctx Arithmetic evaluation context
 * @param op Operator to push
 */
static void push_opstack(arithm_context_t *ctx, op_t *op) {
    if (ctx->nopstack >= MAXOPSTACK) {
        ctx->errflag = true;
        arithm_set_error(
            SHELL_ERR_ARITH_STACK_OVERFLOW, "shunting-yard operator stack push",
            "expression too deeply nested", "operator stack overflow");
        return;
    }
    ctx->opstack[ctx->nopstack++] = op;
}

/**
 * @brief Pop an operator from the operator stack
 * @param ctx Arithmetic evaluation context
 * @return Popped operator, or NULL on underflow
 */
static op_t *pop_opstack(arithm_context_t *ctx) {
    if (ctx->nopstack <= 0) {
        ctx->errflag = true;
        arithm_set_error(SHELL_ERR_ARITH_STACK_UNDERFLOW,
                         "shunting-yard operator stack pop",
                         "missing operand or operator in expression",
                         "operator stack underflow");
        return NULL;
    }
    return ctx->opstack[--ctx->nopstack];
}

/**
 * @brief Push a literal integer value onto the number stack
 * @param ctx Arithmetic evaluation context
 * @param val Integer value to push
 */
static void push_numstackl(arithm_context_t *ctx, ssize_t val) {
    if (ctx->nnumstack >= MAXNUMSTACK) {
        ctx->errflag = true;
        arithm_set_error(
            SHELL_ERR_ARITH_STACK_OVERFLOW, "shunting-yard number stack push",
            "expression has too many operands", "number stack overflow");
        return;
    }
    ctx->numstack[ctx->nnumstack].type = ITEM_LONG_INT;
    ctx->numstack[ctx->nnumstack].val = val;
    ctx->nnumstack++;
}

/**
 * @brief Push a variable reference onto the number stack with executor context
 * @param ctx Arithmetic evaluation context
 * @param var_name Name of the variable to reference
 */
static void push_numstackv_with_context(arithm_context_t *ctx,
                                        const char *var_name) {
    if (ctx->nnumstack >= MAXNUMSTACK) {
        ctx->errflag = true;
        arithm_set_error(
            SHELL_ERR_ARITH_STACK_OVERFLOW, "shunting-yard number stack push",
            "expression has too many operands", "number stack overflow");
        return;
    }
    ctx->numstack[ctx->nnumstack].type = ITEM_VAR_PTR;
    ctx->numstack[ctx->nnumstack].var_name = strdup(var_name);
    ctx->numstack[ctx->nnumstack].executor_context = ctx->executor;
    ctx->nnumstack++;
}

MAYBE_UNUSED
static void push_numstackv(arithm_context_t *ctx, const char *var_name) {
    push_numstackv_with_context(ctx, var_name);
}

/**
 * @brief Pop a value from the number stack
 * @param ctx Arithmetic evaluation context
 * @return Popped stack item, or empty item on underflow
 */
static stack_item_t pop_numstack(arithm_context_t *ctx) {
    stack_item_t empty = {ITEM_LONG_INT, {0}, NULL};
    if (ctx->nnumstack <= 0) {
        ctx->errflag = true;
        arithm_set_error(
            SHELL_ERR_ARITH_STACK_UNDERFLOW, "shunting-yard number stack pop",
            "missing operand in expression", "number stack underflow");
        return empty;
    }
    return ctx->numstack[--ctx->nnumstack];
}

/// ============================================================================
/// EXPRESSION PARSING
/// ============================================================================

/**
 * @brief Look up an operator from expression text
 *
 * Identifies operators in the expression, handling both single-character
 * and two-character operators. Two-character operators are checked first
 * to avoid conflicts (e.g., == vs =).
 *
 * @param expr Pointer to current position in expression
 * @return Pointer to operator structure, or NULL if not an operator
 */
static op_t *get_op(const char *expr) {
    /// Check for increment/decrement operators first (they are 2-char)
    if (expr[0] == '+' && expr[1] == '+') {
        return OP_PREINC; /// Will be handled as pre/post in main parsing
    } else if (expr[0] == '-' && expr[1] == '-') {
        return OP_PREDEC; /// Will be handled as pre/post in main parsing
    }

    /// Check two-character operators first to avoid conflicts
    for (op_t *op = operators; op->op; op++) {
        if (op->chars == 2) {
            if (op->op == CH_EQ && expr[0] == '=' && expr[1] == '=') {
                return op;
            } else if (op->op == CH_NE && expr[0] == '!' && expr[1] == '=') {
                return op;
            } else if (op->op == CH_LE && expr[0] == '<' && expr[1] == '=') {
                return op;
            } else if (op->op == CH_GE && expr[0] == '>' && expr[1] == '=') {
                return op;
            } else if (op->op == CH_LSH && expr[0] == '<' && expr[1] == '<') {
                return op;
            } else if (op->op == CH_RSH && expr[0] == '>' && expr[1] == '>') {
                return op;
            } else if (op->op == CH_AND && expr[0] == '&' && expr[1] == '&') {
                return op;
            } else if (op->op == CH_OR && expr[0] == '|' && expr[1] == '|') {
                return op;
            } else if (op->op == CH_EXP && expr[0] == '*' && expr[1] == '*') {
                return op;
            } else if (op->op == CH_ADDEQ && expr[0] == '+' && expr[1] == '=') {
                return op;
            } else if (op->op == CH_SUBEQ && expr[0] == '-' && expr[1] == '=') {
                return op;
            } else if (op->op == CH_MULEQ && expr[0] == '*' && expr[1] == '=') {
                return op;
            } else if (op->op == CH_DIVEQ && expr[0] == '/' && expr[1] == '=') {
                return op;
            } else if (op->op == CH_MODEQ && expr[0] == '%' && expr[1] == '=') {
                return op;
            }
        }
    }

    /// Then check single-character operators
    for (op_t *op = operators; op->op; op++) {
        if (op->chars == 1 && *expr == op->op) {
            return op;
        }
        /// Special handling for operators with character codes
        if (op->op == CH_TERNARY_Q && *expr == '?') {
            return op;
        }
        if (op->op == CH_TERNARY_C && *expr == ':') {
            return op;
        }
        if (op->op == CH_COMMA && *expr == ',') {
            return op;
        }
    }

    return NULL;
}

/**
 * @brief Parse a numeric literal from expression text
 *
 * Parses decimal, hexadecimal (0x prefix), or octal (0 prefix) numbers.
 *
 * @param expr Pointer to current position in expression
 * @param nchars Output: number of characters consumed
 * @return Parsed numeric value
 */
static ssize_t get_num(const char *expr, int *nchars) {
    char *endptr;
    ssize_t result;

    if (expr[0] == '0' && (expr[1] == 'x' || expr[1] == 'X')) {
        /// Hexadecimal
        result = strtol(expr, &endptr, 16);
    } else if (expr[0] == '0' && isdigit(expr[1])) {
        /// Octal
        result = strtol(expr, &endptr, 8);
    } else {
        /// Decimal
        result = strtol(expr, &endptr, 10);
    }

    *nchars = endptr - expr;
    return result;
}

/**
 * @brief Extract a variable name from expression text
 *
 * Parses a variable name starting at the current position. Variable names
 * can start with a letter, underscore, or digit (for positional parameters).
 * Ensures the variable exists in the symbol table with a default value of "0".
 *
 * @param ctx Arithmetic evaluation context (for executor access)
 * @param expr Pointer to current position in expression
 * @param nchars Output: number of characters consumed
 * @return Allocated variable name string, or NULL on error
 */
static char *get_var_name_with_context(arithm_context_t *ctx
                                       __attribute__((unused)),
                                       const char *expr, int *nchars) {
    const char *start = expr;
    *nchars = 0;

    /// Variable names start with letter, underscore, or digit (for positional
    /// parameters). Non-ASCII identifier-start codepoints are accepted under
    /// FEATURE_UNICODE_IDENTIFIERS via lush_ident_match_start.
    size_t rem = strlen(expr);
    size_t start_n = lush_ident_match_start(expr, rem);
    if (start_n == 0 && !isdigit((unsigned char)*expr)) {
        return NULL;
    }

    /// For numeric positional parameters like $1, $2, only take the single
    /// digit. Digits never satisfy ident_match_start, so start_n is 0 here.
    if (start_n == 0) {
        *nchars = 1;
        expr++;
    } else {
        expr += start_n;
        *nchars = (int)start_n;
        rem -= start_n;
        while (rem > 0) {
            size_t n = lush_ident_match_continue(expr, rem);
            if (n == 0) {
                break;
            }
            expr += n;
            rem -= n;
            *nchars += (int)n;
        }
    }

    /// Extract variable name
    char *name = malloc(*nchars + 1);
    if (!name) {
        arithm_set_error(SHELL_ERR_OUT_OF_MEMORY,
                         "extracting variable name in arithmetic",
                         "out of memory", "memory allocation failed");
        return NULL;
    }

    strncpy(name, start, *nchars);
    name[*nchars] = '\0';

    /// Always ensure variable exists in global symbol table with default value
    /// "0" The actual value resolution will happen during evaluation using
    /// executor context
    symtable_manager_t *manager = symtable_get_global_manager();
    if (manager && !symtable_var_exists(manager, name)) {
        symtable_set_var(manager, name, "0", SYMVAR_NONE);
    }

    return name;
}

MAYBE_UNUSED
static char *get_var_name(const char *expr, int *nchars) {
    return get_var_name_with_context(NULL, expr, nchars);
}

/**
 * @brief Process an operator using the shunting yard algorithm
 *
 * Implements the core shunting yard logic for operator precedence parsing.
 * Handles parentheses matching, operator precedence, and associativity
 * to convert infix notation to evaluation order.
 *
 * @param ctx Arithmetic evaluation context with operator and number stacks
 * @param op Operator to process
 */
static void shunt_op(arithm_context_t *ctx, op_t *op) {
    if (op->op == '(') {
        push_opstack(ctx, op);
    } else if (op->op == CH_TERNARY_C) {
        /// Colon in ternary - process operators until we find the matching '?'
        while (ctx->nopstack > 0 &&
               ctx->opstack[ctx->nopstack - 1]->op != '(' &&
               ctx->opstack[ctx->nopstack - 1]->op != CH_TERNARY_Q) {
            op_t *pop_op = pop_opstack(ctx);
            if (ctx->errflag) {
                return;
            }

            stack_item_t a1 = pop_numstack(ctx);
            if (ctx->errflag) {
                return;
            }

            if (pop_op->unary) {
                push_numstackl(ctx, pop_op->eval(&a1, NULL));
                stack_item_cleanup(&a1);
            } else {
                stack_item_t a2 = pop_numstack(ctx);
                if (ctx->errflag) {
                    stack_item_cleanup(&a1);
                    return;
                }
                push_numstackl(ctx, pop_op->eval(&a2, &a1));
                stack_item_cleanup(&a1);
                stack_item_cleanup(&a2);
            }
            if (s_arithm_error.flagged) {
                ctx->errflag = true;
                return;
            }
        }
        /// Push the colon operator to mark the separation
        push_opstack(ctx, op);
    } else if (op->op == ')') {
        while (ctx->nopstack > 0 &&
               ctx->opstack[ctx->nopstack - 1]->op != '(') {
            op_t *pop_op = pop_opstack(ctx);
            if (ctx->errflag) {
                return;
            }

            /// Handle ternary operator specially
            if (pop_op->op == CH_TERNARY_C) {
                /// We have the false value on the stack
                stack_item_t false_val = pop_numstack(ctx);
                if (ctx->errflag) {
                    return;
                }

                /// Next operator should be '?'
                if (ctx->nopstack > 0 &&
                    ctx->opstack[ctx->nopstack - 1]->op == CH_TERNARY_Q) {
                    pop_opstack(ctx); /// Remove the '?'

                    /// Get true value and condition
                    stack_item_t true_val = pop_numstack(ctx);
                    if (ctx->errflag) {
                        stack_item_cleanup(&false_val);
                        return;
                    }
                    stack_item_t condition = pop_numstack(ctx);
                    if (ctx->errflag) {
                        stack_item_cleanup(&false_val);
                        stack_item_cleanup(&true_val);
                        return;
                    }

                    /// Evaluate: condition ? true_val : false_val
                    ssize_t result = long_value(&condition)
                                         ? long_value(&true_val)
                                         : long_value(&false_val);
                    push_numstackl(ctx, result);

                    stack_item_cleanup(&condition);
                    stack_item_cleanup(&true_val);
                    stack_item_cleanup(&false_val);
                } else {
                    arithm_set_error(SHELL_ERR_ARITH_MISMATCHED_TERNARY,
                                     "parsing ternary expression",
                                     "every '?' needs a matching ':'",
                                     "mismatched ternary operator");
                    stack_item_cleanup(&false_val);
                    ctx->errflag = true;
                    return;
                }
                continue;
            }

            stack_item_t a1 = pop_numstack(ctx);
            if (ctx->errflag) {
                return;
            }

            if (pop_op->unary) {
                push_numstackl(ctx, pop_op->eval(&a1, NULL));
                stack_item_cleanup(&a1);
                if (s_arithm_error.flagged) {
                    ctx->errflag = true;
                    return;
                }
            } else {
                stack_item_t a2 = pop_numstack(ctx);
                if (ctx->errflag) {
                    stack_item_cleanup(&a1);
                    return;
                }
                push_numstackl(ctx, pop_op->eval(&a2, &a1));
                stack_item_cleanup(&a1);
                stack_item_cleanup(&a2);
                if (s_arithm_error.flagged) {
                    ctx->errflag = true;
                    return;
                }
            }
            if (ctx->errflag) {
                return;
            }
        }

        if (ctx->nopstack > 0 && ctx->opstack[ctx->nopstack - 1]->op == '(') {
            pop_opstack(ctx); /// Remove the '('
        } else {
            ctx->errflag = true;
            arithm_set_error(SHELL_ERR_ARITH_MISMATCHED_PARENS,
                             "parsing parenthesized expression",
                             "every '(' needs a matching ')'",
                             "mismatched parentheses");
        }
    } else {
        while (ctx->nopstack > 0 &&
               ctx->opstack[ctx->nopstack - 1]->op != '(' &&
               ctx->opstack[ctx->nopstack - 1]->op != CH_TERNARY_Q &&
               ((op->assoc == ASSOC_LEFT &&
                 op->prec >= ctx->opstack[ctx->nopstack - 1]->prec) ||
                (op->assoc == ASSOC_RIGHT &&
                 op->prec > ctx->opstack[ctx->nopstack - 1]->prec))) {

            op_t *pop_op = pop_opstack(ctx);
            if (ctx->errflag) {
                return;
            }

            /// Handle ternary operator specially
            if (pop_op->op == CH_TERNARY_C) {
                /// We have the false value on the stack
                stack_item_t false_val = pop_numstack(ctx);
                if (ctx->errflag) {
                    return;
                }

                /// Next operator should be '?'
                if (ctx->nopstack > 0 &&
                    ctx->opstack[ctx->nopstack - 1]->op == CH_TERNARY_Q) {
                    pop_opstack(ctx); /// Remove the '?'

                    /// Get true value and condition
                    stack_item_t true_val = pop_numstack(ctx);
                    if (ctx->errflag) {
                        stack_item_cleanup(&false_val);
                        return;
                    }
                    stack_item_t condition = pop_numstack(ctx);
                    if (ctx->errflag) {
                        stack_item_cleanup(&false_val);
                        stack_item_cleanup(&true_val);
                        return;
                    }

                    /// Evaluate: condition ? true_val : false_val
                    ssize_t result = long_value(&condition)
                                         ? long_value(&true_val)
                                         : long_value(&false_val);
                    push_numstackl(ctx, result);

                    stack_item_cleanup(&condition);
                    stack_item_cleanup(&true_val);
                    stack_item_cleanup(&false_val);
                } else {
                    arithm_set_error(SHELL_ERR_ARITH_MISMATCHED_TERNARY,
                                     "parsing ternary expression",
                                     "every '?' needs a matching ':'",
                                     "mismatched ternary operator");
                    stack_item_cleanup(&false_val);
                    ctx->errflag = true;
                    return;
                }
                continue;
            }

            stack_item_t a1 = pop_numstack(ctx);
            if (ctx->errflag) {
                return;
            }

            if (pop_op->unary) {
                push_numstackl(ctx, pop_op->eval(&a1, NULL));
                stack_item_cleanup(&a1);
                if (s_arithm_error.flagged) {
                    ctx->errflag = true;
                    return;
                }
            } else {
                stack_item_t a2 = pop_numstack(ctx);
                if (ctx->errflag) {
                    stack_item_cleanup(&a1);
                    return;
                }
                push_numstackl(ctx, pop_op->eval(&a2, &a1));
                stack_item_cleanup(&a1);
                stack_item_cleanup(&a2);
                if (s_arithm_error.flagged) {
                    ctx->errflag = true;
                    return;
                }
            }
            if (ctx->errflag) {
                return;
            }
        }

        push_opstack(ctx, op);
    }
}

/// Error handling functions
void arithm_init(void) { arithm_clear_error(); }

void arithm_cleanup(void) { arithm_clear_error(); }

void arithm_set_error(shell_error_code_t code, const char *while_context,
                      const char *help, const char *fmt, ...) {
    /// Preserve the first reported error of an evaluation: shunting-yard
    /// cleanup paths often try to set further errors after the original
    /// trip; the original is the actionable one.
    if (s_arithm_error.flagged) {
        return;
    }
    s_arithm_error.flagged = true;
    s_arithm_error.code = code;

    va_list args;
    va_start(args, fmt);
    vsnprintf(s_arithm_error.message, sizeof(s_arithm_error.message), fmt,
              args);
    va_end(args);

    if (while_context) {
        snprintf(s_arithm_error.while_context,
                 sizeof(s_arithm_error.while_context), "%s", while_context);
    } else {
        s_arithm_error.while_context[0] = '\0';
    }
    if (help) {
        snprintf(s_arithm_error.help, sizeof(s_arithm_error.help), "%s", help);
    } else {
        s_arithm_error.help[0] = '\0';
    }
}

void arithm_clear_error(void) {
    s_arithm_error.flagged = false;
    s_arithm_error.code = SHELL_ERR_ARITHMETIC_SYNTAX;
    s_arithm_error.message[0] = '\0';
    s_arithm_error.while_context[0] = '\0';
    s_arithm_error.help[0] = '\0';
}

bool arithm_error_is_flagged(void) { return s_arithm_error.flagged; }

shell_error_code_t arithm_error_code(void) { return s_arithm_error.code; }

const char *arithm_error_message(void) {
    return s_arithm_error.message[0] ? s_arithm_error.message : NULL;
}

const char *arithm_error_while(void) {
    return s_arithm_error.while_context[0] ? s_arithm_error.while_context
                                           : NULL;
}

const char *arithm_error_help(void) {
    return s_arithm_error.help[0] ? s_arithm_error.help : NULL;
}

/// Main arithmetic expansion function
/// Forward declaration for the executor-aware version
static char *arithm_expand_internal(void *executor, const char *orig_expr);

char *arithm_expand(const char *orig_expr) {
    return arithm_expand_internal(NULL, orig_expr);
}

char *arithm_expand_with_executor(executor_t *executor, const char *orig_expr) {
    return arithm_expand_internal(executor, orig_expr);
}

static char *arithm_expand_internal(void *executor, const char *orig_expr) {
    if (!orig_expr) {
        return strdup("0");
    }

    /// Initialize context
    arithm_context_t ctx = {0};
    ctx.executor = executor; /// Store executor context in arithmetic context
    arithm_clear_error();

    /// Parse expression and remove $(( )) wrapper if present
    const char *expr;
    char *cleaned_expr = NULL;

    if (strncmp(orig_expr, "$((", 3) == 0) {
        size_t len = strlen(orig_expr);
        if (len >= 5 && orig_expr[len - 2] == ')' &&
            orig_expr[len - 1] == ')') {
            /// Extract expression between $(( and ))
            size_t expr_len = len - 5;
            cleaned_expr = malloc(expr_len + 1);
            if (!cleaned_expr) {
                arithm_set_error(SHELL_ERR_OUT_OF_MEMORY,
                                 "preparing arithmetic expression",
                                 "out of memory", "memory allocation failed");
                return NULL;
            }
            strncpy(cleaned_expr, orig_expr + 3, expr_len);
            cleaned_expr[expr_len] = '\0';
            expr = cleaned_expr;
        } else {
            arithm_set_error(SHELL_ERR_ARITHMETIC_SYNTAX,
                             "scanning $((...)) wrapper",
                             "every '$((' needs a matching '))'",
                             "malformed arithmetic expression: "
                             "missing closing ))");
            return NULL;
        }
    } else {
        expr = orig_expr;
    }

    /// Tokenize and evaluate expression using clearer state management
    const char *current = expr;
    op_t start_op = {'X', 0, ASSOC_NONE, 0, 0, NULL};
    op_t *last_op = &start_op;

    while (*current && !ctx.errflag) {
        /// Skip whitespace
        while (*current && isspace(*current)) {
            current++;
        }

        if (!*current) {
            break;
        }

        /// Try to parse an operator first
        op_t *op = get_op(current);
        if (op) {
            /// Handle increment/decrement operators
            if (op == OP_PREINC || op == OP_PREDEC) {
                /// Check if this should be post-increment/decrement
                /// If the last token was a variable or closing paren, it's
                /// post-increment
                if (ctx.nnumstack > 0 &&
                    (ctx.numstack[ctx.nnumstack - 1].type == ITEM_VAR_PTR ||
                     (last_op && last_op->op == ')'))) {
                    /// Convert to post-increment/decrement
                    if (op == OP_PREINC) {
                        op = OP_POSTINC;
                    } else {
                        op = OP_POSTDEC;
                    }
                }
            }

            /// Decide whether the current operator is at a position
            /// where it must be unary (no left operand on the stack)
            /// versus a position where the previous token left an
            /// operand. last_op tracks the previous OPERATOR; it is
            /// NULL after an operand (number/variable) and non-NULL
            /// after an operator. The "previous token left an operand"
            /// cases:
            ///   - last_op == NULL                  (number/var)
            ///   - last_op->op == ')'               (closing paren)
            ///   - last_op == OP_POSTINC/OP_POSTDEC (postfix unary,
            ///     result stays on the stack)
            /// In any of those, a binary operator follows naturally
            /// and no unary conversion / error is required. Otherwise
            /// apply the existing unary conversion (-/+ -> umin/uplus)
            /// or report a binary-without-operand error. Issue #100.
            bool left_operand_present =
                (!last_op || last_op->op == ')' || last_op == OP_POSTINC ||
                 last_op == OP_POSTDEC);
            if (last_op && !left_operand_present) {
                if (op->op == '-') {
                    op = OP_UMINUS;
                } else if (op->op == '+') {
                    op = OP_UPLUS;
                } else if (op->op != '(' && !op->unary) {
                    arithm_set_error(SHELL_ERR_ARITHMETIC_SYNTAX,
                                     "applying binary operator",
                                     "binary operators require operands "
                                     "on both sides",
                                     "illegal use of binary operator");
                    break;
                }
            }

            shunt_op(&ctx, op);
            if (ctx.errflag) {
                break;
            }
            last_op = op;
            current += op->chars;
        } else if (isdigit(*current)) {
            /// Parse number
            int nchars;
            ssize_t num = get_num(current, &nchars);
            push_numstackl(&ctx, num);
            if (ctx.errflag) {
                break;
            }
            last_op = NULL;
            current += nchars;
        } else if (*current == '$' && *(current + 1) == '(') {
            /// Handle command substitution $(command) in arithmetic expressions
            /// Find the matching closing parenthesis
            int paren_count = 1;
            const char *start = current + 2; /// Skip $(
            const char *end = start;

            while (*end && paren_count > 0) {
                if (*end == '(') {
                    paren_count++;
                } else if (*end == ')') {
                    paren_count--;
                }
                if (paren_count > 0) {
                    end++;
                }
            }

            if (paren_count == 0 && *end == ')') {
                /// Extract command and execute it
                size_t cmd_len = end - start;
                char *command = malloc(cmd_len + 1);
                if (command) {
                    strncpy(command, start, cmd_len);
                    command[cmd_len] = '\0';

                    /// Simple implementation: handle basic echo commands
                    char *trimmed = command;
                    while (*trimmed && isspace(*trimmed)) {
                        trimmed++;
                    }

                    /// Check if it's a simple echo number command
                    if (strncmp(trimmed, "echo ", 5) == 0) {
                        char *num_str = trimmed + 5;
                        while (*num_str && isspace(*num_str)) {
                            num_str++;
                        }
                        if (isdigit(*num_str) ||
                            (*num_str == '-' && isdigit(*(num_str + 1)))) {
                            long val = strtol(num_str, NULL, 10);
                            push_numstackl(&ctx, val);
                        } else {
                            push_numstackl(&ctx, 0);
                        }
                    } else {
                        /// For other commands, default to 0 for now
                        push_numstackl(&ctx, 0);
                    }

                    free(command);
                } else {
                    push_numstackl(&ctx, 0);
                }

                current = end + 1; /// Skip past the closing )
            } else {
                arithm_set_error(SHELL_ERR_ARITHMETIC_SYNTAX,
                                 "scanning $(...) in arithmetic expression",
                                 "every '$(' needs a matching ')'",
                                 "malformed command substitution in "
                                 "arithmetic");
                break;
            }

            if (ctx.errflag) {
                break;
            }
            last_op = NULL;
        } else if (*current == '$' && *(current + 1) == '{') {
            /// Handle ${variable} syntax in arithmetic expressions
            const char *start = current + 2; /// Skip ${
            const char *end = strchr(start, '}');

            if (end) {
                /// Extract variable name (handle simple ${var} for now)
                /// More complex forms like ${var:-default} would need executor

                /// Find the end of the variable name (before any operator).
                /// lush_ident_match_continue honours
                /// FEATURE_UNICODE_IDENTIFIERS so multibyte codepoints inside
                /// ${...} extend the name.
                const char *name_end = start;
                while (name_end < end) {
                    size_t n = lush_ident_match_continue(
                        name_end, (size_t)(end - name_end));
                    if (n == 0) {
                        break;
                    }
                    name_end += n;
                }

                if (name_end > start) {
                    size_t var_len = name_end - start;
                    char *var_name = malloc(var_len + 1);
                    if (var_name) {
                        strncpy(var_name, start, var_len);
                        var_name[var_len] = '\0';
                        push_numstackv_with_context(&ctx, var_name);
                        free(var_name);
                    } else {
                        push_numstackl(&ctx, 0);
                    }
                } else {
                    push_numstackl(&ctx, 0);
                }

                current = end + 1; /// Skip past }
            } else {
                arithm_set_error(SHELL_ERR_ARITHMETIC_SYNTAX,
                                 "scanning ${...} in arithmetic expression",
                                 "every '${' needs a matching '}'",
                                 "unmatched ${ in arithmetic expression");
                break;
            }

            if (ctx.errflag) {
                break;
            }
            last_op = NULL;
        } else if (*current == '$' &&
                   (isdigit((unsigned char)current[1]) ||
                    lush_ident_match_start(current + 1, strlen(current + 1)) >
                        0)) {
            /// Handle $variable syntax in arithmetic expressions
            current++; /// Skip the '$'
            int nchars;
            char *var_name = get_var_name_with_context(&ctx, current, &nchars);
            if (var_name) {
                push_numstackv_with_context(&ctx, var_name);
                free(var_name);
            } else {
                push_numstackl(&ctx, 0); /// Undefined variable = 0
            }
            if (ctx.errflag) {
                break;
            }
            last_op = NULL;
            current += nchars;
        } else if (lush_ident_match_start(current, strlen(current)) > 0) {
            /// Parse variable name
            int nchars;
            char *var_name = get_var_name_with_context(&ctx, current, &nchars);
            if (var_name) {
                push_numstackv_with_context(&ctx, var_name);
                free(var_name);
            } else {
                push_numstackl(&ctx, 0); /// Undefined variable = 0
            }
            if (ctx.errflag) {
                break;
            }
            last_op = NULL;
            current += nchars;
        } else {
            arithm_set_error(SHELL_ERR_ARITHMETIC_SYNTAX,
                             "tokenizing arithmetic expression",
                             "check operator and operand placement",
                             "syntax error in arithmetic expression");
            break;
        }
    }

    /// Process remaining operators
    while (ctx.nopstack > 0 && !ctx.errflag) {
        op_t *op = pop_opstack(&ctx);
        if (ctx.errflag) {
            break;
        }

        /// '(' and ')' are paren-balance markers with eval==NULL, normally
        /// removed by shunt_op when the matching ')' is processed. If one
        /// survives to end-of-expression, the input was unbalanced; falling
        /// through to the binary-apply path below would call op->eval which
        /// is NULL. Treat it the same as shunt_op's ')' branch does.
        if (op->op == '(' || op->op == ')') {
            arithm_set_error(SHELL_ERR_ARITH_MISMATCHED_PARENS,
                             "parsing parenthesized expression",
                             "every '(' needs a matching ')'",
                             "mismatched parentheses");
            ctx.errflag = true;
            break;
        }

        /// Handle ternary operator specially
        if (op->op == CH_TERNARY_C) {
            /// We have the false value on the stack
            stack_item_t false_val = pop_numstack(&ctx);
            if (ctx.errflag) {
                break;
            }

            /// Next operator should be '?'
            if (ctx.nopstack > 0 &&
                ctx.opstack[ctx.nopstack - 1]->op == CH_TERNARY_Q) {
                pop_opstack(&ctx); /// Remove the '?'

                /// Get true value and condition
                stack_item_t true_val = pop_numstack(&ctx);
                if (ctx.errflag) {
                    stack_item_cleanup(&false_val);
                    break;
                }
                stack_item_t condition = pop_numstack(&ctx);
                if (ctx.errflag) {
                    stack_item_cleanup(&false_val);
                    stack_item_cleanup(&true_val);
                    break;
                }

                /// Evaluate: condition ? true_val : false_val
                ssize_t result = long_value(&condition)
                                     ? long_value(&true_val)
                                     : long_value(&false_val);
                push_numstackl(&ctx, result);

                stack_item_cleanup(&condition);
                stack_item_cleanup(&true_val);
                stack_item_cleanup(&false_val);
            } else {
                arithm_set_error(SHELL_ERR_ARITH_MISMATCHED_TERNARY,
                                 "evaluating ternary expression",
                                 "every '?' needs a matching ':'",
                                 "mismatched ternary operator");
                stack_item_cleanup(&false_val);
                ctx.errflag = true;
                break;
            }
            continue;
        }

        /// Skip '?' - it's handled with ':'
        if (op->op == CH_TERNARY_Q) {
            arithm_set_error(SHELL_ERR_ARITH_MISMATCHED_TERNARY,
                             "evaluating ternary expression",
                             "every '?' needs a matching ':'",
                             "mismatched ternary operator");
            ctx.errflag = true;
            break;
        }

        stack_item_t a1 = pop_numstack(&ctx);
        if (ctx.errflag) {
            break;
        }

        if (op->unary) {
            push_numstackl(&ctx, op->eval(&a1, NULL));
            stack_item_cleanup(&a1);
            if (s_arithm_error.flagged) {
                ctx.errflag = true;
                break;
            }
        } else {
            stack_item_t a2 = pop_numstack(&ctx);
            if (ctx.errflag) {
                stack_item_cleanup(&a1);
                break;
            }
            push_numstackl(&ctx, op->eval(&a2, &a1));
            stack_item_cleanup(&a1);
            stack_item_cleanup(&a2);
            if (s_arithm_error.flagged) {
                ctx.errflag = true;
                break;
            }
        }
        if (ctx.errflag) {
            break;
        }
    }

    /// Clean up
    if (cleaned_expr) {
        free(cleaned_expr);
    }

    /// Check for errors
    if (ctx.errflag || s_arithm_error.flagged) {
        arithm_context_cleanup(&ctx);
        return NULL;
    }

    /// Should have exactly one result
    if (ctx.nnumstack != 1) {
        arithm_set_error(
            SHELL_ERR_ARITHMETIC_SYNTAX, "evaluating arithmetic expression",
            "expression failed to evaluate", "invalid arithmetic expression");
        arithm_context_cleanup(&ctx);
        return NULL;
    }

    /// Format result
    ssize_t result = long_value(&ctx.numstack[0]);
    char *result_str = malloc(32);
    if (!result_str) {
        arithm_set_error(SHELL_ERR_OUT_OF_MEMORY,
                         "formatting arithmetic result", "out of memory",
                         "memory allocation failed");
        arithm_context_cleanup(&ctx);
        return NULL;
    }

    snprintf(result_str, 32, "%ld", result);
    arithm_context_cleanup(&ctx);
    return result_str;
}
