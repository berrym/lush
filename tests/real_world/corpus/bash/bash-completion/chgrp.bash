# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/chgrp.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# chgrp(1) completion                                      -*- shell-script -*-

_comp_cmd_chgrp()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    cur=${cur//\\\\/}

    if [[ $prev == --reference ]]; then
        _comp_compgen_filedir
        return
    fi

    [[ $was_split ]] && return

    # options completion
    if [[ $cur == -* ]]; then
        local w opts=""
        for w in "${words[@]}"; do
            [[ $w == -@(R|-recursive) ]] && opts="-H -L -P" && break
        done
        _comp_compgen -- -W '-c -h -f -R -v --changes --dereference --from
            --no-dereference --silent --quiet --reference --recursive --verbose
            --help --version $opts'
        return
    fi

    # first parameter on line or first since an option?
    if [[ $cword -eq 1 && $cur != -* || $prev == -* ]]; then
        _comp_compgen_allowed_groups
    else
        _comp_compgen_filedir
    fi

} &&
    complete -F _comp_cmd_chgrp chgrp

# ex: filetype=sh
