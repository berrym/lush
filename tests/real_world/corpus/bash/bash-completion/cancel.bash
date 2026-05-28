# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/9b3c713/completions-core/cancel.bash
# UPSTREAM-COMMIT:  9b3c713
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-24
# ============================================================================
# cancel(1) completion                                     -*- shell-script -*-

_comp_cmd_cancel()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    case $prev in
        -h)
            _comp_compgen_known_hosts -- "$cur"
            return
            ;;
        -U)
            return
            ;;
        -u)
            _comp_compgen -- -u
            return
            ;;
    esac

    _comp_compgen_split -- "$(lpstat 2>/dev/null | cut -d' ' -f1)"
} &&
    complete -F _comp_cmd_cancel cancel

# ex: filetype=sh
