# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/avahi-browse.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# bash completion for avahi-browse(1)                      -*- shell-script -*-

_comp_cmd_avahi_browse()
{
    local cur prev words cword was_split comp_args
    _comp_initialize -s -- "$@" || return

    local noargopts='!(-*|*[D]*)'
    # shellcheck disable=SC2254
    case $prev in
        --domain | -${noargopts}D)
            return
            ;;
        --help | --version | -${noargopts}[hV]*)
            return
            ;;
    esac

    [[ $was_split ]] && return

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        [[ ${COMPREPLY-} != *= ]] || compopt -o nospace
        return
    fi

    # Complete service types except with -a/-D/-b
    [[ $1 != *-domains ]] || return
    local word
    for word in "${words[@]}"; do
        case $word in
            --all | --browse-domains | --dump-db | -${noargopts}[aDb]*)
                return
                ;;
        esac
    done
    _comp_compgen_split -- "$("$1" --dump-db --no-db-lookup)"

} &&
    complete -F _comp_cmd_avahi_browse avahi-browse avahi-browse-domains

# ex: filetype=sh
