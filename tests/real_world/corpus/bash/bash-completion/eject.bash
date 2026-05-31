# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-fallback/eject.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# bash completion for eject(1)                             -*- shell-script -*-

# Use of this file is deprecated on Linux.  Upstream completion is
# available in util-linux >= 2.23, use that instead.

_comp_cmd_eject()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    case $prev in
        -h | --help | -V | --version | -c | --changerslot | -x | --cdspeed)
            return
            ;;
        -a | --auto | -i | --manualeject)
            _comp_compgen -- -W 'on off'
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        return
    elif [[ $prev == @(-d|--default) ]]; then
        return
    fi

    _comp_compgen_cd_devices
    _comp_compgen -a dvd_devices
} &&
    complete -F _comp_cmd_eject eject

# ex: filetype=sh
