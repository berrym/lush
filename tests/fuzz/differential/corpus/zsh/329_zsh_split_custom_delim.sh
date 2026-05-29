# Zsh ${(s/:/)str} split on a custom delimiter into array elements.
path="a:bb:ccc:d"
parts=(${(s/:/)path})
echo "count: ${#parts}"
echo "first: $parts[1]"
echo "third: $parts[3]"
echo "joined: ${(j/-/)parts}"
