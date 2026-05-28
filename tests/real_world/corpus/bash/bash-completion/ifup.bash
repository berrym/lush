# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/9b3c713/completions-core/ifup.bash
# UPSTREAM-COMMIT:  9b3c713
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-25
# ============================================================================
# Red Hat & Debian GNU/Linux if{up,down} completion        -*- shell-script -*-

_comp_userland GNU || return 1

_comp_cmd_ifupdown()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local noargopts='!(-*|*[Xoi]*)'
    # shellcheck disable=SC2254
    case $prev in
        --help | --version | --allow | --exclude | --option | -${noargopts}[hVXo])
            return
            ;;
        --interfaces | -${noargopts}i)
            _comp_compgen_filedir
            return
            ;;
        --state-dir)
            _comp_compgen_filedir -d
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        return
    fi

    local REPLY
    _comp_count_args -a "@(--allow|-i|--interfaces|--state-dir|-X|--exclude|-o)"

    if ((REPLY == 1)); then
        _comp_compgen_configured_interfaces
    fi
} &&
    complete -F _comp_cmd_ifupdown ifup ifdown ifquery ifstatus

# ex: filetype=sh
