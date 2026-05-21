# Zsh anonymous functions: a heavy-use feature in oh-my-zsh plugins
# for one-shot scoped logic.

# Anonymous func with no args
() {
    local x=10
    local y=20
    echo "sum: $((x + y))"
}

# With args
() {
    local greeting="$1"
    local name="$2"
    echo "$greeting, $name!"
} "Hello" "Alice"

# Used to scope local variables in a sequence
result=$(
    () {
        local data="(scoped)"
        echo "$data"
    }
)
echo "captured: $result"

# Multiple anonymous funcs in a script (each totally independent)
() { echo "first anonymous"; }
() { echo "second anonymous"; }
() { echo "third with args: $*"; } one two three
