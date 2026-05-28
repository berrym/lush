/**
 * @file screen_buffer.h
 * @brief Screen buffer system - Virtual screen management
 *
 * Implements virtual screen buffer management for differential terminal
 * updates. Maintains virtual representation of terminal state, compares
 * old vs new screens, and generates minimal escape sequences.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 */

#ifndef SCREEN_BUFFER_H
#define SCREEN_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// ============================================================================
/// CONSTANTS
/// ============================================================================

#define SCREEN_BUFFER_MAX_ROWS 100
#define SCREEN_BUFFER_MAX_COLS 512

/// ============================================================================
/// TYPE DEFINITIONS
/// ============================================================================

/**
 * Represents a single character cell in the virtual screen
 *
 * Stores full UTF-8 grapheme clusters (1-4 bytes) to properly support:
 * - ASCII characters (1 byte)
 * - Extended Latin, Cyrillic, etc. (2 bytes)
 * - CJK characters, box drawing (3 bytes)
 * - Emoji and other SMP characters (4 bytes)
 *
 * The visual_width field indicates display columns (0, 1, or 2):
 * - 0 for zero-width characters (combining marks)
 * - 1 for normal width (ASCII, most Unicode)
 * - 2 for wide characters (CJK, emoji)
 */
typedef struct {
    char utf8_bytes[4];   /// Full UTF-8 sequence (1-4 bytes)
    uint8_t byte_len;     /// Actual bytes used (1-4)
    uint8_t visual_width; /// Display width in columns (0, 1, or 2)
    bool is_prompt;       /// True if this cell is part of the prompt
} screen_cell_t;

/**
 * Represents a line prefix (e.g., continuation prompt)
 *
 * Prefixes are rendered before line content and tracked separately for
 * efficient updates. Used for continuation prompts and future features
 * like autosuggestions.
 */
typedef struct {
    char *text;          /// Prefix text (e.g., "> ", "loop> ")
    size_t length;       /// Length in bytes
    size_t visual_width; /// Visual width in columns (excluding ANSI codes)
    bool contains_ansi;  /// True if prefix contains ANSI escape codes
    bool dirty;          /// True if prefix changed since last render
} screen_line_prefix_t;

/**
 * Represents one line in the virtual screen
 */
typedef struct {
    screen_cell_t cells[SCREEN_BUFFER_MAX_COLS];
    int length; /// Number of characters in this line
    bool dirty; /// True if line content changed since last render

    screen_line_prefix_t *prefix; /// Optional prefix (NULL if none)
    bool prefix_dirty;            /// True if prefix changed since last render
} screen_line_t;

/**
 * Virtual screen buffer
 *
 * Tracks the complete display state including command text and any menu/overlay
 * content. By tracking the menu as part of the buffer, cursor positioning
 * calculations work correctly regardless of terminal scrolling.
 */
typedef struct {
    screen_line_t lines[SCREEN_BUFFER_MAX_ROWS];
    int num_rows;          /// Number of rows currently used (command only)
    int terminal_width;    /// Terminal width in columns
    int cursor_row;        /// Cursor row position (0-based, within command)
    int cursor_col;        /// Cursor column position (0-based)
    int command_start_row; /// Row where command text starts (after prompt)
    int command_start_col; /// Column where command text starts (after prompt)

    /// Menu/overlay tracking - tracks content displayed after command text
    /// This allows cursor positioning to account for all displayed content
    int menu_lines;         /// Number of lines the menu occupies (0 if none)
    int ghost_text_lines;   /// Extra lines from autosuggestion wrapping (0 if
                            /// none)
    int total_display_rows; /// Total rows: num_rows + ghost_text_lines +
                            /// menu_lines
    int command_end_row;    /// Row where command text ends (before ghost/menu)
    int command_end_col;    /// Column where command text ends

    /// RPROMPT tracking - right-aligned prompt on command_start_row
    char rprompt_text[512];   /// Rendered RPROMPT string (may contain ANSI)
    int rprompt_visual_width; /// Visual width excluding ANSI escapes
    bool rprompt_fits;        /// True if rprompt fits on prompt row
    int rprompt_col;          /// Starting column (0-based) for rprompt display
} screen_buffer_t;

