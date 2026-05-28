# Bash function-local variables: local shadows the global and is
# restored on return.
g=outer
show() {
  local g=inner
  echo "in-func: $g"
}
echo "before: $g"
show
echo "after: $g"

counter() {
  local n="$1"
  n=$((n + 1))
  echo "$n"
}
echo "c1: $(counter 10)"
echo "c2: $(counter 20)"
