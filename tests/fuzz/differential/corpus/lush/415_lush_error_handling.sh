# Error-handling and exit-code propagation patterns.
# No oracle for lush corpus; checks for crash/UB only.

# || fallback
false || echo "fallback-1"

# && chain
true && true && echo "chain-success"
true && false && echo "should-not-print"

# Capture exit
get_status() {
    "$@"
    return $?
}
get_status true; echo "true-status: $?"
get_status false; echo "false-status: $?"
get_status sh -c 'exit 17'; echo "exit17: $?"

# trap on EXIT
(trap 'echo "in-trap"' EXIT; echo "before-exit")
echo "after-subshell"

# Trap reset
trap - EXIT 2>/dev/null
echo "after-reset"
