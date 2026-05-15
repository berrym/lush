# ${var:-} ${var:=} ${var:+} ${var-} variants on set/unset/null parameters.
unset a
echo "1: ${a:-default}"
echo "2: ${a-default}"

b=""
echo "3: ${b:-default}"
echo "4: ${b-default}"

c="value"
echo "5: ${c:-default}"
echo "6: ${c:+alternative}"

unset d
echo "7: ${d:=assigned}"
echo "8: $d"

# Length operator
e="hello"
echo "9: ${#e}"
