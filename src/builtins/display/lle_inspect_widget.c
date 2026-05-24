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
 *   LUSH_INSPECT_VALUE  Formatted value -- scalar text, joined list, or
 *                       comma-separated map entries; empty for none/unset.
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

/**
 * @brief Scan the buffer near the cursor for a variable reference.
 *
 * On match, fills @p name_out with a NUL-terminated identifier and returns
 * true.  Tolerates the cursor sitting mid-identifier, just past a closing
 * `}`, or directly on `$`.
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

    // Cursor just past a closing '}': pull it back inside the braces so the
    // name walk below lands on the identifier.
    if (pos > 0 && text[pos - 1] == '}') {
        size_t k = pos - 1;
        while (k > 0 && text[k] != '{') {
            k--;
        }
        if (k > 0 && text[k - 1] == '$') {
            pos = k + 1;
        }
    }

    // Walk left across name characters so a cursor mid-identifier still
    // resolves to the full identifier.
    size_t scan = pos;
    while (scan > 0 &&
           (isalnum((unsigned char)text[scan - 1]) || text[scan - 1] == '_')) {
        scan--;
    }

    // Allow `${NAME` (cursor just inside the brace) by rewinding past one '{'.
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
 * @brief Format a map's contents as "k1=v1, k2=v2, ...".
 *
 * Bounded write so an oversized map cannot bloat the published shell
 * variable.  Truncation is silent; the caller decides the buffer size.
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
        int written = snprintf(out + off, cap - off, "%s%s=%s",
                               i == 0 ? "" : ", ", keys[i], v ? v : "");
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

/**
 * @brief Publish inspection data for @p name to the LUSH_INSPECT_* vars.
 */
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
        char *joined =
            view.array ? symtable_array_expand(view.array, " ") : NULL;
        symtable_set_global("LUSH_INSPECT_VALUE", joined ? joined : "");
        free(joined);
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
