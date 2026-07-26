/**
 * @file subscript_key.c
 * @brief Canonicalize an array subscript's raw interior to its key.
 *
 * See subscript_key.h for the contract. The span of the `[...]` is found by
 * scan_subscript_bounds (brace_match.c); this file turns the raw interior into
 * the canonical associative key by removing one level of quoting/escaping and
 * performing `$`-expansion (delegated to expand_variables_in_string so the
 * `$`-semantics stay identical to the rest of the engine).
 */
#include "subscript_key.h"

#include <stdlib.h>
#include <string.h>

#include "brace_match.h"
#include "identifier.h"

/// Grow *buf so it can hold at least `need` bytes.
static bool sk_reserve(char **buf, size_t *cap, size_t need) {
    if (*cap >= need) {
        return true;
    }
    size_t nc = *cap ? *cap : 32;
    while (nc < need) {
        nc *= 2;
    }
    char *nb = realloc(*buf, nc);
    if (!nb) {
        return false;
    }
    *buf = nb;
    *cap = nc;
    return true;
}

/// Append `n` bytes of `src` to the NUL-terminated builder (*buf,*len,*cap).
static bool sk_append(char **buf, size_t *len, size_t *cap, const char *src,
                      size_t n) {
    if (!sk_reserve(buf, cap, *len + n + 1)) {
        return false;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return true;
}

/// Byte length of the `$`-unit that starts at s[i] (which must be '$'). Always
/// >= 1. Handles ${...} / $(...) / $((...)) via the canonical brace matcher,
/// the single-character special params, and a UTF-8 `$name`.
static size_t dollar_unit_len(const char *s, size_t i, size_t len) {
    size_t rem = len - i;
    if (rem < 2) {
        return 1; /// a lone trailing '$' is literal
    }
    char c = s[i + 1];
    if (c == '{' || c == '(') {
        /// Anchor the matcher at the '{' / '(' just past the '$'. For $(( ))
        /// the matcher anchored at the first '(' returns the outer ')', so the
        /// whole arithmetic form is captured.
        size_t off = 0;
        if (lush_find_matching_brace(s + i + 1, rem - 1, &off)) {
            return (i + 1 + off + 1) - i; /// '$' + brace group incl. its close
        }
        return rem; /// unterminated: consume the remainder
    }
    if (c == '?' || c == '$' || c == '!' || c == '#' || c == '@' || c == '*' ||
        c == '-' || (c >= '0' && c <= '9')) {
        return 2; /// single-character special parameter
    }
    size_t start = lush_ident_match_start(s + i + 1, rem - 1);
    if (start == 0) {
        return 1; /// '$' not introducing a name -- literal '$'
    }
    size_t n = start;
    while (i + 1 + n < len) {
        size_t k = lush_ident_match_continue(s + i + 1 + n, len - (i + 1 + n));
        if (k == 0) {
            break;
        }
        n += k;
    }
    return 1 + n; /// '$' + name
}

/// Expand a single `$`-unit [unit, unit+ulen) and append the result.
static bool append_expanded(executor_t *executor, char **out, size_t *olen,
                            size_t *ocap, const char *unit, size_t ulen) {
    char *piece = malloc(ulen + 1);
    if (!piece) {
        return false;
    }
    memcpy(piece, unit, ulen);
    piece[ulen] = '\0';
    char *expanded = expand_variables_in_string(executor, piece);
    free(piece);
    if (!expanded) {
        return false;
    }
    bool ok = sk_append(out, olen, ocap, expanded, strlen(expanded));
    free(expanded);
    return ok;
}

char *subscript_normalize_key(executor_t *executor, const char *interior,
                              size_t len) {
    if (!executor || !interior) {
        return NULL;
    }

    char *out = NULL;
    size_t olen = 0, ocap = 0;
    if (!sk_reserve(&out, &ocap, 32)) {
        return NULL;
    }
    out[0] = '\0';

    size_t i = 0;
    while (i < len) {
        char c = interior[i];
        if (c == '\\' && i + 1 < len) {
            /// Unquoted `\X`: drop the backslash, keep X literally.
            if (!sk_append(&out, &olen, &ocap, &interior[i + 1], 1)) {
                goto oom;
            }
            i += 2;
        } else if (c == '\'') {
            /// Single-quoted: literal, no `$`-expansion.
            size_t j = i + 1;
            while (j < len && interior[j] != '\'') {
                j++;
            }
            if (!sk_append(&out, &olen, &ocap, &interior[i + 1], j - (i + 1))) {
                goto oom;
            }
            i = (j < len) ? j + 1 : j; /// skip the closing quote if present
        } else if (c == '"') {
            /// Double-quoted: `$`-expansion; backslash special only before
            /// `"`, `\`, `$`, `` ` ``.
            size_t j = i + 1;
            while (j < len && interior[j] != '"') {
                if (interior[j] == '\\' && j + 1 < len &&
                    (interior[j + 1] == '"' || interior[j + 1] == '\\' ||
                     interior[j + 1] == '$' || interior[j + 1] == '`')) {
                    if (!sk_append(&out, &olen, &ocap, &interior[j + 1], 1)) {
                        goto oom;
                    }
                    j += 2;
                } else if (interior[j] == '$') {
                    size_t ul = dollar_unit_len(interior, j, len);
                    if (!append_expanded(executor, &out, &olen, &ocap,
                                         &interior[j], ul)) {
                        goto oom;
                    }
                    j += ul;
                } else {
                    if (!sk_append(&out, &olen, &ocap, &interior[j], 1)) {
                        goto oom;
                    }
                    j++;
                }
            }
            i = (j < len) ? j + 1 : j; /// skip the closing quote if present
        } else if (c == '$') {
            size_t ul = dollar_unit_len(interior, i, len);
            if (!append_expanded(executor, &out, &olen, &ocap, &interior[i],
                                 ul)) {
                goto oom;
            }
            i += ul;
        } else {
            if (!sk_append(&out, &olen, &ocap, &interior[i], 1)) {
                goto oom;
            }
            i++;
        }
    }
    return out;

oom:
    free(out);
    return NULL;
}
