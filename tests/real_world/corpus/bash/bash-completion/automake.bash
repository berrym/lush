# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/automake.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# automake(1) completion                                   -*- shell-script -*-

_comp_cmd_automake()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    case "$prev" in
        --help | --version)
            return
            ;;
        --warnings | -W)
            local cats=(gnu obsolete override portability syntax unsupported)
            _comp_compgen -- -W '"${cats[@]}" "${cats[@]/#/no-}" all none
                error'
            return
            ;;
        --libdir)
            _comp_compgen_filedir -d
            return
            ;;
    esac

    [[ $was_split ]] && return

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        [[ ${COMPREPLY-} == *= ]] && compopt -o nospace
        return
    fi

    _comp_compgen_filedir
} &&
    complete -F _comp_cmd_automake automake automake-1.1{0..8}

# ex: filetype=sh
