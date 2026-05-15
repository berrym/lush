# POSIX function syntax: name() { body }. Positional params + return.
greet() {
    echo "hello $1"
    return 0
}

add() {
    n=$(( $1 + $2 ))
    echo "$n"
}

count_args() {
    echo "got $# args: $*"
}

greet world
add 3 4
count_args a b c d

# Function-local-via-subshell idiom (POSIX has no `local` builtin).
outer="visible"
(
    outer="modified"
    echo "inside: $outer"
)
echo "after: $outer"
