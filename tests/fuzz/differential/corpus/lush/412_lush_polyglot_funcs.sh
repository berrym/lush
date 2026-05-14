# Function-definition variants across POSIX/bash/zsh in lush.
# No oracle for lush corpus.

# POSIX form: name() { body; }
greet() { echo "hello $1"; }
greet world

# bash form with 'function' keyword
function add { echo "$(($1 + $2))"; }
add 3 4

# bash form without () after function
function shout { echo "${1^^}"; }
shout hello

# zsh anonymous function
() { echo "anon: $1"; } x

# Function with local
inc() {
    local n=$1
    n=$((n + 1))
    echo "$n"
}
inc 5
