# Zsh ${(f)var} splits a string on newlines into array elements.
data=$'one\ntwo\nthree'
lines=(${(f)data})
echo "count: ${#lines}"
echo "first: $lines[1]"
echo "last: $lines[-1]"
