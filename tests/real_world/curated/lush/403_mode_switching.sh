# Lush polyglot script: switch modes mid-execution. The `mode` builtin
# flips the shell-mode preset at runtime; features curated to that
# mode become active for subsequent statements. This exercises the
# core polyglot identity from CONFIGURATION.md -- modes are presets,
# not restrictions; the engine carries all features and a mode selects
# which defaults / curated behaviors apply.

# Start in lush mode (default).
echo "initial-mode: lush"

# Switch to bash. Now bash-curated defaults (e.g., FEATURE_EXTENDED_GLOB
# default off) take effect for any subsequent globbing decisions.
mode bash
# Bash idiom: case modification ${var^^}
greeting="hello"
echo "bash-upper: ${greeting^^}"

# Switch to zsh. Now zsh-curated defaults apply.
mode zsh
# zsh idiom: ${(U)var} -- same effect, different spelling.
echo "zsh-upper: ${(U)greeting}"

# Back to lush. Both bridges remain available because lush's engine
# is the union of features; the mode just changes defaults.
mode lush
echo "lush-bash-form: ${greeting^^}"
echo "lush-zsh-form: ${(U)greeting}"

# Sanity: the mode switch did NOT lose the variable defined under
# the original mode. State is mode-independent; presets are not
# sandboxes.
echo "var-survives-mode-switch: $greeting"
