/**
 * @file arithmetic_lex.c
 * @brief Lexer for the AST-based arithmetic engine.
 *
 * Pure string -> token[] tokenization. No symbol-table access, no evaluation,
 * no global state: a lexical failure is reported through the caller's
 * arith_diag_t. `$`-forms are expanded away before lexing, so the lexer only
 * sees numbers, identifiers, and arithmetic operators.
 */

#include "arithmetic_ast.h"
#include "identifier.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Record a lexical error in the diagnostic and return false.
 *
 * @param diag    Diagnostic to fill (may be NULL)
 * @param code    Specific structured error code
 * @param help    Actionable "help:" suggestion
 * @param pos     Byte offset of the offending span (caret start)
 * @param len     Byte length of the offending span (caret length, clamped >=1)
 * @param message Primary message (already formatted)
 */
static bool lex_fail(arith_diag_t *diag, shell_error_code_t code,
                     const char *help, size_t pos, size_t len,
                     const char *message) {
    if (diag) {
        diag->flagged = true;
        diag->code = code;
        diag->while_ctx = "tokenizing the arithmetic expression";
        diag->help = help;
        snprintf(diag->message, sizeof(diag->message), "%s", message);
        diag->pos = pos;
        diag->len = (len == 0) ? 1 : len;
    }
    return false;
}

/**
 * @brief Append a token to a growable token array.
 *
 * @return true on success, false on allocation failure.
 */
static bool push_token(arith_token_t **arr, size_t *count, size_t *cap,
                       arith_token_t tok) {
    if (*count >= *cap) {
        size_t new_cap = (*cap == 0) ? 16 : (*cap * 2);
        arith_token_t *grown = realloc(*arr, new_cap * sizeof(**arr));
        if (!grown) {
            return false;
        }
        *arr = grown;
        *cap = new_cap;
    }
    (*arr)[(*count)++] = tok;
    return true;
}

void arith_tokens_free(arith_token_t *tokens, size_t count) {
    if (!tokens) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        /// Free any token that owns heap text: ATK_IDENT (identifier) and
        /// ATK_SUBSCRIPT (the raw `[...]` interior, issue #615). Every other
        /// kind initializes .text to NULL, so an unconditional free is a safe
        /// no-op there and stays correct if a future kind adds owned text.
        free(tokens[i].text);
    }
    free(tokens);
}

/**
 * @brief Read an integer literal under the active base rule.
 *
 * ONE base rule, shared by every reader of a lush number: this lexer and the
 * evaluator's pure-integer fast path for scalar values (#578 keeps a literal
 * and a variable holding the same text in agreement). They must not each carry
 * their own copy -- when they did, `octal_zeroes` reached literals but not
 * scalar reads, so `mode zsh` evaluated `$((010))` as 10 and `x=010; $((x))`
 * as 8.
 *
 * With @p octal_zeroes set, a leading zero denotes octal (base 0: `0x` hex,
 * `0` octal, else decimal). With it clear, a leading zero is not significant
 * and only an explicit `0x` selects a base -- the zsh oracle's default.
 *
 * @param s            Start of the literal.
 * @param octal_zeroes Whether a leading zero denotes octal.
 * @param end          Receives the first byte not consumed (as strtol).
 * @return The parsed value; @p end == @p s means no digits were consumed.
 */
long arith_strtol_based(const char *s, bool octal_zeroes, char **end) {
    if (octal_zeroes) {
        return strtol(s, end, 0);
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return strtol(s, end, 16);
    }
    return strtol(s, end, 10);
}

/**
 * @brief Lex a single operator/punctuation token by longest match.
 *
 * On a recognized operator, sets *kind and *len (byte length consumed) and
 * returns true. Returns false for a character that does not begin an operator.
 */
