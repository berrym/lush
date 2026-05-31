# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/appdata-validate.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# appdata-validate(1) completion                           -*- shell-script -*-

_comp_cmd_appdata_validate()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    case $prev in
        -h | --help | --version)
            return
            ;;
        --output-format)
            _comp_compgen_split -- "$("$1" --help | command sed -ne \
                's/--output-format.*\[\(.*\)\]/\1/' -e 's/|/ /gp')"
            return
            ;;
    esac

    [[ $was_split ]] && return

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        [[ ${COMPREPLY-} == *= ]] && compopt -o nospace
        return
    fi

    _comp_compgen_filedir appdata.xml
} &&
    complete -F _comp_cmd_appdata_validate appdata-validate

# ex: filetype=sh
