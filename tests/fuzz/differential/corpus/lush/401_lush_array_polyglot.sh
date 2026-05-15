# Lush array idioms from both ecosystems. No oracle.
arr=(zero one two three)

# bash style
echo "bash @: ${arr[@]}"
echo "bash 0: ${arr[0]}"
echo "bash len: ${#arr[@]}"
echo "bash slice: ${arr[@]:1:2}"

# zsh style
echo "zsh @: $arr"
echo "zsh 1: $arr[1]"
echo "zsh len: ${#arr}"
echo "zsh slice: $arr[1,2]"

# Bash append
arr+=(four five)
echo "after-append: ${arr[@]}"

# Zsh anonymous function operating on the array
() { for x in "$@"; do echo "anon: $x"; done } "${arr[@]:0:2}"
