# Indexed array operations in whole-word (lush-valid) positions: the
# count and the key/slice/element expansions each occupy their own word,
# never glued to literal text (which SEMANTICS section 3.9 forbids for a
# vector-yielding expansion).
arr=(zero one two three four)
echo "first: ${arr[0]}"
echo "count: ${#arr[@]}"
echo "keys:" "${!arr[@]}"
echo "slice:" "${arr[@]:1:3}"
echo "from2:" "${arr[@]:2}"
arr+=(five six)
echo "appended:" "${arr[@]}"
for x in "${arr[@]}"; do
  echo "item: $x"
done
