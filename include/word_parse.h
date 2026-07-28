/**
 * @file word_parse.h
 * @brief parse_word: build a Word CST (word_t) from the tokenizer's stream.
 *
 * parse_word is THE single builder that turns a run of adjacent word tokens
 * into one word_t tree, replacing collect_word_argument and the ~8 collapse
 * builders. It takes the tokenizer_t* (the live token stream), not the
 * parser_t*: it needs only tokens plus the tokenizer's own lexer-feedback
 * toggles, never the parser's statement state -- so taking tokenizer_t* keeps
 * the bench free of the whole parser_t construction surface and makes "does not
 * take over statement parsing" a type-level guarantee. The bench (wordtool /
 * test_word_parse) runs the same code by constructing the same object:
 * tokenizer_new(line) -> parse_word(tok, ...).
 *
 * See docs/development/WORD_CST_PLAN.md sections 4 for the design. This is the
 * Step 1b build, developed incrementally on the bench, NOT wired into the live
 * parser until the Phase 2 cutover.
 *
 * STEP 1b-1 SCOPE (this slice): the fusion loop + the well-defined single-token
 * mappings: a bare TOK_WORD -> WP_BARE of literal leaves; a plain '...' string
 * -> WP_SINGLE; a "..." string with a uniform double-quote context -> WP_DOUBLE
 * whose interior holds literals and simple $var / ${name} expansions; a simple
 * TOK_VARIABLE ($name, a special like $@/$?, or ${name} with no operator) ->
 * WP_BARE(WP_PARAM); a TOK_NUMBER -> WP_BARE literal. Anything else -- ANSI-C
 * $'...', ${...} operators, subscripts, command/arith/backtick substitutions,
 * the tokenizer's pre-fused mixed-quote tokens, brace/array/procsub/sigil/tilde
 * -- is NOT YET COVERED: parse_word sets *out_fully_handled = false and does
 * not fabricate a best-effort mis-parse. The differential harness classifies a
 * not-fully-handled word as not-yet-covered (distinct from a parity bug).
 */
#ifndef LUSH_WORD_PARSE_H
#define LUSH_WORD_PARSE_H

#include <stdbool.h>

#include "tokenizer.h"
#include "word.h"

/**
 * @brief The context a word is parsed in, selecting downstream eval policy.
 *
 * Structure is context-independent (a word tree is the same shape regardless of
 * where it appears); ctx selects the return-shape / pattern policy the
 * evaluator applies later (Step 1c+). Step 1b-1 accepts it but does not branch
 * on it. More contexts (redir target, case pattern, ...) are added as their
 * slices land.
 */
typedef enum {
    WORD_CTX_ARG,  ///< Command argument / general word (field vector at eval).
    WORD_CTX_COND, ///< [[ ]] operand.
} word_ctx_t;

/**
 * @brief Parse one word from the tokenizer's current position.
 *
 * Consumes the maximal run of adjacent (no intervening whitespace) argument
 * word tokens starting at the tokenizer's current token, builds a word_t, and
 * leaves the tokenizer positioned on the first token that is not part of the
 * word (a whitespace-gapped or non-word token) -- the same post-condition
 * collect_word_argument provides today.
 *
 * @param tok               The tokenizer, positioned on the word's first token.
 * @param ctx               The parse context (see word_ctx_t).
 * @param out_fully_handled Optional; set to true iff every construct in the
 * word was fully structured by this slice, false if any part is not-yet-covered
 * (see the file header). May be NULL.
 * @return The word_t (owner; free with word_free), or NULL on allocation
 * failure or if the current token is not an argument word token at all.
 */
word_t *parse_word(tokenizer_t *tok, word_ctx_t ctx, bool *out_fully_handled);

#endif /* LUSH_WORD_PARSE_H */
