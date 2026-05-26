/**
 * @file display/lle_inspect_widget.c
 * @brief `inspect-variable-at-cursor` widget -- live variable inspection
 *
 * Registers a built-in widget that scans the LLE buffer near the cursor
 * for a `$NAME` or `${NAME}` reference, resolves the name against the
 * live symbol table, and publishes the result to three shell variables
 * the user can compose surfaces around:
 *
 *   LUSH_INSPECT_NAME   The resolved identifier (empty when no ref found).
 *   LUSH_INSPECT_KIND   One of `none`, `unset`, `scalar`, `list`, `map`.
 *   LUSH_INSPECT_VALUE  Formatted value -- scalar text verbatim, list/map
 *                       entries `@Q`-quoted so a value containing spaces,
 *                       `=`, or `,` stays unambiguous and round-trips
 *                       through `eval "arr=($LUSH_INSPECT_VALUE)"`.  Empty
 *                       for none/unset.
 *
 * Token rule: the cursor must sit strictly inside the variable reference --
 * range [token_start, token_end).  A cursor on `$`, `{`, name characters, or
 * `}` resolves; a cursor past the closing `}` is outside the token and
 * reports `none`.
 *
 * The widget is the primitive.  Bind it via `display lle bind <key>
 * inspect-variable-at-cursor` and drive any surface from the variables:
 * a prompt segment, a status line, a post-widget hook, a log file.
 *
 * Lives outside liblle so the LLE library does not pick up a symbol-table
 * dependency.  Registered against the active editor by
 * lush_register_inspect_widget(), called once from lush.c after the LLE
 * editor exists.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/buffer_management.h"
#include "lle/lle_editor.h"
#include "lle/widget_system.h"
#include "symtable.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Shared with the parameter-expansion `@Q` operator in executor.c -- single-
// quoted escaping for printable strings, ANSI-C $'...' for control chars.
extern char *transform_quote(const char *str);

/**
 * @brief Scan the buffer near the cursor for a variable reference.
 *
 * On match, fills @p name_out with a NUL-terminated identifier and returns
 * true.  Strict-token: cursor in [token_start, token_end).  Tolerates the
 * cursor sitting mid-identifier, on `$`/`{`, or on the closing `}`; a
 * cursor past the closing `}` is outside the token and returns false.
 *
 * @param text         Buffer contents.
 * @param len          Buffer length in bytes.
 * @param cursor_pos   Cursor byte offset (clamped to len).
 * @param name_out     Output buffer for the identifier.
 * @param name_cap     Capacity of name_out (must be >= 2).
 * @return true if a reference was found, false otherwise.
 */
static bool find_var_ref_near_cursor(const char *text, size_t len,
                                     size_t cursor_pos, char *name_out,
                                     size_t name_cap) {
    if (!text || !name_out || name_cap < 2) {
        return false;
    }
    size_t pos = cursor_pos > len ? len : cursor_pos;

    // Strict-token rule: cursor must be in [token_start, token_end).  A
    // cursor past the closing `}` is outside the token and reports `none`.

    // If the cursor sits on the leading `$` (or on the `{` of `${`), step it
    // forward into the name so the name-walk below has a name char to anchor
    // on.  Cursor on the closing `}` walks left over name chars to the name
    // body; both cases land inside the token.
    if (pos < len) {
        if (text[pos] == '$' && pos + 1 < len &&
            (text[pos + 1] == '{' || isalnum((unsigned char)text[pos + 1]) ||
             text[pos + 1] == '_')) {
            pos++;
            if (pos < len && text[pos] == '{') {
                pos++;
            }
        } else if (text[pos] == '{' && pos > 0 && text[pos - 1] == '$') {
            pos++;
        }
    }

    // Walk left across name characters so a cursor mid-identifier still
    // resolves to the full identifier.
    size_t scan = pos;
    while (scan > 0 &&
           (isalnum((unsigned char)text[scan - 1]) || text[scan - 1] == '_')) {
        scan--;
    }

    // After the walk, scan points at the first name char.  Require `$` or
    // `${` immediately to the left -- this is what makes the result actually
    // a variable reference rather than a bare word.
    if (scan > 0 && text[scan - 1] == '{' && scan >= 2 &&
        text[scan - 2] == '$') {
        // scan currently points at the first name char; OK.
    } else if (scan == 0 || text[scan - 1] != '$') {
        return false;
    }

    size_t name_start = scan;
    size_t name_end = name_start;
    while (name_end < len &&
           (isalnum((unsigned char)text[name_end]) || text[name_end] == '_')) {
        name_end++;
    }
    if (name_end == name_start) {
        return false;
    }

    size_t nlen = name_end - name_start;
    if (nlen >= name_cap) {
        nlen = name_cap - 1;
    }
    memcpy(name_out, text + name_start, nlen);
    name_out[nlen] = '\0';
    return true;
}

/**
 * @brief Format a list's elements as "'e1' 'e2' ..." (`@Q`-quoted).
 *
 * Each element runs through the parameter-expansion `@Q` formatter so the
 * output is unambiguous and round-trips through `eval "arr=($VALUE)"` -- the
 * same quoting rule the rest of the shell already exposes.  Bounded write;
 * truncation is silent.
 */
