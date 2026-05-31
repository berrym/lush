# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/groupmems.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# groupmems(8) completion                                  -*- shell-script -*-

_comp_cmd_groupmems()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    case $prev in
        -a | --add | -d | --delete)
            _comp_compgen -- -u
            return
            ;;
        -g | --group)
            _comp_compgen -- -g
            return
            ;;
        -R | --root)
            _comp_compgen_filedir -d
            return
            ;;
    esac

    _comp_compgen_help
} &&
    complete -F _comp_cmd_groupmems groupmems

# ex: filetype=sh
