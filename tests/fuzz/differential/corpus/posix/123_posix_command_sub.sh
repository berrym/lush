# POSIX command substitution: simple, nested, and inline.
x=$(echo hello)
echo "x: $x"
y=$(echo "$(echo nested)")
echo "y: $y"
echo "inline: [$(echo a b c)]"
echo "arith-of-sub: $(( $(echo 6) * 7 ))"
