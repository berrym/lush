/**
 * @file test_pty_theme_persistence.c
 * @brief PTY integration test for display.lle.theme persistence.
 *
 * Drives a real interactive lush over a PTY and exercises the theme paths that
 * only run with the LLE prompt composer up -- which unit tests cannot reach:
 *
 *   1. Startup restore: a display.lle.theme persisted in the user's TOML is
 *      applied to the composer when the shell starts.
 *   2. Live config set: `config set display.lle.theme <name>` switches the
 *      active theme through the registry subscriber.
 *   3. Builtin config write: `display lle theme set <name>` records the choice
 *      in the config registry (the SESSION layer; `config save` would then
 *      write it to TOML for a later session).
 *
 * Each case uses a fresh interactive lush so it is a single, self-contained
 * interaction. The active theme is read back from the "Current: <name>" line of
 * `display lle theme`.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "pty_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/// Read the active theme via `display lle theme` and match its "Current:" line.
static int active_theme_is(pty_session_t *s, const char *expected) {
    s->out_len = 0;
    s->output[0] = '\0';
    pty_send(s, "display lle theme\r");
    char needle[80];
    snprintf(needle, sizeof(needle), "Current: %s", expected);
    return pty_expect(s, needle, 5000);
}

/// Case 1: a persisted theme in the user's TOML is restored at startup.
static int theme_restored_from_config(const char *lush_path) {
    const char *test = "theme_restored_from_config";
    char *home = pty_make_tmpdir();
    if (!home) {
        pty_fail(test, "could not create temp HOME");
        return 0;
    }

    char path[2048];
    snprintf(path, sizeof(path), "%s/.config", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/.config/lush", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/.config/lush/lushrc.toml", home);
    FILE *f = fopen(path, "w");
    if (!f) {
        pty_fail(test, "could not write %s", path);
        pty_rmrf(home);
        free(home);
        return 0;
    }
    fprintf(f, "display.lle.theme = \"two-line\"\n");
    fclose(f);

    pty_session_t s;
    const char *argv[] = {"lush", "-i", NULL};
    if (pty_spawn_lle_home(&s, lush_path, argv, home) != 0) {
        pty_fail(test, "spawn failed");
        pty_rmrf(home);
        free(home);
        return 0;
    }
    free(home); /// the session owns its own copy of the path

    pty_drain(&s, 1200); /// let the LLE and prompt composer come up
    int ok = active_theme_is(&s, "two-line");
    if (!ok) {
        fprintf(stderr, "  (captured %zu bytes)\n---\n%s\n---\n", s.out_len,
                s.output);
    }
    pty_cleanup(&s);
    return ok;
}

/// Cases 2 and 3: one setup command in a fresh session, then read back.
static int theme_after_setup(const char *lush_path, const char *setup_cmd,
                             const char *read_cmd, const char *needle,
                             const char *test) {
    pty_session_t s;
    const char *argv[] = {"lush", "-i", NULL};
    if (pty_spawn_lle(&s, lush_path, argv) != 0) {
        pty_fail(test, "spawn failed");
        return 0;
    }
    pty_drain(&s, 1200);

    char line[256];
    snprintf(line, sizeof(line), "%s\r", setup_cmd);
    pty_send(&s, line);
    pty_drain(&s, 600);

    s.out_len = 0;
    s.output[0] = '\0';
    snprintf(line, sizeof(line), "%s\r", read_cmd);
    pty_send(&s, line);
    int ok = pty_expect(&s, needle, 5000);
    if (!ok) {
        fprintf(stderr, "  (captured %zu bytes; wanted '%s')\n---\n%s\n---\n",
                s.out_len, needle, s.output);
    }
    pty_cleanup(&s);
    return ok;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];
    int failures = 0;

    if (theme_restored_from_config(lush)) {
        pty_ok("theme_restored_from_config",
               "persisted theme applied at startup");
    } else {
        failures++;
    }

    /// Live `config set` switches the active theme via the registry subscriber.
    if (theme_after_setup(lush, "config set display.lle.theme classic",
                          "display lle theme", "Current: classic",
                          "config_set_applies_live")) {
        pty_ok("config_set_applies_live", "config set switched the theme live");
    } else {
        failures++;
    }

    /// `display lle theme set` persists the choice to the config key.
    if (theme_after_setup(lush, "display lle theme set dark",
                          "config get display.lle.theme", "dark",
                          "builtin_set_persists")) {
        pty_ok("builtin_set_persists",
               "theme set recorded in the config registry");
    } else {
        failures++;
    }

    if (failures) {
        fprintf(stderr, "test_pty_theme_persistence: %d case(s) failed\n",
                failures);
        return 1;
    }
    printf("test_pty_theme_persistence: all cases passed\n");
    return 0;
}
