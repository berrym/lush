# Sparse indexed arrays: explicit indices, gaps allowed.
arr[0]=zero
arr[2]=two
arr[5]=five

echo "size: ${#arr[@]}"
echo "all: ${arr[*]}"
echo "indices: ${!arr[*]}"

# Access gaps yields empty
echo "gap-1: [${arr[1]}]"
echo "gap-4: [${arr[4]}]"

# Iterate keys-then-value
for k in "${!arr[@]}"; do
    echo "[$k]=${arr[$k]}"
done

# Unset a slot
unset 'arr[2]'
echo "after-unset size: ${#arr[@]}"
echo "after-unset all: ${arr[*]}"
