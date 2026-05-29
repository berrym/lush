# Bash indexed-array keys, length, and slice (whole-word forms).
arr=(zero one two three four)
echo "keys:" "${!arr[@]}"
echo "slice:" "${arr[@]:1:2}"
echo "len: ${#arr[@]}"
echo "elem-len: ${#arr[2]}"
