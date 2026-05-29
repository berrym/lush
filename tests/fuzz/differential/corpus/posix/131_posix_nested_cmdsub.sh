# POSIX nested command substitution and arithmetic over it.
echo "outer: $(echo $(echo inner))"
n=$(echo $(( $(echo 3) + $(echo 4) )))
echo "n: $n"
echo "chain: $(printf '%s' "$(echo a)$(echo b)")"
