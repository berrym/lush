# for_posix without `in`: defaults to "$@"
greet() {
    for arg; do
        echo "got: $arg"
    done
}
greet alpha beta gamma

# Same with do on same line via semicolon
for x; do echo "$x"; done
