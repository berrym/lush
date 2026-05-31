# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/autoreconf.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# autoreconf(1) completion                                 -*- shell-script -*-

_comp_cmd_autoreconf()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    case "$prev" in
        --help | -h | --version | -V)
            return
            ;;
        --warnings | -W)
            local cats=(cross gnu obsolete override portability syntax
                unsupported)
            _comp_compgen -- -W '"${cats[@]}" "${cats[@]/#/no-}" all none
                error'
            return
            ;;
        --prepend-include | -B | --include | -I)
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

    if [[ $1 == *autoheader ]]; then
        _comp_compgen_filedir '@(ac|in)'
    else
        _comp_compgen_filedir -d
    fi
} &&
    complete -F _comp_cmd_autoreconf autoreconf autoheader

# ex: filetype=sh
