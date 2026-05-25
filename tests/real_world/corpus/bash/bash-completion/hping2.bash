# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/9b3c713/completions-core/hping2.bash
# UPSTREAM-COMMIT:  9b3c713
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-25
# ============================================================================
# bash completion for hping2                               -*- shell-script -*-

_comp_cmd_hping2()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local noargopts='!(-*|*[IaoE]*)'
    # shellcheck disable=SC2254
    case $prev in
        --interface | -${noargopts}I)
            _comp_compgen_available_interfaces
            return
            ;;
        --spoof | -${noargopts}a)
            _comp_compgen_known_hosts -- "$cur"
            return
            ;;
        --tos | -${noargopts}o)
            # TODO: parse choices from `--tos help`?
            _comp_compgen -- -W '02 04 08 10'
            return
            ;;
        --file | -${noargopts}E)
            _comp_compgen_filedir
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        _comp_compgen_help
    else
        _comp_compgen_known_hosts -- "$cur"
    fi
} &&
    complete -F _comp_cmd_hping2 hping hping2 hping3

# ex: filetype=sh
