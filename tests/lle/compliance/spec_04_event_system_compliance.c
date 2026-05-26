/**
 * @file spec_04_event_system_compliance.c
 * @brief Spec 04 Event System Compliance Test
 *
 * Verifies that Spec 04 Event System implementation matches specification:
 * - All major event type categories defined
 * - All priority levels defined correctly
 * - All filter result types defined correctly
 * - All structures present
 * - All Phase 1 and Phase 2 API functions declared
 *
 * SPECIFICATION: docs/lle_specification/04_event_system_complete.md
 *
 * This test enforces zero-tolerance policy by automatically verifying
 * spec compliance. It reads actual header values rather than assuming them.
 */

#include "lle/event_system.h"
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
    printf("Spec 04 Event System Compliance Test\n");
    printf("=====================================\n\n");

    /* =====================================================================
     * EVENT TYPE CATEGORY VERIFICATION
     * ===================================================================== */

    printf("Checking Event Type category starting points exist...\n");

    /// Verify major event type categories are defined (starting points)
    COMPLIANCE_ASSERT(LLE_EVENT_KEY_PRESS == 0x1000,
                      "Key events start at 0x1000");
    COMPLIANCE_ASSERT(LLE_EVENT_MOUSE_PRESS == 0x2000,
                      "Mouse events start at 0x2000");
    COMPLIANCE_ASSERT(LLE_EVENT_TERMINAL_RESIZE == 0x3000,
                      "Terminal events start at 0x3000");
    COMPLIANCE_ASSERT(LLE_EVENT_PASTE_START == 0x4000,
                      "Paste events start at 0x4000");
    COMPLIANCE_ASSERT(LLE_EVENT_BUFFER_CHANGED == 0x5000,
                      "Buffer events start at 0x5000");
    COMPLIANCE_ASSERT(LLE_EVENT_HISTORY_CHANGED == 0x6000,
                      "History events start at 0x6000");
    COMPLIANCE_ASSERT(LLE_EVENT_COMPLETION_REQUESTED == 0x7000,
                      "Completion events start at 0x7000");
    COMPLIANCE_ASSERT(LLE_EVENT_SUGGESTION_UPDATED == 0x8000,
                      "Suggestion events start at 0x8000");
    COMPLIANCE_ASSERT(LLE_EVENT_SYSTEM_ERROR == 0x9000,
                      "System events start at 0x9000");
    COMPLIANCE_ASSERT(LLE_EVENT_TIMER_EXPIRED == 0xA000,
                      "Timer events start at 0xA000");
    COMPLIANCE_ASSERT(LLE_EVENT_PLUGIN_CUSTOM == 0xB000,
                      "Plugin events start at 0xB000");
    COMPLIANCE_ASSERT(LLE_EVENT_DISPLAY_UPDATE == 0xC000,
                      "Display events start at 0xC000");
    COMPLIANCE_ASSERT(LLE_EVENT_DEBUG_MARKER == 0xF000,
                      "Debug events start at 0xF000");

    printf("✓ Event type categories verified (13 assertions)\n\n");

    /* =====================================================================
     * PRIORITY LEVEL COMPLIANCE
     * ===================================================================== */

    printf("Checking Priority Level definitions...\n");

    COMPLIANCE_ASSERT(LLE_PRIORITY_CRITICAL == 0,
                      "CRITICAL is highest priority (0)");
    COMPLIANCE_ASSERT(LLE_PRIORITY_HIGH == 1, "HIGH priority is 1");
    COMPLIANCE_ASSERT(LLE_PRIORITY_MEDIUM == 2, "MEDIUM priority is 2");
    COMPLIANCE_ASSERT(LLE_PRIORITY_LOW == 3, "LOW priority is 3");
    COMPLIANCE_ASSERT(LLE_PRIORITY_LOWEST == 4, "LOWEST priority is 4");
    COMPLIANCE_ASSERT(LLE_PRIORITY_COUNT == 5, "Total of 5 priority levels");

    printf("✓ Priority levels verified (6 assertions)\n\n");

    /* =====================================================================
     * FILTER RESULT TYPE COMPLIANCE
     * ===================================================================== */

    printf("Checking Filter Result Type definitions...\n");

    COMPLIANCE_ASSERT(LLE_FILTER_PASS == 0, "PASS is 0");
    COMPLIANCE_ASSERT(LLE_FILTER_BLOCK == 1, "BLOCK is 1");
    COMPLIANCE_ASSERT(LLE_FILTER_TRANSFORM == 2, "TRANSFORM is 2");
    COMPLIANCE_ASSERT(LLE_FILTER_ERROR == 3, "ERROR is 3");

    printf("✓ Filter result types verified (4 assertions)\n\n");

    /* =====================================================================
     * STRUCTURE VERIFICATION
     * ===================================================================== */

    printf("Checking core structure definitions exist...\n");

    COMPLIANCE_ASSERT(sizeof(lle_event_t) > 0, "lle_event_t defined");
    COMPLIANCE_ASSERT(sizeof(lle_event_system_t) > 0,
                      "lle_event_system_t defined");
    COMPLIANCE_ASSERT(sizeof(lle_event_kind_t) > 0, "lle_event_kind_t defined");
    COMPLIANCE_ASSERT(sizeof(lle_event_priority_t) > 0,
                      "lle_event_priority_t defined");
    COMPLIANCE_ASSERT(sizeof(lle_filter_result_t) > 0,
                      "lle_filter_result_t defined");

    printf("✓ Core structures verified (5 assertions)\n\n");

    /* =====================================================================
     * PHASE 1 API VERIFICATION
     * ===================================================================== */

    printf("Checking Phase 1 core API functions...\n");

    /// Verify function pointers can be assigned (header-only check)
    COMPLIANCE_ASSERT(lle_event_system_init != NULL,
                      "lle_event_system_init declared");
    COMPLIANCE_ASSERT(lle_event_system_destroy != NULL,
                      "lle_event_system_destroy declared");
    COMPLIANCE_ASSERT(lle_event_system_start != NULL,
                      "lle_event_system_start declared");
    COMPLIANCE_ASSERT(lle_event_system_stop != NULL,
                      "lle_event_system_stop declared");
    COMPLIANCE_ASSERT(lle_event_create != NULL, "lle_event_create declared");
    COMPLIANCE_ASSERT(lle_event_destroy != NULL, "lle_event_destroy declared");
    COMPLIANCE_ASSERT(lle_event_clone != NULL, "lle_event_clone declared");
    COMPLIANCE_ASSERT(lle_event_enqueue != NULL, "lle_event_enqueue declared");
    COMPLIANCE_ASSERT(lle_event_dequeue != NULL, "lle_event_dequeue declared");
    COMPLIANCE_ASSERT(lle_event_process_queue != NULL,
                      "lle_event_process_queue declared");
    COMPLIANCE_ASSERT(lle_event_process_all != NULL,
                      "lle_event_process_all declared");
    COMPLIANCE_ASSERT(lle_event_handler_register != NULL,
                      "lle_event_handler_register declared");
    COMPLIANCE_ASSERT(lle_event_handler_unregister != NULL,
                      "lle_event_handler_unregister declared");
    COMPLIANCE_ASSERT(lle_event_dispatch != NULL,
                      "lle_event_dispatch declared");

    printf("✓ Phase 1 API verified (14 assertions)\n\n");

    /* =====================================================================
     * PHASE 2B: ENHANCED STATISTICS API
     * ===================================================================== */

    printf("Checking Phase 2B Enhanced Statistics API...\n");

    COMPLIANCE_ASSERT(lle_event_enhanced_stats_init != NULL,
                      "enhanced_stats_init declared");
    COMPLIANCE_ASSERT(lle_event_enhanced_stats_destroy != NULL,
                      "enhanced_stats_destroy declared");
    COMPLIANCE_ASSERT(lle_event_enhanced_stats_get_type != NULL,
                      "enhanced_stats_get_type declared");
    COMPLIANCE_ASSERT(lle_event_enhanced_stats_get_all_types != NULL,
                      "enhanced_stats_get_all_types declared");
    COMPLIANCE_ASSERT(lle_event_enhanced_stats_get_cycles != NULL,
                      "enhanced_stats_get_cycles declared");
    COMPLIANCE_ASSERT(lle_event_enhanced_stats_get_queue_depth != NULL,
                      "enhanced_stats_get_queue_depth declared");
    COMPLIANCE_ASSERT(lle_event_enhanced_stats_reset != NULL,
                      "enhanced_stats_reset declared");

    printf("✓ Phase 2B API verified (7 assertions)\n\n");

    /* =====================================================================
     * PHASE 2C: EVENT FILTERING API
     * ===================================================================== */

    printf("Checking Phase 2C Event Filtering API...\n");

    COMPLIANCE_ASSERT(lle_event_filter_system_init != NULL,
                      "filter_system_init declared");
    COMPLIANCE_ASSERT(lle_event_filter_system_destroy != NULL,
                      "filter_system_destroy declared");
    COMPLIANCE_ASSERT(lle_event_filter_add != NULL, "filter_add declared");
    COMPLIANCE_ASSERT(lle_event_filter_remove != NULL,
                      "filter_remove declared");
    COMPLIANCE_ASSERT(lle_event_filter_enable != NULL,
                      "filter_enable declared");
    COMPLIANCE_ASSERT(lle_event_filter_disable != NULL,
                      "filter_disable declared");
    COMPLIANCE_ASSERT(lle_event_filter_get_stats != NULL,
                      "filter_get_stats declared");

    printf("✓ Phase 2C API verified (7 assertions)\n\n");

    /* =====================================================================
     * PHASE 2D: TIMER EVENTS API
     * ===================================================================== */

    printf("Checking Phase 2D Timer Events API...\n");

    COMPLIANCE_ASSERT(lle_event_timer_system_init != NULL,
                      "timer_system_init declared");
    COMPLIANCE_ASSERT(lle_event_timer_system_destroy != NULL,
                      "timer_system_destroy declared");
    COMPLIANCE_ASSERT(lle_event_timer_add_oneshot != NULL,
                      "timer_add_oneshot declared");
    COMPLIANCE_ASSERT(lle_event_timer_add_repeating != NULL,
                      "timer_add_repeating declared");
    COMPLIANCE_ASSERT(lle_event_timer_cancel != NULL, "timer_cancel declared");
    COMPLIANCE_ASSERT(lle_event_timer_enable != NULL, "timer_enable declared");
    COMPLIANCE_ASSERT(lle_event_timer_disable != NULL,
                      "timer_disable declared");
    COMPLIANCE_ASSERT(lle_event_timer_get_info != NULL,
                      "timer_get_info declared");
    COMPLIANCE_ASSERT(lle_event_timer_process != NULL,
                      "timer_process declared");
    COMPLIANCE_ASSERT(lle_event_timer_get_stats != NULL,
                      "timer_get_stats declared");

    printf("✓ Phase 2D API verified (10 assertions)\n\n");

    /* =====================================================================
     * TYPEDEF VERIFICATION
     * ===================================================================== */

    printf("Checking callback typedef definitions...\n");

    lle_event_handler_fn handler_test = NULL;
    lle_event_filter_fn filter_test = NULL;
    (void)handler_test;
    (void)filter_test;

    COMPLIANCE_ASSERT(sizeof(lle_event_handler_fn) > 0,
                      "lle_event_handler_fn typedef exists");
    COMPLIANCE_ASSERT(sizeof(lle_event_filter_fn) > 0,
                      "lle_event_filter_fn typedef exists");

    printf("✓ Typedefs verified (2 assertions)\n\n");

    /* =====================================================================
     * SUMMARY
     * ===================================================================== */

    printf("=====================================\n");
    printf("✓ Spec 04 Event System: COMPLIANT\n");
    printf("✓ Total assertions: %d\n", assertions_passed);
    printf("=====================================\n");

    return 0;
}
