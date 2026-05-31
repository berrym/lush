# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/add_members.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# mailman add_members completion                           -*- shell-script -*-

_comp_cmd_add_members()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    case $prev in
        -r | -d | --regular-members-file | --digest-members-file)
            _comp_compgen_filedir
            return
            ;;
        -w | -a | --welcome-msg | --admin-notify)
            _comp_compgen -- -W 'y n'
            return
            ;;
    esac

    [[ $was_split ]] && return

    if [[ $cur == -* ]]; then
        _comp_compgen -- -W '--regular-members-file --digest-members-file
            --welcome-msg --admin-notify --help'
    else
        # Prefer `list_lists` in the same dir as command
        local pathcmd
        pathcmd=$(type -P -- "$1") && local PATH=${pathcmd%/*}:$PATH
        _comp_xfunc list_lists mailman_lists
    fi

} &&
    complete -F _comp_cmd_add_members add_members

# ex: filetype=sh
