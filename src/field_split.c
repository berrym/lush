/**
 * @file field_split.c
 * @brief POSIX IFS field splitting + argv accumulation with null-word removal.
 *
 * See field_split.h. Extracted verbatim from executor.c so the live executor
 * and the Word CST evaluator share one implementation. Depends only on libc.
 */
#include "field_split.h"

#include <stdlib.h>
#include <string.h>

int add_to_argv_list(char ***argv_list, int *argv_count, int *argv_capacity,
                     char *arg) {
    if (*argv_count >= *argv_capacity) {
        *argv_capacity = *argv_capacity ? *argv_capacity * 2 : 8;
        char **new_list = realloc(*argv_list, *argv_capacity * sizeof(char *));
        if (!new_list) {
            return 0; /// Failed to expand
        }
        *argv_list = new_list;
    }
    (*argv_list)[(*argv_count)++] = arg;
    return 1;
}

/// True if `c` is an IFS character that is white space (space, tab, newline).
bool ifs_char_is_white(char c, const char *ifs) {
    return strchr(ifs, c) != NULL && (c == ' ' || c == '\t' || c == '\n');
}

/// POSIX field splitting (2.6.5), two-class rule. IFS partitions into white
/// space (space/tab/newline that appear in IFS) and non-white-space
/// delimiters. Leading and trailing IFS white space is discarded; a run of
/// IFS white space delimits one field. Each IFS non-white-space character
/// delimits a field on its own, together with any adjacent IFS white space,
/// so adjacent non-white-space delimiters -- or one at the start -- yield
/// empty fields (`a::b` -> a, "", b; `:a` -> "", a). A trailing
/// non-white-space delimiter does NOT add a trailing empty field, matching
/// bash/dash field splitting (`a:b:` -> a, b); the `read` builtin keeps its
/// trailing field via its own n-variable splitter.
char **ifs_field_split(const char *text, const char *ifs, int *count) {
    if (!text || !count) {
        if (count) {
            *count = 0;
        }
        return NULL;
    }

    /// Default IFS if not provided
    if (!ifs) {
        ifs = " \t\n";
    }

    *count = 0;
    char **result = NULL;
    int capacity = 0;

    const char *p = text;

    /// Discard leading IFS white space (a leading non-white-space delimiter is
    /// kept so it can produce a leading empty field).
    while (*p && ifs_char_is_white(*p, ifs)) {
        p++;
    }

    while (*p) {
        const char *field_start = p;
        while (*p && !strchr(ifs, *p)) {
            p++;
        }
        size_t field_len = (size_t)(p - field_start);

        if (*count >= capacity) {
            capacity = capacity ? capacity * 2 : 4;
            char **new_result = realloc(result, capacity * sizeof(char *));
            if (!new_result) {
                for (int i = 0; i < *count; i++) {
                    free(result[i]);
                }
                free(result);
                *count = 0;
                return NULL;
            }
            result = new_result;
        }
        result[*count] = malloc(field_len + 1);
        if (!result[*count]) {
            for (int i = 0; i < *count; i++) {
                free(result[i]);
            }
            free(result);
            *count = 0;
            return NULL;
        }
        memcpy(result[*count], field_start, field_len);
        result[*count][field_len] = '\0';
        (*count)++;

        if (!*p) {
            break;
        }

        /// Consume one delimiter: an IFS white-space run, at most one IFS
        /// non-white-space character, and a trailing IFS white-space run. This
        /// collapses `<ws> : <ws>` into a single delimiter while leaving each
        /// bare `:` as its own delimiter (so `::` yields an empty field).
        while (*p && ifs_char_is_white(*p, ifs)) {
            p++;
        }
        if (*p && strchr(ifs, *p)) { /// a non-white-space IFS char
            p++;
        }
        while (*p && ifs_char_is_white(*p, ifs)) {
            p++;
        }
    }

    return result;
}

/// Append a finished, fully-expanded scalar word to an argv-style list,
/// applying POSIX null-word removal: an unquoted expansion that produced the
/// empty string contributes ZERO words (so `$x` with x="" is a null command or
/// contributes no argument), while a quoted empty (`"$x"`, `''`) stays one
/// empty word. The predicate keys strictly on the empty string, never on
/// whitespace, so lush's no-word-split curation (`x="a b c"; cmd $x` -> one
/// word) is preserved. Takes ownership of `word`: it is freed whether the word
/// is added or dropped. Returns false only on allocation failure (word already
/// freed).
bool argv_append_word(char ***list, int *count, int *capacity, char *word,
                      bool quoted) {
    if (word && word[0] == '\0' && !quoted) {
        free(word); /// null-word removal: contributes zero words
        return true;
    }
    if (!add_to_argv_list(list, count, capacity, word)) {
        free(word);
        return false;
    }
    return true;
}
