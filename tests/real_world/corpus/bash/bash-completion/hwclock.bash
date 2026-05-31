# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-fallback/hwclock.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# hwclock(8) completion                                    -*- shell-script -*-

# Use of this file is deprecated.  Upstream completion is available in
# util-linux >= 2.23, use that instead.

_comp_cmd_hwclock()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    case $prev in
        -h | --help | -V | --version | --date | --epoch)
            return
            ;;
        -f | --rtc | --adjfile)
            _comp_compgen_filedir
            return
            ;;
    esac

    local PATH=$PATH:/sbin
    _comp_compgen_help
} &&
    complete -F _comp_cmd_hwclock hwclock

# ex: filetype=sh
