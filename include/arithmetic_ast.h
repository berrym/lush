/**
 * @file arithmetic_ast.h
 * @brief AST-based arithmetic engine: token, AST, and diagnostic types plus
 *        the lexer and parser front-end.
 *
 * This is the front half of lush's arithmetic expression engine (the
 * replacement for the legacy shunting-yard evaluator). The pipeline is:
 *
 *     expansion pre-pass  ->  arith_lex  ->  arith_parse  ->  evaluator
 *     (executor layer)        (this file)    (this file)      (arithmetic_eval)
 *
 * The lexer and parser are intentionally PURE: they touch no symbol table,
 * no executor context, and no global error state. A failure is reported by
 * filling a caller-provided arith_diag_t (which carries a shell_error_code_t
 * so the integration layer can raise the exact same structured error the
 * legacy engine did). Variable resolution and mutation -- and therefore the
 * only coupling to the symbol table -- live entirely in the evaluator.
 */

#ifndef ARITHMETIC_AST_H
#define ARITHMETIC_AST_H

#include "shell_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/**
 * @brief Token kinds produced by the arithmetic lexer.
 *
 * The lexer emits pure arithmetic tokens; `$`-forms are expanded away before
 * lexing, so no shell-expansion token kinds appear here.
 */
typedef enum {
    ATK_EOF = 0, ///< End of the token stream (always the final token)
    ATK_NUM,     ///< Integer literal; value in arith_token_t.num
    ATK_IDENT,   ///< Bare identifier; name in arith_token_t.text

    ATK_LPAREN,    ///< (
    ATK_RPAREN,    ///< )
    ATK_LBRACKET,  ///< [ (retained for a stray-']' error; a real subscript is
                   ///< captured whole as ATK_SUBSCRIPT)
    ATK_RBRACKET,  ///< ]
    ATK_SUBSCRIPT, ///< A whole `[...]` array subscript; the raw interior text
                   ///< (never lexed as arithmetic) is in arith_token_t.text

    ATK_PLUS,    ///< +
    ATK_MINUS,   ///< -
    ATK_STAR,    ///< *
    ATK_SLASH,   ///< /
    ATK_PERCENT, ///< %
    ATK_POW,     ///< **

    ATK_SHL, ///< <<
    ATK_SHR, ///< >>
    ATK_LT,  ///< <
    ATK_LE,  ///< <=
    ATK_GT,  ///< >
    ATK_GE,  ///< >=
    ATK_EQ,  ///< ==
    ATK_NE,  ///< !=

    ATK_AMP,   ///< &
    ATK_CARET, ///< ^
    ATK_PIPE,  ///< |
    ATK_AND,   ///< &&
    ATK_OR,    ///< ||
    ATK_NOT,   ///< !
    ATK_TILDE, ///< ~

    ATK_QUESTION, ///< ?
    ATK_COLON,    ///< :
    ATK_COMMA,    ///< ,

    ATK_ASSIGN,    ///< =
    ATK_PLUSEQ,    ///< +=
    ATK_MINUSEQ,   ///< -=
    ATK_STAREQ,    ///< *=
    ATK_SLASHEQ,   ///< /=
    ATK_PERCENTEQ, ///< %=
    ATK_SHLEQ,     ///< <<=
    ATK_SHREQ,     ///< >>=
    ATK_AMPEQ,     ///< &=
    ATK_PIPEEQ,    ///< |=
    ATK_CARETEQ,   ///< ^=

    ATK_INC, ///< ++
    ATK_DEC, ///< --
} arith_token_kind_t;

/**
 * @brief A single lexed token.
 */
typedef struct {
    arith_token_kind_t kind; ///< Token kind
    long num;                ///< Integer value (ATK_NUM only)
    char *text; ///< Owned identifier text (ATK_IDENT only, else NULL)
    size_t pos; ///< Byte offset of the token in the source
    size_t len; ///< Byte length of the token (for error carets)
} arith_token_t;

/**
 * @brief Structured diagnostic filled on a lex or parse failure.
 *
 * Pure: carries the code/context/help/message so the integration layer can
 * forward them verbatim to arithm_set_error, keeping error-code parity with
 * the legacy engine without the front-end depending on the global error state.
 */
typedef struct {
    bool flagged;            ///< True once a failure has been recorded
    shell_error_code_t code; ///< Specific structured error code
    const char *while_ctx;   ///< Static "while:" context phrase (may be NULL)
    const char *help;        ///< Static "help:" suggestion (may be NULL)
    char message[160];       ///< Formatted primary message
    size_t pos; ///< Byte offset the failure refers to (caret start)
    size_t len; ///< Byte span the failure covers (caret length, >=1)
} arith_diag_t;

/**
 * @brief AST node kinds.
 *
 * Assignment, increment, and decrement carry their lvalue as child `a`, which
 * the parser guarantees is an AST_VAR or AST_INDEX (else a parse-time
 * invalid-assignment-target error). The evaluator resolves that lvalue
 * exactly once via eval_lvalue.
 */
typedef enum {
    AST_NUM,   ///< Integer literal (num)
    AST_VAR,   ///< Scalar variable reference (name)
    AST_INDEX, ///< Array element: name + index subtree (a)

    AST_UNOP,   ///< Prefix unary operator (op) on child a
    AST_BINOP,  ///< Binary operator (op) on children a, b
    AST_ASSIGN, ///< Assignment (op is =, +=, ...) : lvalue a, rhs b

    AST_PREINC,  ///< ++x : lvalue a
    AST_POSTINC, ///< x++ : lvalue a
    AST_PREDEC,  ///< --x : lvalue a
    AST_POSTDEC, ///< x-- : lvalue a

    AST_TERNARY, ///< a ? b : c
    AST_COMMA,   ///< a , b
} arith_ast_kind_t;

