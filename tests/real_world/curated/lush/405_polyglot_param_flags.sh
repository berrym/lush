#!/usr/bin/env lush
# Lush polyglot script: bash-style parameter transforms (${var^^} /
# ${var,,} / ${var/pat/repl}) and zsh-style parameter flags
# (${(U)var} / ${(L)var} / ${(C)var}) produce the same effects.
# Both spellings target the same engine feature (case conversion,
# substring substitution) -- the syntax is a polyglot interface
# layer, the engine is unified. See docs/EXTENDED_SYNTAX.md.

word="hello world"

# --- Case conversion: bash ${var^^} vs zsh ${(U)var} ---
echo "bash-upper: ${word^^}"
echo "zsh-upper:  ${(U)word}"
echo "bash-lower: ${word,,}"
echo "zsh-lower:  ${(L)word}"

# --- Capitalize first letter of each word ---
echo "bash-title: ${word^}"      # bash: capitalize first letter only
echo "zsh-cap:    ${(C)word}"     # zsh: capitalize each word

# --- Substring substitution: bash ${var//pat/repl} ---
echo "bash-replace: ${word//o/0}"
# zsh has the same syntax for global pattern replacement
echo "zsh-replace:  ${word//o/0}"

# --- Length: ${#var} in bash, ${#var} in zsh too ---
echo "length-bash-zsh: ${#word}"

# --- Default value: ${var:-default} works in all POSIX shells ---
echo "default-empty: ${UNSET_VAR:-defaulted}"
echo "default-keep:  ${word:-unused}"

# --- Sanity: same variable, multiple expansions, no leakage ---
echo "preserved: $word"
