/**
 * @file word.h
 * @brief The Word CST: a structured, lossless representation of a shell word.
 *
 * A word is a typed tree of parts, and THE STRUCTURE IS THE PROVENANCE. A
 * `$var` written under a double-quoted part is word-split- and glob-suppressed
 * by construction, and stays that way through expansion, because each part
 * evaluates in its own context. There is no parallel per-byte provenance map,
 * no strlen invariant, no threaded in_double_quotes bool, no side-channel node
 * field. A byte map cannot survive expansion; a tree can.
 *
 * See docs/development/WORD_CST_PLAN.md for the full architecture (v2.1). This
 * header is the Phase 2 / Step 1 foundation: the type, the
 * single-responsibility ownership API (word_copy / word_free), and lossless
 * source-span reconstruction. The parser (word_parse.c) and evaluator
 * (word_eval.c) build on this type; they are not wired into the live shell
 * until the bench proves byte-parity.
 *
 * Ownership is Option A: dedicated types with typed C pointers and one
 * recursive copy/free. word_part_copy / word_part_free are switch-on-kind with
 * no default, each wrapped in a `#pragma GCC diagnostic error` region (in
 * word.c) that promotes both -Wswitch and -Wswitch-enum to errors, so a new
 * WP_* kind (or a later `default:` that would mask a missing case) fails the
 * build on both clang and gcc until every kind is handled -- the field-omission
 * bug class (the pre-#657 node loc drop, #488, #498) caught by the compiler
 * rather than by discipline. All child traversal goes through the accessor
 * macros below so a later migration to arena + relative offsets is a localized
 * change (see WORD_CST_PLAN.md section 1.6).
 */
#ifndef LUSH_WORD_H
#define LUSH_WORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct word word_t;
typedef struct word_part word_part_t;

/**
 * @brief The kind of a word part.
 *
 * Structural kinds carry EVALUATION CONTEXT ONLY -- the quoting/split/glob
 * eligibility a parent imposes on its children. Everything orthogonal (an
 * operator's identity, a tilde's user, glob metacharacters, zsh flags) is
 * scalar data riding on a part, never a kind. That is why ~39 syntactic
 * variants collapse onto these 15 kinds.
 */
typedef enum {
    /// Text leaves -- hold BOTH the evaluated bytes and the source span.
    WP_LITERAL, ///< Literal run: text = final bytes, no expansion.
    WP_SINGLE,  ///< '...' interior: dequoted, fully literal.
    WP_ANSIC,   ///< $'...' interior: C-escape-decoded.

    /// Single-body groups -- hold ONE child word_t (their parts sequence).
    WP_DOUBLE, ///< "..." : imposes double-quote context on the body.
    WP_BARE,   ///< Unquoted run : imposes unquoted context (split + glob).

    /// Multi-body groups -- hold a VECTOR of child word_t.
    WP_BRACE,     ///< {a,b} {1..n} : items = alternative Words.
    WP_ARRAY_LIT, ///< =( ... ) : items = element Words (retires the \x1F
                  ///< sentinel).

    /// Parameter expansion -- one kind; op/subscript/operands are typed fields.
    WP_PARAM, ///< $name / ${...} : name + op code + operand/subscript Words.

    /// Deferred-body leaves -- body kept as raw text, re-parsed at eval time.
    WP_CMDSUB,   ///< $(...)
    WP_BACKTICK, ///< `...` : distinct backslash rules -> distinct kind.
    WP_ARITH,    ///< $((expr))

    /// Other leaves.
    WP_TILDE,       ///< ~ / ~user
    WP_PROCSUB_IN,  ///< <(...)
    WP_PROCSUB_OUT, ///< >(...)
    WP_KINDSIGIL,   ///< @name (list) / %name (pair)
} word_part_kind_t;

/**
 * @brief A single lexical fragment inside a word.
 *
 * A tagged discriminated union: `kind` selects the active `u` arm. Every part
 * carries a source span (src_off, src_len) into the original input so the tree
 * is a lossless CST -- the evaluator reads the evaluated bytes, while a
 * formatter / static analyzer / debugger / LSP reads the span to recover the
 * exact original spelling.
 */
struct word_part {
    word_part_kind_t kind; ///< Discriminant.
    uint32_t src_off;      ///< Byte offset of this part in the original input.
    uint32_t src_len;      ///< Byte length of this part in the original input.

    union {
        /// WP_LITERAL / WP_SINGLE / WP_ANSIC
        struct {
            char *text;        ///< Evaluated bytes (owner).
            bool literal_meta; ///< Glob metacharacters in text are literal
                               ///< (quoted/escaped origin); replaces the
                               ///< per-byte U/E provenance map.
        } leaf;

        /// WP_DOUBLE / WP_BARE : single interior parts-sequence.
        struct {
            word_t *body; ///< Child parts vector (owner).
        } group;

        /// WP_BRACE / WP_ARRAY_LIT : multiple sub-Words.
        struct {
            word_t **items;   ///< Array of owned word_t* (owner).
            uint32_t n_items; ///< Item count.
        } multi;

