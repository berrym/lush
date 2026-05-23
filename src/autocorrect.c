/**
 * @file autocorrect.c
 * @brief Auto-Correction System Implementation for Lush Shell
 *
 * Intelligent command spell checking and correction suggestions.
 * Provides "Did you mean...?" functionality with user confirmation prompts.
 * Integrates with existing completion algorithms and configuration system.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (c) 2025 Michael Berry. All rights reserved.
 */

#include "autocorrect.h"
#include "fuzzy_match.h"

#include "builtins.h"
#include "executor.h"
#include "init.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// Global auto-correction configuration
static autocorrect_config_t autocorrect_config = {0};

// Statistics tracking
static struct {
    int corrections_offered;
    int corrections_accepted;
    int commands_learned;
} autocorrect_stats = {0};

// Debug flag
static bool debug_enabled = false;

// Learned commands cache (simple array for now)
#define MAX_LEARNED_COMMANDS 1000
static char *learned_commands[MAX_LEARNED_COMMANDS];
static int learned_commands_count = 0;

// Forward declarations for internal helper functions
static void sort_corrections_by_score(correction_t *corrections, int count);
static bool is_executable_file(const char *path);

/**
 * Initialize auto-correction system
 */
int autocorrect_init(void) {
    // Set default configuration
    autocorrect_get_default_config(&autocorrect_config);

    // Initialize learned commands array
    for (int i = 0; i < MAX_LEARNED_COMMANDS; i++) {
        learned_commands[i] = NULL;
    }
    learned_commands_count = 0;

    // Reset statistics
    autocorrect_reset_stats();

    if (debug_enabled) {
        printf("DEBUG: Auto-correction system initialized\n");
    }

    return 0;
}

/**
 * Cleanup auto-correction system
 */
void autocorrect_cleanup(void) {
    // Free learned commands
    for (int i = 0; i < learned_commands_count; i++) {
        if (learned_commands[i]) {
            free(learned_commands[i]);
            learned_commands[i] = NULL;
        }
    }
    learned_commands_count = 0;

    if (debug_enabled) {
        printf("DEBUG: Auto-correction system cleaned up\n");
    }
}

/**
 * Load auto-correction configuration
 */
int autocorrect_load_config(const autocorrect_config_t *config) {
    if (!config) {
        return -1;
    }

    if (!autocorrect_validate_config(config)) {
        return -1;
    }

    autocorrect_config = *config;

    if (debug_enabled) {
        printf("DEBUG: Auto-correction config loaded - enabled: %s, "
               "max_suggestions: %d\n",
               autocorrect_config.enabled ? "true" : "false",
               autocorrect_config.max_suggestions);
    }

    return 0;
}

/**
 * Check if auto-correction is enabled
 */
bool autocorrect_is_enabled(void) { return autocorrect_config.enabled; }

/**
 * Find correction suggestions for a misspelled command
 */
