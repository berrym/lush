# POSIX arithmetic expansion: operators, precedence, comparison, logic.
echo "add: $((2 + 3 * 4))"
echo "paren: $(((2 + 3) * 4))"
echo "div: $((17 / 5))"
echo "mod: $((17 % 5))"
echo "cmp: $((3 > 2)) $((2 > 3))"
echo "logic: $((1 && 0)) $((1 || 0))"
i=5
i=$((i + 3))
echo "i-now: $i"
