/**
 * @file word.c
 * @brief Word CST ownership: allocation, the single recursive copy/free, and
 *        lossless source reconstruction.
 *
 * This is the entire lifecycle surface for the word IR. word_part_copy and
 * word_part_free are switch (p->kind) with NO default label. Each switch is
 * wrapped in a `#pragma GCC diagnostic error` region that promotes BOTH
 * -Wswitch and -Wswitch-enum to errors (the pragma is honored by gcc and
 * clang). -Wswitch catches a missing case while there is no default;
 * -Wswitch-enum catches it even if a maintainer later adds a default that would
 * mask it. Elevating both means a new WP_* kind is a hard compile error on
 * either compiler regardless of which diagnostic it attributes the gap to --
 * the field-omission bug class (the pre-#657 node loc drop, #488, #498) caught
 * by the compiler rather than by discipline. The pragma travels with this file,
 * independent of the project's warning flags. See word.h and
 * docs/development/WORD_CST_PLAN.md.
 */
#include "word.h"

#include <stdlib.h>
#include <string.h>

/// Internal recursive helpers.

static word_part_t *word_part_copy(const word_part_t *p);

/// Duplicate a possibly-NULL string. Returns NULL for NULL input; the caller
/// distinguishes "was NULL" from "alloc failed" via the source pointer.
static char *dup_opt(const char *s) { return s ? strdup(s) : NULL; }

/// Allocation.

word_t *word_new(uint32_t src_off, uint32_t src_len) {
    word_t *w = calloc(1, sizeof(*w));
    if (!w) {
        return NULL;
    }
    w->src_off = src_off;
    w->src_len = src_len;
    return w;
}

word_part_t *word_part_new(word_part_kind_t kind, uint32_t src_off,
                           uint32_t src_len) {
    word_part_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return NULL;
    }
    p->kind = kind;
    p->src_off = src_off;
    p->src_len = src_len;
    return p;
}

bool word_add_part(word_t *w, word_part_t *p) {
    if (w->n_parts == w->cap_parts) {
        /// Guard the doubling against uint32 wrap. Unreachable for real input
        /// (words are a handful of parts), but keeps the growth total.
        if (w->cap_parts > UINT32_MAX / 2) {
            return false;
        }
        uint32_t cap = w->cap_parts ? w->cap_parts * 2 : 4;
        word_part_t **grown = realloc(w->parts, (size_t)cap * sizeof(*grown));
        if (!grown) {
            return false;
        }
        w->parts = grown;
        w->cap_parts = cap;
    }
    w->parts[w->n_parts++] = p;
    return true;
}

void word_set_body(word_part_t *group, word_t *body) {
    PART_BODY(group) = body;
}

bool word_add_item(word_part_t *multi, word_t *item) {
    /// multi arms grow one at a time: brace/array element counts are small and
    /// known-ish; a tight array keeps word_reconstruct and free simple.
    uint32_t n = multi->u.multi.n_items;
    if (n == UINT32_MAX) {
        return false;
    }
    word_t **grown =
        realloc(multi->u.multi.items, ((size_t)n + 1) * sizeof(*grown));
    if (!grown) {
        return false;
    }
    multi->u.multi.items = grown;
    multi->u.multi.items[n] = item;
    multi->u.multi.n_items = n + 1;
    return true;
}

/// Free.

void word_free(word_t *w) {
    if (!w) {
        return;
    }
    for (uint32_t i = 0; i < w->n_parts; i++) {
        word_part_free(w->parts[i]);
    }
    free(w->parts);
    word_free(w->key);
    free(w);
}

