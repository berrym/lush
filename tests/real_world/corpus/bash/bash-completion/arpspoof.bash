# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/arpspoof.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# arpspoof completion                                      -*- shell-script -*-

_comp_cmd_arpspoof()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    case $prev in
        -i)
            _comp_compgen_available_interfaces -a
            return
            ;;
        -t)
            _comp_compgen_known_hosts -- "$cur"
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        _comp_compgen_usage
    else
        _comp_compgen_known_hosts -- "$cur"
    fi

} &&
    complete -F _comp_cmd_arpspoof arpspoof

# ex: filetype=sh
