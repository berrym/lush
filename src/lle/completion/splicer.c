/**
 * @file splicer.c
 * @brief Splice computation and quote-aware escape rendering implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Pure-function layer of the splicer. The render functions and
 * lle_splicer_compute have no side effects beyond allocating from
 * the supplied pool; they make no system calls and do not touch the
 * buffer or cursor manager. The engine's caller-side wiring that
 * applies a computed splice via lle_buffer_replace_text and a
 * cursor move lands separately, when the completion engine is
 * routed through these primitives.
 *
 * Rendering is byte-oriented for the metacharacters that need
 * escaping (all of which are ASCII per POSIX). Non-ASCII bytes in
 * the candidate (UTF-8 continuation bytes, multi-byte sequences)
 * pass through unchanged: they are not shell-special and need no
 * escape regardless of quote_state.
 */

#include "lle/completion/splicer.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ============================================================================
 * Per-quote-state escape rules
 * ============================================================================
 *
 * needs_escape_in_<state>(byte, position_in_output) tells us whether
 * the given byte at the given output position needs a leading
 * backslash. The position_in_output argument lets the NONE rules
 * treat ~ and # specially at output position 0.
 */

static bool needs_escape_in_none(unsigned char b, size_t out_pos) {
    switch (b) {
    case ' ':
    case '\t':
    case '\n':
    case ';':
    case '|':
    case '&':
    case '<':
    case '>':
    case '(':
    case ')':
    case '{':
    case '}':
    case '$':
    case '`':
    case '\\':
    case '"':
    case '\'':
    case '*':
    case '?':
    case '[':
    case '!':
        return true;
    case '~':
    case '#':
        return out_pos == 0;
    default:
        return false;
    }
}

static bool needs_escape_in_double(unsigned char b) {
    /// Inside "..." only $ ` \ " keep their special meaning.
    return b == '$' || b == '`' || b == '\\' || b == '"';
}

static bool needs_escape_in_backtick(unsigned char b) {
    /// Same as double-quote rules minus " (which has no special
    /// meaning inside `...`).
    return b == '$' || b == '`' || b == '\\';
}

/* ============================================================================
 * Render helpers — append bytes to a growing buffer
 * ============================================================================
 */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} render_buf_t;

static lle_result_t rb_grow(render_buf_t *rb, size_t need,
                            lle_memory_pool_t *pool) {
    (void)pool;
    /// Required capacity = current length + bytes to add + 1 NUL.
    /// Detect overflow before computing the sum.
    if (need > SIZE_MAX - 1 - rb->len)
        return LLE_ERROR_OUT_OF_MEMORY;
    size_t required = rb->len + need + 1;
    if (required <= rb->cap)
        return LLE_SUCCESS;

    size_t new_cap = rb->cap ? rb->cap : 32;
    while (new_cap < required) {
        if (new_cap > SIZE_MAX / 2)
            return LLE_ERROR_OUT_OF_MEMORY;
        new_cap *= 2;
    }
    char *new_buf = lle_pool_alloc(new_cap);
    if (!new_buf)
        return LLE_ERROR_OUT_OF_MEMORY;
    if (rb->buf != NULL && rb->len > 0) {
        memcpy(new_buf, rb->buf, rb->len);
    }
    rb->buf = new_buf;
    rb->cap = new_cap;
    return LLE_SUCCESS;
}

static lle_result_t rb_putc(render_buf_t *rb, char c, lle_memory_pool_t *pool) {
    lle_result_t r = rb_grow(rb, 1, pool);
    if (r != LLE_SUCCESS)
        return r;
    rb->buf[rb->len++] = c;
    return LLE_SUCCESS;
}

static lle_result_t rb_puts(render_buf_t *rb, const char *s, size_t n,
                            lle_memory_pool_t *pool) {
    lle_result_t r = rb_grow(rb, n, pool);
    if (r != LLE_SUCCESS)
        return r;
    memcpy(rb->buf + rb->len, s, n);
    rb->len += n;
    return LLE_SUCCESS;
}

static lle_result_t rb_finish(render_buf_t *rb, lle_memory_pool_t *pool,
                              char **out, size_t *out_len) {
    /// NUL-terminate. rb_grow always reserves +1 for the terminator.
    lle_result_t r = rb_grow(rb, 1, pool);
    if (r != LLE_SUCCESS)
        return r;
    rb->buf[rb->len] = '\0';
    *out = rb->buf;
    if (out_len)
        *out_len = rb->len;
    return LLE_SUCCESS;
}

