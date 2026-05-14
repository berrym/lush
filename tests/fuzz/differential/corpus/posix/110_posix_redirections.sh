# POSIX redirection forms that produce deterministic stdout.
# Avoids filesystem state; uses pipes and /dev/null only.

# stdout to /dev/null (silent)
echo "hidden" >/dev/null
echo "visible"

# stderr to /dev/null
{ echo "out"; echo "err" >&2; } 2>/dev/null

# stderr to stdout via 2>&1
{ echo "out"; echo "err" >&2; } 2>&1 | sort

# Discard both
{ echo "a"; echo "b" >&2; } >/dev/null 2>&1
echo "after-discard"

# Append (to /dev/null)
echo "x" >>/dev/null
echo "after-append"
