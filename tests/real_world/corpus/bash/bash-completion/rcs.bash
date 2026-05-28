# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/9b3c713/completions-core/rcs.bash
# UPSTREAM-COMMIT:  9b3c713
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-25
# ============================================================================
# bash completion for rcs                                  -*- shell-script -*-

_comp_cmd_rcs()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local file dir

    file=${cur##*/}
    dir=${cur%/*}

    # deal with relative directory
    [[ $file == "$dir" ]] && dir=.

    _comp_compgen -c "$dir/RCS/$file" -- -f

    for i in ${!COMPREPLY[*]}; do
        file=${COMPREPLY[i]##*/}
        dir=${COMPREPLY[i]%RCS/*}
        COMPREPLY[i]=$dir$file
    done

    local files
    _comp_expand_glob files '"$dir/$file"*,v' &&
        _comp_compgen -aR -- -W '"${files[@]}"'

    local i
    for i in ${!COMPREPLY[*]}; do
        COMPREPLY[i]=${COMPREPLY[i]%,v}
    done

    # default to files if nothing returned and we're checking in.
    # otherwise, default to directories
    [[ ${#COMPREPLY[@]} -eq 0 && $1 == *ci ]] && _comp_compgen -a filedir || _comp_compgen -a filedir -d
} &&
    complete -F _comp_cmd_rcs ci co rlog rcs rcsdiff

# ex: filetype=sh