/// Free a part and everything it owns. The switch has NO default: a new WP_*
/// kind must be handled here or the build fails.
void word_part_free(word_part_t *p) {
    if (!p) {
        return;
    }
/// No default: every WP_* kind must be handled or the build fails. Elevate both
/// -Wswitch (fires on a missing case with no default) and -Wswitch-enum (fires
/// even if a default is later added) to errors, so the guarantee holds on both
/// clang and gcc regardless of which diagnostic a given compiler attributes the
/// missing case to. See the file header for why this is load-bearing.
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wswitch-enum"
    switch (p->kind) {
    case WP_LITERAL:
    case WP_SINGLE:
    case WP_ANSIC:
        free(p->u.leaf.text);
        break;
    case WP_DOUBLE:
    case WP_BARE:
        word_free(p->u.group.body);
        break;
    case WP_BRACE:
    case WP_ARRAY_LIT:
        for (uint32_t i = 0; i < p->u.multi.n_items; i++) {
            word_free(p->u.multi.items[i]);
        }
        free(p->u.multi.items);
        break;
    case WP_PARAM:
        free(p->u.param.name);
        word_free(p->u.param.subscript);
        word_free(p->u.param.operand);
        word_free(p->u.param.operand2);
        break;
    case WP_CMDSUB:
    case WP_BACKTICK:
    case WP_ARITH:
        free(p->u.defer.body);
        break;
    case WP_TILDE:
        free(p->u.tilde.user);
        break;
    case WP_PROCSUB_IN:
    case WP_PROCSUB_OUT:
        free(p->u.procsub.body);
        break;
    case WP_KINDSIGIL:
        free(p->u.kindsigil.name);
        break;
    }
#pragma GCC diagnostic pop
    free(p);
}

/// Deep copy.

word_t *word_copy(const word_t *w) {
    if (!w) {
        return NULL;
    }
    word_t *copy = word_new(w->src_off, w->src_len);
    if (!copy) {
        return NULL;
    }
    for (uint32_t i = 0; i < w->n_parts; i++) {
        word_part_t *pc = word_part_copy(w->parts[i]);
        if (!pc || !word_add_part(copy, pc)) {
            word_part_free(pc);
            word_free(copy);
            return NULL;
        }
    }
    if (w->key) {
        copy->key = word_copy(w->key);
        if (!copy->key) {
            word_free(copy);
            return NULL;
        }
    }
    return copy;
}

