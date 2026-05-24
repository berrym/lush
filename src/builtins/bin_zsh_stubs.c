/**
 * @file bin_zsh_stubs.c
 * @brief No-op stubs for zsh-specific builtins (bindkey, autoload, zmodload)
 *
 * Real-world zsh scripts (oh-my-zsh, prezto, popular dotfiles)
 * routinely call `bindkey`, `autoload`, and `zmodload` in code paths
 * that lush parses and runs. None of these builtins map to lush's
 * own subsystems verbatim:
 *
 *   bindkey    -- zsh key-binding interface for ZLE. Lush has its
 *                 own `display lle bind` for the LLE editor.
 *   autoload   -- zsh's lazy-function-loading mechanism. Lush
 *                 source-loads functions explicitly via `source`.
 *   zmodload   -- zsh module loader. Lush has no equivalent module
 *                 system; functionality of common modules (e.g.,
 *                 zsh/complist) is built in.
 *
 * The stubs return 0 silently so a script that calls them at
 * top-level does not bail out. The bookkeeping these builtins
 * perform in zsh (binding keys, registering lazy functions, loading
 * modules) is invisible from a non-interactive script's stdout/
 * stderr/exit signal -- the divergence the corpus is measuring.
 *
 * Users who need the underlying behaviour reach for lush's own
 * surfaces: `display lle bind` for keybindings, `source` for
 * function loading, and the built-in equivalents for what zsh
 * modules provide.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

int bin_bindkey(int argc __attribute__((unused)),
                char **argv __attribute__((unused))) {
    // zsh key-binding configuration. Lush uses `display lle bind`;
    // accepting and ignoring bindkey calls lets zsh init scripts run
    // without producing a divergence.
    return 0;
}

int bin_autoload(int argc __attribute__((unused)),
                 char **argv __attribute__((unused))) {
    // zsh's lazy-function loader. Lush loads functions when their
    // source file is sourced; the autoload bookkeeping is a no-op.
    return 0;
}

int bin_zmodload(int argc __attribute__((unused)),
                 char **argv __attribute__((unused))) {
    // zsh module loader. Lush has no module system; the most-used
    // module bodies (e.g., zsh/complist completion list mode) are
    // either built-in or supplanted by lush's own subsystems.
    return 0;
}

int bin_colors(int argc __attribute__((unused)),
               char **argv __attribute__((unused))) {
    // zsh's `colors` is a function autoloaded from $fpath that
    // populates $fg, $bg, $FG, $BG arrays / maps and $reset_color
    // with ANSI escape sequences. Lush's autoload is itself a stub,
    // so `colors` is unavailable to scripts that source the zsh
    // autoload module. Provide a builtin no-op so scripts calling
    // `colors` to set up the maps do not crash; the maps simply
    // stay unset and downstream `${fg[red]}` lookups expand to
    // empty (matching the no-color terminal case rather than a
    // hard error).
    return 0;
}
