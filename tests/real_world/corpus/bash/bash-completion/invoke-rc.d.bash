# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/invoke-rc.d.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# invoke-rc.d(8) completion                                -*- shell-script -*-
#
# Copyright (C) 2004 Servilio Afre Puentes <servilio@gmail.com>

_comp_cmd_invoke_rc_d()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local sysvdir services options

    [[ -d /etc/rc.d/init.d ]] && sysvdir=/etc/rc.d/init.d ||
        sysvdir=/etc/init.d

    options=(--help --quiet --force --try-anyway --disclose-deny --query
        --no-fallback)

    if [[ $cword -eq 1 || $prev == --* ]]; then
        # generate valid_options
        local IFS='|'
        local exclude="@(${words[*]})"
        _comp_unlocal IFS
        _comp_compgen -- -W '"${options[@]}"' -X "$exclude"

        _comp_expand_glob services '"$sysvdir"/!(README*|*.sh|$_comp_backup_glob)' &&
            _comp_compgen -a -- -W '"${services[@]#"$sysvdir"/}"'
    elif [[ -x $sysvdir/$prev ]]; then
        _comp_compgen_split -- "$(command sed -e 'y/|/ /' \
            -ne 's/^.*Usage:[ ]*[^ ]*[ ]*{*\([^}"]*\).*$/\1/p' \
            "$sysvdir/$prev")"
    else
        COMPREPLY=()
    fi

} &&
    complete -F _comp_cmd_invoke_rc_d invoke-rc.d

# ex: filetype=sh
