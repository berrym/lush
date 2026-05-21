# Zsh parameter-flag pipeline -- the heart of "why zsh users love zsh".
# Pattern: build up a list, transform via parameter flags, join.

words=(apple banana cherry date elderberry fig)

# Length
echo "count: ${#words}"

# Upper-case all (zsh: (U))
echo "upper: ${(U)words}"

# Lower-case (already lower, just test the flag round-trips)
echo "lower: ${(L)words}"

# Capitalize first letter of each
echo "caps: ${(C)words}"

# Sort ascending (zsh: (o))
echo "sorted: ${(o)words}"

# Reverse-sort (zsh: (O))
echo "reversed: ${(O)words}"

# Sort with i (case-insensitive)
mixed=(Banana apple Cherry date Elderberry fig)
echo "case-insensitive: ${(oi)mixed}"

# Unique (u flag)
dup=(a b a c b a d)
echo "unique: ${(u)dup}"

# Join with separator
echo "joined: ${(j:,:)words}"

# Split a string back into an array
csv="alpha,beta,gamma,delta"
parts=("${(s:,:)csv}")
echo "split count: ${#parts}"
for p in $parts; do
    echo "  part: $p"
done
