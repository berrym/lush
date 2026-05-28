# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/9b3c713/completions-core/pdftotext.bash
# UPSTREAM-COMMIT:  9b3c713
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-25
# ============================================================================
# bash completion for pdftotext(1)                         -*- shell-script -*-

_comp_cmd_pdftotext()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    case $prev in
        -h | -help | --help | -'?' | -f | -l | -r | -x | -y | -W | -H | \
            -fixed | -opw | -upw)
            return
            ;;
        -enc)
            _comp_compgen_split -- "$("$1" -listenc 2>/dev/null | _comp_tail -n +2)"
            return
            ;;
        -eol)
            _comp_compgen -- -W "unix dos mac"
            return
            ;;
    esac

    if [[ $cur == -* && ${prev,,} != *.pdf ]]; then
        _comp_compgen_help
        return
    fi

    case ${prev,,} in
        - | *.txt) ;;
        *.pdf)
            _comp_compgen -- -W '-'
            _comp_compgen -a filedir txt
            ;;
        *) _comp_compgen_filedir pdf ;;
    esac
} &&
    complete -F _comp_cmd_pdftotext pdftotext

# ex: filetype=sh
