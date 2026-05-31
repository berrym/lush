#!/usr/bin/env lush
# Lush's three shell-option surfaces -- `set -o`, `setopt`, and `shopt` --
# operate on the same underlying flag/feature matrix per
# CONFIGURATION.md. The names diverge per upstream convention but the
# underlying gates are unified.
#
# This fixture demonstrates the surfaces individually (not interconvertibility,
# which is a separate parity item for setopt-name aliasing).

# --- Bash spelling: `set -o NAME` and `set +o NAME` ---
echo "--- set -o ---"
set -o nounset
echo "after-set-nounset: $(set -o | grep nounset | head -1)"

set +o nounset
echo "after-set-+nounset: $(set -o | grep nounset | head -1)"

# --- POSIX short-flag form: -u (= -o nounset) ---
echo "--- short-flag short form ---"
set -u
echo "after-set-u: $(set -o | grep nounset | head -1)"

set +u
echo "after-set-+u: $(set -o | grep nounset | head -1)"

# --- bash extglob via shopt -s / -u ---
echo "--- shopt -s/-u ---"
shopt -s extglob
shopt extglob | head -1

shopt -u extglob
shopt extglob | head -1

# --- zsh setopt: extended_glob (the lush long name for FEATURE_EXTENDED_GLOB) ---
echo "--- setopt (zsh long names) ---"
setopt extended_glob
setopt | grep extended_glob | head -1

unsetopt extended_glob
setopt | grep extended_glob | head -1 || echo "(extended_glob not in setopt listing)"

# --- Cross-surface visibility: setopt sees the shopt-set flag ---
# All three surfaces hit the same flag/feature matrix, so flipping
# `extglob` via shopt is visible via setopt's listing. set -o
# intentionally only shows POSIX option names so the zsh-named
# extension isn't visible there -- that's a presentation choice,
# not a different gate.
echo "--- cross-surface visibility ---"
shopt -s extglob
echo "via-setopt-after-shopt-s:"
setopt | grep extended_glob | head -1
shopt -u extglob