/**
 * @brief Kind of an lvalue the evaluator resolves before a mutation.
 *
 * The evaluator materializes one of these (resolving an array subscript to a
 * concrete integer index exactly once) so assignment / increment / decrement
 * operate on an already-resolved target.
 */
typedef enum {
    LVAL_SCALAR,      ///< A scalar variable
    LVAL_ARRAY_INDEX, ///< An indexed array element (name + resolved index)
    LVAL_ASSOC_KEY,   ///< An associative array element (name + literal key)
} lvalue_kind_t;

/**
 * @brief An AST node. Children not used by a given kind are NULL.
 */
typedef struct arith_ast {
    arith_ast_kind_t kind; ///< Node kind
    arith_token_kind_t op; ///< Operator token (AST_UNOP/AST_BINOP/AST_ASSIGN)
    long num;              ///< Integer value (AST_NUM)
    char *name;            ///< Owned variable name (AST_VAR/AST_INDEX)
    char *index_raw;       ///< Owned raw subscript text (AST_INDEX only): the
                           ///< literal `[...]` interior, arithmetic-evaluated
                           ///< for an indexed array and used as a literal key
                           ///< for an associative array (issue #615)
    struct arith_ast *a;   ///< First child / lvalue / condition
    struct arith_ast *b;   ///< Second child / rhs / then-branch
    struct arith_ast *c;   ///< Third child / else-branch (AST_TERNARY)
    size_t pos;            ///< Byte offset for diagnostics
} arith_ast_t;

/**
 * @brief Read an integer literal under the active base rule (see
 *        arithmetic_lex.c). Shared by the lexer and the evaluator's
 *        scalar fast path so both agree on what a leading zero means.
 */
long arith_strtol_based(const char *s, bool octal_zeroes, char **end);

/**
 * @brief Tokenize an arithmetic expression.
 *
 * On success, *out_tokens points to a freshly allocated array of *out_count
 * tokens whose final element is ATK_EOF; the caller frees it with
 * arith_tokens_free. On failure, fills @p diag, leaves outputs untouched,
 * and returns false.
 *
 * @param expr       NUL-terminated pure-arithmetic source (post expansion)
 * @param out_tokens Receives the token array (owned by caller)
 * @param out_count  Receives the token count (including the ATK_EOF terminator)
 * @param diag       Diagnostic filled on failure
 * @return true on success, false on a lexical error
 */
bool arith_lex(const char *expr, arith_token_t **out_tokens, size_t *out_count,
               arith_diag_t *diag, bool octal_zeroes);

/**
 * @brief Free a token array returned by arith_lex.
 */
void arith_tokens_free(arith_token_t *tokens, size_t count);

/**
 * @brief Parse a token stream into an AST.
 *
 * On success returns the owned root node (free with arith_ast_free). On
 * failure fills @p diag and returns NULL. An empty expression (only ATK_EOF)
 * returns NULL with diag unflagged -- the caller distinguishes empty from
 * error by checking diag->flagged.
 *
 * @param tokens Token array (ATK_EOF terminated) from arith_lex
 * @param count  Token count including the terminator
 * @param diag   Diagnostic filled on failure
 * @return Owned AST root, or NULL (see diag->flagged to distinguish empty)
 */
arith_ast_t *arith_parse(const arith_token_t *tokens, size_t count,
                         arith_diag_t *diag);

/**
 * @brief Recursively free an AST.
 */
void arith_ast_free(arith_ast_t *node);

/**
 * @brief Evaluate a parsed AST to an integer value.
 *
 * The evaluator is the only layer coupled to the symbol table: it resolves
 * variable reads (recursively, so `x="y+1"; y=5` yields 6, depth-capped) and
 * performs assignments (honoring the readonly attribute). It short-circuits
 * `&&`/`||` and evaluates only the taken ternary branch, so side effects in an
 * unreached subexpression do not occur.
 *
 * @param ast      Root AST from arith_parse; a NULL ast evaluates to 0.
 * @param executor Opaque executor context (executor_t *) for variable
 *                 resolution and mutation; NULL falls back to the global scope.
 * @param out      Receives the result on success (untouched on failure).
 * @param diag     Diagnostic filled on an evaluation error (division by zero,
 *                 readonly assignment, variable-resolution depth exceeded,
 * ...).
 * @return true on success, false on an evaluation error.
 */
bool arith_ast_eval(const arith_ast_t *ast, void *executor, ssize_t *out,
                    arith_diag_t *diag);

/**
 * @brief Serialize an AST to a canonical parenthesized S-expression.
 *
 * For unit testing the parser's structure (precedence/associativity/lvalue
 * shape) without an evaluator. Examples:
 *   1 + 2 * 3      -> (+ 1 (* 2 3))
 *   a[i+1] = 9     -> (= (index a (+ i 1)) 9)
 *   a ? b : c      -> (?: a b c)
 * The output is truncated safely if it would exceed @p bufsize.
 */
void arith_ast_to_sexpr(const arith_ast_t *node, char *buf, size_t bufsize);

#endif /// ARITHMETIC_AST_H
