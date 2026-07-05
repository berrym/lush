/**
 * @file test_pty_typeahead.c
 * @brief Type-ahead across prompts: rapid multi-line input is not flushed.
 *
 * When several command lines arrive in a single read -- a multi-line paste,
 * or commands entered faster than the shell renders each prompt -- every line
 * must run, not just the first. The LLE re-enters raw mode for each new prompt;
 * if that transition flushed the terminal's input queue (tcsetattr TCSAFLUSH),
 * the type-ahead buffered while the previous command ran would be discarded and
 * all but the first line lost. Raw mode is entered with TCSANOW so type-ahead
 * survives, matching bash and zsh.
 *
 * This sends three `echo` lines back to back with no inter-line delay (so they
 * land in one read on the slave) and verifies all three outputs appear.
 *
 * Usage: test_pty_typeahead <lush-binary-path>
 */

#include "pty_helpers.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv_main) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv_main[0]);
        return 2;
    }
    const char *lush_path = argv_main[1];

    pty_session_t s;
    const char *argv[] = {"lush", "-i", NULL};
    if (pty_spawn_lle(&s, lush_path, argv) != 0) {
        fprintf(stderr, "test_pty_typeahead: spawn failed\n");
        return 1;
    }

    /// Let the LLE come up and emit its initial prompt.
    pty_drain(&s, 500);

    /// Three commands in one write, no gap: they arrive together, so the
    /// terminal buffers lines two and three as type-ahead while line one runs.
    /// A brace command sits in the middle to also cover a compound line.
    pty_send(&s, "echo tA1\rf_ta() { echo tB2; }\rf_ta\r");

    /// Every output must appear. pty_expect waits per-marker so a slow runner
    /// is tolerated; a flush of type-ahead would drop tB2 (and the function
    /// would never be defined or called).
    int failures = 0;
    if (!pty_expect(&s, "tA1", 5000)) {
        fprintf(stderr, "test_pty_typeahead: 'tA1' missing (first line)\n");
        failures++;
    }
    if (!pty_expect(&s, "tB2", 5000)) {
        fprintf(stderr,
                "test_pty_typeahead: 'tB2' missing -- type-ahead flushed\n");
        failures++;
    }

    if (failures) {
        fprintf(stderr,
                "test_pty_typeahead: captured output (%zu bytes):\n---\n%s\n"
                "---\n",
                s.out_len, s.output);
    }

    pty_send(&s, "exit\r");
    pty_wait(&s, 5000);
    pty_cleanup(&s);

    return failures == 0 ? 0 : 1;
}