static bool lex_operator(const char *p, arith_token_kind_t *kind, size_t *len) {
    char c0 = p[0];
    char c1 = c0 ? p[1] : '\0';
    char c2 = c1 ? p[2] : '\0';

    switch (c0) {
    case '(':
        *kind = ATK_LPAREN;
        *len = 1;
        return true;
    case ')':
        *kind = ATK_RPAREN;
        *len = 1;
        return true;
    case '[':
        *kind = ATK_LBRACKET;
        *len = 1;
        return true;
    case ']':
        *kind = ATK_RBRACKET;
        *len = 1;
        return true;
    case '~':
        *kind = ATK_TILDE;
        *len = 1;
        return true;
    case '?':
        *kind = ATK_QUESTION;
        *len = 1;
        return true;
    case ':':
        *kind = ATK_COLON;
        *len = 1;
        return true;
    case ',':
        *kind = ATK_COMMA;
        *len = 1;
        return true;
    case '+':
        if (c1 == '+') {
            *kind = ATK_INC;
            *len = 2;
        } else if (c1 == '=') {
            *kind = ATK_PLUSEQ;
            *len = 2;
        } else {
            *kind = ATK_PLUS;
            *len = 1;
        }
        return true;
    case '-':
        if (c1 == '-') {
            *kind = ATK_DEC;
            *len = 2;
        } else if (c1 == '=') {
            *kind = ATK_MINUSEQ;
            *len = 2;
        } else {
            *kind = ATK_MINUS;
            *len = 1;
        }
        return true;
    case '*':
        if (c1 == '*') {
            *kind = ATK_POW;
            *len = 2;
        } else if (c1 == '=') {
            *kind = ATK_STAREQ;
            *len = 2;
        } else {
            *kind = ATK_STAR;
            *len = 1;
        }
        return true;
    case '/':
        if (c1 == '=') {
            *kind = ATK_SLASHEQ;
            *len = 2;
        } else {
            *kind = ATK_SLASH;
            *len = 1;
        }
        return true;
    case '%':
        if (c1 == '=') {
            *kind = ATK_PERCENTEQ;
            *len = 2;
        } else {
            *kind = ATK_PERCENT;
            *len = 1;
        }
        return true;
    case '^':
        if (c1 == '=') {
            *kind = ATK_CARETEQ;
            *len = 2;
        } else {
            *kind = ATK_CARET;
            *len = 1;
        }
        return true;
    case '<':
        if (c1 == '<' && c2 == '=') {
            *kind = ATK_SHLEQ;
            *len = 3;
        } else if (c1 == '<') {
            *kind = ATK_SHL;
            *len = 2;
        } else if (c1 == '=') {
            *kind = ATK_LE;
            *len = 2;
        } else {
            *kind = ATK_LT;
            *len = 1;
        }
        return true;
    case '>':
        if (c1 == '>' && c2 == '=') {
            *kind = ATK_SHREQ;
            *len = 3;
        } else if (c1 == '>') {
            *kind = ATK_SHR;
            *len = 2;
        } else if (c1 == '=') {
            *kind = ATK_GE;
            *len = 2;
        } else {
            *kind = ATK_GT;
            *len = 1;
        }
        return true;
    case '=':
        if (c1 == '=') {
            *kind = ATK_EQ;
            *len = 2;
        } else {
            *kind = ATK_ASSIGN;
            *len = 1;
        }
        return true;
    case '!':
        if (c1 == '=') {
            *kind = ATK_NE;
            *len = 2;
        } else {
            *kind = ATK_NOT;
            *len = 1;
        }
        return true;
    case '&':
        if (c1 == '&') {
            *kind = ATK_AND;
            *len = 2;
        } else if (c1 == '=') {
            *kind = ATK_AMPEQ;
            *len = 2;
        } else {
            *kind = ATK_AMP;
            *len = 1;
        }
        return true;
    case '|':
        if (c1 == '|') {
            *kind = ATK_OR;
            *len = 2;
        } else if (c1 == '=') {
            *kind = ATK_PIPEEQ;
            *len = 2;
        } else {
            *kind = ATK_PIPE;
            *len = 1;
        }
        return true;
    default:
        return false;
    }
}

