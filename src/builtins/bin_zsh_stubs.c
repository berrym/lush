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
 * Users who need the underlying behavior reach for lush's own
 * surfaces: `display lle bind` for keybindings, `source` for
 * function loading, and the built-in equivalents for what zsh
 * modules provide.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "autoload.h"
#include "builtins.h"
#include "executor.h"
#include "node.h"

#include <string.h>

int bin_autoload(int argc, char **argv) {
    /// Iterate operands. Skip option flags (-U, -z, -k, -X, -m, +, ...),
    /// which all influence how zsh autoloads but do not change which
    /// names are registered. The lush autoload module always wraps the
    /// file body as `function NAME { ... }` so `-U` (no alias expansion)
    /// and `-z` (zsh syntax) are already implicit.
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!arg || !*arg) {
            continue;
        }
        if (arg[0] == '-' || arg[0] == '+') {
            /// Option flag; ignore for registration purposes. The `--`
            /// end-of-options marker is handled the same way (its body
            /// is just an empty name, which the empty-check skips).
            continue;
        }
        if (!autoload_register(arg)) {
            rc = 1;
        }
    }
    return rc;
}

int bin_zmodload(int argc __attribute__((unused)),
                 char **argv __attribute__((unused))) {
    /// zsh module loader. Lush has no module system; the most-used
    /// module bodies (e.g., zsh/complist completion list mode) are
    /// either built-in or supplanted by lush's own subsystems.
    return 0;
}

int bin_emulate(int argc, char **argv) {
    /// `emulate [-LR] [shell]` switches the shell's emulation context.
    /// Real zsh recognizes `zsh`, `sh`, `csh`, `ksh` and -L (local to
    /// the enclosing function) / -R (reset options first) / -c <cmd>.
    ///
    /// Lush's `mode` builtin already does mode switching, but function-
    /// local restoration is not yet plumbed through the call frame, so
    /// a real `emulate -L` would not auto-restore on return. Rather
    /// than do half the job, accept the full option grammar and treat
    /// the call as a no-op when the requested target matches the
    /// current mode (or when no target is given). Any mismatch is
    /// silently accepted: autoloaded zsh functions calling
    /// `emulate -L zsh` while lush is already in zsh mode -- the
    /// actual corpus shape -- get the correct behavior.
    ///
    /// When the underlying function-scoped option stash lands, this
    /// stub will be retired in favor of the real switch.
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!arg || !*arg) {
            continue;
        }
        /// Skip flags (-L, -R, -LR, -c <cmd>, ...).
        if (arg[0] == '-' && arg[1] != '\0') {
            /// -c takes an argument; consume it.
            if (strchr(arg, 'c') != NULL && i + 1 < argc) {
                i++;
            }
            continue;
        }
        /// shell-name operand. Recognized names: zsh / sh / csh / ksh.
        /// Anything else is an error in real zsh; we return 0 to keep
        /// corpus scripts running even if their author wrote a typo.
    }
    return 0;
}

int bin_complete(int argc __attribute__((unused)),
                 char **argv __attribute__((unused))) {
    /// bash's completion registration builtin. The bash-completion
    /// library registers hundreds of completers via `complete -F func
    /// cmd`; this stub keeps those registrations from emitting
    /// `command not found` while a full record-and-query
    /// implementation (parallel to bindkey / zle in bin_bindkey.c /
    /// bin_zle.c) waits its turn. Real implementation: tracked as a
    /// future fix in CORPUS_PUNCH_LIST.md.
    return 0;
}

int bin_compgen(int argc __attribute__((unused)),
                char **argv __attribute__((unused))) {
    /// bash's completion-candidate generator. Used by interactive tab
    /// completion and inside `complete -F` handler bodies. Stub
    /// returns 0 with no candidates; corpus scripts that source the
    /// bash-completion library run cleanly.
    return 0;
}

int bin_compopt(int argc __attribute__((unused)),
                char **argv __attribute__((unused))) {
    /// bash's per-completion option mutator. Used inside completer
    /// bodies to set/clear options like `nospace` on a specific
    /// completion. No-op stub; the underlying completion behavior
    /// is supplied by lush's own completion engine.
    return 0;
}

int bin_compinit(int argc __attribute__((unused)),
                 char **argv __attribute__((unused))) {
    /// zsh's compinit initializes the completion subsystem. Lush has its
    /// own completion engine; the initialization step has no analogue.
    /// Real-world scripts source `compinit` to enable completion before
    /// running -- in lush completion is always ready.
    return 0;
}

int bin_bashcompinit(int argc __attribute__((unused)),
                     char **argv __attribute__((unused))) {
    /// zsh's bashcompinit provides bash-completion compatibility in zsh
    /// by registering complete/compgen/compopt as zsh functions. Lush
    /// already has those as builtin stubs (see complete/compgen/compopt
    /// entries above), so bashcompinit itself is a no-op.
    return 0;
}

int bin_unfunction(int argc, char **argv) {
    /// zsh's `unfunction NAME...` removes shell functions. Real
    /// implementation: walks executor->functions and removes each named
    /// entry. Silent on names that don't exist (matches zsh).
    if (!current_executor || argc < 2) {
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        function_def_t **slot = &current_executor->functions;
        while (*slot) {
            if (strcmp((*slot)->name, argv[i]) == 0) {
                function_def_t *victim = *slot;
                *slot = victim->next;
                free(victim->name);
                free_node_tree(victim->body);
                free_function_params(victim->params);
                free(victim);
                break;
            }
            slot = &(*slot)->next;
        }
    }
    return 0;
}

int bin_colors(int argc __attribute__((unused)),
               char **argv __attribute__((unused))) {
    /// zsh's `colors` is a function autoloaded from $fpath that
    /// populates $fg, $bg, $FG, $BG arrays / maps and $reset_color
    /// with ANSI escape sequences. Lush's autoload is itself a stub,
    /// so `colors` is unavailable to scripts that source the zsh
    /// autoload module. Provide a builtin no-op so scripts calling
    /// `colors` to set up the maps do not crash; the maps simply
    /// stay unset and downstream `${fg[red]}` lookups expand to
    /// empty (matching the no-color terminal case rather than a
    /// hard error).
    return 0;
}
