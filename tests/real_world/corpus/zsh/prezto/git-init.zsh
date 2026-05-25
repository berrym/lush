# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/sorin-ionescu/prezto/blob/cff2d01/modules/git/init.zsh
# UPSTREAM-COMMIT:  cff2d01
# UPSTREAM-SET:     prezto
# LICENSE:          MIT
# BUCKET:           zsh
# ADAPTED:          2026-05-25
# ============================================================================
#
# Provides Git aliases and functions.
#
# Authors:
#   Sorin Ionescu <sorin.ionescu@gmail.com>
#

# Return if requirements are not found.
if (( ! $+commands[git] )); then
  return 1
fi

# Load dependencies.
pmodload 'helper'

# Load 'run-help' function.
autoload -Uz run-help-git

# Source module files.
source "${0:h}/alias.zsh"
