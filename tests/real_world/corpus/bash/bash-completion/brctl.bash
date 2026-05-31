# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/brctl.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# bash completion for brctl                                -*- shell-script -*-

_comp_cmd_brctl__interfaces()
{
    _comp_compgen_split -- "$(
        "${1:-brctl}" show ${2:+"$2"} 2>/dev/null | _comp_awk \
            '(NR == 1) { next }; (/^\t/) { print $1; next }; { print $4 }'
    )"
}

_comp_cmd_brctl()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local command=${words[1]}

    case $cword in
        1)
            _comp_compgen -- -W "addbr delbr addif delif setageing
                setbridgeprio setfd sethello setmaxage setpathcost setportprio
                show showmacs showstp stp"
            ;;
        2)
            case $command in
                addbr) ;;

                *)
                    _comp_compgen_split -- "$("$1" show |
                        _comp_awk '(NR>1 && !/^\t/) {print $1}')"
                    ;;
            esac
            ;;
        3)
            case $command in
                addif)
                    _comp_compgen_available_interfaces
                    ;;
                delif)
                    _comp_cmd_brctl__interfaces "$1" "$prev"
                    ;;
                stp)
                    _comp_compgen -- -W 'on off'
                    ;;
            esac
            ;;
    esac
} &&
    complete -F _comp_cmd_brctl brctl

# ex: filetype=sh
