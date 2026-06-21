/**
 * @file history_interactive_search.c
 * @brief LLE History System - Interactive Search Implementation (Spec 09)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements Ctrl+R reverse incremental search for the LLE history system:
 * - Real-time search as user types
 * - Reverse chronological search (most recent first)
 * - Navigation through search results (Ctrl+R for next, Ctrl+S for previous)
 * - Accept/cancel search operations
 * - State management for search sessions
 *
 * Behavior (matches bash/readline):
 * - Ctrl+R enters search mode with empty query
 * - Each keypress updates query and triggers new search
 * - Ctrl+R during search moves to next (older) match
 * - Ctrl+S during search moves to previous (newer) match
 * - Enter accepts current match and exits search
 * - Ctrl+C/Ctrl+G cancels search and returns to original line
 * - Other keys exit search and process the key
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

#define SEARCH_QUERY_MAX_LEN 256
#define SEARCH_PROMPT_MAX_LEN 512

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================
 */

/// Note: lle_interactive_search_state_t is already defined in history.h

/**
 * Interactive search session
 *
 * Manages state for a single Ctrl+R search session.
 * Lives from search initialization until accept/cancel.
 */
typedef struct {
    /// Search state
    lle_interactive_search_state_t state;
    bool active;

    /// Search query
    char query[SEARCH_QUERY_MAX_LEN];
    size_t query_len;

    /// Search results
    lle_history_search_results_t *results;
    size_t current_result_index;

    /// History context
    lle_history_core_t *history_core;

    /// Original state (for cancel)
    char *original_line;
    size_t original_cursor_pos;

    /// Display strings
    char prompt_string[SEARCH_PROMPT_MAX_LEN];

    /// Statistics
    uint64_t searches_performed;
    uint64_t total_search_time_us;

} lle_interactive_search_session_t;

/// Global search session (singleton)
static lle_interactive_search_session_t g_search_session = {
    .state = LLE_SEARCH_STATE_INACTIVE,
    .active = false,
    .query = {0},
    .query_len = 0,
    .results = NULL,
    .current_result_index = 0,
    .history_core = NULL,
    .original_line = NULL,
    .original_cursor_pos = 0,
    .prompt_string = {0},
    .searches_performed = 0,
    .total_search_time_us = 0};

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Update prompt string based on current search state
 *
 * Generates the appropriate prompt string for display during interactive
 * search, such as "(reverse-i-search)`query': " or "(failed reverse-i-search)".
 */
static void update_prompt_string(void) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (!session->active) {
        session->prompt_string[0] = '\0';
        return;
    }

    switch (session->state) {
    case LLE_SEARCH_STATE_ACTIVE:
        if (session->results &&
            lle_history_search_results_get_count(session->results) > 0) {
            snprintf(session->prompt_string, SEARCH_PROMPT_MAX_LEN,
                     "(reverse-i-search)`%.255s': ", session->query);
        } else {
            snprintf(session->prompt_string, SEARCH_PROMPT_MAX_LEN,
                     "(reverse-i-search)`%.255s': ", session->query);
        }
        break;

    case LLE_SEARCH_STATE_NO_RESULTS:
        snprintf(session->prompt_string, SEARCH_PROMPT_MAX_LEN,
                 "(failed reverse-i-search)`%.255s': ", session->query);
        break;

    case LLE_SEARCH_STATE_FAILED:
        snprintf(session->prompt_string, SEARCH_PROMPT_MAX_LEN,
                 "(failed reverse-i-search)`%.255s': ", session->query);
        break;

    default:
        session->prompt_string[0] = '\0';
        break;
    }
}

/**
 * @brief Perform search with current query
 *
 * Executes a substring search using the current query string.
 * Updates search state and statistics.
 *
 * @return true if search succeeded and found results, false otherwise
 */
