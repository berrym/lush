# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-core/acpi.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# acpi(1) completion                                       -*- shell-script -*-

_comp_cmd_acpi()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    local noargopts='!(-*|*[d]*)'
    # shellcheck disable=SC2254
    case $prev in
        --help | --version | -${noargopts}[hv])
            return
            ;;
        --directory | -${noargopts}d)
            _comp_compgen_filedir -d
            return
            ;;
    esac

    _comp_compgen_help
} &&
    complete -F _comp_cmd_acpi acpi

# ex: filetype=sh