int autocorrect_find_suggestions(executor_t *executor, const char *command,
                                 correction_results_t *results) {
    if (!command || !results) {
        return 0;
    }

    /* Always zero the results struct before any further early return
     * so callers can safely call autocorrect_free_results() on it
     * regardless of whether we found suggestions, ran at all, or
     * bailed because autocorrect is disabled. The previous code
     * exited on the !enabled check before the memset, leaving the
     * caller with an uninitialised stack struct that free_results
     * then passed to free() on the next "command not found" event
     * (issue #60: bad-free in autocorrect_free_results). */
    memset(results, 0, sizeof(correction_results_t));

    if (!autocorrect_config.enabled) {
        return 0;
    }

    /* Skip autocorrect in non-interactive shells. The "did you mean ...?"
     * suggestions are useful only when the user reads them; in scripts
     * and tight command-not-found loops they are wasted CPU. Per #84
     * sample profile, autocorrect_suggest_path_commands accounts for
     * ~95% of per-iteration cost in a `for ((;;)); do bad; done` loop
     * because every iteration walks $PATH, stat()s every binary, and
     * runs Damerau-Levenshtein fuzzy matching. Skipping in non-
     * interactive mode eliminates the amplifier entirely; interactive
     * users still get suggestions on real misspellings. */
    if (!is_interactive_shell()) {
        return 0;
    }

    results->original_command = strdup(command);

    // Temporary array to collect all suggestions from all sources
    // Each source can contribute up to MAX_CORRECTIONS, then we sort and take
    // best
    correction_t temp_suggestions[MAX_CORRECTIONS * 4]; // Space for all sources
    int temp_count = 0;
    int max_per_source = MAX_CORRECTIONS;

    // Find builtin suggestions
    if (autocorrect_config.correct_builtins &&
        temp_count < MAX_CORRECTIONS * 4) {
        int builtin_count = autocorrect_suggest_builtins(
            command, &temp_suggestions[temp_count], max_per_source,
            autocorrect_config.case_sensitive);
        temp_count += builtin_count;

        if (debug_enabled && builtin_count > 0) {
            printf("DEBUG: Found %d builtin suggestions for '%s'\n",
                   builtin_count, command);
        }
    }

    // Find function suggestions
    if (executor && temp_count < MAX_CORRECTIONS * 4) {
        int function_count = autocorrect_suggest_functions(
            executor, command, &temp_suggestions[temp_count], max_per_source,
            autocorrect_config.case_sensitive);
        temp_count += function_count;

        if (debug_enabled && function_count > 0) {
            printf("DEBUG: Found %d function suggestions for '%s'\n",
                   function_count, command);
        }
    }

    // Find PATH command suggestions
    if (autocorrect_config.correct_external &&
        temp_count < MAX_CORRECTIONS * 4) {
        int path_count = autocorrect_suggest_path_commands(
            command, &temp_suggestions[temp_count], max_per_source,
            autocorrect_config.case_sensitive);
        temp_count += path_count;

        if (debug_enabled && path_count > 0) {
            printf("DEBUG: Found %d PATH suggestions for '%s'\n", path_count,
                   command);
        }
    }

    // Find history suggestions
    if (autocorrect_config.learn_from_history &&
        temp_count < MAX_CORRECTIONS * 4) {
        int history_count = autocorrect_suggest_from_history(
            command, &temp_suggestions[temp_count], max_per_source,
            autocorrect_config.case_sensitive);
        temp_count += history_count;

        if (debug_enabled && history_count > 0) {
            printf("DEBUG: Found %d history suggestions for '%s'\n",
                   history_count, command);
        }
    }

    // Sort suggestions by score (highest first)
    sort_corrections_by_score(temp_suggestions, temp_count);

    // Copy top suggestions to results, filtering by threshold, max count, and
    // duplicates
    int max_to_copy = (autocorrect_config.max_suggestions < MAX_CORRECTIONS)
                          ? autocorrect_config.max_suggestions
                          : MAX_CORRECTIONS;

    for (int i = 0; i < temp_count && results->count < max_to_copy; i++) {
        if (temp_suggestions[i].score >=
            autocorrect_config.similarity_threshold) {
            // Check for duplicates (same command from different sources)
            bool is_duplicate = false;
            for (int j = 0; j < results->count; j++) {
                if (strcmp(results->suggestions[j].command,
                           temp_suggestions[i].command) == 0) {
                    is_duplicate = true;
                    break;
                }
            }
            if (!is_duplicate) {
                results->suggestions[results->count] = temp_suggestions[i];
                temp_suggestions[i].command = NULL; // Transferred ownership
                results->count++;
            }
        }
    }

    // Free any temp_suggestions that were not copied to results
    for (int i = 0; i < temp_count; i++) {
        if (temp_suggestions[i].command) {
            free(temp_suggestions[i].command);
            temp_suggestions[i].command = NULL;
        }
    }

    // Update statistics
    if (results->count > 0) {
        autocorrect_stats.corrections_offered++;
    }

    if (debug_enabled) {
        printf("DEBUG: Returning %d suggestions for '%s'\n", results->count,
               command);
    }

    return results->count;
}

/**
 * Present correction suggestions to user and get selection
 */
