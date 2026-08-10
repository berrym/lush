/**
 * @file wordtool.c
 * @brief Bench CLI for the Word CST: a raw word -> its expanded argv fields.
 *
 * Runs the same code the live parser eventually will -- tokenizer_new ->
 * parse_word -> word_eval -- over a single word given as argv[1], resolving
 * parameters from the process environment (shared with the differential
 * harness's oracle subprocesses). It is the candidate side of the four-way
 * differential (word_diff): its output is compared byte-for-byte against
 * current lush.
 *
 * Output format (matches the harness's `set -- W; printf '%d\n' "$#"; printf
 * '%s\n' "$@"` oracle): line 1 is the field count, then one line per field.
 * This distinguishes a null-word-removed word (count 0) from a quoted empty
 * (count 1, one empty line) without relying on NUL emission across shells.
 *
 * Exit status: 0 = covered (fields printed); 2 = not-yet-covered (parse_word or
 * word_eval reported the word outside the current slice's coverage -- the
 * harness buckets it, not a bug); 1 = usage/allocation error.
 *
 * A --reconstruct mode re-emits the word's source from its spans (lossless-CST
 * check) instead of evaluating.
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokenizer.h"
#include "word.h"
#include "word_eval.h"
#include "word_parse.h"

static char *env_get(void *ctx, const char *name) {
    (void)ctx;
    const char *v = getenv(name);
    return v ? strdup(v) : NULL; /// owned per the word_eval get contract
}

/// Bench apply_op: replicates the four alternation operators so wordtool
/// evaluates ${var:-x} etc. against the process environment. Mirrors the
/// executor's apply_param_operator for op 0/1/10/11.
static char *bench_apply_op(void *ctx, const char *name, const char *value,
                            const char *deflt, int op) {
    (void)ctx;
    (void)name;
    bool empty = !value || value[0] == '\0';
    const char *d = deflt ? deflt : "";
    switch (op) {
    case 0: /// :- use default if unset or empty
        return strdup(empty ? d : value);
    case 1: /// :+ use alternative if set and non-empty
        return strdup(!empty ? d : "");
    case 10: /// - use default if unset (NULL), not if empty
        return strdup(!value ? d : value);
    case 11: /// + use alternative if set (even if empty)
        return strdup(value ? d : "");
    case 2: /// ## remove longest prefix (bench: LITERAL pattern only)
    case 6: /// #  remove shortest prefix
    {
        size_t pl = strlen(d);
        if (value && pl && strncmp(value, d, pl) == 0) {
            return strdup(value + pl);
        }
        return strdup(value ? value : "");
    }
    case 3: /// %% remove longest suffix
    case 7: /// %  remove shortest suffix
    {
        size_t vl = value ? strlen(value) : 0;
        size_t pl = strlen(d);
        if (value && pl && pl <= vl && strcmp(value + vl - pl, d) == 0) {
            char *r = malloc(vl - pl + 1);
            if (r) {
                memcpy(r, value, vl - pl);
                r[vl - pl] = '\0';
            }
            return r ? r : strdup("");
        }
        return strdup(value ? value : "");
    }
    case 4: /// ^^ uppercase all   (bench: EMPTY pattern -> convert all;
    case 8: /// ^  uppercase first  pattern-restricted cases run live)
    case 5: /// ,, lowercase all
    case 9: /// ,  lowercase first
    {
        char *r = strdup(value ? value : "");
        if (!r) {
            return NULL;
        }
        bool upper = (op == 4 || op == 8);
        bool first_only = (op == 8 || op == 9);
        for (char *c = r; *c; c++) {
            *c = upper ? (char)toupper((unsigned char)*c)
                       : (char)tolower((unsigned char)*c);
            if (first_only) {
                break;
            }
        }
        return r;
    }
    case 14: /// ${var:off:len} substring (bench: ASCII byte slice of the simple
    {        /// non-negative spec; UTF-8/grapheme parity runs live)
        const char *v = value ? value : "";
        char *end;
        long off = strtol(d, &end, 10);
        long vlen = (long)strlen(v);
        if (off > vlen) {
            off = vlen;
        }
        long avail = vlen - off;
        long len = avail;
        if (*end == ':') {
            len = strtol(end + 1, NULL, 10);
            if (len > avail) {
                len = avail;
            }
            if (len < 0) {
                len = 0;
            }
        }
        char *r = malloc(len + 1);
        if (!r) {
            return NULL;
        }
        memcpy(r, v + off, len);
        r[len] = '\0';
        return r;
    }
    /// 15 = `//` replace all, 16 = `/` replace first. The bench substitutes a
    /// LITERAL pattern only -- it does NOT model glob patterns or the `#`/`%`
    /// anchors, which the live path matches through pattern_substitute. Those
    /// forms are covered by the CST, so a corpus line carrying one would make
    /// word_diff report a false BUG (bench != live); keep them out of
    /// tests/fuzz/word_corpus and exercise them in the live executor tests.
    case 15:
    case 16: {
        const char *v = value ? value : "";
        bool global = (op == 15);
        const char *sep = strchr(d, '/');
        size_t patlen = sep ? (size_t)(sep - d) : strlen(d);
        const char *repl = sep ? sep + 1 : "";
        size_t rlen = strlen(repl);
        size_t vlen = strlen(v);
        if (patlen == 0) {
            return strdup(v); /// empty pattern -> unchanged (matches legacy)
        }
        char *out = malloc(1);
        if (!out) {
            return NULL;
        }
        size_t olen = 0;
        out[0] = '\0';
        bool replaced = false;
        for (size_t i = 0; i < vlen;) {
            if ((global || !replaced) && i + patlen <= vlen &&
                memcmp(v + i, d, patlen) == 0) {
                char *grown = realloc(out, olen + rlen + 1);
                if (!grown) {
                    free(out);
                    return NULL;
                }
                out = grown;
                memcpy(out + olen, repl, rlen);
                olen += rlen;
                i += patlen;
                replaced = true;
            } else {
                char *grown = realloc(out, olen + 2);
                if (!grown) {
                    free(out);
                    return NULL;
                }
                out = grown;
                out[olen++] = v[i++];
            }
            out[olen] = '\0';
        }
        return out;
    }
    default:
        return strdup("");
    }
}

int main(int argc, char **argv) {
    bool reconstruct = false;
    const char *word = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--reconstruct") == 0) {
            reconstruct = true;
        } else {
            word = argv[i];
        }
    }
    if (!word) {
        fprintf(stderr, "usage: wordtool [--reconstruct] WORD\n");
        return 1;
    }

    tokenizer_t *tok = tokenizer_new(word);
    bool fully = false;
    word_t *w = parse_word(tok, WORD_CTX_ARG, &fully);
    int rc = 0;

    if (!w) {
        rc = 1; /// not a word / allocation failure
        goto done;
    }
    if (!fully) {
        rc = 2; /// not-yet-covered by parse_word
        goto done;
    }
    /// The input must be exactly ONE fully-consumed word. If parse_word left
    /// trailing tokens (a second word, a redirection, an operator), wordtool
    /// evaluated only the first word -- report not-covered so the differential
    /// harness cannot mistake a partially-consumed line for a proven word.
    token_t *rest = tokenizer_current(tok);
    if (rest && rest->type != TOK_EOF) {
        rc = 2;
        goto done;
    }

    if (reconstruct) {
        char *src = word_reconstruct(w, word, strlen(word));
        if (!src) {
            rc = 1;
            goto done;
        }
        fputs(src, stdout);
        free(src);
        goto done;
    }

    word_eval_env_t env = {.get = env_get,
                           .apply_op = bench_apply_op,
                           .ctx = NULL,
                           .ifs = getenv("IFS"),
                           .word_split_default = false,
                           .ansi_c_quoting = true};
    int n = 0;
    bool ok = false;
    char **fields = word_eval(w, &env, &n, &ok);
    if (!ok) {
        rc = 2; /// not-yet-covered by word_eval
        goto done;
    }
    printf("%d\n", n);
    for (int i = 0; i < n; i++) {
        printf("%s\n", fields[i]);
        free(fields[i]);
    }
    free(fields);

done:
    word_free(w);
    tokenizer_free(tok);
    return rc;
}
