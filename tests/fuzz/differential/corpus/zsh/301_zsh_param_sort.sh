# Zsh sort flags: (o) sort ascending, (O) descending, (n) numeric, (i) case-insensitive.
items=(banana Apple cherry apple Banana)
echo "raw: $items"
echo "o:   ${(o)items}"
echo "O:   ${(O)items}"
echo "oi:  ${(oi)items}"

# Numeric sort -- (n) compares as numbers, not strings.
nums=(10 2 30 4 5)
echo "lex: ${(o)nums}"
echo "num: ${(no)nums}"
echo "nO:  ${(nO)nums}"

# Unique
dups=(a b a c b a)
echo "u:   ${(u)dups}"
echo "ou:  ${(ou)dups}"