bool autocorrect_prompt_user(const correction_results_t *results,
                             char *selected_command) {
    if (!results || !selected_command ||
        !autocorrect_config.interactive_prompts) {
        return false;
    }

    // Don't prompt if not running in an interactive terminal
    if (!isatty(STDIN_FILENO)) {
        return false;
    }

    if (results->count == 0) {
        return false;
    }

    printf("lush: command not found: %s\n", results->original_command);
    printf("\nDid you mean:\n");

    for (int i = 0; i < results->count; i++) {
        printf("  %d) %s", i + 1, results->suggestions[i].command);
        if (debug_enabled) {
            printf(" (score: %d, source: %s)", results->suggestions[i].score,
                   results->suggestions[i].source);
        }
        printf("\n");
    }

    printf("  0) Cancel\n");
    printf("\nSelect (0-%d): ", results->count);
    fflush(stdout);

    /* Ensure terminal is in canonical mode with proper CR->NL translation.
     * This is needed because LLE may have left terminal in a state where
     * ICRNL is disabled, causing fgets() to see raw CR (^M) instead of NL. */
    struct termios orig_term, cooked_term;
    bool term_modified = false;
    if (tcgetattr(STDIN_FILENO, &orig_term) == 0) {
        cooked_term = orig_term;
        cooked_term.c_iflag |= ICRNL;           // Translate CR to NL
        cooked_term.c_lflag |= (ICANON | ECHO); // Canonical mode with echo
        if (tcsetattr(STDIN_FILENO, TCSANOW, &cooked_term) == 0) {
            term_modified = true;
        }
    }

    char input[10];
    if (!fgets(input, sizeof(input), stdin)) {
        if (term_modified) {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
        }
        return false;
    }

    // Restore original terminal state
    if (term_modified) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
    }

    int choice = atoi(input);
    if (choice >= 1 && choice <= results->count) {
        strncpy(selected_command, results->suggestions[choice - 1].command,
                MAX_COMMAND_LENGTH - 1);
        selected_command[MAX_COMMAND_LENGTH - 1] = '\0';

        autocorrect_stats.corrections_accepted++;

        if (debug_enabled) {
            printf("DEBUG: User selected correction: '%s'\n", selected_command);
        }

        return true;
    }

    return false;
}

/**
 * Free memory allocated in correction results
 */
void autocorrect_free_results(correction_results_t *results) {
    if (!results) {
        return;
    }

    if (results->original_command) {
        free(results->original_command);
        results->original_command = NULL;
    }

    for (int i = 0; i < results->count; i++) {
        if (results->suggestions[i].command) {
            free(results->suggestions[i].command);
            results->suggestions[i].command = NULL;
        }
    }

    results->count = 0;
}

/**
 * Calculate similarity score between two commands
 * Now delegates to libfuzzy for Unicode-aware matching
 */
int autocorrect_similarity_score(const char *command1, const char *command2,
                                 bool case_sensitive) {
    if (!command1 || !command2) {
        return 0;
    }

    // Use libfuzzy with appropriate options
    fuzzy_match_options_t opts = FUZZY_MATCH_DEFAULT;
    opts.case_sensitive = case_sensitive;
    // Unicode normalization enabled by default for proper matching

    return fuzzy_match_score(command1, command2, &opts);
}

/**
 * Add command to learning history
 */
void autocorrect_learn_command(const char *command) {
    if (!command || !autocorrect_config.learn_from_history) {
        return;
    }

    // Check if command already exists
    for (int i = 0; i < learned_commands_count; i++) {
        if (learned_commands[i] && strcmp(learned_commands[i], command) == 0) {
            return; // Already learned
        }
    }

    // Add new command if there's space
    if (learned_commands_count < MAX_LEARNED_COMMANDS) {
        learned_commands[learned_commands_count] = strdup(command);
        if (learned_commands[learned_commands_count]) {
            learned_commands_count++;
            autocorrect_stats.commands_learned++;

            if (debug_enabled) {
                printf("DEBUG: Learned command: '%s'\n", command);
            }
        }
    }
}

/**
 * Check if a command exists in the system
 */
