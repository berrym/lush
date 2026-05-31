# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/file.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# file(1) completion                                       -*- shell-script -*-

_comp_cmd_file()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local noargopts='!(-*|*[Fmfe]*)'
    # shellcheck disable=SC2254
    case $prev in
        --help | --version | --separator | -${noargopts}[vF])
            return
            ;;
        --magic-file | --files-from | -${noargopts}[mf])
            _comp_compgen_filedir
            return
            ;;
        --exclude | -${noargopts}e)
            _comp_compgen -- -W 'apptype ascii cdf compress elf encoding soft
                tar text tokens troff'
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        return
    fi

    _comp_compgen_filedir
} &&
    complete -F _comp_cmd_file file

# ex: filetype=sh
