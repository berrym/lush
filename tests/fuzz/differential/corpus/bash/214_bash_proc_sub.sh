# Process substitution <(cmd) -- pass cmd's stdout as a readable
# filename argument. <(cmd) expands to /dev/fd/N or named pipe.
# Without the substitution, would need a real temp file.

# Compare outputs of two pipelines via diff
diff <(printf 'a\nb\nc\n') <(printf 'a\nb\nc\n') && echo "diff-same"

# Read from process sub via cat
cat <(echo "one"; echo "two")

# wc -l on process sub
wc -l < <(printf 'x\ny\nz\n') | tr -d ' '