static void format_list_value(array_value_t *array, char *out, size_t cap) {
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!array) {
        return;
    }
    size_t count = 0;
    char **values = symtable_array_get_values(array, &count);
    size_t off = 0;
    for (size_t i = 0; i < count && off < cap - 1; i++) {
        char *q = transform_quote(values[i] ? values[i] : "");
        int written = snprintf(out + off, cap - off, "%s%s", i == 0 ? "" : " ",
                               q ? q : "''");
        free(q);
        if (written < 0) {
            break;
        }
        off += (size_t)written;
    }
    out[off < cap ? off : cap - 1] = '\0';
    if (values) {
        for (size_t i = 0; i < count; i++) {
            free(values[i]);
        }
        free(values);
    }
}

/**
 * @brief Format a map's contents as "'k1'='v1', 'k2'='v2', ..." (`@Q`-quoted).
 *
 * Both keys and values pass through the `@Q` formatter so embedded `=`, `,`,
 * spaces, or quotes are unambiguous.  Bounded write; truncation is silent.
 */
static void format_map_value(array_value_t *array, char *out, size_t cap) {
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!array) {
        return;
    }
    size_t kcount = 0;
    char **keys = symtable_array_get_keys(array, &kcount);
    size_t off = 0;
    for (size_t i = 0; i < kcount && off < cap - 1; i++) {
        const char *v = symtable_array_get_assoc(array, keys[i]);
        char *qk = transform_quote(keys[i]);
        char *qv = transform_quote(v ? v : "");
        int written =
            snprintf(out + off, cap - off, "%s%s=%s", i == 0 ? "" : ", ",
                     qk ? qk : "''", qv ? qv : "''");
        free(qk);
        free(qv);
        if (written < 0) {
            break;
        }
        off += (size_t)written;
    }
    out[off < cap ? off : cap - 1] = '\0';
    if (keys) {
        for (size_t i = 0; i < kcount; i++) {
            free(keys[i]);
        }
        free(keys);
    }
}

/// @brief Publish inspection data for @p name to the LUSH_INSPECT_* vars.
static void publish_inspection(const char *name) {
    symtable_set_global("LUSH_INSPECT_NAME", name ? name : "");
    if (!name || !name[0]) {
        symtable_set_global("LUSH_INSPECT_KIND", "none");
        symtable_set_global("LUSH_INSPECT_VALUE", "");
        return;
    }

    lush_value_view_t view;
    if (!symtable_lookup(name, &view)) {
        symtable_set_global("LUSH_INSPECT_KIND", "unset");
        symtable_set_global("LUSH_INSPECT_VALUE", "");
        return;
    }

    switch (view.kind) {
    case LUSH_VALUE_SCALAR:
        symtable_set_global("LUSH_INSPECT_KIND", "scalar");
        symtable_set_global("LUSH_INSPECT_VALUE",
                            view.scalar_value ? view.scalar_value : "");
        break;
    case LUSH_VALUE_LIST: {
        symtable_set_global("LUSH_INSPECT_KIND", "list");
        char buf[1024];
        format_list_value(view.array, buf, sizeof(buf));
        symtable_set_global("LUSH_INSPECT_VALUE", buf);
        break;
    }
    case LUSH_VALUE_MAP: {
        symtable_set_global("LUSH_INSPECT_KIND", "map");
        char buf[1024];
        format_map_value(view.array, buf, sizeof(buf));
        symtable_set_global("LUSH_INSPECT_VALUE", buf);
        break;
    }
    default:
        symtable_set_global("LUSH_INSPECT_KIND", "unset");
        symtable_set_global("LUSH_INSPECT_VALUE", "");
        break;
    }

    lush_value_view_clear(&view);
}

/**
 * @brief Widget callback: inspect the variable reference under the cursor.
 *
 * Exposed externally (not static) so a dedicated unit test can invoke it
 * against a synthetic editor without spinning up the full LLE.
 *
 * @param editor    LLE editor context.
 * @param user_data Unused.
 * @return Always LLE_SUCCESS -- inspection failures publish kind=none/unset
 *         rather than escalating, since this is a passive query.
 */
lle_result_t lush_inspect_widget_callback(lle_editor_t *editor,
                                          void *user_data) {
    (void)user_data;

    if (!editor || !editor->buffer || !editor->buffer->data) {
        publish_inspection(NULL);
        return LLE_SUCCESS;
    }

    char namebuf[256];
    bool found = find_var_ref_near_cursor(
        editor->buffer->data, editor->buffer->length,
        editor->buffer->cursor.byte_offset, namebuf, sizeof(namebuf));
    publish_inspection(found ? namebuf : NULL);
    return LLE_SUCCESS;
}

lle_result_t lush_register_inspect_widget(lle_widget_registry_t *registry) {
    if (!registry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    return lle_widget_register(registry, "inspect-variable-at-cursor",
                               lush_inspect_widget_callback, LLE_WIDGET_BUILTIN,
                               NULL);
}
