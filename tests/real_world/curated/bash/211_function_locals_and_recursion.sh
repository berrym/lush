#!/bin/bash
## Function semantics: local variables, dynamic scope, recursion,
## return-vs-exit. The dynamic-scoping rules here are the same
## across bash, zsh, and dash (zsh defaults to dynamic like bash;
## per `project-defaults-bash-zsh-consensus` lush follows that
## consensus).
##
## NOTE: $FUNCNAME inspection is intentionally NOT exercised here
## because lush's $FUNCNAME is unset inside function bodies (the
## three reference shells set it). Tracked separately so this script
## stays in the green column while the underlying gap is addressed.

## --- 1. local hides the outer binding inside the function --------------
x=outer
shadow() {
    local x=inner
    echo "inside: $x"
}
shadow
echo "outside: $x"

## --- 2. Functions called recursively get fresh locals -------------------
fact() {
    local n=$1
    if [ "$n" -le 1 ]; then
        echo 1
    else
        local sub
        sub=$(fact $((n - 1)))
        echo $((n * sub))
    fi
}
echo "fact 5: $(fact 5)"

## --- 3. return vs exit: return only ends the function ------------------
check() {
    return 7
}
check
echo "rc-after-return: $?"
echo "continues after return"

## --- 4. Outer var stays unchanged when function uses local --------------
counter=0
bump() {
    local counter
    counter=$((counter + 1))
    counter=$((counter + 1))
    echo "inside-bump: $counter"
}
bump
echo "outside-counter: $counter"

## --- 5. Recursion + function arg + $# inside the function --------------
greet() {
    if [ $# -eq 0 ]; then
        echo "done"
        return
    fi
    echo "hello $1"
    shift
    greet "$@"
}
greet alpha beta gamma