/// Shorthand for an out-of-memory lexer failure at a byte offset.
#define LEX_OOM(diag, pos)                                                     \
    lex_fail((diag), SHELL_ERR_OUT_OF_MEMORY,                                  \
             "the expression is too large to allocate", (pos), 1,              \
             "out of memory tokenizing the arithmetic expression")

bool arith_lex(const char *expr, arith_token_t **out_tokens, size_t *out_count,
               arith_diag_t *diag, bool octal_zeroes) {
    if (!expr || !out_tokens || !out_count) {
        return lex_fail(diag, SHELL_ERR_ARITHMETIC_SYNTAX,
                        "this is an internal error; please report it", 0, 1,
                        "internal: null lexer argument");
    }

    arith_token_t *tokens = NULL;
    size_t count = 0;
    size_t cap = 0;

    const char *base = expr;
    const char *p = expr;

    while (*p) {
        /// Skip whitespace (spaces, tabs, and newlines from multi-line input).
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' ||
            *p == '\f') {
            p++;
            continue;
        }

        size_t pos = (size_t)(p - base);
        arith_token_t tok = {
            .kind = ATK_EOF, .num = 0, .text = NULL, .pos = pos, .len = 1};

        if (isdigit((unsigned char)*p)) {
            /// Integer literal: base 0 handles 0x hex, 0 octal, else decimal --
            /// one convention shared with scalar variable reads (#578), so a
            /// literal and a variable holding the same text always agree.
            char *end = NULL;
            long value = arith_strtol_based(p, octal_zeroes, &end);
            if (end == p) {
                arith_tokens_free(tokens, count);
                return lex_fail(
                    diag, SHELL_ERR_ARITHMETIC_SYNTAX,
                    "expected a decimal, 0x hex, or 0 octal literal", pos, 1,
                    "malformed numeric literal");
            }
            /// A digit strtol refused means the run is not valid in the base
            /// its own prefix selected: a leading 0 makes the literal octal,
            /// and 9 is not an octal digit.
            ///
            /// Curated decision (#594, owner call): a leading zero DENOTES
            /// OCTAL in lush, keeping literals and #578 scalar reads unified
            /// under one base rule. What lush adds is the diagnostic -- the
            /// offending digit is named and the way out is stated, rather than
            /// the run being silently truncated to 0 (the old behavior, which
            /// quietly produced a wrong number) or reported as an opaque
            /// parse failure. The decimal reading stays available as a future
            /// opt-in; zsh already names that knob `octal_zeroes`.
            if (isdigit((unsigned char)*end) && octal_zeroes && p[0] == '0') {
                char bad = *end;
                const char *run = end;
                while (isdigit((unsigned char)*run)) {
                    run++;
                }
                char msg[64];
                snprintf(msg, sizeof(msg), "invalid octal digit '%c'", bad);
                arith_tokens_free(tokens, count);
                return lex_fail(diag, SHELL_ERR_ARITHMETIC_SYNTAX,
                                "a leading 0 means octal, so digits must be "
                                "0-7; drop the 0 for decimal or use 0x for hex",
                                pos, (size_t)(run - p), msg);
            }
            tok.kind = ATK_NUM;
            tok.num = value;
            tok.len = (size_t)(end - p);
            if (!push_token(&tokens, &count, &cap, tok)) {
                arith_tokens_free(tokens, count);
                return LEX_OOM(diag, pos);
            }
            p = end;
            continue;
        }

        size_t id_len = lush_ident_match_start(p, strlen(p));
        if (id_len > 0) {
            /// Identifier: consume the start plus any continuation bytes.
            const char *id_end = p + id_len;
            size_t rem = strlen(id_end);
            while (rem > 0) {
                size_t n = lush_ident_match_continue(id_end, rem);
                if (n == 0) {
                    break;
                }
                id_end += n;
                rem -= n;
            }
            size_t name_len = (size_t)(id_end - p);
            char *name = malloc(name_len + 1);
            if (!name) {
                arith_tokens_free(tokens, count);
                return LEX_OOM(diag, pos);
            }
            memcpy(name, p, name_len);
            name[name_len] = '\0';
            tok.kind = ATK_IDENT;
            tok.text = name;
            tok.len = name_len;
            if (!push_token(&tokens, &count, &cap, tok)) {
                free(name);
                arith_tokens_free(tokens, count);
                return LEX_OOM(diag, pos);
            }
            p = id_end;
            continue;
        }

        /// Array subscript: capture the whole `[...]` interior RAW, without
        /// lexing it (issue #615). An associative-array key may contain any
        /// characters (m[foo.bar], m[a@b], m[a b]); the evaluator uses this
        /// text as a literal key for an associative array and re-lexes it as an
        /// arithmetic expression for an indexed array. Bracket depth is tracked
        /// so a nested subscript (a[b[1]]) captures correctly. In arithmetic a
        /// '[' only ever opens a subscript, so this is unconditional.
        if (*p == '[') {
            const char *interior = p + 1;
            const char *q = interior;
            int depth = 1;
            while (*q) {
                if (*q == '[') {
                    depth++;
                } else if (*q == ']') {
                    depth--;
                    if (depth == 0) {
                        break;
                    }
                }
                q++;
            }
            if (depth != 0) {
                arith_tokens_free(tokens, count);
                return lex_fail(diag, SHELL_ERR_ARITHMETIC_SYNTAX,
                                "every '[' needs a matching ']'", pos, 1,
                                "unmatched '['");
            }
            size_t raw_len = (size_t)(q - interior);
            char *raw = malloc(raw_len + 1);
            if (!raw) {
                arith_tokens_free(tokens, count);
                return LEX_OOM(diag, pos);
            }
            memcpy(raw, interior, raw_len);
            raw[raw_len] = '\0';
            tok.kind = ATK_SUBSCRIPT;
            tok.text = raw;
            tok.len = (size_t)(q - p + 1); /// span of the whole [...]
            if (!push_token(&tokens, &count, &cap, tok)) {
                free(raw);
                arith_tokens_free(tokens, count);
                return LEX_OOM(diag, pos);
            }
            p = q + 1; /// past ']'
            continue;
        }

        arith_token_kind_t op_kind;
        size_t op_len;
        if (lex_operator(p, &op_kind, &op_len)) {
            tok.kind = op_kind;
            tok.len = op_len;
            if (!push_token(&tokens, &count, &cap, tok)) {
                arith_tokens_free(tokens, count);
                return LEX_OOM(diag, pos);
            }
            p += op_len;
            continue;
        }

        /// Unrecognized byte: name it in the message for an actionable caret.
        char msg[64];
        unsigned char bad = (unsigned char)*p;
        if (bad >= 0x20 && bad < 0x7f) {
            snprintf(msg, sizeof(msg), "unexpected character '%c'", bad);
        } else {
            snprintf(msg, sizeof(msg), "unexpected byte 0x%02x", bad);
        }
        arith_tokens_free(tokens, count);
        return lex_fail(diag, SHELL_ERR_ARITHMETIC_SYNTAX,
                        "remove or correct the character; arithmetic accepts "
                        "numbers, variable names, and operators",
                        pos, 1, msg);
    }

    /// Terminating EOF token.
    arith_token_t eof = {.kind = ATK_EOF,
                         .num = 0,
                         .text = NULL,
                         .pos = (size_t)(p - base),
                         .len = 0};
    if (!push_token(&tokens, &count, &cap, eof)) {
        arith_tokens_free(tokens, count);
        return LEX_OOM(diag, 0);
    }

    *out_tokens = tokens;
    *out_count = count;
    return true;
}
