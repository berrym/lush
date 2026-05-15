# mapfile / readarray: read lines from stdin into an array.
mapfile -t lines <<< $'one\ntwo\nthree'
echo "count: ${#lines[@]}"
echo "first: ${lines[0]}"
echo "all: ${lines[*]}"

# readarray is the same builtin
readarray -t more <<< $'a\nb\nc\nd'
echo "more-count: ${#more[@]}"
for x in "${more[@]}"; do
    echo "elem: $x"
done

# -n N limits to N lines
mapfile -t -n 2 capped <<< $'1\n2\n3\n4\n5'
echo "capped-count: ${#capped[@]}"
echo "capped-1: ${capped[1]}"
