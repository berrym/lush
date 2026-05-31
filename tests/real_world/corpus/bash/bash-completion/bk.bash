# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/bk.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# BitKeeper completion                                     -*- shell-script -*-
# adapted from code by  Bart Trojanowski <bart@jukie.net>

_comp_cmd_bk()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local BKCMDS=$(bk help topics 2>/dev/null |
        _comp_awk '/^  bk/ { print $2 }')

    _comp_compgen -- -W "$BKCMDS"
    _comp_compgen -a filedir

} &&
    complete -F _comp_cmd_bk bk

# ex: filetype=sh
