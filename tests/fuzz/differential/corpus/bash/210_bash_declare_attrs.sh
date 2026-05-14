# Bash declare/typeset attributes: -i integer, -r readonly, -u/-l case,
# -a indexed, -A associative. -x export is filesystem-visible so skipped.

declare -i n=5
n=n+10
echo "i-int: $n"

# Integer arithmetic-on-assignment
declare -i m
m="3 + 4"
echo "i-expr: $m"

# Readonly
declare -r ro="locked"
echo "ro: $ro"
ro="changed" 2>/dev/null || echo "readonly-rejected"

# Uppercase / lowercase attributes
declare -u upper="hello"
declare -l lower="WORLD"
echo "upper: $upper"
echo "lower: $lower"

# Array declarations
declare -a indexed=(one two three)
declare -A assoc=([k1]=v1 [k2]=v2)
echo "idx-1: ${indexed[1]}"
echo "assoc-k1: ${assoc[k1]}"
