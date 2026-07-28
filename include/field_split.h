/**
 * @file field_split.h
 * @brief POSIX IFS field splitting and argv accumulation with null-word
 * removal.
 *
 * The self-contained field-splitting primitives, extracted from executor.c so
 * they are a single shared implementation rather than being duplicated. The
 * live executor (build_argv and the word-expansion pipeline) and the Word CST
 * evaluator (word_eval) both call these exact functions, so field splitting and
 * null-word removal cannot drift between the two. Same pattern as brace_match.c
 * and dequote.c. These depend on nothing but libc.
 */
#ifndef LUSH_FIELD_SPLIT_H
#define LUSH_FIELD_SPLIT_H

#include <stdbool.h>

/**
 * @brief Is @p c an IFS character that is white space (space, tab, newline)?
 *
 * White-space IFS characters have the collapsing two-class behaviour; a
 * non-white-space IFS character each delimits a field on its own.
 */
bool ifs_char_is_white(char c, const char *ifs);

/**
 * @brief Append @p arg (ownership transferred) to a growable argv list.
 *
 * Grows the list as needed. @p arg is stored as-is (no null-word filtering --
 * use argv_append_word for that).
 *
 * @return 1 on success, 0 on allocation failure (the list is unchanged).
 */
int add_to_argv_list(char ***argv_list, int *argv_count, int *argv_capacity,
                     char *arg);

/**
 * @brief Split @p text into fields on IFS (POSIX 2.6.5 two-class rule).
 *
 * IFS partitions into white space (space/tab/newline that appear in IFS) and
 * non-white-space delimiters. Leading and trailing IFS white space is
 * discarded; a run of IFS white space delimits one field. Each IFS
 * non-white-space character delimits a field on its own together with any
 * adjacent IFS white space, so adjacent non-white-space delimiters -- or one at
 * the start -- yield empty fields (`a::b` -> a, "", b; `:a` -> "", a). A
 * trailing non-white-space delimiter does NOT add a trailing empty field
 * (`a:b:` -> a, b).
 *
 * @param text  Text to split.
 * @param ifs   Field separators, or NULL for the default " \t\n".
 * @param count Output: number of fields produced.
 * @return Array of @p count owned field strings (caller frees each + the
 *         array), or NULL on error / bad args (with *count == 0).
 */
char **ifs_field_split(const char *text, const char *ifs, int *count);

/**
 * @brief Append an expanded word to an argv list, applying null-word removal.
 *
 * Takes ownership of @p word. An empty, unquoted word contributes zero fields
 * (it is freed and dropped); an empty quoted word (@p quoted true) stays one
 * empty field. Non-empty words are always appended.
 *
 * @return true on success; false on allocation failure (@p word is freed).
 */
bool argv_append_word(char ***list, int *count, int *capacity, char *word,
                      bool quoted);

#endif /* LUSH_FIELD_SPLIT_H */
