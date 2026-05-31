# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/iwlist.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# iwlist completion                                        -*- shell-script -*-

_comp_cmd_iwlist()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    if ((cword == 1)); then
        if [[ $cur == -* ]]; then
            _comp_compgen -- -W '--help --version'
        else
            _comp_compgen_available_interfaces -w
        fi
    else
        _comp_compgen -- -W 'scan scanning freq frequency channel rate bit
            bitrate key enc encryption power txpower retry ap accesspoint peers
            event'
    fi
} &&
    complete -F _comp_cmd_iwlist iwlist

# ex: filetype=sh
