/**
 * @file arithmetic_parse.c
 * @brief Pratt (precedence-climbing) parser for the AST arithmetic engine.
 *
 * Pure token[] -> AST. No symbol table, no evaluation: a syntax error is
 * reported through the caller's arith_diag_t. Binding powers are the inverse
 * of the legacy shunting-yard table (here, higher binds tighter) and preserve
 * its precedence ordering exactly: postfix ++/-- and subscript bind tightest,
 * then prefix unary, then ** (right), then * / %, + -, shifts, relational,
 * equality, & ^ |, && ||, ?: (right), assignment (right), and comma (loosest).
 *
 * An lvalue (assignment / increment / decrement target, and the array name of
 * a subscript) is checked structurally at parse time -- it must be an AST_VAR
 * or AST_INDEX -- so the evaluator never has to re-validate it.
 */

#include "arithmetic_ast.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Parser state over a fixed token array.
 */
typedef struct {
    const arith_token_t *tokens;
    size_t count;
    size_t pos;
    arith_diag_t *diag;
} ap_t;

/// Binding power used when a prefix unary operator parses its operand. It sits
/// above ** (14) and below postfix/subscript (16), so `-a**b` groups as
/// `(-a)**b` and `-a[0]` / `-a++` group as `-(a[0])` / `-(a++)` -- matching the
/// legacy table (unary tighter than **, looser than postfix).
#define AP_PREFIX_BP 15

static arith_ast_t *parse_expr(ap_t *p, int rbp);

static const arith_token_t *cur(ap_t *p) { return &p->tokens[p->pos]; }

static arith_token_kind_t cur_kind(ap_t *p) { return p->tokens[p->pos].kind; }

static void advance(ap_t *p) {
    if (p->pos + 1 < p->count) {
        p->pos++;
    }
}

/**
 * @brief Record a parse error and return NULL (caller frees partial nodes).
 *
 * @param pos Byte offset of the offending token (caret start)
 * @param len Byte span of the offending token (caret length, clamped >=1)
 */
static arith_ast_t *parse_fail(ap_t *p, shell_error_code_t code,
                               const char *while_ctx, const char *help,
                               const char *message, size_t pos, size_t len) {
    if (p->diag) {
        p->diag->flagged = true;
        p->diag->code = code;
        p->diag->while_ctx = while_ctx;
        p->diag->help = help;
        snprintf(p->diag->message, sizeof(p->diag->message), "%s", message);
        p->diag->pos = pos;
        p->diag->len = (len == 0) ? 1 : len;
    }
    return NULL;
}

/**
 * @brief Left binding power of a token when it appears in infix/postfix
 *        position. Higher binds tighter; 0 stops the precedence-climb loop.
 */
static int lbp(arith_token_kind_t k) {
    switch (k) {
    case ATK_COMMA:
        return 1;
    case ATK_ASSIGN:
    case ATK_PLUSEQ:
    case ATK_MINUSEQ:
    case ATK_STAREQ:
    case ATK_SLASHEQ:
    case ATK_PERCENTEQ:
    case ATK_SHLEQ:
    case ATK_SHREQ:
    case ATK_AMPEQ:
    case ATK_PIPEEQ:
    case ATK_CARETEQ:
        return 2;
    case ATK_QUESTION:
        return 3;
    case ATK_OR:
        return 4;
    case ATK_AND:
        return 5;
    case ATK_PIPE:
        return 6;
    case ATK_CARET:
        return 7;
    case ATK_AMP:
        return 8;
    case ATK_EQ:
    case ATK_NE:
        return 9;
    case ATK_LT:
    case ATK_LE:
    case ATK_GT:
    case ATK_GE:
        return 10;
    case ATK_SHL:
    case ATK_SHR:
        return 11;
    case ATK_PLUS:
    case ATK_MINUS:
        return 12;
    case ATK_STAR:
    case ATK_SLASH:
    case ATK_PERCENT:
        return 13;
    case ATK_POW:
        return 14;
    case ATK_INC:
    case ATK_DEC:
    case ATK_SUBSCRIPT:
        return 16;
    default:
        return 0;
    }
}

/**
 * @brief True for assignment-operator token kinds.
 */