/* ============================================================================
 * Per-quote-state renderers
 * ============================================================================
 */

static lle_result_t render_none(const char *text, size_t text_length,
                                lle_memory_pool_t *pool, char **out,
                                size_t *out_length) {
    render_buf_t rb = {0};
    for (size_t i = 0; i < text_length; i++) {
        unsigned char b = (unsigned char)text[i];
        if (needs_escape_in_none(b, rb.len)) {
            lle_result_t r = rb_putc(&rb, '\\', pool);
            if (r != LLE_SUCCESS)
                return r;
        }
        lle_result_t r = rb_putc(&rb, (char)b, pool);
        if (r != LLE_SUCCESS)
            return r;
    }
    return rb_finish(&rb, pool, out, out_length);
}

static lle_result_t render_double(const char *text, size_t text_length,
                                  lle_memory_pool_t *pool, char **out,
                                  size_t *out_length) {
    render_buf_t rb = {0};
    for (size_t i = 0; i < text_length; i++) {
        unsigned char b = (unsigned char)text[i];
        if (needs_escape_in_double(b)) {
            lle_result_t r = rb_putc(&rb, '\\', pool);
            if (r != LLE_SUCCESS)
                return r;
        }
        lle_result_t r = rb_putc(&rb, (char)b, pool);
        if (r != LLE_SUCCESS)
            return r;
    }
    return rb_finish(&rb, pool, out, out_length);
}

static lle_result_t render_backtick(const char *text, size_t text_length,
                                    lle_memory_pool_t *pool, char **out,
                                    size_t *out_length) {
    render_buf_t rb = {0};
    for (size_t i = 0; i < text_length; i++) {
        unsigned char b = (unsigned char)text[i];
        if (needs_escape_in_backtick(b)) {
            lle_result_t r = rb_putc(&rb, '\\', pool);
            if (r != LLE_SUCCESS)
                return r;
        }
        lle_result_t r = rb_putc(&rb, (char)b, pool);
        if (r != LLE_SUCCESS)
            return r;
    }
    return rb_finish(&rb, pool, out, out_length);
}

