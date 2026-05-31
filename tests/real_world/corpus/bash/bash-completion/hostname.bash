# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/hostname.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# hostname(1) completion                                   -*- shell-script -*-

_comp_cmd_hostname()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local noargopts='!(-*|*[F]*)'
    # shellcheck disable=SC2254
    case $prev in
        --help | --version | -${noargopts}[hV])
            return
            ;;
        --file | -${noargopts}F)
            _comp_compgen_filedir
            return
            ;;
    esac

    [[ $cur == -* ]] &&
        _comp_compgen_help || _comp_compgen_usage
} &&
    complete -F _comp_cmd_hostname hostname

# ex: filetype=sh
