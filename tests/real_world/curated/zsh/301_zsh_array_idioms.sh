# Zsh 1-indexed arrays + zsh-specific subscript syntax. Pattern from
# oh-my-zsh plugins and zsh-syntax-highlighting: heavy array manipulation
# via slicing and negative indices.

fruits=(apple banana cherry date elderberry)

# First / last via 1-indexed access
echo "first: $fruits[1]"
echo "last: $fruits[-1]"
echo "second-to-last: $fruits[-2]"

# Slice
echo "first two: $fruits[1,2]"
echo "middle three: $fruits[2,4]"
echo "last two: $fruits[-2,-1]"

# Length forms
echo "len with #: ${#fruits}"
echo "len with @: ${#fruits[@]}"

# Iteration order
i=1
for f in $fruits; do
    echo "  [$i] = $f"
    i=$((i + 1))
done

# Append
fruits+=(fig grape)
echo "after append: ${(j:,:)fruits}"

# Replace by index
fruits[3]="CHANGED"
echo "after replace: ${(j:,:)fruits}"

# Search: index of element (zsh: ${arr[(i)pattern]})
position=${fruits[(i)CHANGED]}
echo "position of CHANGED: $position"

# Subscript with pattern match (zsh: ${arr[(r)pattern]})
found=${fruits[(r)f*]}
echo "first matching f*: $found"