/// ============================================================================
/// FUNCTION DECLARATIONS
/// ============================================================================

/**
 * Initialize a screen buffer
 *
 * @param buffer Buffer to initialize
 * @param terminal_width Terminal width in columns
 */
void screen_buffer_init(screen_buffer_t *buffer, int terminal_width);

/**
 * Clear screen buffer (reset to empty state)
 *
 * Note: This does NOT free line prefixes. Prefixes persist across clears.
 * Use screen_buffer_cleanup() to free all resources.
 *
 * @param buffer Buffer to clear
 */
void screen_buffer_clear(screen_buffer_t *buffer);

/**
 * Cleanup screen buffer and free all resources
 *
 * Frees all line prefixes and resets the buffer to empty state.
 * The buffer can be reused after calling screen_buffer_init() again.
 *
 * @param buffer Buffer to cleanup
 */
void screen_buffer_cleanup(screen_buffer_t *buffer);

/**
 * Render prompt and command into screen buffer
 *
 * This takes the abstract prompt string and command string and renders them
 * into the screen buffer, handling line wrapping automatically.
 *
 * @param buffer Buffer to render into
 * @param prompt_text Prompt string (may contain ANSI codes)
 * @param command_text Command string (may contain ANSI codes)
 * @param cursor_byte_offset Cursor position in command_text (byte offset)
 */
void screen_buffer_render(screen_buffer_t *buffer, const char *prompt_text,
                          const char *command_text, size_t cursor_byte_offset);

/**
 * Callback type for continuation prompt generation during rendering.
 *
 * Called when screen_buffer_render_with_continuation encounters a newline.
 * The callback receives the plain text content of the line that just ended
 * (with ANSI codes stripped) and should return the continuation prompt
 * to display on the next line.
 *
 * @param line_text Plain text content of the line that ended (no ANSI codes)
 * @param line_len Length of line_text in bytes
 * @param line_number The logical line number (0-based, line that just ended)
 * @param user_data Opaque pointer passed to
 * screen_buffer_render_with_continuation
 * @return Continuation prompt string for next line, or NULL for no prefix.
 *         String must remain valid until next callback or render completes.
 */
typedef const char *(*screen_buffer_continuation_cb)(const char *line_text,
                                                     size_t line_len,
                                                     int line_number,
                                                     void *user_data);

/**
 * Render prompt and command into screen buffer with continuation prompt
 * support.
 *
 * Like screen_buffer_render but calls a callback on each newline to get
 * context-aware continuation prompts. This enables proper
 * character-by-character tracking where prompts are set at the exact visual row
 * determined during rendering, not pre-calculated.
 *
 * @param buffer Buffer to render into
 * @param prompt_text Prompt string (may contain ANSI codes)
 * @param command_text Command string (may contain ANSI codes)
 * @param cursor_byte_offset Cursor position in command_text (byte offset)
 * @param continuation_cb Callback to get continuation prompt after each newline
 * @param user_data Opaque pointer passed to continuation_cb
 */
void screen_buffer_render_with_continuation(
    screen_buffer_t *buffer, const char *prompt_text, const char *command_text,
    size_t cursor_byte_offset, screen_buffer_continuation_cb continuation_cb,
    void *user_data);

/**
 * Set RPROMPT (right-aligned prompt) text for display on the prompt row
 *
 * Stores the RPROMPT text and calculates whether it fits on the prompt row.
 * RPROMPT fits if: command_start_col + 1 + rprompt_visual_width <=
 * terminal_width (the 1-column gap prevents prompt and rprompt from touching).
 *
 * When command text grows long enough to overlap, rprompt_fits becomes false
 * and the rprompt is hidden — matching zsh behavior.
 *
 * Must be called AFTER screen_buffer_render() so that command_start_col
 * is valid.
 *
 * @param buffer Buffer to update
 * @param rprompt_text Rendered RPROMPT string (may contain ANSI escape codes)
 */