bool autocorrect_command_exists(executor_t *executor, const char *command) {
    if (!command) {
        return false;
    }

    // Check if it's a builtin (only when compiled with full shell)
#ifndef AUTOCORRECT_STANDALONE_TEST
    if (is_builtin(command)) {
        return true;
    }
#endif

    // Check if it's a function (requires executor context)
    if (executor) {
        // This would need to be implemented with access to executor's function
        // table For now, we'll skip function checking
    }

    // Check PATH
    char *path = getenv("PATH");
    if (!path) {
        return false;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        return false;
    }

    char *dir = strtok(path_copy, ":");
    while (dir) {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, command);

        if (is_executable_file(full_path)) {
            free(path_copy);
            return true;
        }

        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return false;
}

/**
 * Calculate Levenshtein distance
 * Now delegates to libfuzzy for Unicode-aware matching
 */
int autocorrect_levenshtein_distance(const char *s1, const char *s2) {
    // Use libfuzzy with case-insensitive matching (original behavior)
    fuzzy_match_options_t opts = FUZZY_MATCH_DEFAULT;
    opts.case_sensitive = false;
    return fuzzy_levenshtein_distance(s1, s2, &opts);
}

/**
 * Calculate Jaro-Winkler similarity score
 * Now delegates to libfuzzy for Unicode-aware matching
 */
int autocorrect_jaro_winkler_score(const char *s1, const char *s2) {
    // Use libfuzzy with case-insensitive matching (original behavior)
    fuzzy_match_options_t opts = FUZZY_MATCH_DEFAULT;
    opts.case_sensitive = false;
    return fuzzy_jaro_winkler_score(s1, s2, &opts);
}

/**
 * Calculate common prefix length
 * Now delegates to libfuzzy for Unicode-aware matching
 */
int autocorrect_common_prefix_length(const char *s1, const char *s2,
                                     bool case_sensitive) {
    fuzzy_match_options_t opts = FUZZY_MATCH_DEFAULT;
    opts.case_sensitive = case_sensitive;
    return fuzzy_common_prefix_length(s1, s2, &opts);
}

/**
 * Check subsequence match quality
 * Now delegates to libfuzzy for Unicode-aware matching
 */
int autocorrect_subsequence_score(const char *pattern, const char *text,
                                  bool case_sensitive) {
    fuzzy_match_options_t opts = FUZZY_MATCH_DEFAULT;
    opts.case_sensitive = case_sensitive;
    return fuzzy_subsequence_score(pattern, text, &opts);
}

/**
 * Find builtin command suggestions
 */
int autocorrect_suggest_builtins(const char *command, correction_t *suggestions,
                                 int max_suggestions, bool case_sensitive) {
#ifdef AUTOCORRECT_STANDALONE_TEST
    // For standalone testing, skip suggestions to avoid dependencies
    (void)command;
    (void)suggestions;
    (void)max_suggestions;
    (void)case_sensitive;
    return 0;
#else
    int count = 0;

    // Get list of builtin commands
    const char *builtins[] = {
        "cd",     "pwd",   "echo",   "printf",  "test",     "[",
        "true",   "false", ":",      ".",       "break",    "continue",
        "return", "exit",  "export", "set",     "unset",    "shift",
        "eval",   "exec",  "hash",   "type",    "ulimit",   "umask",
        "times",  "trap",  "wait",   "getopts", "readonly", "config"};

    int builtin_count = sizeof(builtins) / sizeof(builtins[0]);

    for (int i = 0; i < builtin_count && count < max_suggestions; i++) {
        int score =
            autocorrect_similarity_score(command, builtins[i], case_sensitive);
        if (score >= MIN_SIMILARITY_SCORE) {
            suggestions[count].command = strdup(builtins[i]);
            suggestions[count].score = score;
            suggestions[count].source = "builtin";
            count++;
        }
    }

    return count;
#endif
}

/**
 * Find function suggestions (stub for now)
 */
int autocorrect_suggest_functions(executor_t *executor, const char *command,
                                  correction_t *suggestions,
                                  int max_suggestions, bool case_sensitive) {
    // This would require access to executor's function table
    // Currently focusing on builtins and PATH commands
    (void)executor;
    (void)command;
    (void)suggestions;
    (void)max_suggestions;
    (void)case_sensitive;
    return 0;
}

/**
 * Find PATH command suggestions
 */
