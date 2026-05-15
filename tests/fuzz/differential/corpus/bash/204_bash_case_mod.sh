# Case modification: ^ (first upper), ^^ (all upper), , (first lower), ,, (all lower).
s="hello world"
echo "1: ${s^}"
echo "2: ${s^^}"
echo "3: ${s,}"
echo "4: ${s,,}"

# Pattern-restricted modification
mixed="aaaBBBccc"
echo "5: ${mixed^^[abc]}"
echo "6: ${mixed,,[BC]}"

# On positional parameters
set -- one TWO three
echo "7: ${1^^}"
echo "8: ${2,,}"
echo "9: ${@^}"