static bool perform_search(void) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (!session->history_core) {
        session->state = LLE_SEARCH_STATE_FAILED;
        return false;
    }

    /// Clean up previous results
    if (session->results) {
        lle_history_search_results_destroy(session->results);
        session->results = NULL;
    }

    /// Empty query = no search
    if (session->query_len == 0) {
        session->state = LLE_SEARCH_STATE_NO_RESULTS;
        session->current_result_index = 0;
        update_prompt_string();
        return false;
    }

    /// Perform substring search (most useful for interactive search)
    session->results = lle_history_search_substring(
        session->history_core, session->query, 100 /// max results
    );

    if (!session->results) {
        session->state = LLE_SEARCH_STATE_FAILED;
        update_prompt_string();
        return false;
    }

    /// Update statistics
    session->searches_performed++;
    session->total_search_time_us +=
        lle_history_search_results_get_time_us(session->results);

    /// Check if we found any results
    size_t result_count =
        lle_history_search_results_get_count(session->results);
    if (result_count == 0) {
        session->state = LLE_SEARCH_STATE_NO_RESULTS;
        session->current_result_index = 0;
        update_prompt_string();
        return false;
    }

    /// Success - we have results
    session->state = LLE_SEARCH_STATE_ACTIVE;
    session->current_result_index =
        0; /// Start with most recent (highest score)
    update_prompt_string();

    return true;
}

/* ============================================================================
 * PUBLIC API - SESSION MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Initialize interactive search session
 *
 * Starts a new Ctrl+R search session. Saves the current line and cursor
 * position so they can be restored on cancel. If a previous session is
 * active, it will be cancelled first.
 *
 * @param history_core History core engine (must not be NULL)
 * @param current_line Current line buffer (will be saved for cancel, may be
 * NULL)
 * @param cursor_pos Current cursor position
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if history_core
 * is NULL
 */
lle_result_t
lle_history_interactive_search_init(lle_history_core_t *history_core,
                                    const char *current_line,
                                    size_t cursor_pos) {
    if (!history_core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_interactive_search_session_t *session = &g_search_session;

    /// If already active, cancel the previous search first
    if (session->active) {
        lle_history_interactive_search_cancel();
    }

    /// CRITICAL FIX: Clean up any leftover results from previous session
    /// If the previous search ended with accept() rather than cancel(),
    /// the results were kept alive for the caller to read. We must free
    /// them now to prevent memory leak.
    if (session->results) {
        lle_history_search_results_destroy(session->results);
        session->results = NULL;
    }

    /// Initialize session
    session->state = LLE_SEARCH_STATE_ACTIVE;
    session->active = true;
    session->history_core = history_core;
    session->query[0] = '\0';
    session->query_len = 0;
    session->results = NULL;
    session->current_result_index = 0;

    /// Save original state for cancel
    if (session->original_line) {
        lle_pool_free(session->original_line);
    }
    session->original_line =
        current_line ? lle_pool_strdup(current_line) : NULL;
    session->original_cursor_pos = cursor_pos;

    /// Update prompt
    update_prompt_string();

    return LLE_SUCCESS;
}

/**
 * @brief Update search query with new character
 *
 * Appends a character to the search query and re-runs the search.
 * This is called for each keypress during interactive search.
 *
 * @param c Character to append to query
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_STATE if no active session,
 *         LLE_ERROR_BUFFER_OVERFLOW if query is at maximum length
 */
lle_result_t lle_history_interactive_search_update_query(char c) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (!session->active) {
        return LLE_ERROR_INVALID_STATE;
    }

    /// Check if we have room for another character
    if (session->query_len >= SEARCH_QUERY_MAX_LEN - 1) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    /// Append character to query
    session->query[session->query_len] = c;
    session->query_len++;
    session->query[session->query_len] = '\0';

    /// Re-run search with updated query
    perform_search();

    return LLE_SUCCESS;
}

/**
 * @brief Remove last character from search query (backspace)
 *
 * Removes the last character from the query and re-runs the search.
 * Does nothing if query is already empty.
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_STATE if no active session
 */
lle_result_t lle_history_interactive_search_backspace(void) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (!session->active) {
        /// No active search session
        return LLE_ERROR_INVALID_STATE;
    }

    if (session->query_len == 0) {
        /// Nothing to delete
        return LLE_SUCCESS;
    }

    /// Remove last character
    session->query_len--;
    session->query[session->query_len] = '\0';

    /// Re-run search with updated query
    perform_search();

    return LLE_SUCCESS;
}

/**
 * @brief Move to next (older) search result
 *
 * Called when user presses Ctrl+R during an active search.
 * Moves to the next match in the result list (older command).
 * Wraps around to first result when reaching the end.
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_STATE if no active session
 */
