# Zsh anonymous functions: () { body } args, executed once with $1...$N.
() {
    echo "no args, no body args: $#"
}

() {
    echo "called with $# args"
    for x in "$@"; do echo "  arg: $x"; done
} one two three

# Capture result via stdout
result=$(() { echo "$(($1 + $2))"; } 3 4)
echo "sum: $result"

# Anonymous with local-ish scope
x=outer
() {
    local x=inner
    echo "inner: $x"
} ignored
echo "outer: $x"
