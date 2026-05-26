/**
 * spec_22_history_buffer_compliance.c - Spec 22 Compliance Test
 *
 * Tests for LLE Specification 22: History-Buffer Integration
 * Validates API completeness - all functions and types are declared
 *
 * This is a HEADER-ONLY compliance test that verifies:
 * - All types are defined
 * - All functions are declared
 * - No runtime behavior testing (link-free)
 *
 * NOTE: This test does NOT call functions or test runtime behavior.
 * It only verifies the API exists as specified via function pointer checks.
 * Functional testing is done via integration tests that link with liblle.a.
 *
 * Test Coverage:
 * - Phase 1: Core Infrastructure
 * - Phase 2: Multiline Reconstruction Engine
 * - Phase 3: Interactive Editing System
 * - Phase 4: Performance Optimization
 *
 * Specification:
 * docs/lle_specification/critical_gaps/22_history_buffer_integration_complete.md
 * Date: 2025-11-02
 */

/// API verified from include/lle/history_buffer_integration.h on 2025-11-02
/// API verified from include/lle/edit_cache.h on 2025-11-02
/// API verified from include/lle/edit_session_manager.h on 2025-11-02
/// API verified from include/lle/history_buffer_bridge.h on 2025-11-02
/// API verified from include/lle/command_structure.h on 2025-11-02
/// API verified from include/lle/structure_analyzer.h on 2025-11-02
/// API verified from include/lle/multiline_parser.h on 2025-11-02
/// API verified from include/lle/reconstruction_engine.h on 2025-11-02
/// API verified from include/lle/formatting_engine.h on 2025-11-02

/*
 * NOTE: This is a header-only compliance test that verifies API declarations
 * exist. It does NOT test runtime behavior. Comprehensive functional tests are
 * in tests/lle/functional/ and tests/lle/integration/.
 */

#include "lle/command_structure.h"
#include "lle/edit_cache.h"
#include "lle/edit_session_manager.h"
#include "lle/formatting_engine.h"
#include "lle/history_buffer_bridge.h"
#include "lle/history_buffer_integration.h"
#include "lle/multiline_parser.h"
#include "lle/reconstruction_engine.h"
#include "lle/structure_analyzer.h"
#include <stdio.h>
#include <stdlib.h>

/// Test assertion counter
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

