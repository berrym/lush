# Command substitution: $(...) and `...` forms.
result=$(echo "modern")
echo "1: $result"

old=`echo "legacy"`
echo "2: $old"

# Nested
nested=$(echo "outer $(echo inner) done")
echo "3: $nested"

# With redirection inside command sub
out=$(printf 'x\ny\nz\n' | head -n 1)
echo "4: $out"

# Multiple lines collapse to single string in unquoted context
multi=$(printf 'a\nb\nc\n')
echo "5: $multi"
echo "6: \"$multi\""
