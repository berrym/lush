# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/badblocks.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# badblocks(8) completion                                  -*- shell-script -*-

_comp_cmd_badblocks()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    case $prev in
        -*[bcedpt])
            return
            ;;
        -*[io])
            _comp_compgen_filedir
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        # Filter out -w (dangerous) and -X (internal use)
        _comp_compgen -R usage
        ((${#COMPREPLY[@]})) &&
            _comp_compgen -- -X '-[wX]' -W '"${COMPREPLY[@]}"'
        return
    fi

    _comp_compgen -c "${cur:-/dev/}" filedir
} &&
    complete -F _comp_cmd_badblocks badblocks

# ex: filetype=sh
