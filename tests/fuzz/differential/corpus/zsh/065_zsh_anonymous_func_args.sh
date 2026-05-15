() {
    echo "anon called with $# args"
    echo "first: $1"
} alpha beta gamma
() print -l one two three
() {
    local result=$(($1 * 2))
    echo $result
} 21
