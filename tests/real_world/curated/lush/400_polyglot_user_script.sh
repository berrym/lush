# Lush polyglot script: a user who knows both bash and zsh syntax idioms
# mixes them freely because lush understands both. No mode switching --
# just write whatever feels natural for the task at hand.

# Bash-style associative array for config
declare -A settings
settings[shell]="lush"
settings[version]="0.3.0-dev"
settings[mode]="polyglot"

# Bash-style ${var^^} uppercase
shell_name="${settings[shell]}"
echo "shell: ${shell_name^^}"

# Zsh-style (U) flag does the same -- both should work
echo "shell-zsh: ${(U)shell_name}"

# Bash array indexed
versions=(0.9 1.0 1.1 0.3.0)
echo "bash-last: ${versions[-1]}"   # bash 4.3+: negative index
echo "zsh-last: ${versions[-1]}"     # zsh: same syntax, native support

# Bash brace expansion
files=(/tmp/test.{log,err,out})
for f in "${files[@]}"; do
    echo "expanded: $f"
done

# Zsh ${arr[1,3]} slicing -- only zsh has this, but we use it freely
# Sub-array via slicing
slice=(${versions[1,3]})
echo "slice count: ${#slice[@]}"

# printf quoting -- the format string is quoted, as lush requires: an
# unquoted leading `%` is the pair sigil, not a literal format character.
text="hello world; rm -rf /"
echo "bash-quoted: $(printf '%q' "$text")"

# Zsh ${(q)var} same idea
echo "zsh-quoted: ${(q)text}"

# Function definition: POSIX form
greet() {
    local name="${1:-anonymous}"
    echo "Hello, $name"
}
greet
greet "Alice"
