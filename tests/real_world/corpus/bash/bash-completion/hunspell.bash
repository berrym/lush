# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/9b3c713/completions-core/hunspell.bash
# UPSTREAM-COMMIT:  9b3c713
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-25
# ============================================================================
# hunspell(1) completion                                   -*- shell-script -*-

_comp_cmd_hunspell()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    case $prev in
        --help | --version | -vv | -[hPv])
            return
            ;;
        -d)
            local -a dicts
            if _comp_expand_glob dicts '/usr/share/hunspell/*.dic /usr/local/share/hunspell/*.dic'; then
                dicts=("${dicts[@]##*/}")
                dicts=("${dicts[@]%.dic}")
                _comp_compgen -- -W '"${dicts[@]}"'
            fi
            return
            ;;
        -i)
            _comp_compgen -x iconv charsets
            return
            ;;
        -p)
            _comp_compgen_filedir
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        return
    fi

    _comp_compgen_filedir
} &&
    complete -F _comp_cmd_hunspell hunspell

# ex: filetype=sh
