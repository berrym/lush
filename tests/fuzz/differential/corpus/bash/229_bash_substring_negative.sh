# Bash substring expansion with negative offset and negative length.
# A space before a negative offset is required to avoid the :- default
# operator; a negative length counts graphemes back from the end.
s=abcdefgh
echo "from2: ${s:2}"
echo "from2len3: ${s:2:3}"
echo "neg-offset: ${s: -3}"
echo "neg-off-len: ${s: -4:2}"
echo "neg-length: ${s:1:-2}"
echo "neg-len-1: ${s:1:-1}"
echo "neg-off-neg-len: ${s: -4:-1}"