/**
 * @brief Fast pre-filter using Damerau-Levenshtein distance
 *
 * Quickly eliminates candidates that are too different before
 * doing expensive Unicode-aware fuzzy matching. Uses Damerau-Levenshtein
 * which counts transpositions (like gti->git) as 1 edit, not 2.
 *
 * @param s1 First string
 * @param s2 Second string
 * @param max_dist Maximum distance to consider (returns max_dist+1 if exceeded)
 * @return Edit distance, or max_dist+1 if too different
 */
static int fast_edit_distance(const char *s1, const char *s2, int max_dist) {
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);

    // Quick length check
    if (len1 > 32 || len2 > 32)
        return max_dist + 1;
    int len_diff = (int)len1 - (int)len2;
    if (len_diff < 0)
        len_diff = -len_diff;
    if (len_diff > max_dist)
        return max_dist + 1;

    /* Full matrix for Damerau-Levenshtein (need prev-prev row for
     * transpositions) */
    int matrix[34][34];

    for (size_t i = 0; i <= len1; i++)
        matrix[i][0] = (int)i;
    for (size_t j = 0; j <= len2; j++)
        matrix[0][j] = (int)j;

    for (size_t i = 1; i <= len1; i++) {
        int row_min = (int)i;

        for (size_t j = 1; j <= len2; j++) {
            // Case-insensitive comparison
            char c1 = s1[i - 1];
            char c2 = s2[j - 1];
            if (c1 >= 'A' && c1 <= 'Z')
                c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z')
                c2 += 32;

            int cost = (c1 == c2) ? 0 : 1;
            int del = matrix[i - 1][j] + 1;
            int ins = matrix[i][j - 1] + 1;
            int sub = matrix[i - 1][j - 1] + cost;

            int min_val =
                del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);

            // Check for transposition (Damerau extension)
            if (i > 1 && j > 1) {
                char prev_c1 = s1[i - 2];
                char prev_c2 = s2[j - 2];
                if (prev_c1 >= 'A' && prev_c1 <= 'Z')
                    prev_c1 += 32;
                if (prev_c2 >= 'A' && prev_c2 <= 'Z')
                    prev_c2 += 32;

                if (c1 == prev_c2 && c2 == prev_c1) {
                    int trans = matrix[i - 2][j - 2] + 1;
                    if (trans < min_val)
                        min_val = trans;
                }
            }

            matrix[i][j] = min_val;
            if (min_val < row_min)
                row_min = min_val;
        }

        // Early termination if entire row exceeds threshold
        if (row_min > max_dist)
            return max_dist + 1;
    }

    return matrix[len1][len2];
}

int autocorrect_suggest_path_commands(const char *command,
                                      correction_t *suggestions,
                                      int max_suggestions,
                                      bool case_sensitive) {
    char *path = getenv("PATH");

    if (!path || max_suggestions <= 0) {
        return 0;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        return 0;
    }

    // Pre-filter threshold: commands within 3 edits are candidates
    const int prefilter_max_dist = 3;

    /* Collect more candidates than requested, then sort and take best.
     * Use local array to avoid overflowing caller's buffer. */
    const int max_candidates = 50;
    correction_t candidates[50];
    int candidate_count = 0;

    char *dir = strtok(path_copy, ":");
    while (dir && candidate_count < max_candidates) {
        DIR *dp = opendir(dir);
        if (!dp) {
            dir = strtok(NULL, ":");
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(dp)) && candidate_count < max_candidates) {
            if (entry->d_name[0] == '.') {
                continue; // Skip hidden files
            }

            // Fast pre-filter: skip if edit distance > threshold
            int edit_dist =
                fast_edit_distance(command, entry->d_name, prefilter_max_dist);
            if (edit_dist > prefilter_max_dist) {
                continue;
            }

            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);

            if (is_executable_file(full_path)) {
                /* Full Unicode-aware scoring for candidates that passed
                 * pre-filter */
                int score = autocorrect_similarity_score(command, entry->d_name,
                                                         case_sensitive);

                if (score >= MIN_SIMILARITY_SCORE) {
                    candidates[candidate_count].command = strdup(entry->d_name);
                    candidates[candidate_count].score = score;
                    candidates[candidate_count].source = "path";
                    candidate_count++;
                }
            }
        }

        closedir(dp);
        dir = strtok(NULL, ":");
    }

    free(path_copy);

    // Sort candidates by score (descending)
    sort_corrections_by_score(candidates, candidate_count);

    // Copy top results to caller's buffer
    int result_count =
        candidate_count < max_suggestions ? candidate_count : max_suggestions;
    for (int i = 0; i < result_count; i++) {
        suggestions[i] = candidates[i];
    }

    // Free unused candidates
    for (int i = result_count; i < candidate_count; i++) {
        free(candidates[i].command);
    }

    return result_count;
}

