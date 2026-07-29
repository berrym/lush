/**
 * @file word_eval.h
 * @brief word_eval: evaluate a Word CST (word_t) to argv fields.
 *
 * The evaluator half of the Word CST bench. It turns a word_t into the argv
 * fields the word expands to, reproducing current lush exactly by composing the
 * shared field_split primitives (ifs_field_split / argv_append_word) -- the
 * same code build_argv uses, so field splitting and null-word removal cannot
 * drift.
 *
 * See docs/development/WORD_CST_PLAN.md section 5. Bench-only in Step 1c; wired
 * into the live executor at the Phase 2 cutover.
 *
 * STEP 1c-1 SCOPE: the lush-mode (no word-split) covered subset -- literals,
 * single/double quotes, simple $name (identifier) references, and adjacency
 * fusion. In lush mode no covered word splits, so each word yields 0 fields
 * (an unquoted expansion that produced empty -- null-word removal) or 1 field.
 * word_eval sets *out_ok = false (not-yet-covered) for anything outside that:
 * an active unquoted glob metacharacter, a special/subscripted/operator param,
 * a vector form, or -- when word_split_default is set (bash mode) -- an
 * unquoted parameter that would need IFS splitting (the split path is Step
 * 1c-1b). It never fabricates a wrong field vector; the differential harness
 * classifies a not-ok word as not-yet-covered, distinct from a parity bug.
 */
#ifndef LUSH_WORD_EVAL_H
#define LUSH_WORD_EVAL_H

#include <stdbool.h>

#include "word.h"

/**
 * @brief The environment word_eval expands against.
 *
 * `get` resolves a parameter name to an OWNED value string (word_eval frees it
 * after copying), or NULL if unset. Owned so the live executor can source
 * values from the symtable (which returns owned copies) without a leak; bench
 * callbacks wrap getenv/a map in strdup to satisfy the contract.
 */
typedef struct {
    char *(*get)(void *ctx,
                 const char *name); ///< $name -> owned value or NULL.
    /// Optional: true iff `$name` would expand in scalar context (a scalar or
    /// unset). Returns false for a list/map, whose bare `$name` reference
    /// yields a VECTOR this single-field slice cannot represent -- word_eval
    /// then defers (not-yet-covered) rather than fabricate one wrong field.
    /// NULL => assume scalar (bench callbacks source only scalars via getenv/a
    /// map).
    bool (*is_scalar)(void *ctx, const char *name);
    void *ctx;               ///< Passed to get()/is_scalar().
    const char *ifs;         ///< IFS, or NULL for the default " \t\n".
    bool word_split_default; ///< FEATURE_WORD_SPLIT_DEFAULT (lush false, bash
                             ///< true).
    bool zsh_extended_glob;  ///< FEATURE_ZSH_EXTENDED_GLOB: `#`/`^` are glob
                             ///< metacharacters, so a bare literal carrying one
                             ///< defers (not-yet-covered). Off by default.
} word_eval_env_t;

/**
 * @brief Evaluate @p w to a NULL-terminated argv field vector.
 *
 * @param w        The word.
 * @param env      The expansion environment.
 * @param nfields  Output: number of fields (0 on a null-word-removed word).
 * @param out_ok   Output: true iff the word was fully evaluated within the
 *                 current slice's coverage; false = not-yet-covered or an
 *                 allocation failure (in both cases the return is NULL).
 * @return A malloc'd `char*[nfields+1]` (NULL-terminated), each field owned, on
 *         success; NULL when *out_ok is false.
 */
char **word_eval(const word_t *w, const word_eval_env_t *env, int *nfields,
                 bool *out_ok);

#endif /* LUSH_WORD_EVAL_H */
