/**
 * @file test_arith_ast.c
 * @brief Unit tests for the AST arithmetic engine front-end (lexer + parser).
 *
 * Stage 2 of the shunting-yard replacement (#607). There is no evaluator yet,
 * so structure is verified through the parser's S-expression serialization
 * (precedence, associativity, lvalue shape, subscripts) and errors through the
 * pure arith_diag_t (specific shell_error_code_t, span).
 */

#include "arithmetic_ast.h"
#include "test_framework.h"

#include <string.h>

/**
 * @brief Lex + parse @p expr and serialize the AST to @p out.
 * @return true on success; false if lex or parse failed.
 */
static bool parse_sexpr(const char *expr, char *out, size_t outsize) {
    arith_token_t *tokens = NULL;
    size_t count = 0;
    arith_diag_t diag = {0};
    if (!arith_lex(expr, &tokens, &count, &diag, true)) {
        return false;
    }
    arith_ast_t *ast = arith_parse(tokens, count, &diag);
    arith_tokens_free(tokens, count);
    if (!ast) {
        return false;
    }
    arith_ast_to_sexpr(ast, out, outsize);
    arith_ast_free(ast);
    return true;
}

/**
 * @brief Lex + parse @p expr, expecting failure with a specific error code.
 * @return true iff a diagnostic was flagged with @p expected_code.
 */
static bool parse_fails(const char *expr, shell_error_code_t expected_code) {
    arith_token_t *tokens = NULL;
    size_t count = 0;
    arith_diag_t diag = {0};
    if (!arith_lex(expr, &tokens, &count, &diag, true)) {
        return diag.flagged && diag.code == expected_code;
    }
    arith_ast_t *ast = arith_parse(tokens, count, &diag);
    arith_tokens_free(tokens, count);
    if (ast) {
        arith_ast_free(ast);
        return false;
    }
    return diag.flagged && diag.code == expected_code;
}

/* ==========================================================================
 * Lexer
 * ========================================================================== */

TEST(lex_number_bases) {
    char s[256];
    /// base 0: decimal, 0x hex, 0 octal -- the value appears in the S-expr.
    ASSERT_TRUE(parse_sexpr("42", s, sizeof(s)) && strcmp(s, "42") == 0,
                "decimal 42");
    ASSERT_TRUE(parse_sexpr("0x10", s, sizeof(s)) && strcmp(s, "16") == 0,
                "hex 0x10 = 16");
    ASSERT_TRUE(parse_sexpr("010", s, sizeof(s)) && strcmp(s, "8") == 0,
                "octal 010 = 8");
}

TEST(lex_rejects_bad_char) {
    /// A byte that begins no token is a lexical syntax error.
    ASSERT_TRUE(parse_fails("1 @ 2", SHELL_ERR_ARITHMETIC_SYNTAX),
                "'@' is not a valid arithmetic character");
}

TEST(lex_span_recorded) {
    /// The diagnostic carries the offending span (offset + length) so the
    /// integration layer can draw a precise caret.
    arith_token_t *tokens = NULL;
    size_t count = 0;
    arith_diag_t diag = {0};
    bool ok = arith_lex("12 @", &tokens, &count, &diag, true);
    ASSERT_TRUE(!ok, "lexing '12 @' fails");
    ASSERT_EQ((long)diag.pos, 3L, "caret at the '@' (offset 3)");
    ASSERT_EQ((long)diag.len, 1L, "caret length 1");
}

/* ==========================================================================
 * Precedence and associativity
 * ========================================================================== */

TEST(parse_mul_over_add) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("1 + 2 * 3", s, sizeof(s)) &&
                    strcmp(s, "(+ 1 (* 2 3))") == 0,
                "* binds tighter than +");
    ASSERT_TRUE(parse_sexpr("1 * 2 + 3", s, sizeof(s)) &&
                    strcmp(s, "(+ (* 1 2) 3)") == 0,
                "left * groups before +");
}

TEST(parse_left_assoc) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("1 - 2 - 3", s, sizeof(s)) &&
                    strcmp(s, "(- (- 1 2) 3)") == 0,
                "- is left-associative");
    ASSERT_TRUE(parse_sexpr("a , b , c", s, sizeof(s)) &&
                    strcmp(s, "(, (, a b) c)") == 0,
                ", is left-associative");
}

TEST(parse_pow_right_assoc) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("2 ** 3 ** 2", s, sizeof(s)) &&
                    strcmp(s, "(** 2 (** 3 2))") == 0,
                "** is right-associative");
}