        /// WP_PARAM : the only compound leaf.
        struct {
            char *name;     ///< Parameter name (owner): "arr", "@", "1".
            int32_t op;     ///< Index into param_operators[], or -1 for a
                            ///< bare reference.
            uint32_t flags; ///< zsh flag bits (orthogonal data, not context).
            word_t *subscript; ///< [key] as a child Word, or NULL.
            word_t *operand;   ///< :- / # / % / /pat operand Word, or NULL.
            word_t *operand2;  ///< Replacement Word for ${v/p/r}, or NULL.
        } param;

        /// WP_CMDSUB / WP_BACKTICK / WP_ARITH
        struct {
            char *body; ///< Raw body / expression text (owner).
        } defer;

        /// WP_TILDE
        struct {
            char *user; ///< "" or NULL = $HOME; else target user (owner).
        } tilde;

        /// WP_PROCSUB_IN / WP_PROCSUB_OUT
        struct {
            char *body; ///< Raw command text (owner).
        } procsub;

        /// WP_KINDSIGIL
        struct {
            char *name; ///< Symbol name (owner).
            char sigil; ///< '@' (list) or '%' (pair).
        } kindsigil;
    } u;
};

/**
 * @brief A word: an ordered vector of parts, plus a whole-word source span.
 *
 * The container the parser returns, the executor consumes, and a node_t
 * references. It imposes no quoting context of its own -- context lives in the
 * parts. `key` is the optional key Word of a keyed array/assoc element and is
 * NULL for every other word (richness pushed below the node<->word seam so the
 * seam stays uniform; see WORD_CST_PLAN.md section 3.4).
 */
struct word {
    word_part_t **parts; ///< Ordered parts vector (owner).
    uint32_t n_parts;    ///< Number of parts in use.
    uint32_t cap_parts;  ///< Allocated capacity of `parts`.
    uint32_t src_off;    ///< Whole-word source span offset.
    uint32_t src_len;    ///< Whole-word source span length.
    word_t *key;         ///< Keyed-element key Word, else NULL (owner).
};

/// Child-traversal accessors. All Word-CST code reaches children through these
/// (never a raw field deref) so a later pointer->offset (arena) migration is a
/// localized edit to this header. See WORD_CST_PLAN.md section 1.6.
#define WORD_PART(w, i) ((w)->parts[(i)])
#define PART_BODY(p) ((p)->u.group.body)
#define PART_ITEM(p, i) ((p)->u.multi.items[(i)])
#define PART_OPERAND(p) ((p)->u.param.operand)
#define PART_OPERAND2(p) ((p)->u.param.operand2)
#define PART_SUBSCRIPT(p) ((p)->u.param.subscript)

/// Ownership API (Option A: typed pointers, one recursive copy/free).

/**
 * @brief Allocate an empty word spanning [src_off, src_off + src_len).
 * @return New word (owner), or NULL on allocation failure.
 */
word_t *word_new(uint32_t src_off, uint32_t src_len);

/**
 * @brief Allocate a part of the given kind spanning [src_off, src_off +
 * src_len).
 *
 * The union is zero-initialized; the caller populates the active arm. Text and
 * child-Word fields start NULL, so word_part_free is safe on a partial part.
 * @return New part (owner), or NULL on allocation failure.
 */
word_part_t *word_part_new(word_part_kind_t kind, uint32_t src_off,
                           uint32_t src_len);

/**
 * @brief Append a part to a word's parts vector (takes ownership of `p`).
 * @return true on success; false on allocation failure (p is left owned by
 *         the caller, who should word_part_free it).
 */
bool word_add_part(word_t *w, word_part_t *p);

/**
 * @brief Set the interior body of a WP_DOUBLE / WP_BARE group (takes
 * ownership).
 */
void word_set_body(word_part_t *group, word_t *body);

/**
 * @brief Append a sub-Word to a WP_BRACE / WP_ARRAY_LIT group (takes
 * ownership).
 * @return true on success; false on allocation failure (item left to caller).
 */
bool word_add_item(word_part_t *multi, word_t *item);

/**
 * @brief Deep-copy a word and everything it owns (the single recursive copier).
 * @return New independent word (owner), or NULL (on NULL input or allocation
 *         failure).
 */
word_t *word_copy(const word_t *w);

/**
 * @brief Free a word and everything it owns (the single recursive freer).
 *        Safe on NULL.
 */
void word_free(word_t *w);

/// Lossless CST support.

/**
 * @brief Reconstruct the original source of a word from its parts' spans.
 *
 * Copies the [src_off, src_off + src_len) slice of `src` covered by the word.
 * Because every part records its exact source span, this is a lossless
 * round-trip: for a word parsed from `src`, the result equals the original
 * spelling (quotes and all). Used by the bench's --reconstruct mode to prove
 * span coverage, and the substrate a future formatter / LSP builds on.
 *
 * @param w      The word.
 * @param src    The original input the word was parsed from.
 * @param srclen Length of @p src; the word's span is validated against it, so
 *               the call is total even if an out-of-contract span drifts past
 *               the buffer (returns NULL rather than reading out of bounds).
 * @return Newly-allocated source slice (owner), or NULL on failure / bad span.
 */
char *word_reconstruct(const word_t *w, const char *src, size_t srclen);

#endif /* LUSH_WORD_H */