/**
 * Find history-based suggestions
 */
int autocorrect_suggest_from_history(const char *command,
                                     correction_t *suggestions,
                                     int max_suggestions, bool case_sensitive) {
    int count = 0;

    // Check learned commands
    for (int i = 0; i < learned_commands_count && count < max_suggestions;
         i++) {
        if (learned_commands[i]) {
            int score = autocorrect_similarity_score(
                command, learned_commands[i], case_sensitive);
            if (score >= MIN_SIMILARITY_SCORE) {
                suggestions[count].command = strdup(learned_commands[i]);
                suggestions[count].score = score;
                suggestions[count].source = "history";
                count++;
            }
        }
    }

    return count;
}

/**
 * Get default configuration
 */
void autocorrect_get_default_config(autocorrect_config_t *config) {
    if (!config) {
        return;
    }

    config->enabled = true;
    config->max_suggestions = 3;
    config->similarity_threshold = MIN_SIMILARITY_SCORE;
    config->interactive_prompts = true;
    config->learn_from_history = true;
    config->correct_builtins = true;
    config->correct_external = true;
    config->case_sensitive = false;
}

/**
 * Validate configuration
 */
bool autocorrect_validate_config(const autocorrect_config_t *config) {
    if (!config) {
        return false;
    }

    if (config->max_suggestions < 1 ||
        config->max_suggestions > MAX_CORRECTIONS) {
        return false;
    }

    if (config->similarity_threshold < 0 ||
        config->similarity_threshold > 100) {
        return false;
    }

    return true;
}

/**
 * Apply configuration changes
 */
int autocorrect_apply_config(const autocorrect_config_t *config) {
    return autocorrect_load_config(config);
}

/**
 * Get statistics
 */
void autocorrect_get_stats(int *corrections_offered, int *corrections_accepted,
                           int *commands_learned) {
    if (corrections_offered) {
        *corrections_offered = autocorrect_stats.corrections_offered;
    }
    if (corrections_accepted) {
        *corrections_accepted = autocorrect_stats.corrections_accepted;
    }
    if (commands_learned) {
        *commands_learned = autocorrect_stats.commands_learned;
    }
}

/**
 * Reset statistics
 */
void autocorrect_reset_stats(void) {
    autocorrect_stats.corrections_offered = 0;
    autocorrect_stats.corrections_accepted = 0;
    autocorrect_stats.commands_learned = 0;
}

/**
 * @brief Set debug mode for auto-correction system.
 * @param enabled True to enable debug output, false to disable.
 */
void autocorrect_set_debug(bool enabled) { debug_enabled = enabled; }

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/**
 * @brief Sort correction suggestions by score in descending order.
 *
 * Uses bubble sort to order corrections from highest to lowest score,
 * ensuring the best matches appear first in the results.
 *
 * @param corrections Array of correction suggestions to sort.
 * @param count Number of corrections in the array.
 */
static void sort_corrections_by_score(correction_t *corrections, int count) {
    // Simple bubble sort by score (descending)
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (corrections[j].score < corrections[j + 1].score) {
                correction_t temp = corrections[j];
                corrections[j] = corrections[j + 1];
                corrections[j + 1] = temp;
            }
        }
    }
}

/**
 * @brief Check if a file path points to an executable file.
 *
 * Verifies that the path refers to a regular file with execute
 * permission for the owner.
 *
 * @param path Path to the file to check.
 * @return true if file exists and is executable, false otherwise.
 */
static bool is_executable_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }

    return S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
}