TEST(parse_assign_right_assoc) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("a = b = 5", s, sizeof(s)) &&
                    strcmp(s, "(= a (= b 5))") == 0,
                "assignment is right-associative");
}

TEST(parse_comparison_chain) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("1 < 2 == 1", s, sizeof(s)) &&
                    strcmp(s, "(== (< 1 2) 1)") == 0,
                "< binds tighter than ==");
}

TEST(parse_bitwise_ladder) {
    char s[256];
    /// & tighter than ^ tighter than |.
    ASSERT_TRUE(parse_sexpr("1 | 2 ^ 3 & 4", s, sizeof(s)) &&
                    strcmp(s, "(| 1 (^ 2 (& 3 4)))") == 0,
                "& < ^ < | precedence ladder");
}

TEST(parse_parens_override) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("(1 + 2) * 3", s, sizeof(s)) &&
                    strcmp(s, "(* (+ 1 2) 3)") == 0,
                "parentheses regroup");
    ASSERT_TRUE(parse_sexpr("((1 + 2))", s, sizeof(s)) &&
                    strcmp(s, "(+ 1 2)") == 0,
                "redundant nested parens");
}

/* ==========================================================================
 * Unary, increment/decrement
 * ========================================================================== */

TEST(parse_unary) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("-a * b", s, sizeof(s)) &&
                    strcmp(s, "(* (- a) b)") == 0,
                "unary minus binds tighter than *");
    ASSERT_TRUE(parse_sexpr("- - a", s, sizeof(s)) &&
                    strcmp(s, "(- (- a))") == 0,
                "double negation nests");
    ASSERT_TRUE(parse_sexpr("!~x", s, sizeof(s)) && strcmp(s, "(! (~ x))") == 0,
                "chained prefix ! ~");
}

TEST(parse_unary_vs_pow) {
    char s[256];
    /// Matches the legacy table: unary binds tighter than ** -> (-a)**b.
    ASSERT_TRUE(parse_sexpr("-a ** b", s, sizeof(s)) &&
                    strcmp(s, "(** (- a) b)") == 0,
                "unary minus binds tighter than ** (legacy parity)");
}

TEST(parse_incdec) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("a++", s, sizeof(s)) &&
                    strcmp(s, "(post++ a)") == 0,
                "postfix increment");
    ASSERT_TRUE(parse_sexpr("++a", s, sizeof(s)) && strcmp(s, "(pre++ a)") == 0,
                "prefix increment");
    ASSERT_TRUE(parse_sexpr("-a++", s, sizeof(s)) &&
                    strcmp(s, "(- (post++ a))") == 0,
                "postfix binds tighter than unary minus");
}

/* ==========================================================================
 * Ternary and assignment shape
 * ========================================================================== */

TEST(parse_ternary) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("a ? b : c", s, sizeof(s)) &&
                    strcmp(s, "(?: a b c)") == 0,
                "ternary shape");
    ASSERT_TRUE(parse_sexpr("a ? b : c ? d : e", s, sizeof(s)) &&
                    strcmp(s, "(?: a b (?: c d e))") == 0,
                "ternary is right-associative");
}

TEST(parse_compound_assign) {
    char s[256];
    ASSERT_TRUE(parse_sexpr("x += 3", s, sizeof(s)) &&
                    strcmp(s, "(+= x 3)") == 0,
                "compound assignment shape");
    ASSERT_TRUE(parse_sexpr("x <<= 2", s, sizeof(s)) &&
                    strcmp(s, "(<<= x 2)") == 0,
                "shift-assign shape (#563 operator)");
    ASSERT_TRUE(parse_sexpr("x ^= 3", s, sizeof(s)) &&
                    strcmp(s, "(^= x 3)") == 0,
                "xor-assign shape (#563 operator)");
}

/* ==========================================================================
 * Array subscripts (the #603 payload)
 * ========================================================================== */

TEST(parse_subscript) {
    char s[256];
    /// The subscript is captured whole and raw by the lexer (issue #615), so an
    /// index node carries the literal `[...]` interior text, not a parsed
    /// sub-AST -- the evaluator interprets it per the array's kind.
    ASSERT_TRUE(parse_sexpr("a[0]", s, sizeof(s)) &&
                    strcmp(s, "(index a [0])") == 0,
                "simple subscript");
    ASSERT_TRUE(parse_sexpr("a[i + 1] = 9", s, sizeof(s)) &&
                    strcmp(s, "(= (index a [i + 1]) 9)") == 0,
                "subscript is an lvalue with a raw index");
    ASSERT_TRUE(parse_sexpr("a[b[0]]", s, sizeof(s)) &&
                    strcmp(s, "(index a [b[0]])") == 0,
                "nested subscript is captured whole (raw text)");
    ASSERT_TRUE(parse_sexpr("a[i]++", s, sizeof(s)) &&
                    strcmp(s, "(post++ (index a [i]))") == 0,
                "an array element is a postfix-increment target");
    /// A non-arithmetic associative key survives lex-capture unchanged.
    ASSERT_TRUE(parse_sexpr("m[foo.bar]", s, sizeof(s)) &&
                    strcmp(s, "(index m [foo.bar])") == 0,
                "a non-arithmetic key is captured as raw text");
}

