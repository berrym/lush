/**
 * @file splicer.h
 * @brief Splice computation and quote-aware escape rendering for completion
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * The splicer is the engine-side counterpart to the word_context
 * analyzer. Given a candidate from a source and the analyzer's
 * context, it produces the bytes that should be spliced into the
 * buffer (replacing the user's typed filename-prefix portion) and
 * decides whether to append a close-quote and/or trailing
 * space/slash on accept.
 *
 * This header exposes the pure layer: rendering rules, close-char
 * lookup, and full splice computation. The apply layer that mutates
 * a real lle_buffer_t (calling lle_buffer_replace_text and the
 * cursor manager) lives separately and is wired in when the
 * completion engine routes through the splicer.
 *
 * Rendering rules per quote_state, applied to the source's literal
 * candidate text:
 *
 *   NONE
 *     Backslash-escape every byte that is shell-special in
 *     unquoted context: whitespace (space/tab/newline), the
 *     statement terminators ;|&, the redirect chars <>, the group
 *     chars (){}, the expansion starters $`, the escape itself \,
 *     both quote chars "', the glob chars *?[, and the
 *     position-sensitive characters ~ # at position 0 of the
 *     rendered output, ! everywhere.
 *
 *   DOUBLE
 *     Backslash-escape only the four bytes that have meaning
 *     inside double quotes: $ ` \ ".
 *
 *   BACKTICK
 *     Backslash-escape \ ` $ (POSIX rule for double-quoted-style
 *     contexts inside backticks).
 *
 *   SINGLE
 *     Backslash escape is impossible inside single quotes (POSIX).
 *     A single-quote byte in the candidate is rendered by closing
 *     the open quote, emitting \', and re-opening: ' becomes '\'';
 *     all other bytes pass through literally.
 *
 *   ESCAPE_PENDING
 *     The user's typed backslash will absorb the first rendered
 *     byte. Currently rendered as if quote_state were NONE; a
 *     future revision may prepend a placeholder byte to neutralize
 *     the pending escape, but the practical impact on user-typed
 *     completion sequences is small.
 *
 * Suffix rules on accept:
 *
 *   DIRECTORY items
 *     Append "/". Do NOT close any open quote. Do NOT append a
 *     trailing space. The cursor stays inside any still-open
 *     quoted span so the next TAB can continue into the directory.
 *
 *   All other types (FILE, COMMAND, BUILTIN, ALIAS, VARIABLE,
 *   HISTORY, CUSTOM)
 *     If quote_state is SINGLE, DOUBLE, or BACKTICK, append the
 *     matching close character. Then append a trailing space.
 *
 * Preview phase (multi-match cycling) omits the suffix entirely:
 * the buffer shows only the rendered candidate, with the close +
 * trailing space added on accept.
 */

#ifndef LLE_SPLICER_H
#define LLE_SPLICER_H

#include "lle/buffer_management.h"
#include "lle/completion/completion_types.h"
#include "lle/completion/word_context.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Result of a splice computation
 * ============================================================================
 *
 * Describes the buffer mutation the engine must perform: delete the
 * bytes [delete_start, delete_start + delete_length) from the buffer,
 * insert insert_text at delete_start, and place the cursor at
 * cursor_after. All offsets are in the buffer's byte coordinate
 * system. The insert_text and the struct itself are pool-allocated
 * by the compute call; the engine consumes them and lets the pool
 * release on its own schedule.
 */
typedef struct lle_splicer_splice {
    size_t delete_start;     /**< First byte to delete from buffer    */
    size_t delete_length;    /**< Number of bytes to delete           */
    char  *insert_text;      /**< Pool-allocated bytes to insert      */
    size_t insert_length;    /**< Length of insert_text in bytes      */
    size_t cursor_after;     /**< Byte offset of cursor after splice  */
} lle_splicer_splice_t;

/* ============================================================================
 * Public API
 * ============================================================================
 */

