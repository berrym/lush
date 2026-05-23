/**
 * @file builtins/display.h
 * @brief Per-LLE-subcommand handlers for the `display` builtin.
 *
 * `display lle <subcommand>` is implemented as a chain of dispatcher
 * delegations: bin_display.c's LLE router matches the subcommand name
 * and calls the corresponding `display_lle_<sub>(argc, argv)` handler
 * declared here. Each handler lives in its own
 * `src/builtins/display/lle_<sub>.c` file.
 *
 * Calling convention: handlers receive a shifted argc/argv where
 * argv[0] is the LLE subcommand name (e.g. "history") and argv[1..]
 * are the sub-subcommand arguments. argc reflects the shifted count.
 * The dispatcher passes `argc - 2, argv + 2` (skipping "display" and
 * "lle").
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef BUILTINS_DISPLAY_H
#define BUILTINS_DISPLAY_H

int display_lle_status(int argc, char **argv);
int display_lle_history(int argc, char **argv);
int display_lle_keybindings(int argc, char **argv);
int display_lle_autosuggestions(int argc, char **argv);
int display_lle_syntax(int argc, char **argv);
int display_lle_transient(int argc, char **argv);
int display_lle_hot_reload(int argc, char **argv);
int display_lle_newline_before(int argc, char **argv);
int display_lle_multiline(int argc, char **argv);
int display_lle_diagnostics(int argc, char **argv);
int display_lle_reset(int argc, char **argv);
int display_lle_theme(int argc, char **argv);
int display_lle_completion(int argc, char **argv);

#endif // BUILTINS_DISPLAY_H
