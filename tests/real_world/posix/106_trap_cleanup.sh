# Trap-based cleanup: the standard hermetic-script pattern. EXIT
# handler ensures tempfiles get removed even on error.

tmpdir=$(mktemp -d -t test_trap.XXXXXX)
cleanup() {
    rm -rf "$tmpdir"
    echo "cleaned up $tmpdir"
}
trap cleanup EXIT

# Create some work in the tmpdir
echo "data1" > "$tmpdir/file1"
echo "data2" > "$tmpdir/file2"

# Verify state
ls "$tmpdir" | sort | sed 's/^/  /'

# Read it back
cat "$tmpdir/file1"
cat "$tmpdir/file2"

# Script exits here -- trap fires
echo "before exit"
