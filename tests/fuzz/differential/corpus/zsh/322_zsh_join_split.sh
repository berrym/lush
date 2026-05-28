# Zsh join (j) and split (s) parameter flags. The flag delimiter is '.'
# so the literal split string ':' is unambiguous: ${(s.:.)str}.
arr=(one two three)
echo "joined: ${(j:-:)arr}"
echo "joined-comma: ${(j:, :)arr}"
str="a:b:c:d"
parts=(${(s.:.)str})
echo "count: ${#parts}"
echo "first: $parts[1]"
echo "last: $parts[-1]"