/**
 * @brief Render a literal candidate for the current quote/escape context
 *
 * Pure function. Allocates the rendered bytes from the supplied pool.
 * Writes the rendered length to *out_length (excluding any terminating
 * NUL; the rendered buffer IS NUL-terminated).
 *
 * @param text Candidate literal bytes (need not be NUL-terminated)
 * @param text_length Length of text in bytes
 * @param quote_state Current quote/escape state at the cursor
 * @param pool Memory pool for allocations
 * @param out_rendered Output for the rendered bytes (NUL-terminated)
 * @param out_length Output for the rendered length (excludes NUL)
 *
 * @return LLE_SUCCESS or LLE_ERROR_INVALID_PARAMETER /
 *         LLE_ERROR_OUT_OF_MEMORY.
 */
lle_result_t lle_splicer_render_for_context(const char *text,
                                            size_t text_length,
                                            lle_quote_state_t quote_state,
                                            lle_memory_pool_t *pool,
                                            char **out_rendered,
                                            size_t *out_length);

/**
 * @brief Get the close character that matches a quote state
 *
 * Returns '\'', '"', or '`' for SINGLE / DOUBLE / BACKTICK
 * respectively. Returns '\0' for NONE and ESCAPE_PENDING (no close
 * character to append).
 */
char lle_splicer_close_char(lle_quote_state_t quote_state);

/**
 * @brief Compute the splice the engine should perform for a candidate
 *
 * Combines rendering, close-quote/suffix logic, and delete-range
 * computation into one pure operation. The result is a fully
 * formed lle_splicer_splice_t the engine can apply atomically via
 * lle_buffer_replace_text and a cursor move.
 *
 * The accept_phase flag distinguishes the two cycling phases:
 *
 *   accept_phase == false  (preview phase, multi-match cycling)
 *     Render the candidate only. No close character is appended,
 *     no trailing space, no slash. The user's prefix and the
 *     rendered candidate are the buffer's content; further bytes
 *     are added on accept.
 *
 *   accept_phase == true   (single-match auto-insert or ENTER pick)
 *     Render the candidate, then for DIRECTORY items append "/"
 *     (no close-char, no space) and for all other types append
 *     the matching close-char (if a quote was open) and a
 *     trailing space.
 *
 * @param context Word context produced by lle_word_context_analyze
 * @param item Completion item chosen by the engine
 * @param accept_phase True for accept, false for preview
 * @param pool Memory pool for allocations
 * @param out On LLE_SUCCESS, populated with the splice data
 *
 * @return LLE_SUCCESS or an error code.
 */
lle_result_t lle_splicer_compute(const lle_word_context_t *context,
                                 const lle_completion_item_t *item,
                                 bool accept_phase,
                                 lle_memory_pool_t *pool,
                                 lle_splicer_splice_t *out);

/**
 * @brief Apply a splice to a real buffer and cursor manager
 *
 * Convenience wrapper that calls lle_splicer_compute then performs
 * the buffer mutation via lle_buffer_replace_text and updates the
 * cursor via lle_cursor_manager_move_to_byte_offset, syncing the
 * buffer's cursor field afterward (the same invariant that
 * existing buffer-mutation code in keybinding_actions maintains).
 *
 * @param buffer Buffer to mutate
 * @param cursor_mgr Cursor manager for the buffer
 * @param context Word context produced by lle_word_context_analyze
 * @param item Completion item chosen by the engine
 * @param pool Memory pool for the splice's transient allocations
 *
 * @return LLE_SUCCESS or an error code from compute / buffer mutation.
 */
lle_result_t lle_splicer_apply_accept(lle_buffer_t *buffer,
                                      lle_cursor_manager_t *cursor_mgr,
                                      const lle_word_context_t *context,
                                      const lle_completion_item_t *item,
                                      lle_memory_pool_t *pool);

/**
 * @brief Apply a preview-phase splice (no close-quote / suffix)
 *
 * Same as lle_splicer_apply_accept but the splice is computed for
 * preview phase: the rendered candidate is inserted without the
 * close-char and trailing space. Used for multi-match cycling
 * preview.
 */
lle_result_t lle_splicer_apply_preview(lle_buffer_t *buffer,
                                       lle_cursor_manager_t *cursor_mgr,
                                       const lle_word_context_t *context,
                                       const lle_completion_item_t *item,
                                       lle_memory_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* LLE_SPLICER_H */