static bool is_assign_op(arith_token_kind_t k) {
    switch (k) {
    case ATK_ASSIGN:
    case ATK_PLUSEQ:
    case ATK_MINUSEQ:
    case ATK_STAREQ:
    case ATK_SLASHEQ:
    case ATK_PERCENTEQ:
    case ATK_SHLEQ:
    case ATK_SHREQ:
    case ATK_AMPEQ:
    case ATK_PIPEEQ:
    case ATK_CARETEQ:
        return true;
    default:
        return false;
    }
}

static bool is_lvalue(const arith_ast_t *n) {
    return n && (n->kind == AST_VAR || n->kind == AST_INDEX);
}

static arith_ast_t *new_node(arith_ast_kind_t kind, size_t pos) {
    arith_ast_t *n = calloc(1, sizeof(*n));
    if (n) {
        n->kind = kind;
        n->pos = pos;
    }
    return n;
}

void arith_ast_free(arith_ast_t *node) {
    if (!node) {
        return;
    }
    arith_ast_free(node->a);
    arith_ast_free(node->b);
    arith_ast_free(node->c);
    free(node->name);
    free(node->index_raw);
    free(node);
}

/**
 * @brief Parse a prefix unary operator, given its AST kind.
 */
static arith_ast_t *parse_prefix(ap_t *p, arith_ast_kind_t kind,
                                 arith_token_kind_t op) {
    size_t pos = cur(p)->pos;
    advance(p);
    arith_ast_t *operand = parse_expr(p, AP_PREFIX_BP);
    if (!operand) {
        return NULL;
    }
    if ((kind == AST_PREINC || kind == AST_PREDEC) && !is_lvalue(operand)) {
        shell_error_code_t code =
            (kind == AST_PREINC) ? SHELL_ERR_ARITH_INVALID_INCREMENT_TARGET
                                 : SHELL_ERR_ARITH_INVALID_DECREMENT_TARGET;
        arith_ast_free(operand);
        return parse_fail(p, code, "evaluating prefix ++/-- operator",
                          "operand must be a variable", "invalid target", pos,
                          2);
    }
    arith_ast_t *n = new_node(kind, pos);
    if (!n) {
        arith_ast_free(operand);
        return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY, "parsing prefix operator",
                          "out of memory", "memory allocation failed", pos, 1);
    }
    n->op = op;
    n->a = operand;
    return n;
}

/**
 * @brief Null denotation: parse an operand or a prefix-led expression.
 */
static arith_ast_t *nud(ap_t *p) {
    const arith_token_t *t = cur(p);
    size_t pos = t->pos;

    switch (t->kind) {
    case ATK_NUM: {
        arith_ast_t *n = new_node(AST_NUM, pos);
        if (!n) {
            return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY, "parsing a number",
                              "out of memory", "memory allocation failed", pos,
                              1);
        }
        n->num = t->num;
        advance(p);
        return n;
    }
    case ATK_IDENT: {
        arith_ast_t *n = new_node(AST_VAR, pos);
        if (!n) {
            return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY, "parsing a variable",
                              "out of memory", "memory allocation failed", pos,
                              1);
        }
        n->name = strdup(t->text);
        if (!n->name) {
            arith_ast_free(n);
            return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY, "parsing a variable",
                              "out of memory", "memory allocation failed", pos,
                              1);
        }
        advance(p);
        return n;
    }
    case ATK_LPAREN: {
        advance(p);
        arith_ast_t *inner = parse_expr(p, 0);
        if (!inner) {
            return NULL;
        }
        if (cur_kind(p) != ATK_RPAREN) {
            arith_ast_free(inner);
            return parse_fail(p, SHELL_ERR_ARITH_MISMATCHED_PARENS,
                              "parsing a parenthesized expression",
                              "every '(' needs a matching ')'",
                              "mismatched parentheses", cur(p)->pos,
                              cur(p)->len);
        }
        advance(p);
        return inner;
    }
    case ATK_MINUS:
        return parse_prefix(p, AST_UNOP, ATK_MINUS);
    case ATK_PLUS:
        return parse_prefix(p, AST_UNOP, ATK_PLUS);
    case ATK_NOT:
        return parse_prefix(p, AST_UNOP, ATK_NOT);
    case ATK_TILDE:
        return parse_prefix(p, AST_UNOP, ATK_TILDE);
    case ATK_INC:
        return parse_prefix(p, AST_PREINC, ATK_INC);
    case ATK_DEC:
        return parse_prefix(p, AST_PREDEC, ATK_DEC);
    default:
        return parse_fail(p, SHELL_ERR_ARITHMETIC_SYNTAX,
                          "parsing an arithmetic operand",
                          "expected a number, variable, or '('",
                          "expected an operand", pos, t->len);
    }
}

