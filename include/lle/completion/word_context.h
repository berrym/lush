/**
 * @file word_context.h
 * @brief Quote/escape/expansion-aware word-context analyzer for completion
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Given a buffer and a cursor byte offset, produce a structured description
 * of what the user is currently completing — where the shell-word starts, what
 * quote/escape state the cursor is in, where the filename portion begins
 * (after any path operators or expansion prefixes), what kind of expansion
 * (if any) the cursor is inside, and what context the word sits in
 * (command position, argument, redirect target, etc.).
 *
 * This is the single source of truth for completion's understanding of the
 * buffer. It replaces the two parallel analyzers
 * (lle_context_analyzer_t in context_analyzer.c and
 *  lle_completion_context_info_t in completion_generator.c) that diverged on
 * word-boundary detection in any input that wasn't ASCII-letters-only.
 *
 * Design rationale lives in docs/development/COMPLETION_REWRITE_PLAN.md
 * Section 6.
 *
 * Key invariants:
 * - Walks codepoint by codepoint via lle_utf8_decode_codepoint and
 *   lle_is_grapheme_boundary; never byte-by-byte.
 * - dequoted_filename_prefix is NFC-normalized; sources receive it
 *   ready-to-pass to lle_unicode_is_prefix.
 * - All byte offsets are in the input buffer's byte coordinate system.
 *   Codepoint and grapheme indices are not exposed directly; consumers that
 *   need them use lle_utf8_index_t or the standard mapping functions.
 * - The struct is allocated from the supplied memory pool and freed via
 *   lle_word_context_free; allocations are not transferable across pools.
 */

#ifndef LLE_WORD_CONTEXT_H
#define LLE_WORD_CONTEXT_H

#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Quote / escape state
 * ============================================================================
 */

/**
 * @brief Quote/escape state at the cursor
 *
 * The cursor sits at a position whose lexical surroundings determine how the
 * splicer must render an inserted candidate. Because the analyzer walks the
 * full shell lexical state machine, this state is exact (matches what the
 * tokenizer would compute at the same byte offset).
 */
typedef enum {
    LLE_QUOTE_NONE,           /**< Not inside any quote, no pending escape  */
    LLE_QUOTE_SINGLE,         /**< Inside a single-quoted span ('...)       */
    LLE_QUOTE_DOUBLE,         /**< Inside a double-quoted span ("...)       */
    LLE_QUOTE_BACKTICK,       /**< Inside a backtick command sub (`...)     */
    LLE_QUOTE_ESCAPE_PENDING, /**< Cursor is right after a backslash; the
                                   next codepoint typed will be escaped.    */
} lle_quote_state_t;

/* ============================================================================
 * Expansion kind (only set when cursor is INSIDE an in-progress expansion)
 * ============================================================================
 */

/**
 * @brief What expansion the cursor is inside, if any
 *
 * Set only when the cursor lies inside an in-progress expansion (the user has
 * typed an opener like '$', '${', '$(', '$((', '`', '{', or a glob
 * metacharacter, and the expansion has not yet closed). When the cursor is
 * past a complete expansion or no expansion is active, this is
 * LLE_EXPANSION_NONE — the word_context's other fields describe the active
 * filename completion in that case.
 */
typedef enum {
    LLE_EXPANSION_NONE,
    LLE_EXPANSION_VARIABLE_NAME, /**< $HO|     — bare $name being typed  */
    LLE_EXPANSION_BRACED_VARIABLE_NAME, /**< ${HO|}   — braced ${name} being
                                           typed */
    LLE_EXPANSION_COMMAND_SUBST, /**< $(ls|)   — inside $(...)           */
    LLE_EXPANSION_ARITHMETIC,    /**< $((1+|)) — inside $((...))         */
    LLE_EXPANSION_BRACE_LIST,    /**< {a,b|}   — inside an open brace    */
    LLE_EXPANSION_GLOB,          /**< *.t|     — cursor in mid-glob      */
} lle_expansion_kind_t;