/// Deep-copy a part. Like word_part_free, the switch has NO default so a new
/// WP_* kind cannot silently ride through the copy with an owned field dropped
/// (the pre-#657 node loc / #488 / #498 failure mode, made a compile error via
/// the same -Wswitch-enum pragma). On any allocation failure the partial copy
/// is freed and NULL is returned.
static word_part_t *word_part_copy(const word_part_t *p) {
    if (!p) {
        return NULL;
    }
    word_part_t *c = word_part_new(p->kind, p->src_off, p->src_len);
    if (!c) {
        return NULL;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wswitch-enum"
    switch (p->kind) {
    case WP_LITERAL:
    case WP_SINGLE:
    case WP_ANSIC:
        c->u.leaf.literal_meta = p->u.leaf.literal_meta;
        if (p->u.leaf.text) {
            c->u.leaf.text = strdup(p->u.leaf.text);
            if (!c->u.leaf.text) {
                goto fail;
            }
        }
        break;
    case WP_DOUBLE:
    case WP_BARE:
        if (p->u.group.body) {
            c->u.group.body = word_copy(p->u.group.body);
            if (!c->u.group.body) {
                goto fail;
            }
        }
        break;
    case WP_BRACE:
    case WP_ARRAY_LIT:
        for (uint32_t i = 0; i < p->u.multi.n_items; i++) {
            word_t *item = word_copy(p->u.multi.items[i]);
            if (!item || !word_add_item(c, item)) {
                word_free(item);
                goto fail;
            }
        }
        break;
    case WP_PARAM:
        c->u.param.op = p->u.param.op;
        c->u.param.flags = p->u.param.flags;
        if (p->u.param.name) {
            c->u.param.name = strdup(p->u.param.name);
            if (!c->u.param.name) {
                goto fail;
            }
        }
        if (p->u.param.subscript) {
            c->u.param.subscript = word_copy(p->u.param.subscript);
            if (!c->u.param.subscript) {
                goto fail;
            }
        }
        if (p->u.param.operand) {
            c->u.param.operand = word_copy(p->u.param.operand);
            if (!c->u.param.operand) {
                goto fail;
            }
        }
        if (p->u.param.operand2) {
            c->u.param.operand2 = word_copy(p->u.param.operand2);
            if (!c->u.param.operand2) {
                goto fail;
            }
        }
        break;
    case WP_CMDSUB:
    case WP_BACKTICK:
    case WP_ARITH:
        if (p->u.defer.body) {
            c->u.defer.body = strdup(p->u.defer.body);
            if (!c->u.defer.body) {
                goto fail;
            }
        }
        break;
    case WP_TILDE:
        c->u.tilde.user = dup_opt(p->u.tilde.user);
        if (p->u.tilde.user && !c->u.tilde.user) {
            goto fail;
        }
        break;
    case WP_PROCSUB_IN:
    case WP_PROCSUB_OUT:
        if (p->u.procsub.body) {
            c->u.procsub.body = strdup(p->u.procsub.body);
            if (!c->u.procsub.body) {
                goto fail;
            }
        }
        break;
    case WP_KINDSIGIL:
        c->u.kindsigil.sigil = p->u.kindsigil.sigil;
        if (p->u.kindsigil.name) {
            c->u.kindsigil.name = strdup(p->u.kindsigil.name);
            if (!c->u.kindsigil.name) {
                goto fail;
            }
        }
        break;
    }
#pragma GCC diagnostic pop
    return c;

fail:
    word_part_free(c);
    return NULL;
}

/// Lossless reconstruction.

static void word_part_rebase(word_part_t *p, uint32_t base);

void word_rebase(word_t *w, uint32_t base) {
    if (!w || base == 0) {
        return;
    }
    w->src_off += base;
    for (uint32_t i = 0; i < w->n_parts; i++) {
        word_part_rebase(w->parts[i], base);
    }
    word_rebase(w->key, base);
}

/// Span-shift one part. Mirrors word_part_copy's traversal so a new kind cannot
/// be added without visiting here: no default label, and the switch is wrapped
/// in the same pragma region that promotes -Wswitch and -Wswitch-enum to
/// errors on both clang and gcc.
static void word_part_rebase(word_part_t *p, uint32_t base) {
    if (!p) {
        return;
    }
    p->src_off += base;

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wswitch-enum"
    switch (p->kind) {
    case WP_DOUBLE:
    case WP_BARE:
        word_rebase(p->u.group.body, base);
        break;
    case WP_BRACE:
    case WP_ARRAY_LIT:
        for (uint32_t i = 0; i < p->u.multi.n_items; i++) {
            word_rebase(p->u.multi.items[i], base);
        }
        break;
    case WP_PARAM:
        /// subscript / operand / operand2 are parsed from their OWN strings, so
        /// their spans are already relative to themselves. Shifting them by
        /// this word's base would make them address unrelated bytes. They stay
        /// in their own coordinate system -- see word_rebase's contract.
        break;
    case WP_LITERAL:
    case WP_SINGLE:
    case WP_ANSIC:
    case WP_CMDSUB:
    case WP_BACKTICK:
    case WP_ARITH:
    case WP_TILDE:
    case WP_PROCSUB_IN:
    case WP_PROCSUB_OUT:
    case WP_KINDSIGIL:
        /// Leaves: the span shifted above is all they carry.
        break;
    }
#pragma GCC diagnostic pop
}

char *word_reconstruct(const word_t *w, const char *src, size_t srclen) {
    if (!w || !src) {
        return NULL;
    }
    /// Validate the span lies within src before reading it. In-contract (src is
    /// the exact buffer the word was parsed from) this always holds; the check
    /// makes the function total for out-of-contract callers -- a formatter or
    /// LSP operating on a re-fetched/edited buffer whose spans have drifted.
    if ((size_t)w->src_off > srclen ||
        (size_t)w->src_len > srclen - (size_t)w->src_off) {
        return NULL;
    }
    char *out = malloc((size_t)w->src_len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, src + w->src_off, w->src_len);
    out[w->src_len] = '\0';
    return out;
}
