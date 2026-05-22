# Bash indexed array idioms beyond the basics: slicing, appending,
# array as function argument, sparse indices.

# Basic
arr=(alpha beta gamma delta epsilon)
echo "count: ${#arr[@]}"
echo "all:   ${arr[*]}"
echo "first: ${arr[0]}"
echo "last:  ${arr[-1]}"

# Slicing
echo "slice 1-3: ${arr[@]:1:3}"
echo "from 2:    ${arr[@]:2}"

# Append
arr+=(zeta eta)
echo "after-append: ${arr[*]}"

# Sparse indices
sparse=([10]=ten [20]=twenty [30]=thirty)
echo "sparse count: ${#sparse[@]}"
echo "sparse keys: ${!sparse[@]}"
echo "sparse[20]: ${sparse[20]}"

# Iterate by index
for i in "${!arr[@]}"; do
    echo "  [$i] = ${arr[$i]}"
done

# Array as function argument (by reference via nameref or by value)
print_each() {
    local item
    for item in "$@"; do
        echo "  - $item"
    done
}
print_each "${arr[@]}"
