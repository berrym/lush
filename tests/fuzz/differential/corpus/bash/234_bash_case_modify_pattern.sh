# Bash case modification with an optional pattern selecting which chars.
s="hello world"
echo "up-all: ${s^^}"
echo "up-l: ${s^^l}"
echo "low-first: ${s,}"
echo "up-vowels: ${s^^[aeiou]}"
