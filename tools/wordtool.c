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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokenizer.h"
#include "word.h"
#include "word_eval.h"
#include "word_parse.h"

static const char *env_get(void *ctx, const char *name) {
    (void)ctx;
    return getenv(name);
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
                           .ctx = NULL,
                           .ifs = getenv("IFS"),
                           .word_split_default = false};
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
