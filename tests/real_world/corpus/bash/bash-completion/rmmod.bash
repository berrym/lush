# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-fallback/rmmod.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# Linux rmmod(8) completion.                               -*- shell-script -*-
# This completes on a list of all currently installed kernel modules.

# Use of this file is deprecated.
# Upstream completion is available in kmod >= 34, use that instead.

_comp_cmd_rmmod()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    case $prev in
        -h | --help | -V | --version)
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        _comp_compgen_help
        return
    fi

    _comp_compgen_inserted_kernel_modules
} &&
    complete -F _comp_cmd_rmmod rmmod

# ex: filetype=sh
