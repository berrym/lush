# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/9b3c713/completions-core/jpegoptim.bash
# UPSTREAM-COMMIT:  9b3c713
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-25
# ============================================================================
# jpegoptim(1) completion                                  -*- shell-script -*-

_comp_cmd_jpegoptim()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    local noargopts='!(-*|*[dmTS]*)'
    # shellcheck disable=SC2254
    case $prev in
        --help | --version | -${noargopts}[hV]*)
            return
            ;;
        --dest | -${noargopts}d)
            _comp_compgen_filedir -d
            return
            ;;
        --max | --threshold | -${noargopts}[mT])
            _comp_compgen -- -W '{0..100}'
            return
            ;;
        --size | -${noargopts}S)
            _comp_compgen -- -W '{1..99}%'
            return
            ;;
    esac

    [[ $was_split ]] && return

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        [[ ${COMPREPLY-} == *= ]] && compopt -o nospace
        return
    fi

    _comp_compgen_filedir 'jp?(e)g'
} &&
    complete -F _comp_cmd_jpegoptim jpegoptim

# ex: filetype=sh
