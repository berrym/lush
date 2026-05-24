# Subshell () vs brace group {} -- the critical distinction many
# scripts depend on for variable scoping.

# Subshell: variables don't leak out
x=outer
(
    x=inner
    echo "inside subshell: x=$x"
)
echo "after subshell: x=$x"   # still outer

# Brace group: variables DO leak out
y=outer
{
    y=inner
    echo "inside braces: y=$y"
}
echo "after braces: y=$y"   # now inner

# Subshell as conditional context
if (cd /tmp && [ -d . ]); then
    echo "subshell condition: ok"
fi
# Confirm we didn't actually change directory
echo "still in: $(basename "$PWD" 2>/dev/null || echo unknown)" | head -c 40
echo

# Brace group return value
result=$({ echo "first"; echo "second"; })
echo "brace-output: $result" | tr '\n' '|'
echo
