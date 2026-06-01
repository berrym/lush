/**
 * @file compdef_source.h
 * @brief Registration of the compdef-binding LLE completion source.
 *
 * compdef_source_init() registers a custom completion source with the
 * LLE source manager. The source's is_applicable callback consults the
 * compdef bindings table (compdef_lookup); when a binding exists for
 * the current word context's command_name, the source's generate
 * callback sets executor->active_comp_result, invokes the bound shell
 * function in-process via executor_run_function (which sets up
 * positional params $1=cmd, $2=current_word, $3=previous_word),
 * restores active_comp_result, and returns. Candidates are
 * accumulated into the result via compadd, which appends through
 * completion_bridge_add while the function runs.
 *
 * Call once at shell startup AFTER lle_editor_create has run so the
 * source manager exists, AND AFTER init_compdef_bindings so the
 * bindings table is ready.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_COMPDEF_SOURCE_H
#define LUSH_COMPDEF_SOURCE_H

/**
 * @brief Register the compdef completion source with the LLE engine.
 *
 * Idempotent. Safe to call multiple times; the underlying
 * lle_completion_register_source is responsible for rejecting duplicate
 * registrations of the same source name.
 */
void compdef_source_init(void);

#endif /* LUSH_COMPDEF_SOURCE_H */
