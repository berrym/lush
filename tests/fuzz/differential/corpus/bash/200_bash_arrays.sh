# Indexed arrays: declare, access, length, keys, slicing.
arr=(zero one two three four)
echo "first: ${arr[0]}"
echo "last-by-index: ${arr[4]}"
echo "all: ${arr[@]}"
echo "all-star: ${arr[*]}"
echo "count: ${#arr[@]}"
echo "keys: ${!arr[@]}"
echo "len-first: ${#arr[0]}"

# Slicing
echo "slice 1-3: ${arr[@]:1:3}"
echo "slice from 2: ${arr[@]:2}"

# Append
arr+=(five six)
echo "after-append: ${arr[@]}"

# Element-wise iteration
for x in "${arr[@]}"; do
    echo "item: $x"
done
