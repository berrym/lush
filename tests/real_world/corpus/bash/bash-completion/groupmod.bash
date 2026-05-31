# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/groupmod.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# groupmod(8) completion                                   -*- shell-script -*-

_comp_cmd_groupmod()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    # TODO: if -o/--non-unique is given, could complete on existing gids
    #       with -g/--gid

    local noargopts='!(-*|*[gnp]*)'
    # shellcheck disable=SC2254
    case $prev in
        --gid | --help | --new-name | --password | -${noargopts}[ghnp])
            return
            ;;
    esac

    [[ $was_split ]] && return

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        [[ ${COMPREPLY-} == *= ]] && compopt -o nospace
        return
    fi

    _comp_compgen -- -g
} &&
    complete -F _comp_cmd_groupmod groupmod

# ex: filetype=sh
