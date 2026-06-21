/**
 * @file test_pty_compdef.c
 * @brief Real-world TAB completion test under a PTY.
 *
 * Spawns lush interactively under a pseudo-terminal, defines a shell
 * function, binds it to a command via compdef, then sends a literal
 * TAB byte and verifies that compadd-emitted candidates appear in the
 * LLE's rendered output. This exercises the full integration path
 * (executor + LLE + display + terminal) that the C-only integration
 * test in test_compdef_e2e.c cannot reach.
 *
 * Uses pty_spawn_lle (defined in pty_helpers.h) which wires all three
 * stdio streams to a single PTY pair so isatty(STDIN_FILENO) returns
 * true inside the child and the LLE enters interactive mode. Sending
 * a literal 0x09 byte to the master is processed by the LLE input
 * layer identically to a TAB key press (terminal_unix_interface.c:872
 * explicitly excludes 0x09 from Ctrl-letter conversion and lets it
 * flow as a CHARACTER event).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "pty_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv_main) {
    /// Meson invokes PTY tests with the lush binary path as argv[1]
    /// (see test_pty_sighup_cascade.c's contract).
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv_main[0]);
        return 2;
    }
    const char *lush_path = argv_main[1];

    pty_session_t s;
    /// -i forces interactive mode (so the LLE always initializes
    /// regardless of any other defaults). argv[0] is conventionally
    /// the binary name; not significant for behavior.
    const char *argv[] = {"lush", "-i", NULL};
    if (pty_spawn_lle(&s, lush_path, argv) != 0) {
        fprintf(stderr, "test_pty_compdef: spawn failed\n");
        return 1;
    }

    /// Let the LLE come up and emit its initial prompt. 500ms matches
    /// the settle time used by the existing PTY tests
    /// (test_pty_sighup_cascade.c).
    pty_drain(&s, 500);

    /// cd into a directory where the file/directory completion source
    /// won't drown out the compdef candidates -- HOME was set to a
    /// fresh empty tmpdir by pty_spawn_lle. The LLE's menu caps at
    /// max_visible_items=20; with 19+ cwd entries the file source
    /// alone would exhaust the cap and silently truncate the compdef
    /// candidates that came after it in the source ordering.
    pty_send(&s, "cd ~\r");
    pty_drain(&s, 200);

    /// Define the completion function. Use \r (carriage return) to
    /// commit the line: LLE's raw input layer treats \r as Enter; \n
    /// would be a literal newline byte that gets buffered into the
    /// current line.
    pty_send(&s, "_subs() { compadd checkout branch merge; }\r");
    pty_drain(&s, 200);

    /// Register the binding.
    pty_send(&s, "compdef _subs git\r");
    pty_drain(&s, 200);

    /// Reset the captured-output buffer so the assertion sees only
    /// what the TAB press produced, not the echoed cd / fn-define /
    /// compdef-bind lines from above.
    s.out_len = 0;
    s.output[0] = '\0';

    /// Type "git " then TAB. No carriage return -- we don't want to
    /// execute the partial line, just trigger completion at the
    /// cursor. The TAB byte (\x09) is processed by the LLE input
    /// layer as a TAB key press.
    pty_send(&s, "git \t");
    /// Wait for each candidate to render rather than for a fixed idle gap:
    /// menu output can arrive in bursts on a loaded runner.
    pty_expect(&s, "checkout", 5000);
    pty_expect(&s, "branch", 5000);
    pty_expect(&s, "merge", 5000);

    /// All three candidates should appear in the LLE's rendered
    /// output. The menu renderer outputs candidate text literally
    /// (ANSI escapes surround it for selection highlighting but the
    /// text itself is unescaped); strstr is the established
    /// assertion pattern used by the other PTY tests.
    int failures = 0;
    if (!strstr(s.output, "checkout")) {
        fprintf(stderr, "test_pty_compdef: 'checkout' not in output\n");
        failures++;
    }
    if (!strstr(s.output, "branch")) {
        fprintf(stderr, "test_pty_compdef: 'branch' not in output\n");
        failures++;
    }
    if (!strstr(s.output, "merge")) {
        fprintf(stderr, "test_pty_compdef: 'merge' not in output\n");
        failures++;
    }

    if (failures) {
        fprintf(
            stderr,
            "test_pty_compdef: captured output (%zu bytes):\n---\n%s\n---\n",
            s.out_len, s.output);
    }

    /// Cancel the partial line (Ctrl-C) then re-run the test with a
    /// typed prefix that should narrow the candidate set. Only
    /// "checkout" starts with "c"; "branch" and "merge" must be
    /// filtered out by the bridge so they don't appear in the menu.
    pty_send(&s, "\x03");
    pty_drain(&s, 100);

    s.out_len = 0;
    s.output[0] = '\0';
    pty_send(&s, "git c\t");
    /// Wait for the menu to render the surviving candidate, then settle so
    /// the absence checks below see the final menu state.
    pty_expect(&s, "checkout", 5000);
    pty_drain(&s, 300);

    if (!strstr(s.output, "checkout")) {
        fprintf(stderr,
                "test_pty_compdef: 'checkout' missing under c-prefix\n");
        failures++;
    }
    if (strstr(s.output, "branch")) {
        fprintf(stderr,
                "test_pty_compdef: 'branch' leaked through c-prefix filter\n");
        failures++;
    }
    if (strstr(s.output, "merge")) {
        fprintf(stderr,
                "test_pty_compdef: 'merge' leaked through c-prefix filter\n");
        failures++;
    }

    if (failures) {
        fprintf(stderr,
                "test_pty_compdef: prefix-filter captured output (%zu bytes):"
                "\n---\n%s\n---\n",
                s.out_len, s.output);
    }

    /// In-menu type-to-filter: open the menu with a bare prefix,
    /// then type additional characters while the menu is visible.
    /// The new chars must narrow the menu rather than insert into
    /// the buffer; "checkout" must stay visible after typing "h"
    /// because it is the only candidate that matches "ch".
    pty_send(&s, "\x03");
    pty_drain(&s, 100);

    s.out_len = 0;
    s.output[0] = '\0';
    /// Open menu with prefix "c": all three c-prefix candidates
    /// (checkout, clone, commit) appear; first is auto-previewed.
    pty_send(&s, "git c\t");
    pty_expect(&s, "checkout", 5000);
    /// Reset so the post-filter assertion sees only the re-render after 'h',
    /// not the wider menu that was open before it.
    s.out_len = 0;
    s.output[0] = '\0';
    /// Type 'h' while menu is open. The keybinding intercept treats
    /// it as a filter keystroke; result must narrow to "checkout"
    /// only.
    pty_send(&s, "h");
    pty_expect(&s, "checkout", 5000);
    pty_drain(&s, 300);

    if (!strstr(s.output, "checkout")) {
        fprintf(stderr,
                "test_pty_compdef: 'checkout' missing after in-menu 'h'\n");
        failures++;
    }
    if (strstr(s.output, "clone")) {
        fprintf(stderr, "test_pty_compdef: 'clone' leaked after in-menu 'h'\n");
        failures++;
    }
    if (strstr(s.output, "commit")) {
        fprintf(stderr,
                "test_pty_compdef: 'commit' leaked after in-menu 'h'\n");
        failures++;
    }

    if (failures) {
        fprintf(stderr,
                "test_pty_compdef: in-menu type-to-filter output "
                "(%zu bytes):\n---\n%s\n---\n",
                s.out_len, s.output);
    }

    pty_send(&s, "\x03");
    pty_drain(&s, 100);
    pty_send(&s, "exit\r");
    pty_wait(&s, 5000);
    pty_cleanup(&s);

    return failures == 0 ? 0 : 1;
}
