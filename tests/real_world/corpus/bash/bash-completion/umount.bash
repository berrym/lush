# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/scop/bash-completion/blob/ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b/completions-fallback/umount.bash
# UPSTREAM-COMMIT:  ef936aaf7b8968be0bdf71027d07e1c15bbe8a4b
# UPSTREAM-SET:     bash-completion
# LICENSE:          GPL-2.0-or-later
# BUCKET:           bash
# ADAPTED:          2026-05-31
# ============================================================================
# umount(8) completion                                     -*- shell-script -*-

# Use of this file is deprecated on Linux.  Upstream completion is
# available in util-linux >= 2.28, use that instead.

if [[ $OSTYPE == *linux* ]]; then
    . "${BASH_SOURCE%.bash}.linux.bash"
    return
fi

# umount(8) completion. This relies on the mount point being the third
# space-delimited field in the output of mount(8)
#
_comp_cmd_umount()
{
    local cur prev words cword comp_args
    _comp_initialize -- "$@" || return

    _comp_compgen_split -l -- "$(mount | cut -d" " -f 3)"
} &&
    complete -F _comp_cmd_umount -o dirnames umount

# ex: filetype=sh