static lle_result_t render_single(const char *text, size_t text_length,
                                  lle_memory_pool_t *pool, char **out,
                                  size_t *out_length) {
    /// Inside '...' nothing escapes. A single quote in the candidate
    /// is rendered by closing the open quote, emitting \', and
    /// re-opening: '...'\''...'.
    render_buf_t rb = {0};
    for (size_t i = 0; i < text_length; i++) {
        char c = text[i];
        if (c == '\'') {
            lle_result_t r = rb_puts(&rb, "'\\''", 4, pool);
            if (r != LLE_SUCCESS)
                return r;
        } else {
            lle_result_t r = rb_putc(&rb, c, pool);
            if (r != LLE_SUCCESS)
                return r;
        }
    }
    return rb_finish(&rb, pool, out, out_length);
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

lle_result_t lle_splicer_render_for_context(
    const char *text, size_t text_length, lle_quote_state_t quote_state,
    lle_memory_pool_t *pool, char **out_rendered, size_t *out_length) {
    if (!text || !pool || !out_rendered) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    switch (quote_state) {
    case LLE_QUOTE_NONE:
    case LLE_QUOTE_ESCAPE_PENDING:
        /// ESCAPE_PENDING currently shares NONE rendering. The
        /// user's pending backslash will absorb the first emitted
        /// byte, producing a literal-escape interaction the
        /// engine can treat as a documented edge case.
        return render_none(text, text_length, pool, out_rendered, out_length);
    case LLE_QUOTE_DOUBLE:
        return render_double(text, text_length, pool, out_rendered, out_length);
    case LLE_QUOTE_BACKTICK:
        return render_backtick(text, text_length, pool, out_rendered,
                               out_length);
    case LLE_QUOTE_SINGLE:
        return render_single(text, text_length, pool, out_rendered, out_length);
    }
    return LLE_ERROR_INVALID_PARAMETER;
}

char lle_splicer_close_char(lle_quote_state_t quote_state) {
    switch (quote_state) {
    case LLE_QUOTE_SINGLE:
        return '\'';
    case LLE_QUOTE_DOUBLE:
        return '"';
    case LLE_QUOTE_BACKTICK:
        return '`';
    case LLE_QUOTE_NONE:
    case LLE_QUOTE_ESCAPE_PENDING:
        return '\0';
    }
    return '\0';
}

/* True when an item's type takes the directory-style suffix (just "/")
 * rather than the file-style suffix (close + trailing space). */
static bool item_type_is_directory(lle_completion_type_t type) {
    return type == LLE_COMPLETION_TYPE_DIRECTORY;
}

lle_result_t lle_splicer_compute(const lle_word_context_t *context,
                                 const lle_completion_item_t *item,
                                 bool accept_phase, lle_memory_pool_t *pool,
                                 lle_splicer_splice_t *out) {
    if (!context || !item || !item->text || !pool || !out) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Render the candidate's literal text into the cursor's
    /// quote/escape context.
    char *rendered = NULL;
    size_t rendered_len = 0;
    lle_result_t r = lle_splicer_render_for_context(
        item->text, strlen(item->text), context->quote_state, pool, &rendered,
        &rendered_len);
    if (r != LLE_SUCCESS)
        return r;

    /// Build the final insert string: rendered + optional suffix.
    render_buf_t rb = {0};
    r = rb_puts(&rb, rendered, rendered_len, pool);
    if (r != LLE_SUCCESS)
        return r;

    if (accept_phase) {
        if (item_type_is_directory(item->type)) {
            r = rb_putc(&rb, '/', pool);
            if (r != LLE_SUCCESS)
                return r;
        } else {
            char close = lle_splicer_close_char(context->quote_state);
            if (close != '\0') {
                r = rb_putc(&rb, close, pool);
                if (r != LLE_SUCCESS)
                    return r;
            }
            r = rb_putc(&rb, ' ', pool);
            if (r != LLE_SUCCESS)
                return r;
        }
    }

    char *insert_text = NULL;
    size_t insert_len = 0;
    r = rb_finish(&rb, pool, &insert_text, &insert_len);
    if (r != LLE_SUCCESS)
        return r;

    /// Splice replaces [filename_portion_start, word_end) with
    /// insert_text. The cursor sits at the end of the inserted bytes.
    out->delete_start = context->filename_portion_start;
    out->delete_length = context->word_end - context->filename_portion_start;
    out->insert_text = insert_text;
    out->insert_length = insert_len;
    out->cursor_after = context->filename_portion_start + insert_len;

    return LLE_SUCCESS;
}

/* ============================================================================
 * Apply layer
 * ============================================================================
 */

static lle_result_t apply_phase(lle_buffer_t *buffer,
                                lle_cursor_manager_t *cursor_mgr,
                                const lle_word_context_t *context,
                                const lle_completion_item_t *item,
                                lle_memory_pool_t *pool, bool accept_phase) {
    if (!buffer || !cursor_mgr || !context || !item || !pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_splicer_splice_t splice;
    lle_result_t r =
        lle_splicer_compute(context, item, accept_phase, pool, &splice);
    if (r != LLE_SUCCESS)
        return r;

    /// Atomic delete + insert via the buffer's combined replace
    /// operation.
    r = lle_buffer_replace_text(buffer, splice.delete_start,
                                splice.delete_length, splice.insert_text,
                                splice.insert_length);
    if (r != LLE_SUCCESS)
        return r;

    /// Move the cursor manager and sync the buffer's cached cursor
    /// field, matching the invariant maintained by other buffer-
    /// mutation paths in the keybinding actions.
    r = lle_cursor_manager_move_to_byte_offset(cursor_mgr, splice.cursor_after);
    if (r != LLE_SUCCESS)
        return r;
    lle_cursor_manager_get_position(cursor_mgr, &buffer->cursor);

    return LLE_SUCCESS;
}

lle_result_t lle_splicer_apply_accept(lle_buffer_t *buffer,
                                      lle_cursor_manager_t *cursor_mgr,
                                      const lle_word_context_t *context,
                                      const lle_completion_item_t *item,
                                      lle_memory_pool_t *pool) {
    return apply_phase(buffer, cursor_mgr, context, item, pool, true);
}

lle_result_t lle_splicer_apply_preview(lle_buffer_t *buffer,
                                       lle_cursor_manager_t *cursor_mgr,
                                       const lle_word_context_t *context,
                                       const lle_completion_item_t *item,
                                       lle_memory_pool_t *pool) {
    return apply_phase(buffer, cursor_mgr, context, item, pool, false);
}
