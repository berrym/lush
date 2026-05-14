# zsh subscript-search flags on arrays:
#   (r)pat -- value of first matching element
#   (R)pat -- last matching value
#   (i)pat -- 1-based index of first match (already in #99 fix)
#   (I)pat -- 1-based index of last match
items=(apple banana cherry banana grape)

# Search returning value
echo "r: ${items[(r)banana]}"

# Search returning index
echo "i: ${items[(i)banana]}"
echo "I: ${items[(I)banana]}"

# No match: (r) returns empty, (i) returns N+1, (I) returns 0
echo "r-none: [${items[(r)nope]}]"
echo "i-none: ${items[(i)nope]}"
echo "I-none: ${items[(I)nope]}"

# Glob in search pattern
echo "r-glob: ${items[(r)c*]}"
echo "i-glob: ${items[(i)c*]}"
