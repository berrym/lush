#!/bin/sh
## POSIX trap handling: EXIT pseudo-signal, trap removal, traps in
## subshells. Inter-process signal delivery is not tested here
## because `kill $$` from inside a subshell still hits the outer
## script's PID (POSIX guarantees $$ is the original shell), so the
## test process can't reliably target the subshell handler.

## --- 1. EXIT trap on main shell fires at script end --------------------
trap 'echo "main-exit-trap"' EXIT
echo "main-script-body"

## --- 2. EXIT trap inside subshell fires when subshell exits ------------
( trap 'echo "subshell-exit-trap"' EXIT; echo "subshell-body" )
echo "after-subshell"

## --- 3. Trap removal: `trap - SIG` clears the handler ------------------
trap 'echo "should-never-fire"' EXIT
trap - EXIT
echo "after-trap-removal"

## --- 4. Trap is per-subshell: inner trap doesn't leak to outer ---------
trap 'echo "outer-EXIT-replaced"' EXIT
( trap 'echo "inner-EXIT"' EXIT; : )
echo "between-subshells"
## The outer EXIT trap reinstalled by line 4 fires once at script end.

## --- 5. trap with no args prints current traps in restore-able form ---
trap 'echo "for-listing"' EXIT
out=$(trap)
case "$out" in
    *for-listing*) echo "trap-list: contains EXIT trap" ;;
    *)             echo "trap-list: MISSING EXIT trap" ;;
esac
