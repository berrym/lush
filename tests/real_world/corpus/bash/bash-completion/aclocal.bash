# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/aclocal.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# aclocal(1) completion                                    -*- shell-script -*-

_comp_cmd_aclocal()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    case "$prev" in
        --help | --print-ac-dir | --version)
            return
            ;;
        --acdir | -I)
            _comp_compgen_filedir -d
            return
            ;;
        --output)
            _comp_compgen_filedir
            return
            ;;
        --warnings | -W)
            local cats=(syntax unsupported)
            _comp_compgen -- -W '"${cats[@]}" "${cats[@]/#/no-}" all none
                error'
            return
            ;;
    esac

    [[ $was_split ]] && return

    _comp_compgen_help
    [[ ${COMPREPLY-} == *= ]] && compopt -o nospace
} &&
    complete -F _comp_cmd_aclocal aclocal aclocal-1.1{0..8}

# ex: filetype=sh