/**
 * @brief Left denotation: extend @p left with an infix or postfix operator.
 *
 * Takes ownership of @p left and frees it on error.
 */
static arith_ast_t *led(ap_t *p, arith_ast_t *left) {
    const arith_token_t *t = cur(p);
    arith_token_kind_t k = t->kind;
    size_t pos = t->pos;

    /// Postfix increment / decrement.
    if (k == ATK_INC || k == ATK_DEC) {
        if (!is_lvalue(left)) {
            shell_error_code_t code =
                (k == ATK_INC) ? SHELL_ERR_ARITH_INVALID_INCREMENT_TARGET
                               : SHELL_ERR_ARITH_INVALID_DECREMENT_TARGET;
            arith_ast_free(left);
            return parse_fail(p, code, "evaluating postfix ++/-- operator",
                              "operand must be a variable", "invalid target",
                              pos, t->len);
        }
        advance(p);
        arith_ast_t *n =
            new_node((k == ATK_INC) ? AST_POSTINC : AST_POSTDEC, pos);
        if (!n) {
            arith_ast_free(left);
            return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY,
                              "parsing postfix operator", "out of memory",
                              "memory allocation failed", pos, 1);
        }
        n->op = k;
        n->a = left;
        return n;
    }

    /// Array subscript: name ATK_SUBSCRIPT. The lexer captured the whole
    /// `[...]` interior as raw text (issue #615); the evaluator interprets it
    /// per the array's kind (literal key for associative, arithmetic for
    /// indexed), so the parser only records it -- it never parses the interior.
    if (k == ATK_SUBSCRIPT) {
        if (left->kind != AST_VAR) {
            arith_ast_free(left);
            return parse_fail(p, SHELL_ERR_ARITHMETIC_SYNTAX,
                              "parsing an array subscript",
                              "a subscript applies to an array name",
                              "invalid subscript target", pos, t->len);
        }
        char *raw = strdup(t->text ? t->text : "");
        if (!raw) {
            arith_ast_free(left);
            return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY,
                              "parsing an array subscript", "out of memory",
                              "memory allocation failed", pos, 1);
        }
        arith_ast_t *n = new_node(AST_INDEX, left->pos);
        if (!n) {
            free(raw);
            arith_ast_free(left);
            return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY,
                              "parsing an array subscript", "out of memory",
                              "memory allocation failed", pos, 1);
        }
        n->name = left->name; /// transfer ownership of the array name
        left->name = NULL;
        arith_ast_free(left);
        n->index_raw = raw;
        advance(p); /// consume the ATK_SUBSCRIPT token
        return n;
    }

    /// Ternary conditional: left '?' then ':' else.
    if (k == ATK_QUESTION) {
        advance(p);
        arith_ast_t *then_branch = parse_expr(p, 0);
        if (!then_branch) {
            arith_ast_free(left);
            return NULL;
        }
        if (cur_kind(p) != ATK_COLON) {
            arith_ast_free(left);
            arith_ast_free(then_branch);
            return parse_fail(p, SHELL_ERR_ARITH_MISMATCHED_TERNARY,
                              "parsing a ternary expression",
                              "every '?' needs a matching ':'",
                              "mismatched ternary operator", cur(p)->pos,
                              cur(p)->len);
        }
        advance(p);
        arith_ast_t *else_branch = parse_expr(p, lbp(ATK_QUESTION) - 1);
        if (!else_branch) {
            arith_ast_free(left);
            arith_ast_free(then_branch);
            return NULL;
        }
        arith_ast_t *n = new_node(AST_TERNARY, pos);
        if (!n) {
            arith_ast_free(left);
            arith_ast_free(then_branch);
            arith_ast_free(else_branch);
            return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY,
                              "parsing a ternary expression", "out of memory",
                              "memory allocation failed", pos, 1);
        }
        n->a = left;
        n->b = then_branch;
        n->c = else_branch;
        return n;
    }

    /// Assignment (right-associative); the lhs must be an lvalue.
    if (is_assign_op(k)) {
        if (!is_lvalue(left)) {
            arith_ast_free(left);
            return parse_fail(p, SHELL_ERR_ARITH_INVALID_ASSIGN_TARGET,
                              "evaluating an assignment operator",
                              "left-hand side must be a variable name",
                              "invalid assignment target", pos, t->len);
        }
        advance(p);
        arith_ast_t *rhs = parse_expr(p, lbp(k) - 1);
        if (!rhs) {
            arith_ast_free(left);
            return NULL;
        }
        arith_ast_t *n = new_node(AST_ASSIGN, pos);
        if (!n) {
            arith_ast_free(left);
            arith_ast_free(rhs);
            return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY,
                              "parsing an assignment", "out of memory",
                              "memory allocation failed", pos, 1);
        }
        n->op = k;
        n->a = left;
        n->b = rhs;
        return n;
    }

    /// Binary operators. `**` is right-associative; comma and the rest are
    /// left-associative.
    {
        int right_rbp = (k == ATK_POW) ? lbp(k) - 1 : lbp(k);
        arith_ast_kind_t node_kind = (k == ATK_COMMA) ? AST_COMMA : AST_BINOP;
        advance(p);
        arith_ast_t *rhs = parse_expr(p, right_rbp);
        if (!rhs) {
            arith_ast_free(left);
            return NULL;
        }
        arith_ast_t *n = new_node(node_kind, pos);
        if (!n) {
            arith_ast_free(left);
            arith_ast_free(rhs);
            return parse_fail(p, SHELL_ERR_OUT_OF_MEMORY,
                              "parsing a binary operator", "out of memory",
                              "memory allocation failed", pos, 1);
        }
        n->op = k;
        n->a = left;
        n->b = rhs;
        return n;
    }
}

