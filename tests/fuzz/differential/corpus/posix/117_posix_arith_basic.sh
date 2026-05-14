# POSIX arithmetic expansion $((...)) operators.
# Avoids bash-only ** exponent and (( cmd )) form -- those go in
# the bash corpus.

# Basic ops
echo "$((1 + 2))"
echo "$((10 - 3))"
echo "$((4 * 5))"
echo "$((20 / 7))"
echo "$((20 % 7))"

# Precedence
echo "$((2 + 3 * 4))"
echo "$(((2 + 3) * 4))"

# Variable references
a=7
b=3
echo "$((a + b))"
echo "$((a * b - 1))"

# Variable assignment inside expansion
c=$((a + b))
echo "c=$c"

# Bitwise
echo "$((5 & 3))"
echo "$((5 | 3))"
echo "$((5 ^ 3))"
echo "$((~0))"
echo "$((1 << 4))"
echo "$((16 >> 2))"

# Comparison (POSIX returns 1=true, 0=false)
echo "$((5 > 3))"
echo "$((5 < 3))"
echo "$((5 == 5))"

# Logical
echo "$((1 && 0))"
echo "$((0 || 1))"
echo "$((!0))"

# Ternary
echo "$((5 > 3 ? 100 : 200))"