lle_result_t lle_history_interactive_search_next(void) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (!session->active) {
        return LLE_ERROR_INVALID_STATE;
    }

    if (!session->results || session->state != LLE_SEARCH_STATE_ACTIVE) {
        /// No results to navigate
        return LLE_SUCCESS;
    }

    size_t result_count =
        lle_history_search_results_get_count(session->results);
    if (result_count == 0) {
        return LLE_SUCCESS;
    }

    /// Move to next result (wrap around)
    session->current_result_index++;
    if (session->current_result_index >= result_count) {
        session->current_result_index = 0; /// Wrap to first result
    }

    return LLE_SUCCESS;
}

/**
 * @brief Move to previous (newer) search result
 *
 * Called when user presses Ctrl+S during an active search.
 * Moves to the previous match in the result list (newer command).
 * Wraps around to last result when reaching the beginning.
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_STATE if no active session
 */
lle_result_t lle_history_interactive_search_prev(void) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (!session->active) {
        return LLE_ERROR_INVALID_STATE;
    }

    if (!session->results || session->state != LLE_SEARCH_STATE_ACTIVE) {
        /// No results to navigate
        return LLE_SUCCESS;
    }

    size_t result_count =
        lle_history_search_results_get_count(session->results);
    if (result_count == 0) {
        return LLE_SUCCESS;
    }

    /// Move to previous result (wrap around)
    if (session->current_result_index == 0) {
        session->current_result_index =
            result_count - 1; /// Wrap to last result
    } else {
        session->current_result_index--;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Accept current search result and exit search mode
 *
 * Returns the selected command for execution. The caller is responsible
 * for placing this command in the input buffer. The returned string is
 * owned by the search session and must be copied before the next search.
 *
 * @return Command string (owned by search session, must be copied) or NULL if
 * no active session or no results
 */
const char *lle_history_interactive_search_accept(void) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (!session->active) {
        return NULL;
    }

    const char *selected_command = NULL;

    /// Get the currently selected result
    if (session->results && session->state == LLE_SEARCH_STATE_ACTIVE) {
        size_t result_count =
            lle_history_search_results_get_count(session->results);
        if (result_count > 0 && session->current_result_index < result_count) {
            const lle_search_result_t *result = lle_history_search_results_get(
                session->results, session->current_result_index);
            if (result) {
                selected_command = result->command;
            }
        }
    }

    /// Clean up session but don't free the command yet (caller needs it)
    if (session->results) {
        /// Note: We return a pointer to command in results, so we can't destroy
        /// yet. Caller must copy the string before we destroy results. For now,
        /// we'll keep results alive and destroy on next init.
    }

    /// Mark session as inactive
    session->active = false;
    session->state = LLE_SEARCH_STATE_INACTIVE;

    return selected_command;
}

/**
 * @brief Cancel search and restore original line
 *
 * Returns the original line that was active when search started.
 * Frees search results and marks the session as inactive.
 *
 * @return Original line (owned by session, must be copied) or NULL if no active
 * session
 */
const char *lle_history_interactive_search_cancel(void) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (!session->active) {
        return NULL;
    }

    const char *original = session->original_line;

    /// Clean up search results
    if (session->results) {
        lle_history_search_results_destroy(session->results);
        session->results = NULL;
    }

    /// Mark session as inactive
    session->active = false;
    session->state = LLE_SEARCH_STATE_INACTIVE;

    /// Note: We don't free original_line here because caller needs it.
    /// It will be freed on next init or when session is destroyed.

    return original;
}

/* ============================================================================
 * PUBLIC API - QUERY INFORMATION
 * ============================================================================
 */

/**
 * @brief Check if search is currently active
 *
 * Returns whether an interactive search session is currently in progress.
 *
 * @return true if search session is active, false otherwise
 */
bool lle_history_interactive_search_is_active(void) {
    return g_search_session.active;
}

/**
 * @brief Get current search query
 *
 * Returns the current search query string entered by the user.
 *
 * @return Current query string (read-only), or empty string if no active
 * session
 */
const char *lle_history_interactive_search_get_query(void) {
    if (!g_search_session.active) {
        return "";
    }
    return g_search_session.query;
}

