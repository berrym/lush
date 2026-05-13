# Zsh arrays are 1-indexed by default.
arr=(alpha beta gamma delta)
echo "first:  $arr[1]"
echo "second: $arr[2]"
echo "last:   $arr[-1]"
echo "len:    ${#arr}"
echo "all:    $arr"
echo "qall:   ${arr[@]}"

# Slicing
echo "1-2: $arr[1,2]"
echo "2--1: $arr[2,-1]"
echo "-2--1: $arr[-2,-1]"

# Membership / index-of
items=(one two three two one)
echo "${items[(i)two]}"
echo "${items[(I)two]}"