/**
 * @brief Precedence-climbing expression parser.
 */
static arith_ast_t *parse_expr(ap_t *p, int rbp) {
    arith_ast_t *left = nud(p);
    if (!left) {
        return NULL;
    }
    while (rbp < lbp(cur_kind(p))) {
        left = led(p, left);
        if (!left) {
            return NULL;
        }
    }
    return left;
}

arith_ast_t *arith_parse(const arith_token_t *tokens, size_t count,
                         arith_diag_t *diag) {
    if (!tokens || count == 0) {
        return NULL;
    }
    ap_t p = {.tokens = tokens, .count = count, .pos = 0, .diag = diag};

    /// Empty expression (only EOF): not an error; caller checks diag->flagged.
    if (cur_kind(&p) == ATK_EOF) {
        return NULL;
    }

    arith_ast_t *root = parse_expr(&p, 0);
    if (!root) {
        return NULL;
    }

    if (cur_kind(&p) != ATK_EOF) {
        size_t pos = cur(&p)->pos;
        size_t len = cur(&p)->len;
        arith_ast_free(root);
        return parse_fail(&p, SHELL_ERR_ARITHMETIC_SYNTAX,
                          "parsing an arithmetic expression",
                          "check operator and operand placement",
                          "unexpected trailing token", pos, len);
    }
    return root;
}

/* ==========================================================================
 * S-expression serialization (unit-test aid; no evaluation)
 * ========================================================================== */

/**
 * @brief Symbol for an operator token, for S-expression output.
 */