/* ============================================================================
 * Completion context type
 * ============================================================================
 */

/**
 * @brief Where the word sits in the surrounding command structure
 *
 * Determines which sources the engine queries for candidates. Sources read
 * this field to decide applicability without having to re-walk the buffer.
 */
typedef enum {
    LLE_CONTEXT_COMMAND_POSITION, /**< Start of pipeline/list element   */
    LLE_CONTEXT_ARGUMENT,         /**< Argument to the current command  */
    LLE_CONTEXT_REDIRECT_TARGET,  /**< After >, <, >>, etc.             */
    LLE_CONTEXT_VARIABLE_NAME,    /**< Inside $name or ${name (variable
                                       name completion, not file)        */
    LLE_CONTEXT_ASSIGNMENT_VALUE, /**< Right side of VAR=value           */
    LLE_CONTEXT_FOR_IN_LIST,      /**< for x in <here>                   */
    LLE_CONTEXT_CASE_PATTERN,     /**< case x in <here>)                 */
    LLE_CONTEXT_HEREDOC_BODY,     /**< Inside a heredoc body (refuse to
                                       complete; literal text)           */
    LLE_CONTEXT_UNKNOWN,
} lle_word_context_type_t;

/* Note: a "function body" is not a distinct context_type. Inside a
 * function definition the cursor is still at COMMAND_POSITION (for the
 * first word of an inner statement) or ARGUMENT (for arguments to that
 * statement); completion dispatch is identical to top-level. If a
 * future need arises to distinguish "we are inside a function" — for
 * example to surface only function-local variables — that can be
 * added as a separate flag without overloading context_type. */

/* ============================================================================
 * Multi-value expansion branch
 * ============================================================================
 */

/**
 * @brief One branch of a multi-value expansion (brace expansion only at
 *        present)
 *
 * Populated when the analyzer determines that the typed shell-word contains
 * a brace expansion that produces multiple resolved paths. The engine uses
 * this set to run per-branch file completion and intersect/union the
 * candidate sets per the configured brace_expansion_mode.
 *
 * For simple single-value expansions (variables, tildes, parameter,
 * arithmetic), this set is empty and the engine uses the fields
 * `expanded_directory` (resolved once) and `dequoted_filename_prefix`
 * directly.
 */
typedef struct lle_word_context_branch {
    char *expanded_directory;       /**< Absolute path to scan (resolved)   */
    char *dequoted_filename_prefix; /**< NFC-normalized post-/ prefix       */
} lle_word_context_branch_t;

/* ============================================================================
 * The analyzer's output
 * ============================================================================
 */

/**
 * @brief Structured description of completion context at a buffer position
 *
 * All fields are owned by the supplied memory pool. The struct itself and any
 * internally-allocated strings are released by lle_word_context_free.
 */
