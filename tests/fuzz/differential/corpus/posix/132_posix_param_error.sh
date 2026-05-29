# POSIX ${var:?word}: print word to stderr and exit non-zero when unset.
echo "before"
unset missing
: "${missing:?is required}"
echo "after (should not print)"
