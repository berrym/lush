# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/avctrl.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# avctrl completion                                        -*- shell-script -*-

_comp_cmd_avctrl()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    if [[ $cur == -* ]]; then
        _comp_compgen -- -W '--help --quiet'
    else
        local REPLY
        _comp_count_args
        if ((REPLY == 1)); then
            _comp_compgen -- -W 'discover switch'
        fi
    fi
} &&
    complete -F _comp_cmd_avctrl avctrl

# ex: filetype=sh