typedef struct lle_word_context {

    /* Buffer coordinates -------------------------------------------------- */
    size_t word_start;             /**< Byte offset where the current
                                        shell-word begins, including any
                                        open quote character.               */
    size_t word_end;               /**< Equals the cursor byte offset.      */
    size_t expansion_prefix_end;   /**< Byte offset where the expansion
                                        portion of the word ends. After
                                        this byte is either a path
                                        separator + filename, or the rest
                                        of a non-expansion word.            */
    size_t filename_portion_start; /**< Byte offset where the filename-
                                        prefix portion begins. For path-
                                        shaped words this is the byte after
                                        the last '/' in the dequoted word
                                        content. For non-path words this
                                        equals word_start (or word_start+N
                                        when an open quote/escape consumes
                                        leading bytes).                     */

    /* Lexical state at cursor -------------------------------------------- */
    lle_quote_state_t quote_state;
    lle_expansion_kind_t expansion_kind;

    /* Surrounding-command context --------------------------------------- */
    lle_word_context_type_t context_type;
    char *command_name;    /**< Owner command for builtin-arg
                                dispatch (e.g., "cd", "set"). NULL
                                in command-position contexts.       */
    int arg_index;         /**< Zero-based index of this argument
                                within its command. -1 if not
                                applicable.                         */
    char **arguments;      /**< Pool-allocated array of completed
                                argument strings before the
                                cursor's current word, dequoted
                                and NFC-normalized. Used by
                                builtin-arg sources to walk
                                subcommand hierarchies (e.g.,
                                recognizing the chain in
                                `display lle theme set <here>`).
                                NULL when no completed arguments
                                exist.                              */
    size_t argument_count; /**< Number of entries in arguments[]. */

    /* Resolved data for sources ------------------------------------------ */
    char *expanded_directory;       /**< Absolute path to scan when the word
                                         is path-shaped and produces a
                                         single resolved directory. NULL for
                                         non-path words and for multi-value
                                         expansions (use branches[] then).   */
    char *dequoted_filename_prefix; /**< NFC-normalized, dequoted, post-/
                                         literal prefix the source uses for
                                         prefix-matching against
                                         candidates.                         */

    /* Multi-value expansion (brace only currently) ----------------------- */
    lle_word_context_branch_t *branches; /**< Array of per-branch resolved
                                              directory + prefix pairs.
                                              NULL when single-value.       */
    size_t branch_count;

    /* Bookkeeping -------------------------------------------------------- */
    lle_memory_pool_t *pool; /**< Pool used for all allocations.      */
} lle_word_context_t;

/* ============================================================================
 * Public API
 * ============================================================================
 */

/**
 * @brief Analyze the buffer at a cursor position and produce a word context
 *
 * Walks the buffer codepoint by codepoint from the most recent unambiguous
 * statement boundary up to the cursor, tracking quote/escape state, paren/
 * brace/bracket nesting, and current command/argument structure. Resolves
 * single-value expansions (variables, tildes, parameter, arithmetic) via
 * lush's expansion machinery (src/expand.c) so the resulting
 * `expanded_directory` is ready for opendir(); for multi-value brace
 * expansion, populates `branches`.
 *
 * Side-effect-free expansions (variables, tildes, parameter, arithmetic)
 * are always evaluated. Command substitution `$(...)` and backticks are
 * evaluated only when `completion.eval_command_subst` is true (the
 * bash/zsh-consensus default); when false the analyzer treats them as
 * opaque and may set `expansion_kind` accordingly.
 *
 * @param buffer Input buffer (must be NUL-terminated and valid UTF-8 up to
 *               at least cursor_byte_offset).
 * @param cursor_byte_offset Byte offset of the cursor within `buffer`.
 *               Must be <= strlen(buffer).
 * @param pool Memory pool for all allocations made by this call.
 * @param out_context On LLE_SUCCESS, set to a freshly-allocated context.
 *               Must be released via lle_word_context_free.
 *
 * @return LLE_SUCCESS or an error code (LLE_ERROR_INVALID_PARAMETER,
 *         LLE_ERROR_OUT_OF_MEMORY).
 */
lle_result_t lle_word_context_analyze(const char *buffer,
                                      size_t cursor_byte_offset,
                                      lle_memory_pool_t *pool,
                                      lle_word_context_t **out_context);

/**
 * @brief Release a word context and all its pool-owned allocations
 *
 * Safe to call with NULL.
 *
 * @param context Context to free.
 */
void lle_word_context_free(lle_word_context_t *context);

/**
 * @brief Render a quote_state as a stable string (for logging/tests)
 * @param state The quote state to render.
 * @return A static string. Never NULL.
 */
const char *lle_quote_state_name(lle_quote_state_t state);

/**
 * @brief Render an expansion_kind as a stable string (for logging/tests)
 * @param kind The expansion kind to render.
 * @return A static string. Never NULL.
 */
const char *lle_expansion_kind_name(lle_expansion_kind_t kind);

/**
 * @brief Render a context_type as a stable string (for logging/tests)
 * @param type The context type to render.
 * @return A static string. Never NULL.
 */
const char *lle_word_context_type_name(lle_word_context_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* LLE_WORD_CONTEXT_H */
