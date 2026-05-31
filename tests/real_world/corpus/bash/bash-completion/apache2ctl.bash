# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/apache2ctl.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# apache2ctl(1) completion                                 -*- shell-script -*-

_comp_cmd_apache2ctl()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local APWORDS
    APWORDS=$("$1" 2>&1 >/dev/null | _comp_awk 'NR < 2 { print $3; exit }')

    _comp_compgen_split -F $' \t\n|' -- "$APWORDS"
} &&
    complete -F _comp_cmd_apache2ctl apache2ctl

# ex: filetype=sh