static const char *op_sym(arith_token_kind_t k) {
    switch (k) {
    case ATK_PLUS:
        return "+";
    case ATK_MINUS:
        return "-";
    case ATK_STAR:
        return "*";
    case ATK_SLASH:
        return "/";
    case ATK_PERCENT:
        return "%";
    case ATK_POW:
        return "**";
    case ATK_SHL:
        return "<<";
    case ATK_SHR:
        return ">>";
    case ATK_LT:
        return "<";
    case ATK_LE:
        return "<=";
    case ATK_GT:
        return ">";
    case ATK_GE:
        return ">=";
    case ATK_EQ:
        return "==";
    case ATK_NE:
        return "!=";
    case ATK_AMP:
        return "&";
    case ATK_CARET:
        return "^";
    case ATK_PIPE:
        return "|";
    case ATK_AND:
        return "&&";
    case ATK_OR:
        return "||";
    case ATK_NOT:
        return "!";
    case ATK_TILDE:
        return "~";
    case ATK_ASSIGN:
        return "=";
    case ATK_PLUSEQ:
        return "+=";
    case ATK_MINUSEQ:
        return "-=";
    case ATK_STAREQ:
        return "*=";
    case ATK_SLASHEQ:
        return "/=";
    case ATK_PERCENTEQ:
        return "%=";
    case ATK_SHLEQ:
        return "<<=";
    case ATK_SHREQ:
        return ">>=";
    case ATK_AMPEQ:
        return "&=";
    case ATK_PIPEEQ:
        return "|=";
    case ATK_CARETEQ:
        return "^=";
    case ATK_INC:
        return "++";
    case ATK_DEC:
        return "--";
    default:
        return "?";
    }
}

/**
 * @brief Append a formatted fragment, advancing a bounded write cursor.
 */
static void sx_append(char *buf, size_t bufsize, size_t *off, const char *fmt,
                      ...) {
    if (*off >= bufsize) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, bufsize - *off, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *off += (size_t)n;
        if (*off >= bufsize) {
            *off = bufsize - 1;
        }
    }
}

static void sx_write(const arith_ast_t *n, char *buf, size_t bufsize,
                     size_t *off) {
    if (!n) {
        sx_append(buf, bufsize, off, "<null>");
        return;
    }
    switch (n->kind) {
    case AST_NUM:
        sx_append(buf, bufsize, off, "%ld", n->num);
        break;
    case AST_VAR:
        sx_append(buf, bufsize, off, "%s", n->name ? n->name : "<noname>");
        break;
    case AST_INDEX:
        /// The subscript is raw text (lex-captured, #615), not a sub-AST.
        sx_append(buf, bufsize, off, "(index %s [%s])",
                  n->name ? n->name : "<noname>",
                  n->index_raw ? n->index_raw : "");
        break;
    case AST_UNOP:
        sx_append(buf, bufsize, off, "(%s ", op_sym(n->op));
        sx_write(n->a, buf, bufsize, off);
        sx_append(buf, bufsize, off, ")");
        break;
    case AST_BINOP:
    case AST_ASSIGN:
    case AST_COMMA: {
        const char *sym = (n->kind == AST_COMMA) ? "," : op_sym(n->op);
        sx_append(buf, bufsize, off, "(%s ", sym);
        sx_write(n->a, buf, bufsize, off);
        sx_append(buf, bufsize, off, " ");
        sx_write(n->b, buf, bufsize, off);
        sx_append(buf, bufsize, off, ")");
        break;
    }
    case AST_PREINC:
    case AST_PREDEC:
    case AST_POSTINC:
    case AST_POSTDEC: {
        const char *tag = (n->kind == AST_PREINC)    ? "pre++"
                          : (n->kind == AST_PREDEC)  ? "pre--"
                          : (n->kind == AST_POSTINC) ? "post++"
                                                     : "post--";
        sx_append(buf, bufsize, off, "(%s ", tag);
        sx_write(n->a, buf, bufsize, off);
        sx_append(buf, bufsize, off, ")");
        break;
    }
    case AST_TERNARY:
        sx_append(buf, bufsize, off, "(?: ");
        sx_write(n->a, buf, bufsize, off);
        sx_append(buf, bufsize, off, " ");
        sx_write(n->b, buf, bufsize, off);
        sx_append(buf, bufsize, off, " ");
        sx_write(n->c, buf, bufsize, off);
        sx_append(buf, bufsize, off, ")");
        break;
    }
}

void arith_ast_to_sexpr(const arith_ast_t *node, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0) {
        return;
    }
    size_t off = 0;
    buf[0] = '\0';
    sx_write(node, buf, bufsize, &off);
}