void screen_buffer_set_rprompt(screen_buffer_t *buffer,
                               const char *rprompt_text);

/**
 * Calculate visual width of text, handling ANSI codes and UTF-8
 *
 * @param text Text to measure
 * @param byte_length Length in bytes
 * @return Visual width in columns
 */
size_t screen_buffer_visual_width(const char *text, size_t byte_length);

/**
 * Copy screen buffer (for saving old state)
 *
 * @param dest Destination buffer
 * @param src Source buffer
 */
void screen_buffer_copy(screen_buffer_t *dest, const screen_buffer_t *src);

/// ============================================================================
/// PREFIX SUPPORT FUNCTIONS (Phase 2: Continuation Prompts)
/// ============================================================================

/**
 * Set prefix for a line (e.g., continuation prompt)
 *
 * The prefix is rendered before the line content. Prefixes are tracked
 * separately from content for efficient updates (independent dirty tracking).
 *
 * @param buffer Screen buffer
 * @param line_num Line number (0-based)
 * @param prefix_text Prefix text (will be copied, can be freed after call)
 * @return true on success, false on error (invalid line, allocation failure)
 */
bool screen_buffer_set_line_prefix(screen_buffer_t *buffer, int line_num,
                                   const char *prefix_text);

/**
 * Clear prefix for a line
 *
 * Removes and frees the prefix for the specified line.
 *
 * @param buffer Screen buffer
 * @param line_num Line number (0-based)
 * @return true on success, false on error (invalid line)
 */
bool screen_buffer_clear_line_prefix(screen_buffer_t *buffer, int line_num);

/**
 * Get prefix text for a line
 *
 * @param buffer Screen buffer
 * @param line_num Line number (0-based)
 * @return Prefix text (NULL if no prefix or invalid line)
 */
const char *screen_buffer_get_line_prefix(const screen_buffer_t *buffer,
                                          int line_num);

/**
 * Get visual width of line prefix
 *
 * Returns the visual width of the prefix in columns, accounting for
 * ANSI escape sequences, UTF-8, wide characters, and tabs.
 *
 * @param buffer Screen buffer
 * @param line_num Line number (0-based)
 * @return Visual width in columns (0 if no prefix or invalid line)
 */
size_t screen_buffer_get_line_prefix_visual_width(const screen_buffer_t *buffer,
                                                  int line_num);

/**
 * Check if line prefix is dirty
 *
 * @param buffer Screen buffer
 * @param line_num Line number (0-based)
 * @return true if prefix changed since last render, false otherwise
 */
bool screen_buffer_is_line_prefix_dirty(const screen_buffer_t *buffer,
                                        int line_num);

/**
 * Clear line prefix dirty flag
 *
 * Marks the prefix as clean (rendered). Note: This does NOT clear the
 * content dirty flag - they are tracked independently.
 *
 * @param buffer Screen buffer
 * @param line_num Line number (0-based)
 */
void screen_buffer_clear_line_prefix_dirty(screen_buffer_t *buffer,
                                           int line_num);

/**
 * Translate buffer column to display column
 *
 * Translates a column position in the line content (buffer space) to
 * the corresponding column position on the display (display space),
 * accounting for the prefix width.
 *
 * Example: If prefix is "loop> " (6 columns), buffer column 5 maps to
 *          display column 11.
 *
 * @param buffer Screen buffer
 * @param line_num Line number (0-based)
 * @param buffer_col Column in buffer space (0-based)
 * @return Column in display space, or -1 on error
 */
int screen_buffer_translate_buffer_to_display_col(const screen_buffer_t *buffer,
                                                  int line_num, int buffer_col);

/**
 * Translate display column to buffer column
 *
 * Translates a column position on the display (display space) to the
 * corresponding column position in the line content (buffer space),
 * accounting for the prefix width.
 *
 * If the display column is within the prefix area, returns 0 (start of
 * content).
 *
 * @param buffer Screen buffer
 * @param line_num Line number (0-based)
 * @param display_col Column in display space (0-based)
 * @return Column in buffer space, or -1 on error
 */
