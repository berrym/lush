# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/9b3c713/completions-core/gendiff.bash
# UPSTREAM-COMMIT:  9b3c713
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-24
# ============================================================================
# gendiff(1) completion                                    -*- shell-script -*-

_comp_cmd_gendiff()
{
    local cur prev words cword comp_args
    _comp_initialize -o '@(diff|patch)' -- "$@" || return

    ((cword == 1)) && _comp_compgen_filedir -d
} &&
    complete -F _comp_cmd_gendiff gendiff

# ex: filetype=sh
