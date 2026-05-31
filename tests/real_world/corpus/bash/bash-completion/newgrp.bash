# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-fallback/newgrp.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# newgrp(1) completion                                     -*- shell-script -*-

# Use of this file is deprecated on Linux.  Upstream completion is
# available in util-linux >= 2.23, use that instead.

_comp_cmd_newgrp()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    if [[ $cur == "-" ]]; then
        COMPREPLY=(-)
    else
        _comp_compgen_allowed_groups
    fi
} &&
    complete -F _comp_cmd_newgrp newgrp

# ex: filetype=sh
