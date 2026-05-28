# Zsh 1-indexed arrays: positive/negative indexing and slice ranges.
arr=(alpha beta gamma delta)
echo "idx1: $arr[1]"
echo "idxlast: $arr[-1]"
echo "len: ${#arr}"
echo "slice12: $arr[1,2]"
echo "slice2last: $arr[2,-1]"
echo "slicelast2: $arr[-2,-1]"