int screen_buffer_translate_display_to_buffer_col(const screen_buffer_t *buffer,
                                                  int line_num,
                                                  int display_col);

/**
 * Render a single line with prefix into a string
 *
 * Renders the prefix (if present) followed by the line content into
 * the provided output buffer.
 *
 * @param buffer Screen buffer
 * @param line_num Line number (0-based)
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return true on success, false on error (buffer too small, invalid line)
 */
bool screen_buffer_render_line_with_prefix(const screen_buffer_t *buffer,
                                           int line_num, char *output,
                                           size_t output_size);

/**
 * Render multiple lines with prefixes into a string
 *
 * Renders a range of lines (each with prefix if present) into the
 * provided output buffer, separated by newlines.
 *
 * @param buffer Screen buffer
 * @param start_line First line to render (0-based, inclusive)
 * @param num_lines Number of lines to render
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return true on success, false on error (buffer too small, invalid range)
 */
bool screen_buffer_render_multiline_with_prefixes(const screen_buffer_t *buffer,
                                                  int start_line, int num_lines,
                                                  char *output,
                                                  size_t output_size);

/**
 * Calculate visual width of text with ANSI, UTF-8, wide chars, and tabs
 *
 * This is an enhanced version of screen_buffer_visual_width() that also
 * handles tab expansion.
 *
 * @param text Text to measure
 * @param start_col Starting column position (for tab expansion)
 * @return Visual width in columns
 */
size_t screen_buffer_calculate_visual_width(const char *text, size_t start_col);

/**
 * Render completion menu through screen buffer virtual layout
 *
 * Properly handles ANSI codes and calculates actual line count.
 * Separate from main render function to avoid breaking existing code.
 *
 * @param buffer Screen buffer to use for layout calculation
 * @param menu_text The rendered menu text with ANSI codes
 * @param terminal_width Current terminal width
 * @return Number of lines the menu occupies
 */
int screen_buffer_render_menu(screen_buffer_t *buffer, const char *menu_text,
                              int terminal_width);

/**
 * Calculate visual width of menu without rendering
 *
 * @param menu_text The menu text to measure
 * @return Maximum line width in the menu
 */
int screen_buffer_calculate_menu_width(const char *menu_text);

/**
 * Add plain text rows to screen buffer (for menu, hints, etc.)
 *
 * Parses text line-by-line and adds each line as a new row starting
 * at the specified row. Updates buffer->num_rows to include new rows.
 *
 * This is for adding content AFTER the main command text, like menus.
 * The cursor position is NOT modified - it stays in the command area.
 *
 * Handles:
 * - ANSI escape sequences (colors, bold, etc.) - skip in width calc
 * - UTF-8 characters with proper width calculation
 * - Wide characters (CJK, emoji) - 2 columns
 * - Line wrapping at terminal_width
 * - Explicit newlines in text
 *
 * @param buffer Screen buffer to modify
 * @param start_row Row number to start adding (usually buffer->num_rows)
 * @param text Multi-line text to add (may contain \n, ANSI codes, UTF-8)
 * @return Number of rows added, or -1 on error
 */
int screen_buffer_add_text_rows(screen_buffer_t *buffer, int start_row,
                                const char *text);

/**
 * Get total display rows including any added text rows
 *
 * Returns buffer->num_rows which includes command + menu rows.
 *
 * @param buffer Screen buffer
 * @return Total display rows
 */
int screen_buffer_get_total_display_rows(const screen_buffer_t *buffer);

/**
 * Calculate rows from cursor to end of display
 *
 * Returns how many rows need to be moved up from the end of all
 * displayed content to reach the cursor position.
 *
 * @param buffer Screen buffer
 * @return Number of rows from end of display to cursor
 */
int screen_buffer_get_rows_below_cursor(const screen_buffer_t *buffer);

