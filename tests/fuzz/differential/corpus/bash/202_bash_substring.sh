# Substring expansion: ${var:offset} and ${var:offset:length}.
# bash is 0-indexed and byte-oriented.
s="abcdefghij"
echo "1: ${s:0:1}"
echo "2: ${s:0:3}"
echo "3: ${s:3}"
echo "4: ${s:3:4}"
echo "5: ${s: -3}"
echo "6: ${s: -3:2}"

# Length
echo "len: ${#s}"

# Empty length
empty=""
echo "empty: '${empty:-default}'"
