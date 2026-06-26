/**
 * @file test_display_completion.c
 * @brief `display lle completion` behavior-knob verbs.
 *
 * The verbs (enabled / case_sensitive / threshold / menu_shadow_ghost) read and
 * write the completion config registry, which is available in any
 * non-interactive shell -- so they are driven here in a real lush subprocess
 * (`lush -c`). Each test asserts the verb sets and reads back its key, or
 * rejects invalid input.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "test_framework.h"
#include "test_shell_harness.h"

#include <string.h>

TEST(completion_threshold_verb_persists_to_config_key) {
    run_result_t r =
        run_shell_subprocess("display lle completion threshold 75\nconfig get "
                             "completion.threshold\n");
    ASSERT_EQ(r.exit_status, 0,
              "setting threshold via the verb should succeed");
    ASSERT_TRUE(strstr(r.out, "75") != NULL,
                "config get should report the threshold set via the verb");
}

TEST(completion_threshold_verb_rejects_out_of_range) {
    run_result_t r =
        run_shell_subprocess("display lle completion threshold 999\n");
    ASSERT_NE(r.exit_status, 0, "an out-of-range threshold should fail");
    ASSERT_TRUE(strstr(r.err, "invalid value") != NULL,
                "an out-of-range threshold should be reported as invalid");
}

TEST(completion_bool_verb_persists_to_config_key) {
    run_result_t r =
        run_shell_subprocess("display lle completion case_sensitive on\n"
                             "config get completion.case_sensitive\n");
    ASSERT_EQ(r.exit_status, 0, "setting case_sensitive via the verb succeeds");
    ASSERT_TRUE(strstr(r.out, "true") != NULL,
                "config get should report case_sensitive enabled via the verb");
}

TEST(completion_bool_verb_rejects_non_onoff) {
    run_result_t r =
        run_shell_subprocess("display lle completion enabled maybe\n");
    ASSERT_NE(r.exit_status, 0, "a non-on/off value should fail");
    ASSERT_TRUE(strstr(r.err, "invalid value") != NULL,
                "a non-on/off value should be reported as invalid");
}

TEST(completion_verb_bare_reports_status) {
    run_result_t r =
        run_shell_subprocess("display lle completion menu_shadow_ghost\n");
    ASSERT_EQ(r.exit_status, 0, "a bare verb prints status and succeeds");
    ASSERT_TRUE(strstr(r.out, "menu_shadow_ghost:") != NULL,
                "a bare verb should print the current value");
}

int main(void) {
    printf("=== display lle completion verb tests ===\n");
    RUN_TEST(completion_threshold_verb_persists_to_config_key);
    RUN_TEST(completion_threshold_verb_rejects_out_of_range);
    RUN_TEST(completion_bool_verb_persists_to_config_key);
    RUN_TEST(completion_bool_verb_rejects_non_onoff);
    RUN_TEST(completion_verb_bare_reports_status);
    return TEST_RESULT();
}