/// ============================================================================
/// LINE INDEX (paginator cursor-math helper)
/// ============================================================================
///
/// A line-index splits a `const char *` content blob into logical lines
/// (\n-terminated) and records, per line: the byte offset into the
/// content, the byte length (excluding the trailing newline), and the
/// visual_height when wrapped at the given terminal width. Visual width
/// per line is computed using screen_buffer_visual_width, which already
/// handles ANSI escape skipping and UTF-8 wide-character (CJK / emoji)
/// width.
///
/// The index is *immutable* with respect to its source content: the
/// caller owns the content blob and must keep it alive as long as the
/// index is in use. A SIGWINCH-style terminal width change is handled
/// by recomputing visual_height per entry via screen_line_index_rewidth.
///
/// This module is pure cursor math — no terminal I/O, no allocation
/// of the source content, no concurrency. The pager layer (which lives
/// elsewhere) composes a screen_buffer view from a slice of the index;
/// streaming / append-aware variants are layered on top in later steps.

/**
 * @brief One logical line in a paginated content blob
 */
typedef struct {
    size_t byte_offset;   ///< Byte offset into the source content
    size_t byte_length;   ///< Length in bytes; excludes the trailing '\n'
    size_t visual_height; ///< Wrapped rows on the index's terminal_width
} screen_line_index_entry_t;

/**
 * @brief Line-index over a const-char content blob
 *
 * `entries` is a malloc'd array of `count` items; `capacity` is the
 * current allocation. `terminal_width` is the width used to compute
 * each entry's `visual_height` (used for resize invalidation, see
 * screen_line_index_rewidth). `total_visual_rows` is the sum of all
 * `visual_height` values across entries — useful for pager scroll
 * math (how many rows the paginated view will take when fully
 * unrolled).
 */
typedef struct {
    screen_line_index_entry_t *entries;
    size_t count;
    size_t capacity;
    int terminal_width;
    size_t total_visual_rows;
} screen_line_index_t;

/**
 * @brief Build a line index over `content`
 *
 * `content` may contain embedded NULs (caller passes explicit `len`),
 * may or may not end with a newline, may contain ANSI escape sequences
 * (skipped for visual-width purposes), and may contain UTF-8 wide
 * characters (counted as 2 visual columns each).
 *
 * `terminal_width` must be positive. A width of 0 or negative is
 * treated as 1 for safety (each visual column always contributes at
 * least one row).
 *
 * Empty content (`len == 0`) yields a zero-entry index; the function
 * still initializes the struct to a safe state and returns 0.
 *
 * On success, returns 0; the caller releases the entries array with
 * screen_line_index_free. On OOM, returns -1 and the struct is
 * left in a safe (empty) state.
 *
 * @param out             Index to populate; freed first if non-empty
 * @param content         Source content (caller owns; must outlive index)
 * @param len             Length of content in bytes
 * @param terminal_width  Width in columns for visual_height calculation
 * @return 0 on success, -1 on OOM
 */
int screen_line_index_build(screen_line_index_t *out, const char *content,
                            size_t len, int terminal_width);

/**
 * @brief Recompute visual_height entries for a new terminal width
 *
 * Used on SIGWINCH-style resize: byte_offset and byte_length are
 * width-invariant and stay the same; only visual_height per entry
 * and total_visual_rows change. Idempotent — re-calling with the same
 * width is a no-op other than CPU cost.
 *
 * Requires the original content to still be live (the function does
 * not re-derive byte spans; it just recomputes visual widths using
 * the byte_offset / byte_length already on each entry).
 *
 * @param idx               Index to recompute in place
 * @param content           Original content the index was built from
 * @param len               Length of content
 * @param new_terminal_width New terminal width in columns
 */
void screen_line_index_rewidth(screen_line_index_t *idx, const char *content,
                               size_t len, int new_terminal_width);

/**
 * @brief Release the index's entries array
 *
 * Leaves the struct in a safe re-buildable state (entries=NULL,
 * count=0, capacity=0). Does not free the struct itself.
 *
 * @param idx Index to release
 */
void screen_line_index_free(screen_line_index_t *idx);

#ifdef __cplusplus
}
#endif

#endif /// SCREEN_BUFFER_H