/* ==========================================================================
 * Errors -> specific structured codes
 * ========================================================================== */

TEST(err_missing_operand) {
    ASSERT_TRUE(parse_fails("1 +", SHELL_ERR_ARITHMETIC_SYNTAX),
                "trailing operator lacks an operand");
}

TEST(err_mismatched_parens) {
    ASSERT_TRUE(parse_fails("(1 + 2", SHELL_ERR_ARITH_MISMATCHED_PARENS),
                "unclosed parenthesis");
}

TEST(err_mismatched_ternary) {
    ASSERT_TRUE(parse_fails("1 ? 2", SHELL_ERR_ARITH_MISMATCHED_TERNARY),
                "ternary without ':'");
}

TEST(err_invalid_assign_target) {
    ASSERT_TRUE(parse_fails("5 = 3", SHELL_ERR_ARITH_INVALID_ASSIGN_TARGET),
                "cannot assign to a literal");
}

TEST(err_invalid_incdec_target) {
    ASSERT_TRUE(parse_fails("5++", SHELL_ERR_ARITH_INVALID_INCREMENT_TARGET),
                "cannot post-increment a literal");
    ASSERT_TRUE(parse_fails("--5", SHELL_ERR_ARITH_INVALID_DECREMENT_TARGET),
                "cannot pre-decrement a literal");
}

TEST(err_unclosed_subscript) {
    ASSERT_TRUE(parse_fails("a[0", SHELL_ERR_ARITHMETIC_SYNTAX),
                "unclosed subscript");
}

TEST(err_trailing_token) {
    ASSERT_TRUE(parse_fails("1 2", SHELL_ERR_ARITHMETIC_SYNTAX),
                "two operands with no operator");
}

TEST(empty_is_not_an_error) {
    /// An empty expression parses to NULL with no flagged diagnostic.
    arith_token_t *tokens = NULL;
    size_t count = 0;
    arith_diag_t diag = {0};
    ASSERT_TRUE(arith_lex("   ", &tokens, &count, &diag, true),
                "lexing blanks");
    arith_ast_t *ast = arith_parse(tokens, count, &diag);
    arith_tokens_free(tokens, count);
    ASSERT_NULL(ast, "empty expression yields a NULL AST");
    ASSERT_TRUE(!diag.flagged, "empty expression is not an error");
}

int main(void) {
    printf("=== Arithmetic AST Front-End Tests (Stage 2) ===\n");

    printf("\n--- Lexer ---\n");
    RUN_TEST(lex_number_bases);
    RUN_TEST(lex_rejects_bad_char);
    RUN_TEST(lex_span_recorded);

    printf("\n--- Precedence and Associativity ---\n");
    RUN_TEST(parse_mul_over_add);
    RUN_TEST(parse_left_assoc);
    RUN_TEST(parse_pow_right_assoc);
    RUN_TEST(parse_assign_right_assoc);
    RUN_TEST(parse_comparison_chain);
    RUN_TEST(parse_bitwise_ladder);
    RUN_TEST(parse_parens_override);

    printf("\n--- Unary and Increment/Decrement ---\n");
    RUN_TEST(parse_unary);
    RUN_TEST(parse_unary_vs_pow);
    RUN_TEST(parse_incdec);

    printf("\n--- Ternary and Assignment ---\n");
    RUN_TEST(parse_ternary);
    RUN_TEST(parse_compound_assign);

    printf("\n--- Array Subscripts (#603) ---\n");
    RUN_TEST(parse_subscript);

    printf("\n--- Structured Errors ---\n");
    RUN_TEST(err_missing_operand);
    RUN_TEST(err_mismatched_parens);
    RUN_TEST(err_mismatched_ternary);
    RUN_TEST(err_invalid_assign_target);
    RUN_TEST(err_invalid_incdec_target);
    RUN_TEST(err_unclosed_subscript);
    RUN_TEST(err_trailing_token);
    RUN_TEST(empty_is_not_an_error);

    return TEST_RESULT();
}
