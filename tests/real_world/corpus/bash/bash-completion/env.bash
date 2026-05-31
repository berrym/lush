# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/env.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# bash completion for env(1)                              -*- shell-script -*-

_comp_cmd_env()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    local i noargopts='!(-*|*[uCS]*)'
    for ((i = 1; i <= cword; i++)); do
        if [[ ${words[i]} != -* || ${words[i]} == -?(-) && i -lt cword ]]; then
            [[ ${words[i]} == -?(-) ]] && ((i++))
            for (( ; i <= cword; i++)); do
                if [[ ${words[i]} != *=* ]]; then
                    _comp_command_offset "$i"
                    return
                fi
            done
            break
        fi

        # shellcheck disable=SC2254
        [[ ${words[i]} == @(--@(unset|chdir|split-string)|-${noargopts}[uCS]) ]] &&
            ((i++))
    done

    # shellcheck disable=SC2254
    case "$prev" in
        --unset | -${noargopts}u)
            _comp_compgen -- -A variable
            return
            ;;
        --chdir | -${noargopts}C)
            _comp_compgen_filedir -d
            return
            ;;
        --split-string | -${noargopts}S)
            return
            ;;
        --block-signal | --default-signal | --ignore-signal)
            # TODO signals, but only if completing with a =SIG
            ;;
    esac

    [[ $was_split ]] && return

    _comp_variable_assignments "$cur" && return

    if [[ $cur == -* ]]; then
        _comp_compgen_help || _comp_compgen_usage
        [[ ${COMPREPLY-} == *= ]] && compopt -o nospace
        return
    fi
} &&
    complete -F _comp_cmd_env env

# ex: filetype=sh
