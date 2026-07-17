/**
 * @file spec_22_history_buffer_compliance.c
 * @brief Spec 22 history-buffer-integration behavioral compliance test
 *
 * Exercises the Spec 22 subsystems against the real memory subsystem and
 * asserts their behavior: the edit cache stores and retrieves entries, the
 * multiline parser distinguishes complete from incomplete commands, and the
 * integration / session / bridge / reconstruction / formatting components
 * construct end-to-end with their real dependencies.
 *
 * The prior version checked only that function symbols were non-NULL,
 * documenting a "runtime testing requires integration environment"
 * limitation. That limitation was a missing memory-pool initialization
 * (lush_pool_init); the subsystems run correctly once the pool is up.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/edit_cache.h"
#include "lle/edit_session_manager.h"
#include "lle/event_system.h"
#include "lle/formatting_engine.h"
#include "lle/history.h"
#include "lle/history_buffer_bridge.h"
#include "lle/history_buffer_integration.h"
#include "lle/multiline_parser.h"
#include "lle/reconstruction_engine.h"
#include "lle/structure_analyzer.h"
#include "lush_memory_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int assertions_passed = 0;

#define COMPLIANCE_ASSERT(condition, message)                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "COMPLIANCE VIOLATION: %s\n", message);            \
            fprintf(stderr, "   at %s:%d\n", __FILE__, __LINE__);              \
            exit(1);                                                           \
        }                                                                      \
        assertions_passed++;                                                   \
    } while (0)

/// The edit cache stores entries and reports hits, misses, and size.
static void test_edit_cache(void) {
    lle_edit_cache_config_t config;
    lle_edit_cache_get_default_config(&config);
    lle_edit_cache_t *cache = NULL;
    COMPLIANCE_ASSERT(lle_edit_cache_create(&cache, NULL, &config) ==
                          LLE_SUCCESS,
                      "edit cache creation");

    COMPLIANCE_ASSERT(lle_edit_cache_insert(cache, 0, 1, "echo original", 13,
                                            "echo reconstructed",
                                            18) == LLE_SUCCESS,
                      "inserting a cache entry");

    lle_edit_cache_entry_t *hit = NULL;
    COMPLIANCE_ASSERT(lle_edit_cache_lookup(cache, 0, &hit) == LLE_SUCCESS &&
                          hit != NULL,
                      "looking up a present entry hits");
    COMPLIANCE_ASSERT(hit->original_text != NULL &&
                          strcmp(hit->original_text, "echo original") == 0,
                      "the hit returns the original text");
    COMPLIANCE_ASSERT(
        hit->reconstructed_text != NULL &&
            strcmp(hit->reconstructed_text, "echo reconstructed") == 0,
        "the hit returns the reconstructed text");

    lle_edit_cache_entry_t *miss = NULL;
    lle_edit_cache_lookup(cache, 99, &miss);
    COMPLIANCE_ASSERT(miss == NULL, "looking up an absent entry misses");

    lle_edit_cache_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    COMPLIANCE_ASSERT(lle_edit_cache_get_stats(cache, &stats) == LLE_SUCCESS,
                      "cache statistics are available");
    COMPLIANCE_ASSERT(stats.current_entries == 1,
                      "the cache holds the one inserted entry");
    COMPLIANCE_ASSERT(stats.hits >= 1, "the hit was counted");
    COMPLIANCE_ASSERT(stats.misses >= 1, "the miss was counted");

    lle_edit_cache_destroy(cache);
}

/// The multiline parser counts lines and detects complete vs incomplete
/// commands.
static void test_multiline_parser(void) {
    lle_analyzer_config_t acfg;
    lle_structure_analyzer_get_default_config(&acfg);
    lle_structure_analyzer_t *analyzer = NULL;
    COMPLIANCE_ASSERT(lle_structure_analyzer_create(&analyzer, NULL, &acfg) ==
                          LLE_SUCCESS,
                      "structure analyzer creation");

    lle_parser_config_t pcfg;
    lle_multiline_parser_get_default_config(&pcfg);
    lle_multiline_parser_t *parser = NULL;
    COMPLIANCE_ASSERT(lle_multiline_parser_create(&parser, NULL, analyzer,
                                                  &pcfg) == LLE_SUCCESS,
                      "multiline parser creation");

    const char *complete = "for i in 1 2\ndo echo $i\ndone";
    lle_multiline_parse_result_t *result = NULL;
    COMPLIANCE_ASSERT(lle_multiline_parser_parse(parser, complete,
                                                 strlen(complete),
                                                 &result) == LLE_SUCCESS &&
                          result != NULL,
                      "parsing a complete command succeeds");
    COMPLIANCE_ASSERT(result->line_count == 3,
                      "the three-line for loop parses to three lines");
    COMPLIANCE_ASSERT(result->is_complete,
                      "the closed for loop is reported complete");
    COMPLIANCE_ASSERT(!result->has_syntax_error,
                      "the well-formed command has no syntax error");

    const char *incomplete = "for i in 1 2\ndo echo $i";
    lle_multiline_parse_result_t *partial = NULL;
    lle_multiline_parser_parse(parser, incomplete, strlen(incomplete),
                               &partial);
    COMPLIANCE_ASSERT(partial != NULL && !partial->is_complete,
                      "a for loop missing 'done' is reported incomplete");

    const char *single = "echo hello";
    lle_multiline_parse_result_t *one = NULL;
    lle_multiline_parser_parse(parser, single, strlen(single), &one);
    COMPLIANCE_ASSERT(one != NULL && one->line_count == 1 && one->is_complete,
                      "a single-line command parses to one complete line");

    lle_multiline_parser_destroy(parser);
    lle_structure_analyzer_destroy(analyzer);
}

/// The integration, session, bridge, reconstruction, and formatting
/// components construct end-to-end against the real memory subsystem -- the
/// path the prior version could not reach.
static void test_subsystem_construction(void) {
    lle_history_core_t *history = NULL;
    COMPLIANCE_ASSERT(lle_history_core_create(&history, NULL, NULL) ==
                          LLE_SUCCESS,
                      "history core creation");

    lle_analyzer_config_t acfg;
    lle_structure_analyzer_get_default_config(&acfg);
    lle_structure_analyzer_t *analyzer = NULL;
    lle_structure_analyzer_create(&analyzer, NULL, &acfg);

    lle_parser_config_t pcfg;
    lle_multiline_parser_get_default_config(&pcfg);
    lle_multiline_parser_t *parser = NULL;
    lle_multiline_parser_create(&parser, NULL, analyzer, &pcfg);

    lle_reconstruction_engine_t *recon = NULL;
    COMPLIANCE_ASSERT(lle_reconstruction_engine_create(
                          &recon, NULL, analyzer, parser, NULL) == LLE_SUCCESS,
                      "reconstruction engine construction");

    lle_formatting_engine_t *fmt = NULL;
    COMPLIANCE_ASSERT(
        lle_formatting_engine_create(&fmt, NULL, analyzer, NULL) == LLE_SUCCESS,
        "formatting engine construction");

    lle_edit_session_manager_t *sessions = NULL;
    COMPLIANCE_ASSERT(lle_edit_session_manager_create(&sessions, NULL, history,
                                                      NULL) == LLE_SUCCESS,
                      "edit session manager construction");

    lle_history_buffer_bridge_t *bridge = NULL;
    COMPLIANCE_ASSERT(lle_history_buffer_bridge_create(
                          &bridge, NULL, history, parser, recon) == LLE_SUCCESS,
                      "history-buffer bridge construction");

    lle_event_system_t *events = NULL;
    lle_event_system_init(&events, NULL);
    lle_history_buffer_integration_t *integration = NULL;
    COMPLIANCE_ASSERT(lle_history_buffer_integration_create(
                          &integration, history, NULL, events) == LLE_SUCCESS &&
                          integration != NULL,
                      "history-buffer integration construction");

    lle_integration_state_t state;
    memset(&state, 0, sizeof(state));
    COMPLIANCE_ASSERT(lle_history_buffer_integration_get_state(
                          integration, &state) == LLE_SUCCESS,
                      "integration state is queryable");

    lle_integration_config_t icfg;
    memset(&icfg, 0, sizeof(icfg));
    COMPLIANCE_ASSERT(lle_history_buffer_integration_get_config(
                          integration, &icfg) == LLE_SUCCESS,
                      "integration config is queryable");

    lle_history_buffer_integration_destroy(integration);

    /// The history core owns a hashtable index allocated outside the arena
    /// (libhashtable calloc), so it is not reclaimed by pool shutdown and
    /// requires an explicit destroy. The remaining standalone subsystems are
    /// pool-owned and released by lush_pool_shutdown() in main.
    lle_history_core_destroy(history);
}

int main(void) {
    printf("Spec 22 History-Buffer Integration Behavioral Compliance\n");
    printf("========================================================\n\n");

    lush_pool_config_t pool_config = lush_pool_get_default_config();
    if (lush_pool_init(&pool_config) != LUSH_POOL_SUCCESS) {
        fprintf(stderr, "FATAL: failed to initialize memory pool\n");
        return 1;
    }

    test_edit_cache();
    test_multiline_parser();
    test_subsystem_construction();

    printf("Spec 22 behavioral compliance: %d assertions passed\n",
           assertions_passed);

    /// Subsystem lifetimes are pool-owned: the construction paths above
    /// allocate internal buffers via the shared arena and delegate reclamation
    /// to pool teardown rather than per-object frees. Shutting the pool down
    /// releases them in bulk, matching production lifecycle.
    lush_pool_shutdown();
    return 0;
}