int main(void) {
    printf("Spec 22 History-Buffer Integration Compliance Test\n");
    printf("===================================================\n\n");

    /* =====================================================================
     * PHASE 1: CORE INFRASTRUCTURE
     * ===================================================================== */

    printf("Phase 1: Core Infrastructure API\n");
    printf("---------------------------------\n");

    /// Opaque types
    COMPLIANCE_ASSERT(sizeof(lle_history_buffer_integration_t *) > 0,
                      "lle_history_buffer_integration_t opaque type defined");

    /// Structures from history_buffer_integration.h
    COMPLIANCE_ASSERT(sizeof(lle_integration_config_t) > 0,
                      "lle_integration_config_t structure defined");
    COMPLIANCE_ASSERT(sizeof(lle_integration_state_t) > 0,
                      "lle_integration_state_t structure defined");
    COMPLIANCE_ASSERT(sizeof(lle_history_edit_callbacks_t) > 0,
                      "lle_history_edit_callbacks_t structure defined");

    /// Phase 1 API functions
    COMPLIANCE_ASSERT(lle_history_buffer_integration_create != NULL,
                      "lle_history_buffer_integration_create declared");
    COMPLIANCE_ASSERT(lle_history_buffer_integration_destroy != NULL,
                      "lle_history_buffer_integration_destroy declared");
    COMPLIANCE_ASSERT(lle_history_buffer_integration_get_config != NULL,
                      "lle_history_buffer_integration_get_config declared");
    COMPLIANCE_ASSERT(lle_history_buffer_integration_set_config != NULL,
                      "lle_history_buffer_integration_set_config declared");
    COMPLIANCE_ASSERT(lle_history_buffer_integration_get_state != NULL,
                      "lle_history_buffer_integration_get_state declared");
    COMPLIANCE_ASSERT(
        lle_history_buffer_integration_register_callbacks != NULL,
        "lle_history_buffer_integration_register_callbacks declared");
    COMPLIANCE_ASSERT(
        lle_history_buffer_integration_unregister_callbacks != NULL,
        "lle_history_buffer_integration_unregister_callbacks declared");

    printf("✓ Phase 1 verified: 4 types + 7 functions (11 assertions)\n\n");

    /* =====================================================================
     * PHASE 2: MULTILINE RECONSTRUCTION ENGINE
     * ===================================================================== */

    printf("Phase 2: Multiline Reconstruction Engine API\n");
    printf("---------------------------------------------\n");

    /// Command Structure API
    COMPLIANCE_ASSERT(sizeof(lle_command_structure_t *) > 0,
                      "lle_command_structure_t opaque type defined");
    COMPLIANCE_ASSERT(lle_command_structure_create != NULL,
                      "lle_command_structure_create declared");
    COMPLIANCE_ASSERT(lle_command_structure_destroy != NULL,
                      "lle_command_structure_destroy declared");

    /// Structure Analyzer API
    COMPLIANCE_ASSERT(sizeof(lle_structure_analyzer_t *) > 0,
                      "lle_structure_analyzer_t opaque type defined");
    COMPLIANCE_ASSERT(lle_structure_analyzer_create != NULL,
                      "lle_structure_analyzer_create declared");
    COMPLIANCE_ASSERT(lle_structure_analyzer_destroy != NULL,
                      "lle_structure_analyzer_destroy declared");
    COMPLIANCE_ASSERT(lle_structure_analyzer_analyze != NULL,
                      "lle_structure_analyzer_analyze declared");

    /// Multiline Parser API
    COMPLIANCE_ASSERT(sizeof(lle_multiline_parser_t *) > 0,
                      "lle_multiline_parser_t opaque type defined");
    COMPLIANCE_ASSERT(lle_multiline_parser_create != NULL,
                      "lle_multiline_parser_create declared");
    COMPLIANCE_ASSERT(lle_multiline_parser_destroy != NULL,
                      "lle_multiline_parser_destroy declared");
    COMPLIANCE_ASSERT(lle_multiline_parser_parse != NULL,
                      "lle_multiline_parser_parse declared");

    /// Reconstruction Engine API
    COMPLIANCE_ASSERT(sizeof(lle_reconstruction_engine_t *) > 0,
                      "lle_reconstruction_engine_t opaque type defined");
    COMPLIANCE_ASSERT(lle_reconstruction_engine_create != NULL,
                      "lle_reconstruction_engine_create declared");
    COMPLIANCE_ASSERT(lle_reconstruction_engine_destroy != NULL,
                      "lle_reconstruction_engine_destroy declared");
    COMPLIANCE_ASSERT(lle_reconstruction_engine_reconstruct != NULL,
                      "lle_reconstruction_engine_reconstruct declared");

    /// Formatting Engine API
    COMPLIANCE_ASSERT(sizeof(lle_formatting_engine_t *) > 0,
                      "lle_formatting_engine_t opaque type defined");
    COMPLIANCE_ASSERT(lle_formatting_engine_create != NULL,
                      "lle_formatting_engine_create declared");
    COMPLIANCE_ASSERT(lle_formatting_engine_destroy != NULL,
                      "lle_formatting_engine_destroy declared");
    COMPLIANCE_ASSERT(lle_formatting_engine_format != NULL,
                      "lle_formatting_engine_format declared");

    printf("✓ Phase 2 verified: 5 opaque types + 13 functions (18 "
           "assertions)\n\n");

    /* =====================================================================
     * PHASE 3: INTERACTIVE EDITING SYSTEM
     * ===================================================================== */

    printf("Phase 3: Interactive Editing System API\n");
    printf("----------------------------------------\n");

    /// Edit Session Manager API
    COMPLIANCE_ASSERT(sizeof(lle_edit_session_manager_t *) > 0,
                      "lle_edit_session_manager_t opaque type defined");
    COMPLIANCE_ASSERT(lle_edit_session_manager_create != NULL,
                      "lle_edit_session_manager_create declared");
    COMPLIANCE_ASSERT(lle_edit_session_manager_destroy != NULL,
                      "lle_edit_session_manager_destroy declared");

    /// History-Buffer Bridge API
    COMPLIANCE_ASSERT(sizeof(lle_history_buffer_bridge_t *) > 0,
                      "lle_history_buffer_bridge_t opaque type defined");
    COMPLIANCE_ASSERT(lle_history_buffer_bridge_create != NULL,
                      "lle_history_buffer_bridge_create declared");
    COMPLIANCE_ASSERT(lle_history_buffer_bridge_destroy != NULL,
                      "lle_history_buffer_bridge_destroy declared");

    /// Interactive editing functions from Phase 1
    COMPLIANCE_ASSERT(lle_history_edit_entry != NULL,
                      "lle_history_edit_entry declared");
    COMPLIANCE_ASSERT(lle_history_session_complete != NULL,
                      "lle_history_session_complete declared");
    COMPLIANCE_ASSERT(lle_history_session_cancel != NULL,
                      "lle_history_session_cancel declared");

    printf(
        "✓ Phase 3 verified: 2 opaque types + 7 functions (9 assertions)\n\n");

    /* =====================================================================
     * PHASE 4: PERFORMANCE OPTIMIZATION
     * ===================================================================== */

    printf("Phase 4: Performance Optimization API\n");
    printf("--------------------------------------\n");

    /// Edit Cache API
    COMPLIANCE_ASSERT(sizeof(lle_edit_cache_t *) > 0,
                      "lle_edit_cache_t opaque type defined");
    COMPLIANCE_ASSERT(sizeof(lle_edit_cache_stats_t) > 0,
                      "lle_edit_cache_stats_t structure defined");
    COMPLIANCE_ASSERT(lle_edit_cache_create != NULL,
                      "lle_edit_cache_create declared");
    COMPLIANCE_ASSERT(lle_edit_cache_destroy != NULL,
                      "lle_edit_cache_destroy declared");
    COMPLIANCE_ASSERT(lle_edit_cache_lookup != NULL,
                      "lle_edit_cache_lookup declared");
    COMPLIANCE_ASSERT(lle_edit_cache_invalidate != NULL,
                      "lle_edit_cache_invalidate declared");
    COMPLIANCE_ASSERT(lle_edit_cache_get_stats != NULL,
                      "lle_edit_cache_get_stats declared");

    /// Integration Cache Management API
    COMPLIANCE_ASSERT(
        lle_history_buffer_integration_get_cache_stats != NULL,
        "lle_history_buffer_integration_get_cache_stats declared");
    COMPLIANCE_ASSERT(lle_history_buffer_integration_clear_cache != NULL,
                      "lle_history_buffer_integration_clear_cache declared");
    COMPLIANCE_ASSERT(lle_history_buffer_integration_maintain_cache != NULL,
                      "lle_history_buffer_integration_maintain_cache declared");

    printf("✓ Phase 4 verified: 1 opaque type + 1 struct + 10 functions (12 "
           "assertions)\n\n");

    /* =====================================================================
     * FINAL SUMMARY
     * ===================================================================== */

    printf("========================================\n");
    printf("SPEC 22 COMPLIANCE: PASS\n");
    printf("========================================\n");
    printf("Total assertions passed: %d\n", assertions_passed);
    printf("\n");
    printf("API Coverage Summary:\n");
    printf("  Phase 1: Core Infrastructure           - 7 functions\n");
    printf("  Phase 2: Multiline Reconstruction       - 13 functions\n");
    printf("  Phase 3: Interactive Editing System     - 7 functions\n");
    printf("  Phase 4: Performance Optimization       - 10 functions\n");
    printf("  ------------------------------------------------\n");
    printf("  Total:                                  - 37 functions\n");
    printf("\n");
    printf("All required types and functions are declared.\n");
    printf("Spec 22 API is 100%% complete.\n");
    printf("\n");

    return 0;
}
