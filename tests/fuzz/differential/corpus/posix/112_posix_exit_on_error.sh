# set -e: a failed simple command aborts the script.
# Subshell each test so the parent process can continue.

# Without -e: false does not abort
(false; echo "no-exit")

# With -e: false aborts before echo
(set -e; false; echo "should-not-print") 2>/dev/null
echo "after-set-e-test-1: rc=$?"

# set -e suppressed inside if-condition (POSIX exception)
(set -e; if false; then echo a; else echo "in-else"; fi; echo "still-here")

# set -e suppressed inside ||
(set -e; false || echo "fallback"; echo "after-or")
