# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/autorpm.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# autorpm(8) completion                                    -*- shell-script -*-

_comp_cmd_autorpm()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    _comp_compgen -- -W '--notty --debug --help --version auto add fullinfo
        info help install list remove set'

} &&
    complete -F _comp_cmd_autorpm autorpm

# ex: filetype=sh
