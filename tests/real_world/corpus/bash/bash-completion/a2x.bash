# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/a2x.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# a2x(1) completion                                        -*- shell-script -*-

_comp_cmd_a2x()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    local noargopts='!(-*|*[aDd]*)'
    # shellcheck disable=SC2254
    case $prev in
        --attribute | --asciidoc-opts | --dblatex-opts | --fop-opts | --help | \
            --version | --xsltproc-opts | -${noargopts}[ah])
            return
            ;;
        --destination-dir | --icons-dir | -${noargopts}D)
            _comp_compgen_filedir -d
            return
            ;;
        --doctype | -${noargopts}d)
            _comp_compgen -x asciidoc doctype
            return
            ;;
        --stylesheet)
            _comp_compgen_filedir css
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
    complete -F _comp_cmd_a2x a2x

# ex: filetype=sh