/**
 * @brief Get current search prompt string
 *
 * Returns the prompt string to display (e.g., "(reverse-i-search)`query': ").
 * Updates based on search state (active, no results, failed).
 *
 * @return Prompt string (read-only), or empty string if no active session
 */
const char *lle_history_interactive_search_get_prompt(void) {
    if (!g_search_session.active) {
        return "";
    }
    return g_search_session.prompt_string;
}

/**
 * @brief Get currently selected command
 *
 * Returns the command that is currently highlighted in the search results.
 * This is what would be accepted if the user presses Enter.
 *
 * @return Current command (read-only) or NULL if no active session, no results,
 * or index out of bounds
 */
const char *lle_history_interactive_search_get_current_command(void) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (!session->active || !session->results) {
        return NULL;
    }

    if (session->state != LLE_SEARCH_STATE_ACTIVE) {
        return NULL;
    }

    size_t result_count =
        lle_history_search_results_get_count(session->results);
    if (result_count == 0 || session->current_result_index >= result_count) {
        return NULL;
    }

    const lle_search_result_t *result = lle_history_search_results_get(
        session->results, session->current_result_index);

    return result ? result->command : NULL;
}

/**
 * @brief Get search state
 *
 * Returns the current state of the interactive search session.
 *
 * @return Current search state (INACTIVE, ACTIVE, NO_RESULTS, or FAILED)
 */
lle_interactive_search_state_t lle_history_interactive_search_get_state(void) {
    return g_search_session.state;
}

/* ============================================================================
 * PUBLIC API - STATISTICS AND DIAGNOSTICS
 * ============================================================================
 */

/**
 * @brief Get search statistics
 *
 * Returns cumulative statistics about search performance since last reset.
 *
 * @param searches_performed Output for number of searches performed (may be
 * NULL)
 * @param total_time_us Output for total search time in microseconds (may be
 * NULL)
 * @param avg_time_us Output for average search time in microseconds (may be
 * NULL)
 * @return LLE_SUCCESS always
 */
lle_result_t
lle_history_interactive_search_get_stats(uint64_t *searches_performed,
                                         uint64_t *total_time_us,
                                         uint64_t *avg_time_us) {
    lle_interactive_search_session_t *session = &g_search_session;

    if (searches_performed) {
        *searches_performed = session->searches_performed;
    }

    if (total_time_us) {
        *total_time_us = session->total_search_time_us;
    }

    if (avg_time_us) {
        if (session->searches_performed > 0) {
            *avg_time_us =
                session->total_search_time_us / session->searches_performed;
        } else {
            *avg_time_us = 0;
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Print search statistics (for debugging)
 *
 * Outputs detailed search session statistics to stdout, including
 * active state, query, search counts, timing, and current results.
 */
void lle_history_interactive_search_print_stats(void) {
    lle_interactive_search_session_t *session = &g_search_session;

    printf("Interactive Search Statistics:\n");
    printf("  Active: %s\n", session->active ? "yes" : "no");
    printf("  State: %s\n",
           session->state == LLE_SEARCH_STATE_INACTIVE     ? "inactive"
           : session->state == LLE_SEARCH_STATE_ACTIVE     ? "active"
           : session->state == LLE_SEARCH_STATE_NO_RESULTS ? "no results"
                                                           : "failed");
    printf("  Query: \"%s\" (len=%zu)\n", session->query, session->query_len);
    printf("  Total Searches: %" PRIu64 "\n", session->searches_performed);
    printf("  Total Time: %" PRIu64 " μs\n", session->total_search_time_us);

    if (session->searches_performed > 0) {
        printf("  Avg Time: %" PRIu64 " μs\n",
               session->total_search_time_us / session->searches_performed);
    }

    if (session->results) {
        size_t count = lle_history_search_results_get_count(session->results);
        printf("  Current Results: %zu\n", count);
        printf("  Current Index: %zu\n", session->current_result_index);
    }
}

/**
 * @brief Reset search statistics
 *
 * Clears the cumulative search statistics (searches performed and total time).
 */
void lle_history_interactive_search_reset_stats(void) {
    g_search_session.searches_performed = 0;
    g_search_session.total_search_time_us = 0;
}
