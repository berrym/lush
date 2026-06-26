/**
 * @file test_pty_autosuggestions.c
 * @brief Autosuggestion-toggle wiring test under a PTY.
 *
 * Drives the display.autosuggestions toggle through every surface that can
 * flip it and asserts, via `display lle autosuggestions` status, that the
 * change took. Each assertion uses a FRESH interactive lush so it is a single,
 * self-contained interaction (spawn -> one setup command -> one status read):
 * the LLE's interactive render of rapid command/output sequences in a single
 * session is timing-sensitive to capture, whereas one interaction per spawn is
 * deterministic.
 *
 * config.display_autosuggestions is the single field all three autosuggestion
 * gates read -- generation (update_autosuggestion), acceptance
 * (has_autosuggestion) and render (the controller->autosuggestions_enabled
 * sync in refresh_display). Before the fix it was never connected to the
 * render gate, so the toggle was inert and autosuggestions were constantly on.
 * This is the deterministic regression guard for that wiring: `config set
 * display.autosuggestions`, the `display lle autosuggestions on|off` builtin,
 * and the lush-mode default all resolve to the field the gates consult.
 * (That a config set persists across later commands -- the registry SESSION
 * layer -- is covered by the config-registry tests; here each check is an
 * isolated single interaction.)
 *
 * The visual ghost render and Right-arrow acceptance are intentionally not
 * asserted: suggestion generation and the async ghost draw are timing-
 * dependent under a PTY, which makes a behavioral assertion flaky. The status
 * read reports the exact field the gates consult, so it guards the wiring
 * without racing the renderer.
 *
 * Uses pty_spawn_lle (pty_helpers.h): all three stdio streams share one PTY
 * pair so isatty(STDIN_FILENO) is true and the LLE enters interactive mode.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "pty_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Spawn a fresh interactive lush, optionally run one setup command, read
/// `display lle autosuggestions`, and report whether the status shows the
/// expected enabled/disabled state. Returns 1 on match, 0 on mismatch/timeout,
/// -1 on spawn failure.
static int status_after(const char *lush_path, const char *setup_cmd,
                        const char *expected /* "enabled" | "disabled" */) {
    pty_session_t s;
    const char *argv[] = {"lush", "-i", NULL};
    if (pty_spawn_lle(&s, lush_path, argv) != 0) {
        return -1;
    }
    /// Let the LLE come up and emit its initial prompt.
    pty_drain(&s, 1200);

    if (setup_cmd) {
        char line[256];
        snprintf(line, sizeof(line), "%s\r", setup_cmd);
        pty_send(&s, line);
        pty_drain(&s, 600);
    }

    s.out_len = 0;
    s.output[0] = '\0';
    pty_send(&s, "display lle autosuggestions\r");

    char needle[64];
    snprintf(needle, sizeof(needle), "Autosuggestions: %s", expected);
    int ok = pty_expect(&s, needle, 5000);
    if (!ok) {
        fprintf(stderr, "  (captured %zu bytes; wanted '%s')\n---\n%s\n---\n",
                s.out_len, needle, s.output);
    }

    /// The status has been read; tear the session down directly (pty_cleanup
    /// SIGKILLs the child). No clean `exit` round-trip -- it adds a multi-
    /// second wait per spawn and nothing here depends on a graceful exit.
    pty_cleanup(&s);
    return ok;
}

int main(int argc, char **argv_main) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv_main[0]);
        return 2;
    }
    const char *lush_path = argv_main[1];

    int failures = 0;
    struct {
        const char *cmd;  /// NULL = read the default
        const char *want; /// "enabled" | "disabled"
        const char *what;
    } cases[] = {
        {                                      NULL,  "enabled","default (lush mode) enabled"                     },
        {"config set display.autosuggestions false", "disabled",
         "config set ... false disables"           },
        { "config set display.autosuggestions true",  "enabled",
         "config set ... true enabled"             },
        {         "display lle autosuggestions off", "disabled",
         "display lle autosuggestions off disables"},
        {          "display lle autosuggestions on",  "enabled",
         "display lle autosuggestions on enabled"  },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int r = status_after(lush_path, cases[i].cmd, cases[i].want);
        if (r < 0) {
            fprintf(stderr, "test_pty_autosuggestions: spawn failed\n");
            return 1;
        }
        if (r == 0) {
            fprintf(stderr, "test_pty_autosuggestions: %s -- status not '%s'\n",
                    cases[i].what, cases[i].want);
            failures++;
        }
    }

    return failures == 0 ? 0 : 1;
}
