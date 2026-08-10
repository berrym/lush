/**
 * @file param_op.h
 * @brief Pure parameter-expansion operator core, shared by the executor and
 *        the Word CST bench.
 *
 * The `${var OP operand}` operators split into two groups. Most of them are
 * PURE: given the variable's value and the already-expanded operand they
 * compute a result with no shell state involved -- alternation (`:-` `:+` `-`
 * `+`), pattern strip (`#` `##` `%` `%%`), case conversion (`^` `^^` `,` `,,`),
 * substring (`:off:len`), substitution (`/` `//`) and the value half of the
 * assign operators (`:=` `=`). The rest need the executor: the zsh modifier
 * chains on `:`, the `@` transforms, and the `:?` / `?` error operators.
 *
 * This module owns the pure group and the primitives it is built from, so
 * there is exactly ONE implementation of each. The executor's
 * apply_param_operator delegates here, and the Word CST bench (wordtool,
 * test_word_eval) calls lush_param_op_apply directly as its apply_op callback
 * instead of re-implementing the operators. That is what makes the bench a
 * faithful oracle: the differential harness compares wordtool against live
 * lush, so a hand-written bench copy would report false divergences for any
 * glob pattern, `#`/`%` anchor or multi-byte grapheme it did not model
 * (issue #681). Sharing the code the way field_split.c and escape.c are
 * shared removes the drift class instead of documenting it.
 *
 * The pattern matchers honor the active shell mode (extglob / zsh extended
 * glob) through lush_shell_pattern_match, so mode-gated behavior is shared
 * too rather than re-derived.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_PARAM_OP_H
#define LUSH_PARAM_OP_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief True when @p op_type is computed entirely by lush_param_op_apply.
 *
 * The impure operators -- 14 when it carries a zsh modifier chain or a
 * `$`-expanding spec, 17 (`@` transforms), 18/19 (`:?` / `?` errors) -- need
 * executor state and stay in the executor. Operator numbers are the indices
 * of param_operators[] in src/executor.c.
 */
bool lush_param_op_is_pure(int op_type);

/**
 * @brief Apply a pure parameter-expansion operator.
 *
 * @param op_type      Operator index (see lush_param_op_is_pure).
 * @param var_value    The variable's value, or NULL when it is UNSET. The
 *                     NULL-vs-empty distinction is significant: `-` / `+` / `=`
 *                     test set-ness, their `:` forms test set-and-non-empty.
 * @param operand      The already-expanded operand text (default value, glob
 *                     pattern, numeric spec, or `pattern/replacement` spec).
 *                     NULL is treated as empty.
 * @param assign_back  Optional out-flag, set true by `:=` / `=` when the
 *                     caller must write the result back to the lvalue. This
 *                     module never touches the symbol table itself.
 * @return Newly allocated result string for a pure operator (NULL only on
 *         allocation failure), or NULL when @p op_type is not pure. Callers
 *         that must tell the two apart should check lush_param_op_is_pure
 *         first; word_eval treats NULL as "defer to the legacy expander".
 */
char *lush_param_op_apply(int op_type, const char *var_value,
                          const char *operand, bool *assign_back);

/// Length in bytes of the shortest (longest == false) or longest prefix of
/// @p str matching glob @p pattern -- the `${var#p}` / `${var##p}` engine.
int lush_prefix_match_len(const char *str, const char *pattern, bool longest);

/// Length in bytes of the shortest or longest matching suffix -- the
/// `${var%p}` / `${var%%p}` engine.
int lush_suffix_match_len(const char *str, const char *pattern, bool longest);

/// Grapheme-aware `${var:offset:length}`. Negative offsets count from the end;
/// a negative length keeps everything but that many trailing graphemes.
char *lush_substring_extract(const char *str, int offset, int length,
                             bool has_length);

/// Grapheme-cluster slice: @p count graphemes starting at 0-based
/// @p start_grapheme (count < 0 means "to end").
char *lush_slice_graphemes(const char *str, size_t str_len, int start_grapheme,
                           int count);

/// UTF-8 aware case conversion of the whole string / the first character.
char *lush_case_all_upper(const char *str);
char *lush_case_all_lower(const char *str);
char *lush_case_first_upper(const char *str);
char *lush_case_first_lower(const char *str);
/// Capitalize the first letter of each word (the zsh `:C` modifier).
char *lush_case_capitalize_words(const char *str);
/// Pattern-restricted case modification: convert only the characters matching
/// @p pattern, either all of them or just the first.
char *lush_case_pattern(const char *str, const char *pattern, bool to_upper,
                        bool first_only);

/// True when @p pattern opens a bash extglob group -- used to decide whether a
/// substitution pattern may match beyond a single literal run.
bool lush_pattern_opens_extglob_group(const char *pattern);

/// Replace the first (global == false) or every occurrence of glob
/// @p pattern in @p str with @p replacement. Honors the `#` / `%` anchors.
char *lush_pattern_substitute(const char *str, const char *pattern,
                              const char *replacement, bool global);

#endif /* LUSH_PARAM_OP_H */
