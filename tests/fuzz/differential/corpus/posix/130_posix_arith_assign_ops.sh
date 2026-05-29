# POSIX arithmetic compound assignment operators inside $(( )).
n=10
echo "plus: $((n += 5))"
echo "minus: $((n -= 3))"
echo "times: $((n *= 2))"
echo "div: $((n /= 4))"
echo "mod: $((n %= 4))"
echo "final: $n"
